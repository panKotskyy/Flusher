#include <Arduino.h>
#include <ESP32Servo.h>
#include <ESPAsyncWebServer.h>
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>
#include <UniversalTelegramBot.h>
#include <WiFi.h>

#include "ElegantOTA.h"
#include "LittleFS.h"
#include "WiFiManager.h"
#include "time.h"

// Constants for pin definitions
#define SERVO_PIN 13
#define TRIG_PIN 14  // blue
#define ECHO_PIN 27  // purple
#define PIR_PIN 34   // white
#define FORCE_SENSOR_PIN 33

// Constants for sound speed in cm/uS
#define SOUND_SPEED 0.034

// Network credentials
const char *ssid = "ASUS_60";
const char *password = "bohdan1010";

// Database credentials
#define INFLUXDB_URL "https://us-east-1-1.aws.cloud2.influxdata.com"
#define INFLUXDB_TOKEN "OrddY9ea5LGv5bpZNEdnYrBBn7-4nwL1zrZI_5W-XVYghPNeIbh6a7J-1uNnfzaQsk7E78JHdX7l6ypsZNrXBg=="
#define INFLUXDB_ORG "d977e787a53d425a"
#define INFLUXDB_BUCKET "Flusher"

// Time zone info
#define TZ_INFO "EET-2EEST,M3.5.0/3,M10.5.0/4"

// Telegram bot token and chat ID
#define BOTtoken "5933476596:AAG-mZ1tfNy0boHKzrOdjFmFLCckUIfEClc"
#define CHAT_ID "-948044538"

struct Config {
  int deepSleepInterval;
  int sensorReadingInterval;
  int forceSensorSendingInterval;
  int autoFlushDelay;
  bool deepSleepMode;
  bool sendData;
  bool debug;
  bool enableManual;
  bool enableAuto;
  int autoFlushStartTime;
  int autoFlushStopTime;
};

RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR int interruptBootCount = 0;
RTC_DATA_ATTR float defaultDistance;
RTC_DATA_ATTR bool movementDetected;
RTC_DATA_ATTR unsigned long previousMovement;

unsigned long currentTime;
unsigned long previousSensorReading = 0;
unsigned long previousForceSensorSend = 0;
float distance;
int force;
int forceDigital;
int pos = 0;
bool newRequest = false;

Config config;
WiFiManager wifiManager(ssid, password);
AsyncWebServer server(80);
Servo myservo;
WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);
InfluxDBClient dbClient(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
Point measurments("measurements");

void handleCounters();
void initSerial();
void initWiFi();
void initFileSystem();
void initWebServer();
void intitTelegram();
void initSensors();
void initDB();
void printWakeupReason();

unsigned long getCurrentTime();
void handleSensorReading();
void handleTelegramMessages();
void handleForceDetection();
void handleMovementDetection();
void handleAutoFlush();
void handleDeepSleep();

void setup() {
  handleCounters();
  initWiFi();
  initFileSystem();
  initWebServer();
  intitTelegram();
  initSensors();
  initDB();
  printWakeupReason();
}

void loop() {
  currentTime = getCurrentTime();
  handleSensorReading();
  handleTelegramMessages();
  handleForceDetection();
  handleMovementDetection();
  handleAutoFlush();
  handleDeepSleep();
}

void handleCounters() {
  ++bootCount;
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
    ++interruptBootCount;
  }
}

void initSerial() {
  Serial.begin(115200);
}

void initWiFi() {
  wifiManager.connect();
}

bool loadConfig() {
  if (!LittleFS.begin()) {
    Serial.println("Failed to mount LittleFS");
    return false;
  }

  File file = LittleFS.open("/config.json", "r");
  if (!file) {
    Serial.println("Failed to open config file");
    return false;
  }

  size_t size = file.size();
  if (size > 1024) {
    Serial.println("Config file size is too large");
    return false;
  }

  std::unique_ptr<char[]> buf(new char[size]);
  file.readBytes(buf.get(), size);

  DynamicJsonDocument doc(1024);
  auto error = deserializeJson(doc, buf.get());
  if (error) {
    Serial.println("Failed to parse config file");
    return false;
  }

  config.deepSleepInterval = doc["deepSleepInterval"];
  config.sensorReadingInterval = doc["sensorReadingInterval"];
  config.forceSensorSendingInterval = doc["forceSensorSendingInterval"];
  config.autoFlushDelay = doc["autoFlushDelay"];
  config.deepSleepMode = doc["deepSleepMode"];
  config.sendData = doc["sendData"];
  config.debug = doc["debug"];
  config.enableManual = doc["enableManual"];
  config.enableAuto = doc["enableAuto"];
  config.autoFlushStartTime = doc["autoFlushStartTime"];
  config.autoFlushStopTime = doc["autoFlushStopTime"];

  return true;
}

bool saveConfig() {
  DynamicJsonDocument doc(1024);

  doc["deepSleepInterval"] = config.deepSleepInterval;
  doc["sensorReadingInterval"] = config.sensorReadingInterval;
  doc["forceSensorSendingInterval"] = config.forceSensorSendingInterval;
  doc["autoFlushDelay"] = config.autoFlushDelay;
  doc["deepSleepMode"] = config.deepSleepMode;
  doc["sendData"] = config.sendData;
  doc["debug"] = config.debug;
  doc["enableManual"] = config.enableManual;
  doc["enableAuto"] = config.enableAuto;
  doc["autoFlushStartTime"] = config.autoFlushStartTime;
  doc["autoFlushStopTime"] = config.autoFlushStopTime;

  File file = LittleFS.open("/config.json", "w");
  if (!file) {
    Serial.println("Failed to open config file for writing");
    return false;
  }

  serializeJson(doc, file);
  return true;
}

void initFileSystem() {
  if (!LittleFS.begin()) {
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }

  if (!loadConfig()) {
    Serial.println("Failed to load config");
    // Set default values if config loading fails
    config.deepSleepInterval = 3;
    config.sensorReadingInterval = 1;
    config.forceSensorSendingInterval = 3;
    config.autoFlushDelay = 40;
    config.deepSleepMode = false;
    config.sendData = true;
    config.debug = false;
    config.enableManual = false;
    config.enableAuto = false;
    config.autoFlushStartTime = 0;   // midnight
    config.autoFlushStopTime = 420;  // 7AM
    saveConfig();
  }
}

void intitTelegram() {
  client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
}

float readDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH);

  float distance = duration * SOUND_SPEED / 2;

  Serial.print("Distance (cm): ");
  Serial.println(distance);

  return distance;
}

void initSensors() {
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(PIR_PIN, INPUT);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIR_PIN, HIGH);

  if (!defaultDistance) {
    defaultDistance = readDistance();
    bot.sendMessage(CHAT_ID, "Reset reason: " + String(esp_reset_reason()));
    bot.sendMessage(CHAT_ID, "Default distance: " + String(defaultDistance, 1));
  }
}

void initDB() {
  timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");
  delay(1000);
  // configTzTime(TZ_INFO, "0.ua.pool.ntp.org");

  if (dbClient.validateConnection()) {
    Serial.print("Connected to InfluxDB: ");
    Serial.println(dbClient.getServerUrl());
  } else {
    Serial.print("InfluxDB connection failed: ");
    Serial.println(dbClient.getLastErrorMessage());
  }
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

unsigned long getCurrentTime() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return (0);
  }
  time(&now);
  return now;
}

void storeData() {
  measurments.addField("bootCount", bootCount);
  measurments.addField("interruptBootCount", interruptBootCount);
  measurments.addField("movement", digitalRead(PIR_PIN));
  measurments.addField("distance", distance);
  measurments.addField("force", force);

  Serial.print("Writing: ");
  Serial.println(dbClient.pointToLineProtocol(measurments));

  dbClient.writePoint(measurments);
  measurments.clearFields();
}

void handleSensorReading() {
  if (currentTime - previousSensorReading >= config.sensorReadingInterval) {
    readDistance();
    force = analogRead(FORCE_SENSOR_PIN);
    forceDigital = digitalRead(FORCE_SENSOR_PIN);
    previousSensorReading = currentTime;
    if (config.sendData && distance > 0 && distance < defaultDistance) {
      storeData();
    }
  }
}

String minutesToHHMM(int minutes) {
  int hours = minutes / 60;
  int remainingMinutes = minutes % 60;
  char formattedTime[6];  // "HHLMM" + '\0'
  sprintf(formattedTime, "%02d:%02d", hours, remainingMinutes);
  return String(formattedTime);
}

String getTimeFormatted(unsigned long &ts) {
  struct tm timeinfo;
  time_t timestamp = ts;
  localtime_r(&timestamp, &timeinfo);
  char formattedTime[9];
  strftime(formattedTime, sizeof(formattedTime), "%H:%M:%S", &timeinfo);

  return String(formattedTime);
}

bool isTimeInRange(unsigned long epochTime) {
  // Get the current time struct
  struct tm *timeinfo;
  timeinfo = localtime((time_t *)&epochTime);

  int currentMinutes = timeinfo->tm_hour * 60 + timeinfo->tm_min;
  if (config.autoFlushStartTime > config.autoFlushStopTime) {
    return currentMinutes > config.autoFlushStartTime || currentMinutes < config.autoFlushStopTime;
  } else {
    return currentMinutes > config.autoFlushStartTime && currentMinutes < config.autoFlushStopTime;
  }
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
      config.debug = !config.debug;
      bot.sendMessage(CHAT_ID, config.debug ? "Debug is ON" : "Debug is OFF");
    }
    if (text == "/manual") {
      config.enableManual = !config.enableManual;
      bot.sendMessage(CHAT_ID, config.enableManual ? "Manual Flush is ON" : "Manual Flush is OFF");
    }
    if (text == "/auto") {
      config.enableAuto = !config.enableAuto;
      bot.sendMessage(CHAT_ID, config.enableAuto ? "Auto Flush is ON" : "Auto Flush is OFF");
    }
    if (text == "/sendData") {
      config.sendData = !config.sendData;
      bot.sendMessage(CHAT_ID, config.sendData ? "Send Data is ON" : "Send Data is OFF");
    }
    if (text == "/deepSleep") {
      config.deepSleepMode = !config.deepSleepMode;
      bot.sendMessage(CHAT_ID, config.deepSleepMode ? "Deep Sleep is ON" : "Deep Sleep is OFF");
    }
    if (text == "/stat") {
      String response = "IP: " + WiFi.localIP().toString()
      + "\nBoot Count (by interrupt): " + String(bootCount) + " (" + String(interruptBootCount) + ")\n"
      + "\nDebug: " + (config.debug ? "ON" : "OFF")
      + "\nDeep Sleep: " + (config.deepSleepMode ? "ON" : "OFF")
      + "\nSend Data: " + (config.sendData ? "ON" : "OFF")
      + "\nManual Flush: " + (config.enableManual ? "ON" : "OFF")
      + "\nAuto Flush: " + (config.enableAuto ? "ON" : "OFF")
      + "\nAuto Flush Starts: " + minutesToHHMM(config.autoFlushStartTime)
      + "\nAuto Flush Ends: " + minutesToHHMM(config.autoFlushStopTime)
      + "\nNow: " + getTimeFormatted(currentTime) + " (" + String(currentTime) + ")"
      + "\ninRange: " + (isTimeInRange(currentTime) ? "YES" : "NO")

      + "\n\nPIR: " + (digitalRead(PIR_PIN) ? "HIGH" : "LOW")
      + "\nMovement Detected: " + (movementDetected ? "Yes" : "No")
      + "\nMovement Time: " + getTimeFormatted(previousMovement)

      + "\n\nDistance: " + String(distance, 1)
      + "\nDefault Distance: " + String(defaultDistance, 1);

      bot.sendMessage(chat_id, response, "");
    }
  }
}

void handleTelegramMessages() {
  if ((WiFi.getMode() == WIFI_STA) && (WiFi.status() == WL_CONNECTED)) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

    while (numNewMessages) {
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
  }
}

void handleForceDetection() {
  if (force > 300 && currentTime - previousForceSensorSend >= config.forceSensorSendingInterval) {
    bot.sendMessage(CHAT_ID, "Force DETECTED (" + String(force) + ", " + String(forceDigital) + ")");
    previousForceSensorSend = currentTime;
  }
}

void handleMovementDetection() {
  if (config.enableManual && distance < 8 && distance > 0) {
    bot.sendMessage(CHAT_ID, "Manual Flush");
    newRequest = true;
  } else if (defaultDistance - distance > 5 && distance > 0) {
    if (!movementDetected) {
      bot.sendMessage(CHAT_ID, "Movement DETECTED (" + String(distance, 1) + " cm)");
    }
    previousMovement = currentTime;
    movementDetected = true;
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

void handleAutoFlush() {
  if (digitalRead(PIR_PIN) == LOW && defaultDistance - distance < 1 && movementDetected && currentTime - previousMovement > config.autoFlushDelay) {
    if (config.enableAuto && isTimeInRange(currentTime)) {
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
}

void handleDeepSleep() {
  if (config.deepSleepMode && digitalRead(PIR_PIN) == LOW) {
    Serial.println("Going to sleep now");
    // delay(1000);
    // bot.sendMessage(CHAT_ID, "Going to deep sleep...");
    if (config.debug || movementDetected) {
      esp_sleep_enable_timer_wakeup(180000000);  // 3 min
    } else {
      esp_sleep_enable_timer_wakeup(3600000000);  // 60 min
    }
    esp_deep_sleep_start();
  }
}

void handleSettingsRequest(AsyncWebServerRequest *request) {
  // Create a JSON document
  StaticJsonDocument<512> jsonDoc;

  // Populate the JSON document with the configuration settings
  jsonDoc["deepSleepInterval"] = config.deepSleepInterval;
  jsonDoc["sensorReadingInterval"] = config.sensorReadingInterval;
  jsonDoc["forceSensorSendInterval"] = config.forceSensorSendingInterval;
  jsonDoc["autoFlushDelay"] = config.autoFlushDelay;
  jsonDoc["deepSleepMode"] = config.deepSleepMode;
  jsonDoc["sendData"] = config.sendData;
  jsonDoc["debug"] = config.debug;
  jsonDoc["enableManual"] = config.enableManual;
  jsonDoc["enableAuto"] = config.enableAuto;
  jsonDoc["autoFlushStartTime"] = config.autoFlushStartTime;
  jsonDoc["autoFlushStopTime"] = config.autoFlushStopTime;

  // Convert the JSON document to a string
  String jsonString;
  serializeJson(jsonDoc, jsonString);

  // Send the JSON string as a response
  request->send(200, "application/json", jsonString);
}

void setPosition(int newPos) {
  if (!myservo.attached()) {
    myservo.attach(SERVO_PIN);
  }
  if (newPos > pos) {
    for (; pos <= newPos; pos++) {
      myservo.write(pos);
      delay(2);
    }
  } else {
    for (; pos >= newPos; pos--) {
      myservo.write(pos);
      delay(2);
    }
  }
}

// Initialize WebServer
void initWebServer() {
  // Route to load index.html file
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html");
  });

  // Route to load styles.css file
  server.on("/styles.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/styles.css", "text/css");
  });

  // Route to load script.js file
  server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/script.js", "text/javascript");
  });

  // Serve the settings as JSON
  server.on("/settings", HTTP_GET, handleSettingsRequest);

  server.on("/set", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("position")) {
      int newPos = request->getParam("position")->value().toInt();
      setPosition(newPos);
    } else if (request->hasParam("servo")) {
      int state = request->getParam("servo")->value().toInt();
      if (state && !myservo.attached()) {
        myservo.attach(SERVO_PIN);
      } else {
        myservo.detach();
      }
    } else {
      if (request->hasParam("deepSleepInterval")) {
        config.deepSleepInterval = request->getParam("deepSleepInterval")->value().toInt();
      }
      if (request->hasParam("sensorReadingInterval")) {
        config.sensorReadingInterval = request->getParam("sensorReadingInterval")->value().toInt();
      }
      if (request->hasParam("forceSensorSendingInterval")) {
        config.forceSensorSendingInterval = request->getParam("forceSensorSendingInterval")->value().toInt();
      }
      if (request->hasParam("autoFlushDelay")) {
        config.autoFlushDelay = request->getParam("autoFlushDelay")->value().toInt();
      }
      if (request->hasParam("deepSleepMode")) {
        config.deepSleepMode = request->getParam("deepSleepMode")->value().equalsIgnoreCase("true");
      }
      if (request->hasParam("sendData")) {
        config.sendData = request->getParam("sendData")->value().equalsIgnoreCase("true");
      }
      if (request->hasParam("debug")) {
        config.debug = request->getParam("debug")->value().equalsIgnoreCase("true");
      }
      if (request->hasParam("enableManual")) {
        config.enableManual = request->getParam("enableManual")->value().equalsIgnoreCase("true");
      }
      if (request->hasParam("enableAuto")) {
        config.enableAuto = request->getParam("enableAuto")->value().equalsIgnoreCase("true");
      }
      if (request->hasParam("autoFlushStartTime")) {
        config.autoFlushStartTime = request->getParam("autoFlushStartTime")->value().toInt();
      }
      if (request->hasParam("autoFlushStopTime")) {
        config.autoFlushStopTime = request->getParam("autoFlushStopTime")->value().toInt();
      }
      saveConfig();
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
