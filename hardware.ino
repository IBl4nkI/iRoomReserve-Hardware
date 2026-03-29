#include <WiFi.h>
#include <time.h>

// ── WiFi Credentials ──────────────────────────────────────────
const char* ssid     ="4th generation";
const char* password = "Behappy@131516";

// ── NTP Time Config (UTC+8 Philippines) ───────────────────────
const char* ntpServer   = "pool.ntp.org";
const long  gmtOffset   = 28800;   // UTC+8 = 8 * 3600
const int   daylightOffset = 0;

// ── Pin Definitions ───────────────────────────────────────────
#define RXD2      25
#define TXD2      26
#define STATE_PIN 34
#define LED_PIN   13

// ── Reservation Alert Time ────────────────────────────────────
#define ALERT_HOUR   22   // 10 PM
#define ALERT_MINUTE 30   // :30

// ── Variables ─────────────────────────────────────────────────
int  occupancyCount  = 0;
bool lastState       = false;
bool currentState    = false;
bool alertSent       = false;   // prevents repeated alerts

// ── Get Current Time String ───────────────────────────────────
String getTimeString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "TIME_ERROR";
  }

  char buffer[30];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %I:%M:%S %p", &timeinfo);
  return String(buffer);
}

// ── Get Hour and Minute ───────────────────────────────────────
void getCurrentHourMinute(int &hour, int &minute) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    hour = -1; minute = -1;
    return;
  }
  hour   = timeinfo.tm_hour;
  minute = timeinfo.tm_min;
}

// ── Setup ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

  pinMode(STATE_PIN, INPUT);
  pinMode(LED_PIN,   OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.println("=====================================");
  Serial.println("  Occupancy Detection + Time Sync   ");
  Serial.println("=====================================");

  // ── Connect to WiFi ───────────────────────────────────────
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    // ── Sync Time via NTP ────────────────────────────────
    configTime(gmtOffset, daylightOffset, ntpServer);
    delay(2000);

    Serial.print("⏰ Current Time: ");
    Serial.println(getTimeString());

  } else {
    Serial.println("\n❌ WiFi Failed — Timestamps unavailable");
  }

  Serial.println("-------------------------------------");
  Serial.println("Waiting for Bluetooth connection...");
  Serial.println("Alert scheduled at: 10:30 PM");
  Serial.println("-------------------------------------");
}

// ── Main Loop ─────────────────────────────────────────────────
void loop() {

  // 1. Read STATE pin ──────────────────────────────────────────
  currentState = digitalRead(STATE_PIN);

  // 2. Device CONNECTED ────────────────────────────────────────
  if (currentState == HIGH && lastState == LOW) {
    occupancyCount++;
    digitalWrite(LED_PIN, HIGH);

    String timeNow = getTimeString();

    Serial.println("-------------------------------------");
    Serial.println("✅ Device Connected!");
    Serial.print  ("   Time            : ");
    Serial.println(timeNow);
    Serial.print  ("   Occupancy Count : ");
    Serial.println(occupancyCount);
    Serial.println("-------------------------------------");

    // Notify phone
    Serial2.println("CONNECTED:" + timeNow);
    Serial2.print("OCCUPIED:");
    Serial2.println(occupancyCount);
  }

  // 3. Device DISCONNECTED ─────────────────────────────────────
  if (currentState == LOW && lastState == HIGH) {
    if (occupancyCount > 0) occupancyCount--;
    digitalWrite(LED_PIN, LOW);

    String timeNow = getTimeString();

    Serial.println("-------------------------------------");
    Serial.println("❌ Device Disconnected!");
    Serial.print  ("   Time            : ");
    Serial.println(timeNow);
    Serial.print  ("   Occupancy Count : ");
    Serial.println(occupancyCount);
    Serial.println("-------------------------------------");

    // Notify phone
    Serial2.println("DISCONNECTED:" + timeNow);
    Serial2.print("OCCUPIED:");
    Serial2.println(occupancyCount);
  }

  // 4. End of Reservation Alert at 10:30 PM ────────────────────
  int currentHour, currentMinute;
  getCurrentHourMinute(currentHour, currentMinute);

  if (currentHour  == ALERT_HOUR   &&
      currentMinute == ALERT_MINUTE &&
      !alertSent) {

    String timeNow = getTimeString();

    Serial.println("=====================================");
    Serial.println("⚠️  END OF RESERVATION ALERT!");
    Serial.print  ("   Time: ");
    Serial.println(timeNow);
    Serial.print  ("   Current Occupancy: ");
    Serial.println(occupancyCount);
    Serial.println("=====================================");

    // Send alert to phone
    Serial2.println("ALERT:END_OF_RESERVATION");
    Serial2.println("ALERT:Time is 10:30 PM - Reservation period has ended!");
    Serial2.print("ALERT:Current Occupancy:");
    Serial2.println(occupancyCount);

    alertSent = true;   // prevent repeated alerts
  }

  // Reset alert flag at midnight for next day ──────────────────
  if (currentHour == 0 && currentMinute == 0) {
    alertSent = false;
  }

  // 5. Receive commands from phone ─────────────────────────────
  if (Serial2.available()) {
    String incoming = Serial2.readStringUntil('\n');
    incoming.trim();

    // Filter out automatic module status messages
    if (incoming == "ERROR" || 
        incoming == "OK"    || 
        incoming == "FAIL"  ||
        incoming == "+DISC:SUCCESS") {
      Serial.print("⚙️ Module Status: ");
      Serial.println(incoming);
      lastState = currentState;
      delay(200);
      return;
    }

    Serial.print("📱 Received: ");
    Serial.println(incoming);

    if (incoming == "RESET") {
      occupancyCount = 0;
      digitalWrite(LED_PIN, LOW);
      Serial.println("🔄 Occupancy reset to 0");
      Serial2.println("RESET:OK");
    }

    if (incoming == "COUNT") {
      Serial2.print("OCCUPIED:");
      Serial2.println(occupancyCount);
    }

    if (incoming == "STATUS") {
      Serial2.print("STATUS:");
      Serial2.println(currentState ? "CONNECTED" : "IDLE");
    }

    if (incoming == "TIME") {
      Serial2.print("TIME:");
      Serial2.println(getTimeString());
    }
  }

  // 6. Save state for next loop ────────────────────────────────
  lastState = currentState;
  delay(200);
}
