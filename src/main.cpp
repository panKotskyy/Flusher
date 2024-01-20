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

// Define PINS
#define SERVO_PIN 13
#define SOUND_DIGITAL_PIN 35 // yellow
#define SOUND_ANALOG_PIN 32  // green
#define TRIG_PIN 14          // blue
#define ECHO_PIN 27          // purple
#define PIR_PIN 34           // white

// Define sound speed in cm/uS
#define SOUND_SPEED 0.034

#define DEEP_SLEEP_INTERVAL 3
#define SOUND_THRESHOLD 0
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
#define TZ_INFO "UTC2"

#define BOTtoken "5933476596:AAG-mZ1tfNy0boHKzrOdjFmFLCckUIfEClc"
#define CHAT_ID "-948044538"

RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR int bootCountInterrupt = 0;
RTC_DATA_ATTR bool deepSleepMode = true;
RTC_DATA_ATTR bool sendData = true;
RTC_DATA_ATTR bool debug = true;

unsigned long currentMillis;
RTC_DATA_ATTR unsigned long previousMovementDetected;
unsigned long previousSensorReading = 0;

RTC_DATA_ATTR float defaultDistance;
RTC_DATA_ATTR bool movementOnSeat;
RTC_DATA_ATTR unsigned long previousMovementOnSeat = 0;

int digitalSound;
int analogSound;
int minAnalogSound = 99999;
int maxAnalogSound;
float distance;
RTC_DATA_ATTR float minDistance = 99999;
RTC_DATA_ATTR float maxDistance;
int pos;
bool newRequest = false;

// HTML to build the web page
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Flush Button</title>
    <style>
        body {
            display: flex;
            justify-content: center;
            align-items: center;
            height: 100vh;
            margin: 0;
        }
        button {
            font-size: 1.5em;
            padding: 10px 20px;
        }
    </style>
</head>
<body>
    <button onclick="sendPostRequest()">Flush!</button>

    <script>
        function sendPostRequest() {
            fetch('/', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json'
                    // Add any other headers as needed
                },
                // Add body data if required
                // body: 
            })
            .then(response => {
                // Handle the response as needed
                console.log('POST request sent successfully');
            })
            .catch(error => {
                // Handle errors
                console.error('Error sending POST request:', error);
            });
        }
    </script>
</body>
</html>
)rawliteral";

WiFiManager wifiManager(ssid, password);
AsyncWebServer server(80);
Servo myservo;
WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);
// InfluxDB client instance with preconfigured InfluxCloud certificate
InfluxDBClient dbClient(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

// Data point
Point sensorReadings("measurements");

void initBot();
void initWebServer();
void initServo();
void initSoundDetector();
void initDistanceReader();
void initPir();
void printWakeupReason();

void pirInterrupt();
void readSound();
void readDistance();
void flush();
void handleTelegram();
void handleNewMessages(int numNewMessages);
void storeData();

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

void setup() {
  Serial.begin(115200);
  ++bootCount;
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
    ++bootCountInterrupt;
  }

  wifiManager.connect();
  initWebServer();
  initServo();
  initBot();
  initSoundDetector();
  initDistanceReader();
  initPir();

  if (!defaultDistance) {
    readDistance();
    defaultDistance = distance;
    bot.sendMessage(CHAT_ID, "Reset reason: " + String(esp_reset_reason()));
    bot.sendMessage(CHAT_ID, "Default distance: " + String(defaultDistance));
  }

  // Accurate time is necessary for certificate validation
  // For the fastest time sync find NTP servers in your area: https://www.pool.ntp.org/zone/
  // Syncing progress and the time will be printed to Serial
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
  currentMillis = getTime();
  if (previousSensorReading + SENSOR_READING_INTERVAL < currentMillis) {
    readSound();
    readDistance();
    previousSensorReading = currentMillis;
    if (sendData && distance > 0 && distance < 100) {
      storeData();
    }
  }

  handleTelegram();

  // currentMillis = getTime();
  Serial.printf("isAuto: %d %d %d\n", digitalRead(PIR_PIN) == LOW, movementOnSeat, previousMovementDetected + AUTO_FLUSH_DELAY < currentMillis);
  Serial.printf("millis: %d %d\n", previousMovementDetected, currentMillis);
  if (digitalRead(PIR_PIN) == LOW && movementOnSeat && previousMovementDetected + AUTO_FLUSH_DELAY < currentMillis) {
    bot.sendMessage(CHAT_ID, "Auto Flush");
    newRequest = true;
  }
  
  if (distance < 8 && distance > 0) {
    bot.sendMessage(CHAT_ID, "Manual Flush");
    newRequest = true;
  } 

  if (newRequest) {
    flush();
    movementOnSeat = false;
    newRequest = false;
  }

  if (distance < defaultDistance - 5 && distance > 0) {
    if (!movementOnSeat) {
      bot.sendMessage(CHAT_ID, "Movement DETECTED (" + String(distance) + ")");
    }
    previousMovementDetected = currentMillis;
    movementOnSeat = true;
  }

  if (deepSleepMode && digitalRead(PIR_PIN) == LOW) {
    // Go to sleep now
    Serial.println("Going to sleep now");
    delay(1000);
    // bot.sendMessage(CHAT_ID, "Going to deep sleep...", "");
    if (debug || movementOnSeat) {
      esp_sleep_enable_timer_wakeup(180000000); // 3 min
    } else {
      esp_sleep_enable_timer_wakeup(3600000000); // 60 min
    }
    esp_deep_sleep_start();
  }

  // delay(500);
}

void initBot() {
    client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
    // bot.sendMessage(CHAT_ID, "Flusher Started", "");
}

// Initialize WebServer
void initWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", index_html);
  });

  server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (request->url() == "/") {
      String body(reinterpret_cast<char *>(data), len);

      Serial.printf("Received body: %s", body);

      newRequest = true;

      request->send(200, "text/html", index_html);
    }
  });

  ElegantOTA.begin(&server);
  server.begin();
}

void initServo() {
  myservo.attach(SERVO_PIN);
}

void initSoundDetector() {
  pinMode(SOUND_DIGITAL_PIN, INPUT);
}

void initDistanceReader() {
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
}

void initPir() {
  pinMode(PIR_PIN, INPUT);
  // attachInterrupt(digitalPinToInterrupt(PIR_PIN), pirInterrupt, FALLING);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIR_PIN, HIGH);
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

void pirInterrupt() {
  previousMovementDetected = getTime();
}

void readSound() {
  digitalSound = digitalRead(SOUND_DIGITAL_PIN);
  analogSound = analogRead(SOUND_ANALOG_PIN);

  minAnalogSound = analogSound > 0 && analogSound < minAnalogSound ? analogSound : minAnalogSound;
  maxAnalogSound = analogSound > maxAnalogSound ? analogSound : maxAnalogSound;

  if (analogSound > SOUND_THRESHOLD) {
    Serial.print(analogSound);
    Serial.print("\t");
    Serial.println(digitalSound);
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
      Serial.println("got response");
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
  }
}

void storeData() {
      // Add readings as fields to point
    sensorReadings.addField("bootCount", bootCount);
    sensorReadings.addField("movement", digitalRead(PIR_PIN));
    sensorReadings.addField("distance", distance);
    sensorReadings.addField("digitalSound", digitalSound);
    sensorReadings.addField("analogSound", analogSound);

    // Print what are we exactly writing
    Serial.print("Writing: ");
    Serial.println(dbClient.pointToLineProtocol(sensorReadings));
  
    // Write point into buffer
    dbClient.writePoint(sensorReadings);

    // Clear fields for next usage. Tags remain the same.
    sensorReadings.clearFields();
}

void handleNewMessages(int numNewMessages) {
  Serial.println("handleNewMessages");
  Serial.println(String(numNewMessages));

  for (int i = 0; i < numNewMessages; i++) {
    // Chat id of the requester
    String chat_id = String(bot.messages[i].chat_id);
    if (chat_id != CHAT_ID) {
      bot.sendMessage(chat_id, "Unauthorized user", "");
      continue;
    }

    // Print the received message
    String text = bot.messages[i].text;
    String from_name = bot.messages[i].from_name;
    Serial.printf("Received message: %s\nFrom: %s\n", text, from_name);

    // Handle commands...
    if (text == "/version") {
      bot.sendMessage(CHAT_ID, "0.1v Beta", "");
    }
    if (text == "/restart") {
      bot.sendMessage(CHAT_ID, "Restaring...", "");
      bot.getUpdates(bot.last_message_received + 1);
      esp_restart();
    }
    if (text == "/help") {
      bot.sendMessageWithReplyKeyboard(CHAT_ID, "Choose command:", "", "[[\"/debug\", \"/stat\", \"/flush\", \"/sendData\"], [\"/deepSleep\", \"/restart\", \"/version\"]]", true);
      // bot.sendMessageWithInlineKeyboard(CHAT_ID, "Choose command:", "", "[[\"/debug\", \"/stat\", \"/flush\", \"/sendData\", \"/deepSleep\", \"/restart\", \"/version\"]]");
    }
    if (text == "/flush") {
      newRequest = true;
      bot.sendMessage(chat_id, "Flushing...", "");
    }
    if (text == "/cancel") {
      movementOnSeat = false;
      bot.sendMessage(chat_id, "Flushing canceled", "");
    }
    if (text == "/debug") {
      debug = !debug;
      bot.sendMessage(CHAT_ID, String(debug), "");
    }
    if (text == "/sendData") {
      sendData = !sendData;
      bot.sendMessage(CHAT_ID, String(sendData), "");
    }
    if (text == "/deepSleep") {
      deepSleepMode = !deepSleepMode;
      bot.sendMessage(CHAT_ID, String(deepSleepMode), "");
    }
    if (text == "/stat") {
      String response = "Boot Count: ";
      response += String(bootCount);
      response += "\nBoot Count Interrupt: ";
      response += String(bootCountInterrupt);
      response += "\nDebug: ";
      response += debug ? "Yes" : "No";
      response += "\nMovement Detected: ";
      response += digitalRead(PIR_PIN) ? "Yes" : "No";
      response += "\nMovement On Seat: ";
      response += movementOnSeat ? "Yes" : "No";
      response += "\nDefault Distance: ";
      response += String(defaultDistance);
      response += "\nDistance (min/max): ";
      response += String(distance);
      response += " (";
      response += String(minDistance);
      response += "/";
      response += String(maxDistance);
      response += ")\nAnalog Sound (min/max): ";
      response += String(analogSound);
      response += " (";
      response += String(minAnalogSound);
      response += "/";
      response += String(maxAnalogSound);
      response += ")\nDeep Sleep Mode: ";
      response += deepSleepMode ? "Yes" : "No";
      response += "\nSend Data: ";
      response += sendData ? "Yes" : "No";

      bot.sendMessage(chat_id, response, "");
    }
  }
}

void flush() {
  Serial.println("Flushing...");

  for (pos = 15; pos <= 150; pos += 5) {
    myservo.write(pos);
    delay(10);
  }
  delay(4000);
  for (pos = 150; pos >= 15; pos -= 5) {
    myservo.write(pos);
    delay(10);
  }
}
