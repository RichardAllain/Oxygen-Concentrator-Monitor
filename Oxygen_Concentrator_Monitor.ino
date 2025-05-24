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
bool objectDetected = false, isTimerRunning = false;
unsigned long startTime = 0, usageTime = 0;

// Queue
QueueHandle_t dataQueue;

struct SensorData {
  double o2;
  double flow;
  unsigned long usageTime;
};

void setup() {
  Serial.begin(9600);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  pinMode(PROXIMITY_PIN, INPUT);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    while (1);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 10);
  display.println("Initializing...");
  display.setCursor(0, 20);
  display.println("Connecting WiFi...");
  display.display();

  WiFi.begin(ssid, pass);
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 5000) {
    delay(500);
    Serial.print(".");
  }

  bool wifiConnected = WiFi.status() == WL_CONNECTED;
  Serial.println(wifiConnected ? "\nWiFi connected" : "\nWiFi not connected");
  display.setCursor(0, 30);
  display.println(wifiConnected ? "WiFi Connected" : "WiFi Offline Mode");
  display.display();

  dataQueue = xQueueCreate(10, sizeof(SensorData));

  xTaskCreatePinnedToCore(
    sendDataTask, "SendDataTask", 4096, NULL, 1, NULL, 1
  );
}

void loop() {
  readSensor();
  updateDisplay();
  updateTimer();

  static unsigned long lastSendTime = 0;
  if (millis() - lastSendTime > 10000) {
    lastSendTime = millis();
    SensorData data = {oxydouble, flowdouble, getTotalSeconds()};
    xQueueSend(dataQueue, &data, portMAX_DELAY);
  }

  delay(100);
}

void sendDataTask(void *parameter) {
  SensorData data;
  while (true) {
    if (xQueueReceive(dataQueue, &data, portMAX_DELAY)) {
      if (WiFi.status() == WL_CONNECTED) {
        sendToGoogleSheets(data.o2, data.flow, data.usageTime);
        sendToThingSpeak(data.o2, data.flow);
      } else {
        Serial.println("Offline mode: Data not sent");
      }
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
        Serial2.read();
      }
    } else {
      Serial2.read();
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
  unsigned long t = getTotalSeconds();
  display.clearDisplay();
  display.setCursor(0, 0);
  display.printf("O2: %.1f%%\nFlow: %.1f LPM\nObject: %s\nTime: %02lu:%02lu:%02lu\n",
                 oxydouble, flowdouble,
                 objectDetected ? "Detected" : "Not Detected",
                 t / 3600, (t % 3600) / 60, t % 60);
  display.display();
}

void sendToGoogleSheets(double oxygen, double flow, unsigned long timeUsed) {
  HTTPClient http;
  http.begin(googleScriptURL);
  http.addHeader("Content-Type", "application/json");
  String payload = "{\"o2\":" + String(oxygen) +
                   ",\"flow\":" + String(flow) +
                   ",\"usageTime\":" + String(timeUsed) + "}";
  int response = http.POST(payload);
  Serial.printf("Google Sheets response: %d\n", response);
  http.end();
}

void sendToThingSpeak(double oxygen, double flow) {
  HTTPClient http;
  http.begin(thingspeakURL);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String postData = "api_key=" + String(thingspeakAPIKey) +
                    "&field1=" + String(oxygen) +
                    "&field2=" + String(flow);
  int response = http.POST(postData);
  Serial.printf("ThingSpeak response: %d\n", response);
  http.end();
}
