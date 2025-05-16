#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <HTTPClient.h>

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// PIN
#define PROXIMITY_PIN 14
#define RXD2 16
#define TXD2 17

// WiFi
const char* ssid = "Rich";
const char* pass = "anaheim13";
const char* googleScriptURL = "https://script.google.com/macros/s/AKfycbw41LWmtleNll8EYNq8ytNPq1K20G6fY9vDuA_51EKH5UaRjEmP8cGKcSP1O8qNC8tA/exec";
const char* thingspeakURL = "https://api.thingspeak.com/update";
const char* thingspeakAPIKey = "E14N6NIPEFLQZ0R4";

// Data Sensor
unsigned char o2[12];
unsigned int o2c = 0, o2f = 0;
double oxydouble = 0.0, flowdouble = 0.0;
bool objectDetected = false;
bool isTimerRunning = false;
unsigned long startTime = 0, usageTime = 0;

// Queue
QueueHandle_t dataQueue;

// Struct untuk data kiriman
struct SensorData {
  double o2;
  double flow;
  unsigned long usageTime;
};

void setup() {
  Serial.begin(9600);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  pinMode(PROXIMITY_PIN, INPUT);

  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    while (1);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 10);
  display.println("Initializing...");
  display.display();
  delay(1000);

  // Buat queue untuk komunikasi antar task
  dataQueue = xQueueCreate(10, sizeof(SensorData));

  // Jalankan task untuk kirim data di core 1
  xTaskCreatePinnedToCore(
    sendDataTask,
    "SendDataTask",
    4096,
    NULL,
    1,
    NULL,
    1  // Core 1
  );
}

void loop() {
  readSensor();
  updateDisplay();
  updateTimer();

  // Kirim data ke queue setiap 10 detik
  static unsigned long lastSendTime = 0;
  if (millis() - lastSendTime > 10000) {
    lastSendTime = millis();
    SensorData data = {oxydouble, flowdouble, getTotalSeconds()};
    xQueueSend(dataQueue, &data, portMAX_DELAY);
  }

  delay(100);
}

// TASK: Kirim data ke Google Sheets & ThingSpeak (core 1)
void sendDataTask(void *parameter) {
  SensorData data;
  while (true) {
    if (xQueueReceive(dataQueue, &data, portMAX_DELAY)) {
      sendToGoogleSheets(data.o2, data.flow, data.usageTime);
      sendToThingSpeak(data.o2, data.flow);
    }
  }
}

void readSensor() {
  while (Serial2.available() >= 12) {
    if (Serial2.peek() == 0x16) {
      Serial2.readBytes(o2, 12);
      if (o2[0] == 0x16 && o2[1] == 0x09 && o2[2] == 0x01) {
        o2c = (o2[3] << 8) | o2[4];
        o2f = (o2[5] << 8) | o2[6];
        oxydouble = o2c / 10.0;
        flowdouble = o2f / 10.0;
      } else {
        Serial2.read(); // Buang 1 byte jika tidak valid
      }
    } else {
      Serial2.read(); // Buang byte hingga header ditemukan
    }
  }
  objectDetected = digitalRead(PROXIMITY_PIN) == LOW;
}

void updateTimer() {
  if (objectDetected && oxydouble >= 90.0) {
    if (!isTimerRunning) {
      isTimerRunning = true;
      startTime = millis();
    }
  } else if (isTimerRunning) {
    usageTime += millis() - startTime;
    isTimerRunning = false;
  }
}

unsigned long getTotalSeconds() {
  return usageTime / 1000 + (isTimerRunning ? (millis() - startTime) / 1000 : 0);
}

void updateDisplay() {
  unsigned long totalSeconds = getTotalSeconds();
  unsigned int hours = totalSeconds / 3600;
  unsigned int minutes = (totalSeconds % 3600) / 60;
  unsigned int seconds = totalSeconds % 60;

  display.clearDisplay();
  display.setCursor(0, 0);
  display.printf("O2: %.1f%%\nFlow: %.1f LPM\nObject: %s\nTime: %02d:%02d:%02d\n",
                 oxydouble, flowdouble, objectDetected ? "Detected" : "Not", hours, minutes, seconds);
  display.display();
}

void sendToGoogleSheets(double oxygen, double flow, unsigned long timeUsed) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(googleScriptURL);
    http.addHeader("Content-Type", "application/json");

    String jsonPayload = "{\"o2\":" + String(oxygen) + ",\"flow\":" + String(flow) + ",\"usageTime\":" + String(timeUsed) + "}";
    int httpResponseCode = http.POST(jsonPayload);
    Serial.printf("Google Sheets response: %d\n", httpResponseCode);
    http.end();
  } else {
    Serial.println("WiFi not connected (Google Sheets)");
  }
}

void sendToThingSpeak(double oxygen, double flow) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(thingspeakURL);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    String postData = "api_key=" + String(thingspeakAPIKey) +
                      "&field1=" + String(oxygen) +
                      "&field2=" + String(flow);

    int httpResponseCode = http.POST(postData);
    Serial.printf("ThingSpeak response: %d\n", httpResponseCode);
    http.end();
  } else {
    Serial.println("WiFi not connected (ThingSpeak)");
  }
}