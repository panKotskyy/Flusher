#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "ElegantOTA.h"
#include <ESP32Servo.h>

// Define PINS
#define servoPin 13
#define sound_digital 35 //yellow
#define sound_analog 32 //green
#define trigPin 14 //blue
#define echoPin 27 //purple
#define pirPin 34 //white

// Define sound speed in cm/uS
#define SOUND_SPEED 0.034

// Define network credentials
const char* ssid = "ASUS_60";
const char* password = "bohdan1010";

RTC_DATA_ATTR int bootCount = 0;

unsigned long lastTimeMovementDetected = 0;

long duration;
float distance;
int pos = 0;    // variable to store the servo position
int angle = 0;
bool newRequest = false; // Variable to detect whether a new request occurred
int threshold = 0; // Sound threshold

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
        input {
            margin-right: 10px;
            padding: 5px;
            font-size: 1em;
        }
    </style>
</head>
<body>
    <input type="number" id="angleInput" placeholder="Enter angle">
    <button onclick="sendPostRequest()">Flush</button>

    <script>
        function sendPostRequest() {
            const angle = document.getElementById('angleInput').value;

            fetch('/', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json'
                    // Add any other headers as needed
                },
                // Add body data if required
                body: angle
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

// unsigned long previousMillis = 0;
unsigned long lastTimeWiFiReconects = 0;
const long interval = 10000;  // interval to wait for Wi-Fi connection (milliseconds)
const long wifiReconectInterval = 30000;  // interval between Wi-Fi reconecting tries (milliseconds)

void initWiFi();

void initWebServer();
void initServo();
void initSoundDetector();
void initDistanceReader();
void initPir();
void print_wakeup_reason();

void pirInterrupt();
void detectSound();
void readDistance();
void flush();



void setup() {
  Serial.begin(115200);
  ++bootCount;

  initWiFi();
  initWebServer();
  initServo();
  initSoundDetector();
  initDistanceReader();
  initPir();

  print_wakeup_reason();
}

void loop() {
  if (newRequest){
    flush();
    newRequest = false;
  }

  detectSound();
  readDistance();

  if (digitalRead(pirPin) == LOW && lastTimeMovementDetected + 180000 < millis()) {
    //Go to sleep now
    Serial.println("Going to sleep now");
    delay(1000);
    esp_deep_sleep_start();
  }
  
  delay(500);
}

void WiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info){
  Serial.println("Connected to AP successfully!");
}

void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info){
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info){
  Serial.println("Disconnected from WiFi access point");
  Serial.print("WiFi lost connection. Reason: ");
  Serial.println(info.wifi_sta_disconnected.reason);
  Serial.println("Trying to Reconnect");
  if ((WiFi.getMode() == WIFI_STA) && (WiFi.status() != WL_CONNECTED)) {
    if (millis() - lastTimeWiFiReconects >= wifiReconectInterval) {
      Serial.println("Reconnecting to WiFi...");
      WiFi.disconnect();
      WiFi.reconnect();
      lastTimeWiFiReconects = millis();
    }
  }
}

// Initialize WiFi
void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.onEvent(WiFiStationConnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(WiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(WiFiStationDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  // Serial.println(WiFi.localIP());
}

// Initialize WebServer
void initWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", index_html);
  });

  // server.on("/", HTTP_POST, [](AsyncWebServerRequest *request){
  //   // Handle the request body
  //   String requestBody = request->getParam("plain")->value();
    
  //   // Parse the request body to extract the "angle" parameter
  //   if(requestBody.length() > 0){
  //       angle = requestBody.toInt();
  //       newRequest = true;
  //   } else {
  //     angle = 0;
  //   }
    
  //   request->send(200, "text/html", index_html);
  // });

  server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (request->url() == "/" ) {
      // Extracting angle value from request body
      String angle_str = "";
      for (size_t i = 0; i < len; i++) {
        angle_str += (char)data[i];
      }

      // Do something with the angle value (e.g., print it)
      Serial.print("Received angle: ");
      Serial.println(angle_str);

      if (angle_str.length() > 0) {
        angle = angle_str.toInt();
        newRequest = true;
      } else {
        angle = 0;
      }

      request->send(200, "text/html", index_html);
    }
  });

  ElegantOTA.begin(&server);
  server.begin();
}

void initServo() {
  myservo.attach(servoPin);
}

void initSoundDetector() {
  pinMode(sound_digital, INPUT);
}

void initDistanceReader() {
  pinMode(trigPin, OUTPUT); // Sets the trigPin as an Output
  pinMode(echoPin, INPUT); // Sets the echoPin as an Input
}

void initPir() {
  pinMode(pirPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(pirPin), pirInterrupt, RISING); // Setup PIR interrupt
  esp_sleep_enable_ext0_wakeup((gpio_num_t)pirPin, HIGH);
}

void print_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
  }
}

void pirInterrupt() {
  lastTimeMovementDetected = millis();
}

void detectSound() {
  int val_digital = digitalRead(sound_digital);
  int val_analog = analogRead(sound_analog);

  if (val_analog > threshold) {
    Serial.print(val_analog);
    Serial.print("\t");
    Serial.println(val_digital);
  }
}

void readDistance() {
  // Clears the trigPin
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  // Sets the trigPin on HIGH state for 10 micro seconds
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  // Reads the echoPin, returns the sound wave travel time in microseconds
  duration = pulseIn(echoPin, HIGH);
  
  // Calculate the distance
  distance = duration * SOUND_SPEED/2;
  
  // Prints the distance in the Serial Monitor
  Serial.print("Distance (cm): ");
  Serial.println(distance);
}

void flush() {
  Serial.println("Flushing...");

  // if (pos < angle) {
  //   while(pos <= angle) {
  //     myservo.write(pos);
	// 	  delay(10);
  //     pos += 5; 
  //   }
  // } else if (pos > angle) {
  //   while(pos >= angle) {
  //     myservo.write(pos);
	// 	  delay(10);
  //     pos -= 5; 
  //   }
  // }

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