#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <NimBLEDevice.h>
#include "secrets.h"

// Room configuration
const char* roomId   = "AX2Ir6dYLvLQ6TMtQLc";
const char* roomName = "Room 509";
const char* beaconId = "gd3-509-beacon";

// BLE configuration
const char* BLE_DEVICE_NAME       = beaconId;
const char* BLE_SERVICE_UUID      = "7becefce-f0e2-4a3e-8db6-53a9ee63f176";
const char* BLE_BEACON_CHAR_UUID  = "2c993f0e-0b22-47c1-b9c2-8d1fbe4b1973";
const char* BLE_ROOM_CHAR_UUID    = "e6c852eb-6b87-4c6d-ada4-264f19b5da6c";

// NTP time config (UTC+8 Philippines)
const char* ntpServer      = "pool.ntp.org";
const long  gmtOffset      = 28800;
const int   daylightOffset = 0;

// Pin definitions
#define LED_PIN 13

// Reservation alert time
#define ALERT_HOUR   22
#define ALERT_MINUTE 30

// Timing
// Send frequent heartbeats so the dashboard can tell powered beacons apart
// from unplugged ones within about a minute instead of several minutes later.
const unsigned long intervalMS       = 30000;
const unsigned long wifiRetryMS      = 10000;
const unsigned long wifiWaitMS       = 10000;
const uint16_t httpConnMS            = 10000;
const uint16_t httpReadMS            = 10000;
const unsigned long loopDelayMS      = 200;
const unsigned long reconnectDelayMS = 500;

// State
int occupancyCount               = 0;
bool deviceConnected             = false;
bool previousDeviceConnected     = false;
bool alertSent                   = false;
bool firstBoot                   = true;
bool wifiWasConnected            = false;
unsigned long lastIntervalSend   = 0;
unsigned long lastWiFiRetry      = 0;
unsigned long lastLoopDebug      = 0;
NimBLEServer* bleServer          = nullptr;

void startBleAdvertising() {
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  NimBLEAdvertisementData advertisementData;
  NimBLEAdvertisementData scanResponseData;

  advertisementData.setFlags(0x06);
  advertisementData.setName(BLE_DEVICE_NAME);
  advertisementData.addServiceUUID(BLE_SERVICE_UUID);

  scanResponseData.setName(BLE_DEVICE_NAME);
  scanResponseData.addServiceUUID(BLE_SERVICE_UUID);

  advertising->stop();
  advertising->setAdvertisementType(BLE_HS_ADV_TYPE_ADV_IND);
  advertising->setAdvertisementData(advertisementData);
  advertising->setScanResponseData(scanResponseData);
  advertising->enableScanResponse(true);
  advertising->addServiceUUID(BLE_SERVICE_UUID);
  advertising->start();

  Serial.println("BLE advertising started");
  Serial.print("Device name  : ");
  Serial.println(BLE_DEVICE_NAME);
  Serial.print("Service UUID : ");
  Serial.println(BLE_SERVICE_UUID);
  Serial.print("Beacon ID    : ");
  Serial.println(beaconId);
}

String getTimeString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "TIME_ERROR";

  char buffer[32];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &timeinfo);
  return String(buffer) + "+08:00";
}

void getCurrentHourMinute(int& hour, int& minute) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    hour = -1;
    minute = -1;
    return;
  }

  hour = timeinfo.tm_hour;
  minute = timeinfo.tm_min;
}

String getConnectionStatus(bool connected) {
  return connected ? "CONNECTED" : "DISCONNECTED";
}

void syncOccupancyToState(bool connected) {
  occupancyCount = connected ? 1 : 0;
  digitalWrite(LED_PIN, connected ? HIGH : LOW);
}

void onWiFiConnected() {
  Serial.println("\nWiFi connected");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  configTime(gmtOffset, daylightOffset, ntpServer);
  delay(3000);

  Serial.print("Current Time: ");
  Serial.println(getTimeString());
}

bool connectWiFi(unsigned long maxWaitMs) {
  Serial.print("Connecting to WiFi");
  WiFi.disconnect();
  WiFi.begin(ssid, password);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < maxWaitMs) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    wifiWasConnected = true;
    onWiFiConnected();
    return true;
  }

  wifiWasConnected = false;
  Serial.println("WiFi connect failed");
  return false;
}

void ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiWasConnected) {
      wifiWasConnected = true;
      onWiFiConnected();
    }
    return;
  }

  if (wifiWasConnected) {
    Serial.println("\nWiFi connection lost");
    wifiWasConnected = false;
  }

  if (millis() - lastWiFiRetry < wifiRetryMS) return;

  lastWiFiRetry = millis();
  Serial.println("Retrying WiFi connection...");
  connectWiFi(wifiWaitMS);
}

void sendToServer(const String& connectionStatus, const String& eventType) {
  ensureWiFi();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, skipping HTTP send");
    return;
  }

  HTTPClient http;
  http.setConnectTimeout(httpConnMS);
  http.setTimeout(httpReadMS);
  http.begin(serverURL);
  http.addHeader("Content-Type", "application/json");

  String payload = "{";
  payload += "\"roomId\":\"" + String(roomId) + "\",";
  payload += "\"roomName\":\"" + String(roomName) + "\",";
  payload += "\"beaconId\":\"" + String(beaconId) + "\",";
  payload += "\"occupancy\":" + String(occupancyCount) + ",";
  payload += "\"timestamp\":\"" + getTimeString() + "\",";
  payload += "\"connectionStatus\":\"" + connectionStatus + "\",";
  payload += "\"eventType\":\"" + eventType + "\"";
  payload += "}";

  int responseCode = http.POST(payload);

  Serial.print("Sent to Next.js, response: ");
  Serial.println(responseCode);

  if (responseCode >= 200 && responseCode < 300) {
    Serial.println("Server received data");
  } else if (responseCode > 0) {
    Serial.println("Server returned an error");
    Serial.println(http.getString());
  } else {
    Serial.print("Failed to reach server: ");
    Serial.println(http.errorToString(responseCode));
  }

  http.end();
}

class ReservationBleCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override {
    deviceConnected = true;
    Serial.println("-------------------------------------");
    Serial.println("BLE client connected");
    Serial.print("Time            : ");
    Serial.println(getTimeString());
    Serial.println("-------------------------------------");
  }

  void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override {
    deviceConnected = false;
    Serial.println("-------------------------------------");
    Serial.println("BLE client disconnected");
    Serial.print("Time            : ");
    Serial.println(getTimeString());
    Serial.println("-------------------------------------");
  }
};

void setupBle() {
  NimBLEDevice::init(BLE_DEVICE_NAME);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  bleServer = NimBLEDevice::createServer();
  bleServer->setCallbacks(new ReservationBleCallbacks());

  NimBLEService* service = bleServer->createService(BLE_SERVICE_UUID);

  NimBLECharacteristic* beaconChar = service->createCharacteristic(
    BLE_BEACON_CHAR_UUID,
    NIMBLE_PROPERTY::READ
  );
  beaconChar->setValue(beaconId);

  NimBLECharacteristic* roomChar = service->createCharacteristic(
    BLE_ROOM_CHAR_UUID,
    NIMBLE_PROPERTY::READ
  );
  roomChar->setValue(roomId);

  service->start();
  startBleAdvertising();
}

void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.println("=====================================");
  Serial.println(" ESP32 BLE Reservation Beacon");
  Serial.println("=====================================");
  Serial.print("Room ID      : ");
  Serial.println(roomId);
  Serial.print("Room Name    : ");
  Serial.println(roomName);
  Serial.print("Beacon ID    : ");
  Serial.println(beaconId);
  Serial.println("=====================================");

  WiFi.mode(WIFI_STA);
  connectWiFi(20000);
  setupBle();

  lastIntervalSend = 0;

  Serial.println("-------------------------------------");
  Serial.println("Waiting for BLE reservation client...");
  Serial.println("Alert scheduled at: 10:30 PM");
  Serial.println("-------------------------------------");
}

void loop() {
  ensureWiFi();

  if (millis() - lastLoopDebug >= 5000) {
    lastLoopDebug = millis();
    Serial.print("Loop alive, millis=");
    Serial.print(millis());
    Serial.print(", WiFi=");
    Serial.print(WiFi.status() == WL_CONNECTED ? "connected" : "disconnected");
    Serial.print(", deviceConnected=");
    Serial.println(deviceConnected ? "true" : "false");
  }

  if (deviceConnected != previousDeviceConnected) {
    syncOccupancyToState(deviceConnected);

    if (deviceConnected) {
      Serial.println("Device connected event");
      sendToServer("CONNECTED", "DEVICE_CONNECTED");
    } else {
      Serial.println("Device disconnected event");
      sendToServer("DISCONNECTED", "DEVICE_DISCONNECTED");
      delay(reconnectDelayMS);
      startBleAdvertising();
      Serial.println("BLE advertising restarted");
    }

    previousDeviceConnected = deviceConnected;
  }

  if (firstBoot || millis() - lastIntervalSend >= intervalMS) {
    firstBoot = false;
    lastIntervalSend = millis();
    Serial.println("Interval update sending...");
    sendToServer(getConnectionStatus(deviceConnected), "INTERVAL_UPDATE");
  }

  int currentHour, currentMinute;
  getCurrentHourMinute(currentHour, currentMinute);

  if (currentHour == ALERT_HOUR &&
      currentMinute == ALERT_MINUTE &&
      !alertSent) {
    String timeNow = getTimeString();
    Serial.println("=====================================");
    Serial.println("END OF RESERVATION ALERT");
    Serial.print("Time      : ");
    Serial.println(timeNow);
    Serial.print("Occupancy : ");
    Serial.println(occupancyCount);
    Serial.println("=====================================");

    sendToServer(getConnectionStatus(deviceConnected), "END_OF_RESERVATION");
    alertSent = true;
  }

  if (currentHour == 0 && currentMinute == 0) {
    alertSent = false;
  }

  delay(loopDelayMS);
}
