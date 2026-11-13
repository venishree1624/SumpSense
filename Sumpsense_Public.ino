#define BLYNK_TEMPLATE_ID   "YOUR_TEMPLATE_ID"
#define BLYNK_TEMPLATE_NAME "SumpSense"
#define BLYNK_AUTH_TOKEN    "YOUR_BLYNK_AUTH_TOKEN"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <U8g2lib.h>

// Wi-Fi credentials
char ssid[] = "YOUR_WIFI_NAME";
char pass[] = "YOUR_WIFI_PASSWORD";

BlynkTimer timer;

// OLED: SH1106 128x64
#define OLED_SDA 13
#define OLED_SCL 26

U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(
  U8G2_R0, U8X8_PIN_NONE
);

// HC-SR04
#define TRIG_PIN 27
#define ECHO_PIN 34

// RGB LED
#define RED_PIN   25
#define GREEN_PIN 33
#define BLUE_PIN  32

// Buzzer and physical button
#define BUZZER_PIN 14
#define BUTTON_PIN 35

// Physical measurements
const float CONTAINER_HEIGHT_CM = 18.0;
const float SENSOR_TO_BOTTOM_CM = 21.0;
const float LOW_WATER_DEPTH_CM = 4.0;
const float HIGH_WATER_DEPTH_CM = 14.0;

// Button debounce
const unsigned long DEBOUNCE_MS = 50;
bool rawButtonState = HIGH;
bool stableButtonState = HIGH;
unsigned long lastButtonChange = 0;

// Alarm state
bool alarmMuted = false;
bool remoteMute = false;

// Connection animation
bool hasConnectedOnce = false;
unsigned long lastBlynkAttempt = 0;
unsigned long lastAnimationUpdate = 0;
int animationStep = 0;

// Sensor state
enum WaterStatus {
  SENSOR_ERROR,
  HIGH_WATER,
  NORMAL_WATER,
  LOW_WATER
};

WaterStatus currentStatus = SENSOR_ERROR;
WaterStatus previousStatus = SENSOR_ERROR;

float lastDistance = -1.0;

// --------------------------------------------------
// LED control
// --------------------------------------------------

void setLED(bool red, bool green, bool blue) {
  digitalWrite(RED_PIN, red ? HIGH : LOW);
  digitalWrite(GREEN_PIN, green ? HIGH : LOW);
  digitalWrite(BLUE_PIN, blue ? HIGH : LOW);
}

// --------------------------------------------------
// One ultrasonic reading
// --------------------------------------------------

float readOneDistanceCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(3);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 30000);

  if (duration == 0) return -1.0;

  float distance = duration * 0.0343 / 2.0;

  // Reject readings outside the useful measurement range.
  if (distance < 2.0 || distance > 400.0) {
    return -1.0;
  }

  return distance;
}

// --------------------------------------------------
// Median filter: rejects occasional bad readings
// --------------------------------------------------

float readDistanceCM() {
  const int SAMPLE_COUNT = 5;
  float samples[SAMPLE_COUNT];
  int validCount = 0;

  for (int i = 0; i < SAMPLE_COUNT; i++) {
    float reading = readOneDistanceCM();

    if (reading >= 0) {
      samples[validCount++] = reading;
    }

    delay(25);
  }

  // Require at least 3 valid readings.
  if (validCount < 3) return -1.0;

  // Sort readings in ascending order.
  for (int i = 0; i < validCount - 1; i++) {
    for (int j = i + 1; j < validCount; j++) {
      if (samples[j] < samples[i]) {
        float temp = samples[i];
        samples[i] = samples[j];
        samples[j] = temp;
      }
    }
  }

  // Return the median.
  return samples[validCount / 2];
}

// --------------------------------------------------
// Water depth and percentage
// --------------------------------------------------

float calculateWaterDepth(float distance) {
  float depth = SENSOR_TO_BOTTOM_CM - distance;

  if (depth < 0) depth = 0;
  if (depth > CONTAINER_HEIGHT_CM) {
    depth = CONTAINER_HEIGHT_CM;
  }

  return depth;
}

int calculateWaterPercentage(float distance) {
  float depth = calculateWaterDepth(distance);

  return (int)(
    depth * 100.0 / CONTAINER_HEIGHT_CM + 0.5
  );
}

// --------------------------------------------------
// Text labels
// --------------------------------------------------

const char* getStatusText() {
  switch (currentStatus) {
    case HIGH_WATER:   return "High Water!";
    case NORMAL_WATER: return "Medium Water";
    case LOW_WATER:    return "Low Water!";
    default:           return "Sensor Error";
  }
}

const char* getAlarmText() {
  if (currentStatus == SENSOR_ERROR ||
      currentStatus == NORMAL_WATER) {
    return "Off";
  }

  if (alarmMuted || remoteMute) return "Muted";

  return "Active";
}

bool isAlarmActive() {
  return (currentStatus == HIGH_WATER ||
          currentStatus == LOW_WATER) &&
         !alarmMuted && !remoteMute;
}

// --------------------------------------------------
// OLED Screen 1: startup
// --------------------------------------------------

void showStartupScreen() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x12_tf);

  const char* title = "SumpSense";
  const char* subtitle = "Water Level Monitoring";

  oled.drawStr(
    (128 - oled.getStrWidth(title)) / 2,
    25, title
  );

  oled.drawStr(
    (128 - oled.getStrWidth(subtitle)) / 2,
    45, subtitle
  );

  oled.sendBuffer();
}

// --------------------------------------------------
// OLED Screen 2: connecting animation
// --------------------------------------------------

void showConnectingScreen() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x12_tf);

  const char* message = "Connecting";

  oled.drawStr(
    (128 - oled.getStrWidth(message)) / 2,
    24, message
  );

  const int barX = 14;
  const int barY = 37;
  const int barW = 100;
  const int barH = 12;
  const int segmentW = 24;
  const int travelW = barW - segmentW - 4;

  oled.drawFrame(barX, barY, barW, barH);

  int segmentX = barX + 2 +
    ((animationStep * 4) % (travelW + 1));

  oled.drawBox(segmentX, barY + 2, segmentW, barH - 4);

  animationStep++;
  oled.sendBuffer();
}

// --------------------------------------------------
// OLED Screen 3: monitoring
// --------------------------------------------------

void showMonitoringScreen() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x12_tf);

  oled.drawStr(0, 10, "SumpSense");
  oled.drawHLine(0, 14, 128);

  char text[32];

  if (lastDistance >= 0) {
    snprintf(text, sizeof(text),
             "Distance: %.1f cm", lastDistance);
  } else {
    snprintf(text, sizeof(text), "Distance: --");
  }
  oled.drawStr(0, 26, text);

  if (lastDistance >= 0) {
    snprintf(text, sizeof(text), "Level: %d%%",
             calculateWaterPercentage(lastDistance));
  } else {
    snprintf(text, sizeof(text), "Level: --");
  }
  oled.drawStr(0, 38, text);

  snprintf(text, sizeof(text), "Status: %s", getStatusText());
  oled.drawStr(0, 50, text);

  snprintf(text, sizeof(text), "Alarm: %s", getAlarmText());
  oled.drawStr(0, 62, text);

  oled.sendBuffer();
}

// --------------------------------------------------
// Blynk remote mute
// --------------------------------------------------

BLYNK_WRITE(V6) {
  remoteMute = (param.asInt() == 1);
}

BLYNK_CONNECTED() {
  Blynk.virtualWrite(V9, "Connected");
  Blynk.virtualWrite(V6, remoteMute ? 1 : 0);
}

// --------------------------------------------------
// Update Blynk dashboard
// --------------------------------------------------

void updateBlynk() {
  if (!Blynk.connected()) return;

  if (lastDistance < 0) {
    Blynk.virtualWrite(V1, "Sensor Error");
    Blynk.virtualWrite(V2, "Off");
    Blynk.virtualWrite(V3, 0);
    Blynk.virtualWrite(V4, 0);
    Blynk.virtualWrite(V5, 0);
    return;
  }

  Blynk.virtualWrite(V0, lastDistance);
  Blynk.virtualWrite(V8, calculateWaterPercentage(lastDistance));
  Blynk.virtualWrite(V1, getStatusText());
  Blynk.virtualWrite(V2, getAlarmText());

  Blynk.virtualWrite(V3, currentStatus == HIGH_WATER ? 1 : 0);
  Blynk.virtualWrite(V4, currentStatus == NORMAL_WATER ? 1 : 0);
  Blynk.virtualWrite(V5, currentStatus == LOW_WATER ? 1 : 0);
}

// --------------------------------------------------
// Debounced physical mute button
// --------------------------------------------------

void handlePhysicalButton() {
  static bool lastRawReading = HIGH;
  static bool stableReading = HIGH;
  static unsigned long lastChangeTime = 0;

  bool reading = digitalRead(BUTTON_PIN);

  if (reading != lastRawReading) {
    lastRawReading = reading;
    lastChangeTime = millis();
  }

  // Confirm the button state after 30 ms.
  if (millis() - lastChangeTime >= 30) {
    if (reading != stableReading) {
      stableReading = reading;

      // Button pressed: GPIO35 goes LOW.
      if (stableReading == LOW &&
          (currentStatus == HIGH_WATER ||
           currentStatus == LOW_WATER)) {

        alarmMuted = true;

        if (Blynk.connected()) {
          Blynk.virtualWrite(V6, 1);
        }

        Serial.println("Physical button: Alarm muted");
      }
    }
  }
}

// --------------------------------------------------
// Sensor, alarms and notifications
// --------------------------------------------------

void readAndUpdateSensor() {
  handlePhysicalButton();

  lastDistance = readDistanceCM();

  previousStatus = currentStatus;

  if (lastDistance < 0) {
    currentStatus = SENSOR_ERROR;
    setLED(false, false, false);
    digitalWrite(BUZZER_PIN, LOW);

    if (hasConnectedOnce) showMonitoringScreen();
    return;
  }

  float waterDepth = calculateWaterDepth(lastDistance);

  if (waterDepth < LOW_WATER_DEPTH_CM) {
    currentStatus = LOW_WATER;
    setLED(false, false, true);
  }
  else if (waterDepth > HIGH_WATER_DEPTH_CM) {
    currentStatus = HIGH_WATER;
    setLED(true, false, false);
  }
  else {
    currentStatus = NORMAL_WATER;
    setLED(false, true, false);

    // Reset mute when water returns to medium level.
    alarmMuted = false;
    remoteMute = false;

    if (Blynk.connected()) {
      Blynk.virtualWrite(V6, 0);
    }
  }

  digitalWrite(BUZZER_PIN, isAlarmActive() ? HIGH : LOW);

  // Trigger only when entering a warning status.
  if (currentStatus != previousStatus &&
      Blynk.connected()) {

    if (currentStatus == HIGH_WATER) {
      Blynk.logEvent(
        "high_water",
        "SumpSense alert: high water level detected."
      );
    }
    else if (currentStatus == LOW_WATER) {
      Blynk.logEvent(
        "low_water",
        "SumpSense alert: low water level detected."
      );
    }
  }

  if (hasConnectedOnce) showMonitoringScreen();

  Serial.print("Distance: ");
  Serial.print(lastDistance);
  Serial.print(" cm | Level: ");
  Serial.print(calculateWaterPercentage(lastDistance));
  Serial.print("% | Status: ");
  Serial.print(getStatusText());
  Serial.print(" | Alarm: ");
  Serial.println(getAlarmText());
}

// --------------------------------------------------
// Setup
// --------------------------------------------------

void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);

  pinMode(BUZZER_PIN, OUTPUT);

  // GPIO35 needs the existing external pull-up resistor.
  pinMode(BUTTON_PIN, INPUT);

  digitalWrite(TRIG_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  setLED(false, false, false);

  Wire.begin(OLED_SDA, OLED_SCL);
  oled.begin();

  // Screen 1
  showStartupScreen();
  delay(1800);

  // Screen 2
  showConnectingScreen();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  Blynk.config(BLYNK_AUTH_TOKEN);

  timer.setInterval(700L, readAndUpdateSensor);
  timer.setInterval(2000L, updateBlynk);

  Serial.println("SumpSense starting...");
}

// --------------------------------------------------
// Main loop
// --------------------------------------------------

void loop() {
  Blynk.run();
  timer.run();

  if (WiFi.status() == WL_CONNECTED &&
      !Blynk.connected() &&
      millis() - lastBlynkAttempt >= 5000) {
    lastBlynkAttempt = millis();
    Blynk.connect(100);
  }

  if (Blynk.connected() && !hasConnectedOnce) {
    hasConnectedOnce = true;
    showMonitoringScreen();
  }

  if (!hasConnectedOnce &&
      millis() - lastAnimationUpdate >= 150) {
    lastAnimationUpdate = millis();
    showConnectingScreen();
  }
}
