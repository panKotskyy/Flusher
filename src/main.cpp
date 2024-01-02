#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "ElegantOTA.h"
#include <ESP32Servo.h>
#include "WiFiManager.h"

// Define PINS
#define SERVO_PIN 13
#define SOUND_DIGITAL_PIN 35 // yellow
#define SOUND_ANALOG_PIN 32  // green
#define TRIG_PIN 14          // blue
#define ECHO_PIN 27          // purple
#define PIR_PIN 34           // white

// Define sound speed in cm/uS
#define SOUND_SPEED 0.034

// Define network credentials
const char *ssid = "ASUS_60";
const char *password = "bohdan1010";

RTC_DATA_ATTR int bootCount = 0;

unsigned long lastTimeMovementDetected = 0;

long duration;
float distance;
int pos = 0;                // variable to store the servo position
bool newRequest = false;    // Variable to detect whether a new request occurred
int threshold = 0;          // Sound threshold

AsyncWebServer server(80);
Servo myservo;

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
    <button onclick="sendPostRequest()">Flush</button>

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

void initWebServer();
void initServo();
void initSoundDetector();
void initDistanceReader();
void initPir();
void printWakeupReason();

void pirInterrupt();
void detectSound();
void readDistance();
void flush();

WiFiManager wifiManager(ssid, password);

void setup() {
  Serial.begin(115200);
  ++bootCount;

  wifiManager.connect();
  initWebServer();
  initServo();
  initSoundDetector();
  initDistanceReader();
  initPir();

  printWakeupReason();
}

void loop() {
  if (newRequest) {
    flush();
    newRequest = false;
  }

  detectSound();
  readDistance();

  if (digitalRead(PIR_PIN) == LOW && lastTimeMovementDetected + 180000 < millis()) {
    // Go to sleep now
    Serial.println("Going to sleep now");
    delay(1000);
    esp_deep_sleep_start();
  }

  delay(500);
}

// Initialize WebServer
void initWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", index_html);
  });

  server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (request->url() == "/") {
      // Extracting angle value from request body
      String angleStr(reinterpret_cast<char *>(data), len);
      int angleValue = angleStr.toInt();

      // Do something with the angle value (e.g., print it)
      Serial.print("Received angle: ");
      Serial.println(angleValue);

      if (angleStr.length() > 0) {
        pos = angleValue;
        newRequest = true;
      } else {
        pos = 0;
      }

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
  pinMode(TRIG_PIN, OUTPUT); // Sets the TRIG_PIN as an Output
  pinMode(ECHO_PIN, INPUT);  // Sets the ECHO_PIN as an Input
}

void initPir() {
  pinMode(PIR_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIR_PIN), pirInterrupt, RISING); // Setup PIR interrupt
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIR_PIN, HIGH);
}

void printWakeupReason() {
  esp_sleep_wakeup_cause_t wakeupReason;
  wakeupReason = esp_sleep_get_wakeup_cause();

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
  lastTimeMovementDetected = millis();
}

void detectSound() {
  int valDigital = digitalRead(SOUND_DIGITAL_PIN);
  int valAnalog = analogRead(SOUND_ANALOG_PIN);

  if (valAnalog > threshold) {
    Serial.print(valAnalog);
    Serial.print("\t");
    Serial.println(valDigital);
  }
}

void readDistance() {
  // Clears the TRIG_PIN
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  // Sets the TRIG_PIN on HIGH state for 10 microseconds
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // Reads the ECHO_PIN, returns the sound wave travel time in microseconds
  duration = pulseIn(ECHO_PIN, HIGH);

  // Calculate the distance
  distance = duration * SOUND_SPEED / 2;

  // Prints the distance in the Serial Monitor
  Serial.print("Distance (cm): ");
  Serial.println(distance);
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
