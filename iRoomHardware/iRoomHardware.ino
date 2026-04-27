#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>

// WiFi credentials
const char* ssid     = "HUAWEI-2.4G-f22F";
const char* password = "Otchia123";

// Next.js server
const char* serverURL = "http://192.168.100.165:3000/api/occupancy";

// ─── ROOM CONFIGURATION ───────────────────────────────────────────
const char* roomId   = "AX2Ir6dYLvLQ6TMtQLc";
const char* roomName = "Room 101";
const char* beaconId = "C4:BE:84:D7:DF:FC";
// ──────────────────────────────────────────────────────────────────

// NTP time config (UTC+8 Philippines)
const char* ntpServer      = "pool.ntp.org";
const long  gmtOffset      = 28800;
const int   daylightOffset = 0;

// Pin definitions
#define RXD2      25
#define TXD2      26
#define STATE_PIN 34
#define LED_PIN   13

// Reservation alert time
#define ALERT_HOUR   22
#define ALERT_MINUTE 30

// Timing
const unsigned long intervalMS   = 600000;
const unsigned long wifiRetryMS  = 10000;
const unsigned long wifiWaitMS   = 10000;
const uint16_t      httpConnMS   = 3000;
const uint16_t      httpReadMS   = 5000;
const unsigned long loopDelayMS  = 200;
const unsigned long serialTOms   = 50;

// State
int occupancyCount               = 0;
bool lastState                   = false;
bool currentState                = false;
bool alertSent                   = false;
bool firstBoot                   = true;
bool wifiWasConnected            = false;
unsigned long lastIntervalSend   = 0;
unsigned long lastWiFiRetry      = 0;

String getTimeString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "TIME_ERROR";

  char buffer[32];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &timeinfo);
  return String(buffer) + "+08:00";
}

void getCurrentHourMinute(int &hour, int &minute) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    hour = -1;
    minute = -1;
    return;
  }
  hour   = timeinfo.tm_hour;
  minute = timeinfo.tm_min;
}

String getConnectionStatus(bool state) {
  return state ? "CONNECTED" : "DISCONNECTED";
}

void syncOccupancyToState(bool state) {
  occupancyCount = state ? 1 : 0;
}

void onWiFiConnected() {
  Serial.println("\nWiFi connected");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  configTime(gmtOffset, daylightOffset, ntpServer);
  delay(500);

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

void sendToServer(const String &connectionStatus, const String &eventType) {
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

  // ✅ beaconId now included in every payload
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

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  Serial2.setTimeout(serialTOms);

  pinMode(STATE_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.println("=====================================");
  Serial.println(" Occupancy Detection + HTTP Send");
  Serial.println("=====================================");
  Serial.print("Room ID   : ");
  Serial.println(roomId);
  Serial.print("Room Name : ");
  Serial.println(roomName);
  Serial.print("Beacon ID : ");
  Serial.println(beaconId);
  Serial.println("=====================================");

  WiFi.mode(WIFI_STA);
  connectWiFi(20000);

  lastIntervalSend = 0;

  Serial.println("-------------------------------------");
  Serial.println("Waiting for Bluetooth connection...");
  Serial.println("Alert scheduled at: 10:30 PM");
  Serial.println("-------------------------------------");
}

void loop() {
  ensureWiFi();

  currentState = digitalRead(STATE_PIN);

  if (currentState == HIGH && lastState == LOW) {
    syncOccupancyToState(true);
    digitalWrite(LED_PIN, HIGH);

    String timeNow = getTimeString();
    Serial.println("-------------------------------------");
    Serial.println("Device connected");
    Serial.print("Time            : ");
    Serial.println(timeNow);
    Serial.print("Occupancy Count : ");
    Serial.println(occupancyCount);
    Serial.println("-------------------------------------");

    Serial2.println("CONNECTED:" + timeNow);
    Serial2.print("OCCUPIED:");
    Serial2.println(occupancyCount);

    sendToServer("CONNECTED", "DEVICE_CONNECTED");
  }

  if (currentState == LOW && lastState == HIGH) {
    syncOccupancyToState(false);
    digitalWrite(LED_PIN, LOW);

    String timeNow = getTimeString();
    Serial.println("-------------------------------------");
    Serial.println("Device disconnected");
    Serial.print("Time            : ");
    Serial.println(timeNow);
    Serial.print("Occupancy Count : ");
    Serial.println(occupancyCount);
    Serial.println("-------------------------------------");

    Serial2.println("DISCONNECTED:" + timeNow);
    Serial2.print("OCCUPIED:");
    Serial2.println(occupancyCount);

    sendToServer("DISCONNECTED", "DEVICE_DISCONNECTED");
  }

  if (firstBoot || millis() - lastIntervalSend >= intervalMS) {
    firstBoot        = false;
    lastIntervalSend = millis();
    Serial.println("Interval update sending...");
    sendToServer(getConnectionStatus(currentState), "INTERVAL_UPDATE");
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

    Serial2.println("ALERT:END_OF_RESERVATION");
    Serial2.println("ALERT:10:30 PM - Reservation ended!");
    Serial2.print("ALERT:Occupancy:");
    Serial2.println(occupancyCount);

    sendToServer(getConnectionStatus(currentState), "END_OF_RESERVATION");
    alertSent = true;
  }

  if (currentHour == 0 && currentMinute == 0) {
    alertSent = false;
  }

  if (Serial2.available()) {
    String incoming = Serial2.readStringUntil('\n');
    incoming.trim();

    if (incoming == "ERROR" ||
        incoming == "OK" ||
        incoming == "FAIL" ||
        incoming == "+DISC:SUCCESS") {
      Serial.print("Module Status: ");
      Serial.println(incoming);
      lastState = currentState;
      delay(loopDelayMS);
      return;
    }

    Serial.print("Received: ");
    Serial.println(incoming);

    if (incoming == "RESET") {
      syncOccupancyToState(currentState);
      digitalWrite(LED_PIN, currentState ? HIGH : LOW);
      Serial.println("Occupancy synchronized to current connection state");
      Serial2.println("RESET:OK");
      sendToServer(getConnectionStatus(currentState), "RESET");
    }

    if (incoming == "COUNT") {
      Serial2.print("OCCUPIED:");
      Serial2.println(occupancyCount);
    }

    if (incoming == "STATUS") {
      Serial2.print("STATUS:");
      Serial2.println(getConnectionStatus(currentState));
    }

    if (incoming == "TIME") {
      Serial2.print("TIME:");
      Serial2.println(getTimeString());
    }
  }

  lastState = currentState;
  delay(loopDelayMS);
}