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

// Config
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

int pos = 15;

RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR int interruptBootCount = 0;
RTC_DATA_ATTR bool deepSleepMode = false;
RTC_DATA_ATTR bool sendData = true;
RTC_DATA_ATTR bool debug = false;
RTC_DATA_ATTR bool enableManual = false;
RTC_DATA_ATTR bool enableAuto = false;

unsigned long now;
unsigned long previousSensorReading = 0;

RTC_DATA_ATTR float defaultDistance;
RTC_DATA_ATTR bool movementDetected;
RTC_DATA_ATTR unsigned long previousMovement;

float distance;
int digitalSound;
int analogSound;

int minAnalogSound = 99999;
int maxAnalogSound;
RTC_DATA_ATTR float minDistance = 99999;
RTC_DATA_ATTR float maxDistance;

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
            flex-direction: column;
            align-items: center;
            justify-content: center;
            height: 100vh;
            margin: 0;
            font-family: 'Arial', sans-serif;
            background-color: #f0f0f0;
            color: #333;
        }
        button {
            font-size: 1.5em;
            padding: 10px 20px;
            background-color: #4CAF50;
            color: white;
            border: none;
            border-radius: 5px;
            cursor: pointer;
            margin-bottom: 20px;
        }
        button:hover {
            background-color: #45a049;
        }
        p {
            font-size: 1.2em;
            margin-bottom: 10px;
        }
        .slider {
            width: 300px;
            -webkit-appearance: none;
            appearance: none;
            height: 10px;
            border-radius: 5px;
            background: #d3d3d3;
            outline: none;
            opacity: 0.7;
            -webkit-transition: .2s;
            transition: opacity .2s;
        }
        .slider:hover {
            opacity: 1;
        }
        .slider::-webkit-slider-thumb {
            -webkit-appearance: none;
            appearance: none;
            width: 20px;
            height: 20px;
            border-radius: 50%;
            background: #4CAF50;
            cursor: pointer;
        }
        .slider::-moz-range-thumb {
            width: 20px;
            height: 20px;
            border-radius: 50%;
            background: #4CAF50;
            cursor: pointer;
        }
        #detachButton {
            font-size: 1.2em;
            padding: 10px 20px;
            background-color: #f44336;
            color: white;
            border: none;
            border-radius: 5px;
            cursor: pointer;
            margin-top: 20px;
        }
        #detachButton:hover {
            background-color: #d32f2f;
        }
    </style>
</head>
<body>
    <button onclick="sendPostRequest()">Flush!</button>
    
    <p>Position: <span id="servoPos"></span></p>
    <input type="range" min="15" max="150" value="15" class="slider" id="servoSlider" onchange="servo(this.value)"/>
  
    <button id="detachButton" onclick="detachServo()">Detach Servo</button>

    <button onclick="renderGraph()">Render Graph</button>

    <canvas id="myChart" width="400" height="200"></canvas>

    // <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <script src="https://cdn.jsdelivr.net/npm/chart.js@^3"></script>
    <script src="https://cdn.jsdelivr.net/npm/moment@^2"></script>
    <script src="https://cdn.jsdelivr.net/npm/chartjs-adapter-moment@^1"></script>
    <script>
        var timeout = null;
        var slider = document.getElementById("servoSlider");
        var servoP = document.getElementById("servoPos");
        servoP.innerHTML = slider.value;
        slider.oninput = function() {
            slider.value = this.value;
            servoP.innerHTML = this.value;
        }

        // Function to fetch data from InfluxDB
        async function fetchData() {
            const response = await fetch('https://us-east-1-1.aws.cloud2.influxdata.com/api/v2/query', {
                method: 'POST',
                headers: {
                    'Authorization': 'Token OrddY9ea5LGv5bpZNEdnYrBBn7-4nwL1zrZI_5W-XVYghPNeIbh6a7J-1uNnfzaQsk7E78JHdX7l6ypsZNrXBg==',
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify({
                    "query": "from(bucket:\"Flusher\") |> range(start: -1d) |> filter(fn: (r) => r._measurement == \"measurements\" and r._field == \"distance\") |> keep(columns: [\"_time\", \"_value\"])"
                })
            });
            console.log(response);
            const data = await response.text();
            console.log("response", data);
            return data;
        }

        // Function to format data for Chart.js
        function formatData(influxData) {
            const NUM_POINTS = 24 * 60 * 60;
            const now = Math.round(Date.now() / 1000) * 1000;
            const start = now - NUM_POINTS * 1000;

            const lines = influxData.split('\r\n').slice(1); // Split lines and remove the header
            console.log("lines", lines);
            const tm = Date.parse(lines[0].slice(11).split(',')[0]);
            console.log(now, start, tm, now - tm, start - tm, NUM_POINTS);

            const timestamps = [];
            const distances = [];

            const pointData = [];

            // let k = 0;
            // for (let i = 0; i < NUM_POINTS - 1; i++) {
            //     let timestamp = start + i * 1000;
            //     let distance = 55;

            //     if (k < lines.length) {
            //         const [db_timestamp, db_distance] = lines[k].slice(11).split(',');

            //         if (timestamp == Math.round(Date.parse(db_timestamp) / 1000) * 1000) {
            //             distance = parseFloat(db_distance);
            //             k++;
            //             console.log(timestamp);
            //         }
            //     }

            //     // console.log(timestamps.length);

            //     timestamps.push(timestamp);
            //     distances.push(distance);

            //     pointData.push({x: timestamp, y: distance});
            // }

            pointData.push({x: start, y: 55});

            lines.forEach((line) => {
              const [db_timestamp, db_distance] = line.slice(11).split(',');
              pointData.push({x: Date.parse(db_timestamp), y: parseFloat(db_distance)});
            });

            return pointData;
        }

        // Function to create and render the graph
        async function renderGraph() {
            const influxData = await fetchData();
            const pointData = formatData(influxData);

            console.log("pointData", pointData);

            const decimation = {
                enabled: false,
                // algorithm: 'min-max',
            };

            const data = {
                datasets: [{
                    borderColor: 'rgba(75, 192, 192, 1)',
                    borderWidth: 1,
                    data: pointData,
                    label: 'Large Dataset',
                    radius: 0,
                }]
            };

            const config = {
                type: 'line',
                data: data,
                options: {
                    // Turn off animations and data parsing for performance
                    animation: false,
                    parsing: false,

                    interaction: {
                        mode: 'nearest',
                        axis: 'x',
                        intersect: false
                    },
                    plugins: {
                        decimation: decimation,
                    },
                    scales: {
                        x: {
                            type: 'time',
                            ticks: {
                                source: 'auto',
                                // Disabled rotation for performance
                                maxRotation: 0,
                                autoSkip: true,
                            }
                        }
                    }
                }
            };

            const ctx = document.getElementById('myChart').getContext('2d');
            const myChart = new Chart(ctx, config);
        }

        // Call the renderGraph function
        // renderGraph();

        function servo(pos) {
            clearTimeout(timeout);
            timeout = setTimeout(function() {
                fetch('/set?position=' + pos, {
                    method: 'GET',
                    headers: {
                        'Content-Type': 'application/json'
                    },
                });
            }, 500);
        }

        function detachServo() {
            fetch('/set?servo=0', {
                method: 'GET',
                headers: {
                    'Content-Type': 'application/json'
                },
            });
        }

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
InfluxDBClient dbClient(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
Point measurments("measurements");

void initBot();
void initWebServer();
void initServo();
void initSoundDetector();
void initDistanceReader();
void initPir();
void printWakeupReason();

void readSound();
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
    readSound();
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

  if (enableAuto && digitalRead(PIR_PIN) == LOW && defaultDistance - distance < 1 && movementDetected && now - previousMovement > AUTO_FLUSH_DELAY) {
    bot.sendMessage(CHAT_ID, "Auto Flush");
    newRequest = true;
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

void initBot() {
    client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
}

// Initialize WebServer
void initWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", index_html);
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
    measurments.addField("digitalSound", digitalSound);
    measurments.addField("analogSound", analogSound);

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
      + "\nMin Distance: " + String(minDistance, 1);

      // + "\n\nDigital Sound: " + (digitalSound ? "HIGH" : "LOW")
      // + "\nAnalog Sound: " + String(analogSound)
      // + "\nMax Analog Sound: " + String(maxAnalogSound)
      // + "\nMin Analog Sound: " + String(minAnalogSound);

      bot.sendMessage(chat_id, response, "");
    }
  }
}

void flush() {
  Serial.println("Flushing...");
  if (!myservo.attached()) {
    myservo.attach(SERVO_PIN);
  }
  for (int pos = 15; pos <= 150; pos += 5) {
    myservo.write(pos);
    delay(10);
  }
  delay(5000);
  for (int pos = 150; pos >= 15; pos -= 5) {
    myservo.write(pos);
    delay(10);
  }
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
