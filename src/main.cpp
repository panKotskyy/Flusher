#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "ElegantOTA.h"
#include <ESP32Servo.h>
#include "WiFiManager.h"
#include <UniversalTelegramBot.h>
#include <WiFiClientSecure.h>
#include <FirebaseESP32.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include "time.h"

// Define PINS
#define SERVO_PIN 13
#define SOUND_DIGITAL_PIN 35 // yellow
#define SOUND_ANALOG_PIN 32  // green
#define TRIG_PIN 14          // blue
#define ECHO_PIN 27          // purple
#define PIR_PIN 34           // white

// Define sound speed in cm/uS
#define SOUND_SPEED 0.034

#define DEEP_SLEEP_INTERVAL 180000
#define SOUND_THRESHOLD 0
#define SENSOR_READING_INTERVAL 1000

// Define network credentials
const char *ssid = "ASUS_60";
const char *password = "bohdan1010";

#define API_KEY "AIzaSyDrYZiMsJ-ZG2JxN3060mgpHgKIbvBvCv8"
// Insert Authorized Email and Corresponding Password
#define USER_EMAIL "b.kotskyy@gmail.com"
#define USER_PASSWORD "parachute"
// Insert RTDB URLefine the RTDB URL
#define DATABASE_URL "https://flusher-900f7-default-rtdb.europe-west1.firebasedatabase.app"

const char* ntpServer = "pool.ntp.org";

#define BOTtoken "5933476596:AAG-mZ1tfNy0boHKzrOdjFmFLCckUIfEClc"
#define CHAT_ID "-948044538"

RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR bool deepSleepMode;
RTC_DATA_ATTR bool sendData;

unsigned long currentMillis;
unsigned long previousMovementDetected = 0;
unsigned long previousSensorReading = 0;

int digitalSound;
int analogSound;
int minAnalogSound = 99999;
int maxAnalogSound;
float distance;
float minDistance = 99999;
float maxDistance;
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
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
String uid;
int timestamp;
FirebaseJson json;

void initBot();
void initWebServer();
void initServo();
void initSoundDetector();
void initDistanceReader();
void initPir();
void initFirebase();
void printWakeupReason();

void pirInterrupt();
void detectSound();
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
    //Serial.println("Failed to obtain time");
    return(0);
  }
  time(&now);
  return now;
}

void setup() {
  Serial.begin(115200);
  ++bootCount;

  wifiManager.connect();
  initWebServer();
  initServo();
  initBot();
  initSoundDetector();
  initDistanceReader();
  initPir();
  configTime(0, 0, ntpServer);
  initFirebase();

  printWakeupReason();
}

void loop() {
  if (newRequest) {
    flush();
    newRequest = false;
  }

  currentMillis = millis();
  if (previousSensorReading + SENSOR_READING_INTERVAL < currentMillis) {
    detectSound();
    readDistance();
    handleTelegram();
    previousSensorReading = currentMillis;
    if (sendData) {
      storeData();
    }
  }

  if (deepSleepMode && digitalRead(PIR_PIN) == LOW && previousMovementDetected + DEEP_SLEEP_INTERVAL < millis()) {
    // Go to sleep now
    Serial.println("Going to sleep now");
    delay(1000);
    esp_deep_sleep_start();
  }

  // delay(500);
}

void initBot() {
    client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
    bot.sendMessage(CHAT_ID, "Flusher Started", "");
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
  attachInterrupt(digitalPinToInterrupt(PIR_PIN), pirInterrupt, RISING);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIR_PIN, HIGH);
}

void initFirebase() {
  // Assign the api key (required)
  config.api_key = API_KEY;

  // Assign the user sign in credentials
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  // Assign the RTDB URL (required)
  config.database_url = DATABASE_URL;

  Firebase.reconnectWiFi(true);
  fbdo.setResponseSize(4096);

  // Assign the callback function for the long running token generation task */
  config.token_status_callback = tokenStatusCallback; //see addons/TokenHelper.h

  // Assign the maximum retry of token generation
  config.max_token_generation_retry = 5;

  // Initialize the library with the Firebase authen and config
  Firebase.begin(&config, &auth);

  // Getting the user UID might take a few seconds
  Serial.println("Getting User UID");
  while ((auth.token.uid) == "") {
    Serial.print('.');
    delay(1000);
  }
  // Print user UID
  uid = auth.token.uid.c_str();
  Serial.print("User UID: ");
  Serial.println(uid);
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
  previousMovementDetected = millis();
}

void detectSound() {
  digitalSound = digitalRead(SOUND_DIGITAL_PIN);
  analogSound = analogRead(SOUND_ANALOG_PIN);

  minAnalogSound = analogSound < minAnalogSound ? analogSound : minAnalogSound;
  maxAnalogSound = analogSound > maxAnalogSound ? analogSound : maxAnalogSound;

  if (analogSound > SOUND_THRESHOLD) {
    // Serial.print(analogSound);
    // Serial.print("\t");
    // Serial.println(digitalSound);
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

  minDistance = distance < minDistance ? distance : minDistance;
  maxDistance = distance > maxDistance ? distance : maxDistance;

  // Serial.print("Distance (cm): ");
  // Serial.println(distance);
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
  if (Firebase.ready()) {
    //Get current timestamp
    int timestamp = getTime();
    Serial.print ("time: ");
    Serial.println (timestamp);

    json.set(F("/bootCount"), bootCount);
    json.set(F("/movement"), digitalRead(PIR_PIN));
    json.set(F("/distance"), distance);
    json.set(F("/minDistance"), minDistance);
    json.set(F("/maxDistance"), maxDistance);
    json.set(F("/digitalSound"), digitalSound);
    json.set(F("/analogSound"), analogSound);
    json.set(F("/minAnalogSound"), minAnalogSound);
    json.set(F("/maxAnalogSound"), maxAnalogSound);
    
    // Serial.printf("Set bootCount... %s\n", Firebase.setInt(fbdo, F("/test/bootCount"), bootCount) ? "ok" : fbdo.errorReason().c_str());
    // Serial.printf("Get bootCount... %s\n", Firebase.getInt(fbdo, F("/test/bootCount")) ? String(fbdo.to<int>()).c_str() : fbdo.errorReason().c_str());
    // Serial.printf("Set movement... %s\n", Firebase.setInt(fbdo, F("/test/movement"), digitalRead(PIR_PIN)) ? "ok" : fbdo.errorReason().c_str());
    // Serial.printf("Get movement... %s\n", Firebase.getInt(fbdo, F("/test/movement")) ? String(fbdo.to<int>()).c_str() : fbdo.errorReason().c_str());
    // Serial.printf("Set distance... %s\n", Firebase.setFloat(fbdo, F("/test/distance"), distance) ? "ok" : fbdo.errorReason().c_str());
    // Serial.printf("Get distance... %s\n", Firebase.getFloat(fbdo, F("/test/distance")) ? String(fbdo.to<float>()).c_str() : fbdo.errorReason().c_str());
    // Serial.printf("Set minDistance... %s\n", Firebase.setFloat(fbdo, F("/test/minDistance"), minDistance) ? "ok" : fbdo.errorReason().c_str());
    // Serial.printf("Get minDistance... %s\n", Firebase.getFloat(fbdo, F("/test/minDistance")) ? String(fbdo.to<float>()).c_str() : fbdo.errorReason().c_str());
    // Serial.printf("Set maxDistance... %s\n", Firebase.setFloat(fbdo, F("/test/maxDistance"), maxDistance) ? "ok" : fbdo.errorReason().c_str());
    // Serial.printf("Get maxDistance... %s\n", Firebase.getFloat(fbdo, F("/test/maxDistance")) ? String(fbdo.to<float>()).c_str() : fbdo.errorReason().c_str());
    // Serial.printf("Set digitalSound... %s\n", Firebase.setInt(fbdo, F("/test/digitalSound"), digitalSound) ? "ok" : fbdo.errorReason().c_str());
    // Serial.printf("Get digitalSound... %s\n", Firebase.getInt(fbdo, F("/test/digitalSound")) ? String(fbdo.to<int>()).c_str() : fbdo.errorReason().c_str());
    // Serial.printf("Set analogSound... %s\n", Firebase.setInt(fbdo, F("/test/analogSound"), analogSound) ? "ok" : fbdo.errorReason().c_str());
    // Serial.printf("Get analogSound... %s\n", Firebase.getInt(fbdo, F("/test/analogSound")) ? String(fbdo.to<int>()).c_str() : fbdo.errorReason().c_str());
    // Serial.printf("Set minAnalogSound... %s\n", Firebase.setInt(fbdo, F("/test/minAnalogSound"), minAnalogSound) ? "ok" : fbdo.errorReason().c_str());
    // Serial.printf("Get minAnalogSound... %s\n", Firebase.getInt(fbdo, F("/test/minAnalogSound")) ? String(fbdo.to<int>()).c_str() : fbdo.errorReason().c_str());
    // Serial.printf("Set maxAnalogSound... %s\n", Firebase.setInt(fbdo, F("/test/maxAnalogSound"), maxAnalogSound) ? "ok" : fbdo.errorReason().c_str());
    // Serial.printf("Get maxAnalogSound... %s\n", Firebase.getInt(fbdo, F("/test/maxAnalogSound")) ? String(fbdo.to<int>()).c_str() : fbdo.errorReason().c_str());

    String path = "/test/";
    path += String(timestamp);

    Serial.printf("Set json... %s\n", Firebase.set(fbdo, path.c_str(), json) ? "ok" : fbdo.errorReason().c_str());
  }
  else {
    Serial.println("FAILED");
    Serial.printf("REASON: %s", fbdo.errorReason());
  }
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
      bot.sendMessageWithReplyKeyboard(CHAT_ID, "Choose command:", "", "[[\"/help\", \"/stat\", \"/flush\", \"/restart\", \"/version\"]]", true);
      // bot.sendMessageWithInlineKeyboard(CHAT_ID, "Choose command:", "", "[[\"/stat\", \"/flush\", \"/restart\", \"/version\"]]");
    }
    if (text == "/flush") {
      newRequest = true;
      bot.sendMessage(chat_id, "Flushing...", "");
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
      response += "\nMovement Detected: ";
      response += String(digitalRead(PIR_PIN));
      response += "\nDistance: ";
      response += String(distance);
      response += "\nMin: ";
      response += String(minDistance);
      response += "\nMax: ";
      response += String(maxDistance);
      response += "\nAnalog Sound: ";
      response += String(analogSound);
      response += "\nMin: ";
      response += String(minAnalogSound);
      response += "\nMax: ";
      response += String(maxAnalogSound);
      response += "\nDeep Sleep Mode: ";
      response += String(deepSleepMode);
      response += "\nSend Data: ";
      response += String(sendData);

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
