#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "ElegantOTA.h"
#include <ESP32Servo.h>
#include "WiFiManager.h"
#include <UniversalTelegramBot.h>
#include "time.h"
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>
#include "LittleFS.h"

// Define PINS
#define SERVO_PIN 13
#define TRIG_PIN 14          // blue
#define ECHO_PIN 27          // purple
#define PIR_PIN 34           // white

// Define sound speed in cm/uS
#define SOUND_SPEED 0.034

// Config
#define DEEP_SLEEP_INTERVAL 3
#define SENSOR_READING_INTERVAL 1
#define AUTO_FLUSH_DELAY 150

// Define network credentials
const char *ssid = "ASUS_60";
const char *password = "bohdan1010";

#define INFLUXDB_URL "https://us-east-1-1.aws.cloud2.influxdata.com"
#define INFLUXDB_TOKEN "OrddY9ea5LGv5bpZNEdnYrBBn7-4nwL1zrZI_5W-XVYghPNeIbh6a7J-1uNnfzaQsk7E78JHdX7l6ypsZNrXBg=="
#define INFLUXDB_ORG "d977e787a53d425a"
#define INFLUXDB_BUCKET "Flusher"
  
// Time zone info
#define TZ_INFO "UTC-2"

#define BOTtoken "5933476596:AAG-mZ1tfNy0boHKzrOdjFmFLCckUIfEClc"
#define CHAT_ID "-948044538"

int pos = 0;

RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR int interruptBootCount = 0;
RTC_DATA_ATTR bool deepSleepMode = false;
RTC_DATA_ATTR bool sendData = true;
RTC_DATA_ATTR bool debug = false;
RTC_DATA_ATTR bool enableManual = false;
RTC_DATA_ATTR bool enableAuto = false;
RTC_DATA_ATTR float defaultDistance;

unsigned long now;
unsigned long previousSensorReading = 0;

RTC_DATA_ATTR bool movementDetected;
RTC_DATA_ATTR unsigned long previousMovement;

float distance;
RTC_DATA_ATTR float minDistance = 99999;
RTC_DATA_ATTR float maxDistance;

bool newRequest = false;

WiFiManager wifiManager(ssid, password);
AsyncWebServer server(80);
Servo myservo;
WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);
InfluxDBClient dbClient(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
Point measurments("measurements");

void initWebServer();
void printWakeupReason();
void readDistance();
void flush();
void handleTelegram();
void handleNewMessages(int numNewMessages);
void storeData();
void setPosition(int newPos);

// Function that gets current epoch time
unsigned long getTime() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return(0);
  }
  time(&now);
  return now;
}

bool isTimeBetweenMidnightAndNine(unsigned long epochTime) {
  // Get the current time struct
  struct tm *timeinfo;
  timeinfo = localtime((time_t*)&epochTime);
  
  // Extract hour and minute from time struct
  int hour = timeinfo->tm_hour;
  int minute = timeinfo->tm_min;
  
  // Check if the time falls between 24:00 and 9:00
  if ((hour >= 0 && hour < 6) || (hour == 6 && minute == 30)) {
    return true;
  } else {
    return false;
  }
}


String getTimeFormatted(unsigned long &ts) {
  struct tm timeinfo;
  time_t timestamp = ts;
  gmtime_r(&timestamp, &timeinfo);
  char formattedTime[9];
  strftime(formattedTime, sizeof(formattedTime), "%H:%M:%S", &timeinfo);

  return String(formattedTime);
}

void setup() {
  Serial.begin(115200);
  
  ++bootCount;
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
    ++interruptBootCount;
  }

  if(!LittleFS.begin()){
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }

  wifiManager.connect();
  initWebServer();
  client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(PIR_PIN, INPUT);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIR_PIN, HIGH);
  
  if (!defaultDistance) {
    readDistance();
    defaultDistance = distance;
    bot.sendMessage(CHAT_ID, "Reset reason: " + String(esp_reset_reason()));
    bot.sendMessage(CHAT_ID, "Default distance: " + String(defaultDistance, 1));
  }

  timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");

  if (dbClient.validateConnection()) {
    Serial.print("Connected to InfluxDB: ");
    Serial.println(dbClient.getServerUrl());
  } else {
    Serial.print("InfluxDB connection failed: ");
    Serial.println(dbClient.getLastErrorMessage());
  }

  printWakeupReason();
}

void loop() {
  now = getTime();
  if (now - previousSensorReading >= SENSOR_READING_INTERVAL) {
    readDistance();
    previousSensorReading = now;
    if (sendData && distance > 0 && distance < 100) {
      storeData();
    }
  }

  handleTelegram();

  if (enableManual && distance < 8 && distance > 0) {
    bot.sendMessage(CHAT_ID, "Manual Flush");
    newRequest = true;
  } else if (defaultDistance - distance > 5 && distance > 0) {
    if (!movementDetected) {
      bot.sendMessage(CHAT_ID, "Movement DETECTED (" + String(distance, 1) + " cm)");
    }
    previousMovement = now;
    movementDetected = true;
  }

  if (digitalRead(PIR_PIN) == LOW && defaultDistance - distance < 1 && movementDetected && now - previousMovement > AUTO_FLUSH_DELAY) {
    if (enableAuto || isTimeBetweenMidnightAndNine(now)) {
      bot.sendMessage(CHAT_ID, "Auto Flush");
      newRequest = true;
    } else {
      movementDetected = false;
    }
  }

  if (newRequest) {
    flush();
    movementDetected = false;
    newRequest = false;
  }

  if (deepSleepMode && digitalRead(PIR_PIN) == LOW) {
    Serial.println("Going to sleep now");
    // delay(1000);
    // bot.sendMessage(CHAT_ID, "Going to deep sleep...");
    if (debug || movementDetected) {
      esp_sleep_enable_timer_wakeup(180000000); // 3 min
    } else {
      esp_sleep_enable_timer_wakeup(3600000000); // 60 min
    }
    esp_deep_sleep_start();
  }
}

// Initialize WebServer
void initWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html");
  });

  server.on("/set", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("position")) {
      int newPos = request->getParam("position")->value().toInt();
      setPosition(newPos);
    }
    if (request->hasParam("servo")) {
      int state = request->getParam("servo")->value().toInt();
      if (state && !myservo.attached()) {
        myservo.attach(SERVO_PIN);
      } else {
        myservo.detach();
      }
    }
  });

  server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (request->url() == "/") {
      String body(reinterpret_cast<char *>(data), len);
      Serial.printf("Received body: %s", body);
      newRequest = true;
      request->send(LittleFS, "/index.html");
    }
  });

  ElegantOTA.begin(&server);
  server.begin();
}

void printWakeupReason() {
  esp_sleep_wakeup_cause_t wakeupReason = esp_sleep_get_wakeup_cause();

  switch (wakeupReason) {
    case ESP_SLEEP_WAKEUP_EXT0:
      Serial.println("Wakeup caused by external signal using RTC_IO");
      break;
    case ESP_SLEEP_WAKEUP_EXT1:
      Serial.println("Wakeup caused by external signal using RTC_CNTL");
      break;
    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("Wakeup caused by timer");
      break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
      Serial.println("Wakeup caused by touchpad");
      break;
    case ESP_SLEEP_WAKEUP_ULP:
      Serial.println("Wakeup caused by ULP program");
      break;
    default:
      Serial.printf("Wakeup was not caused by deep sleep: %d\n", wakeupReason);
      break;
  }
}

void readDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH);

  distance = duration * SOUND_SPEED / 2;

  minDistance = distance > 0 && distance < minDistance ? distance : minDistance;
  maxDistance = distance > maxDistance ? distance : maxDistance;

  Serial.print("Distance (cm): ");
  Serial.println(distance);
}

void handleTelegram() {
  if ((WiFi.getMode() == WIFI_STA) && (WiFi.status() == WL_CONNECTED)) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

    while (numNewMessages) {
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
  }
}

void storeData() {
    measurments.addField("bootCount", bootCount);
    measurments.addField("interruptBootCount", interruptBootCount);
    measurments.addField("movement", digitalRead(PIR_PIN));
    measurments.addField("distance", distance);

    Serial.print("Writing: ");
    Serial.println(dbClient.pointToLineProtocol(measurments));
  
    dbClient.writePoint(measurments);
    measurments.clearFields();
}

void handleNewMessages(int numNewMessages) {
  Serial.println("handleNewMessages");
  Serial.println(String(numNewMessages));

  for (int i = 0; i < numNewMessages; i++) {
    // Chat id of the requester
    String chat_id = String(bot.messages[i].chat_id);
    if (chat_id != CHAT_ID) {
      bot.sendMessage(chat_id, "Unauthorized user");
      continue;
    }

    // Print the received message
    String text = bot.messages[i].text;
    String from_name = bot.messages[i].from_name;
    Serial.printf("Received message: %s\nFrom: %s\n", text, from_name);

    // Handle commands...
    if (text == "/help") {
      bot.sendMessageWithReplyKeyboard(CHAT_ID, "Choose command:", "", "[[\"/version\", \"/stat\", \"/flush\", \"/cancel\"], [\"/debug\", \"/deepSleep\", \"/sendData\", \"/restart\"]]", true);
      // bot.sendMessageWithInlineKeyboard(CHAT_ID, "Choose command:", "", "[[\"/debug\", \"/stat\", \"/flush\", \"/sendData\", \"/deepSleep\", \"/restart\", \"/version\"]]");
    }
    if (text == "/version") {
      bot.sendMessage(CHAT_ID, "0.1v Beta");
    }
    if (text == "/restart") {
      bot.sendMessage(CHAT_ID, "Restaring...");
      bot.getUpdates(bot.last_message_received + 1);
      esp_restart();
    }
    if (text == "/flush") {
      newRequest = true;
      bot.sendMessage(chat_id, "Flushing...");
    }
    if (text == "/cancel") {
      movementDetected = false;
      bot.sendMessage(chat_id, "Flushing canceled");
    }
    if (text == "/debug") {
      debug = !debug;
      bot.sendMessage(CHAT_ID, debug ? "Debug is ON" : "Debug is OFF");
    }
    if (text == "/manual") {
      enableManual = !enableManual;
      bot.sendMessage(CHAT_ID, enableManual ? "Manual Flush is ON" : "Manual Flush is OFF");
    }
    if (text == "/auto") {
      enableAuto = !enableAuto;
      bot.sendMessage(CHAT_ID, enableAuto ? "Auto Flush is ON" : "Auto Flush is OFF");
    }
    if (text == "/sendData") {
      sendData = !sendData;
      bot.sendMessage(CHAT_ID, sendData ? "Send Data is ON" : "Send Data is OFF");
    }
    if (text == "/deepSleep") {
      deepSleepMode = !deepSleepMode;
      bot.sendMessage(CHAT_ID, deepSleepMode ? "Deep Sleep is ON" : "Deep Sleep is OFF");
    }
    if (text == "/stat") {
      String response = "IP: " + WiFi.localIP().toString()
      + "\nBoot Count (by interrupt): " + String(bootCount) + " (" + String(interruptBootCount) + ")"
      + "\nDebug: " + (debug ? "ON" : "OFF")
      + "\nDeep Sleep: " + (deepSleepMode ? "ON" : "OFF")
      + "\nSend Data: " + (sendData ? "ON" : "OFF")
      + "\nManual Flush: " + (enableManual ? "ON" : "OFF")
      + "\nAuto Flush: " + (enableAuto ? "ON" : "OFF")

      + "\n\nPIR: " + (digitalRead(PIR_PIN) ? "HIGH" : "LOW")
      + "\nMovement Detected: " + (movementDetected ? "Yes" : "No")
      + "\nMovement Time: " + getTimeFormatted(previousMovement)

      + "\n\nDistance: " + String(distance, 1)
      + "\nDefault Distance: " + String(defaultDistance, 1)
      + "\nMax Distance: " + String(maxDistance, 1)
      + "\nMin Distance: " + String(minDistance, 1)
      
      + "\nServo position: " + String(myservo.read());

      bot.sendMessage(chat_id, response, "");
    }
  }
}

void flush() {
  Serial.println("Flushing...");
  if (!myservo.attached()) {
    myservo.attach(SERVO_PIN);
  }
  for (int pos = 0; pos <= 75; pos += 5) {
    myservo.write(pos);
    delay(50);
  }
  delay(6000);
  for (int pos = 75; pos >= 0; pos -= 5) {
    myservo.write(pos);
    delay(50);
  }
  myservo.write(10);
  delay(200);
  myservo.write(0);
  delay(300);
  myservo.detach();
}

void setPosition(int newPos) {
  if (!myservo.attached()) {
    myservo.attach(SERVO_PIN);
  }
  if (newPos > pos) {
    for (;pos<=newPos; pos++) {
      myservo.write(pos);
      delay(2);
    }
  } else {
    for (;pos>=newPos;pos--) {
      myservo.write(pos);
      delay(2);
    }
  }
}
