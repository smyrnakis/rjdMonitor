#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <NTPClient.h>
#include "DHT.h"
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "secrets.h"

#define DHTTYPE DHT11
#define PCBLED D0 // 16 , LED_BUILTIN
#define DHTPIN D1
#define ESPLED D4 // 2
#define ANLG_IN A0
#define GREEN_LED D2
#define BLUE_LED D3
#define RED_LED D5 

int redVal = 0;   // Variables to store the values to send to the pins
int grnVal = 0;
int bluVal = 0;

char defaultSSID[] = WIFI_DEFAULT_SSID;
char defaultPASS[] = WIFI_DEFAULT_PASS;

char apiKey[] = THINGSP_WR_APIKEY;
char autoRemoteMac[] = AUTOREM_MAC;
char autoRemotePlus6[] = AUTOREM_PLUS6;
char autoRemotePass[] = AUTOREM_PASS;

char otaAuthPin[] = OTA_AUTH_PIN;

// ~~~~ Constants and variables
String httpHeader;
String serverReply;
String localIPaddress;
String formatedTime;
// short lastRecorderTemp;

float temperature;
float humidity;
int analogValue = 0;
bool movement;
bool allowNtp = true;
bool allowFlamePrint = true;
// bool allowEeprom = true;

unsigned long previousMillis = 0;

const int eepromSaveInterval = 60000;
const int uploadInterval = 15000;
const int sensorsInterval = 6000;
const int ntpInterval = 2000;
const int secondInterval = 1000;

const char* thinkSpeakAPIurl = "api.thingspeak.com"; // "184.106.153.149" or api.thingspeak.com
const char* autoRemoteURL = "autoremotejoaomgcd.appspot.com";

// Network Time Protocol
const long utcOffsetInSeconds = 3600; // 1H (3600) for winter time / 2H (7200) for summer time
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

ESP8266WebServer server(80);
WiFiClient client;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);

DHT dht(DHTPIN, DHTTYPE);


void setup() {
  pinMode(DHTPIN, INPUT);
  pinMode(PCBLED, OUTPUT);
  pinMode(ESPLED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  
  digitalWrite(PCBLED, HIGH);
  digitalWrite(ESPLED, HIGH);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(BLUE_LED, LOW);
  digitalWrite(RED_LED, LOW);

  randomSeed(analogRead(0));

  Serial.begin(115200);
  delay(100);

  WiFiManager wifiManager;
  //wifiManager.resetSettings();
  wifiManager.setConfigPortalTimeout(180);  // 180 sec timeout for WiFi configuration
  wifiManager.autoConnect(defaultSSID, defaultPASS);

  Serial.println("Connected to WiFi.");
  Serial.print("IP: ");
  localIPaddress = (WiFi.localIP()).toString();
  Serial.println(localIPaddress);

  server.on("/", handle_OnConnect);
  server.on("/about", handle_OnConnectAbout);
  server.onNotFound(handle_NotFound);
  
  server.begin();
  Serial.println("HTTP server starter on port 80.");

  timeClient.begin();

  dht.begin();

  // while (WiFi.waitForConnectResult() != WL_CONNECTED) {
  //   Serial.println("Connection Failed! Rebooting...");
  //   delay(5000);
  //   ESP.restart();
  // }

  // handle OTA updates
  handleOTA();

  delay(400);
}

// Send message to AutoRemote
void sendToAutoRemote(char message[], char deviceKey[], char password[]) {
  client.stop();
  if (client.connect(autoRemoteURL,80)) {
    String url = "/sendmessage?key=";
    url += (String)deviceKey;
    url += "&message=";
    url += (String)message;
    url += "&sender=";
    url += "SmyESP-1";
    url += "&password=";
    url += (String)password;

    client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + autoRemoteURL + "\r\n" +
               "Connection: close\r\n\r\n");
    // Timeout 5 sec
    unsigned long timeout = millis();
    while (client.available() == 0) {
      if (millis() - timeout > 5000) {
        client.stop();
        Serial.println("ERROR: could not send message to AutoRemote!");
        return;
      }
    }

    while (client.available()) {
        serverReply = client.readStringUntil('\r');
    }
      
    serverReply.trim();
    client.stop();
    // Serial.println("Data uploaded to thingspeak!");
  }
  else {
    Serial.println("ERROR: could not send data to AutoRemote!");
  }
}

// OTA code update
void handleOTA() {
  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname("SmyESP-1");

  ArduinoOTA.setPassword((const char *)otaAuthPin);

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
}

// Sending data to Thingspeak
void thingSpeakRequest() {
  client.stop();
  if (client.connect(thinkSpeakAPIurl,80)) 
  {
    String postStr = apiKey;
    postStr +="&field1=";
    postStr += String(temperature);
    postStr +="&field2=";
    postStr += String(humidity);
    postStr +="&field3=";
    postStr += String(analogValue);
    postStr +="&field4=";
    postStr += String(movement);
    postStr += "\r\n\r\n";

    client.print("POST /update HTTP/1.1\n");
    client.print("Host: api.thingspeak.com\n");
    client.print("Connection: close\n");
    client.print("X-THINGSPEAKAPIKEY: " + (String)apiKey + "\n");
    client.print("Content-Type: application/x-www-form-urlencoded\n");
    client.print("Content-Length: ");
    client.print(postStr.length());
    client.print("\n\n");
    client.print(postStr);
    client.stop();
    // Serial.println("Data uploaded to thingspeak!");
  }
  else {
    Serial.println("ERROR: could not upload data to thingspeak!");
  }
}

// Sending data to Thingspeak (fill beeHive data)
void thingSpeakRequestBeeHive() {
  client.stop();
  if (client.connect(thinkSpeakAPIurl,80)) 
  {
    char apiKeyBeehive[] = BEEHIVE_WR_APIKEY;
    int weight = random(65, 76);

    String postStr = apiKeyBeehive;
    postStr +="&field1=";
    postStr += String(temperature);
    postStr +="&field2=";
    postStr += String(humidity);
    postStr +="&field3=";
    postStr += String(weight);
    postStr += "\r\n\r\n";

    client.print("POST /update HTTP/1.1\n");
    client.print("Host: api.thingspeak.com\n");
    client.print("Connection: close\n");
    client.print("X-THINGSPEAKAPIKEY: " + (String)apiKeyBeehive + "\n");
    client.print("Content-Type: application/x-www-form-urlencoded\n");
    client.print("Content-Length: ");
    client.print(postStr.length());
    client.print("\n\n");
    client.print(postStr);
    client.stop();
    // Serial.println("Data uploaded to thingspeak!");
  }
  else {
    Serial.println("ERROR: could not upload data to thingspeak (beehive)!");
  }
}

// Handle HTML page calls
void handle_OnConnect() {
  digitalWrite(ESPLED, LOW);
  getSensorData();
  server.send(200, "text/html", HTMLpresentData());
  digitalWrite(ESPLED, HIGH);
}

void handle_OnConnectAbout() {
  digitalWrite(ESPLED, LOW);
  server.send(200, "text/plain", "A smart home automation! (C) Apostolos Smyrnakis");
  digitalWrite(ESPLED, HIGH);
}

void handle_NotFound(){
  server.send(404, "text/html", HTMLnotFound());
}

// HTML pages structure
String HTMLpresentData(){
  String ptr = "<!DOCTYPE html> <html>\n";
  ptr +="<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr +="<title>RJD Monitor</title>\n";
  ptr +="<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n";
  ptr +="body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;}\n";
  ptr +="p {font-size: 24px;color: #444444;margin-bottom: 10px;}\n";
  ptr +="</style>\n";
  ptr +="</head>\n";
  ptr +="<body>\n";
  ptr +="<div id=\"webpage\">\n";
  ptr +="<h1>RJD Monitor</h1>\n";
  
  ptr +="<p>Local IP: ";
  ptr += (String)localIPaddress;
  ptr +="</p>";

  ptr +="<p>Temperature: ";
  ptr +=(String)temperature;
  ptr +="&#176C</p>"; // '°' is '&#176' in HTML
  ptr +="<p>Humidity: ";
  ptr +=(String)humidity;
  ptr +="%</p>";
  ptr +="<p>IR sensor: ";
  ptr +=(String)analogValue;
  ptr +="%</p>";
  ptr += "<p>Timestamp: ";
  ptr +=(String)formatedTime;
  ptr += "</p>";

  // ptr +="<p>Last recorder temp: ";
  // ptr +=(String)lastRecorderTemp;
  // ptr +="&#176C</p>";
  
  ptr +="</div>\n";
  ptr +="</body>\n";
  ptr +="</html>\n";
  return ptr;
}

String HTMLnotFound(){
  String ptr = "<!DOCTYPE html> <html>\n";
  ptr +="<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr +="<title>RJD Monitor</title>\n";
  ptr +="<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: left;}\n";
  ptr +="body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;}\n";
  ptr +="p {font-size: 24px;color: #444444;margin-bottom: 10px;}\n";
  ptr +="</style>\n";
  ptr +="</head>\n";
  ptr +="<body>\n";
  ptr +="<div id=\"webpage\">\n";
  ptr +="<h1>You know this 404 thing ?</h1>\n";
  ptr +="<p>What you asked can not be found... :'( </p>";
  ptr +="</div>\n";
  ptr +="</body>\n";
  ptr +="</html>\n";
  return ptr;
}

// Read all sensors
void getSensorData() {
  //temperature  = random(10, 21);
  //humidity  = random(65, 85);
  movement = random(0, 2);
  temperature = dht.readTemperature();
  humidity = dht.readHumidity();
  // analogValue = analogRead(ANLG_IN);
  // analogValue = map(analogValue, 0, 1024, 1024, 0);
}

// LED color for IR level
void handlerLED() {
  if ((analogValue >= 50) && (analogValue < 150)) {
    digitalWrite(GREEN_LED, HIGH);
    digitalWrite(BLUE_LED, LOW);
    digitalWrite(RED_LED, LOW);
  } else if ((analogValue >= 150) && (analogValue < 300))
  {
    digitalWrite(GREEN_LED, HIGH);
    digitalWrite(BLUE_LED, LOW);
    digitalWrite(RED_LED, HIGH);
  } else if ((analogValue >= 300) && (analogValue < 512)) {
    digitalWrite(GREEN_LED, LOW);
    digitalWrite(BLUE_LED, HIGH);
    digitalWrite(RED_LED, HIGH);
  } else if (analogValue >= 512) {
    digitalWrite(GREEN_LED, LOW);
    digitalWrite(BLUE_LED, LOW);
    digitalWrite(RED_LED, HIGH);
  } else {
    digitalWrite(GREEN_LED, LOW);
    digitalWrite(BLUE_LED, LOW);
    digitalWrite(RED_LED, LOW);
  }

  // if (analogValue < 341)  // Lowest third of the potentiometer's range (0-340)
  // {                  
  //   analogValue = (analogValue * 3) / 4; // Normalize to 0-255

  //   redVal = 256 - analogValue;  // Red from full to off
  //   grnVal = analogValue;        // Green from off to full
  //   bluVal = 1;             // Blue off
  // }
  // else if (analogValue < 682) // Middle third of potentiometer's range (341-681)
  // {
  //   analogValue = ( (analogValue-341) * 3) / 4; // Normalize to 0-255

  //   redVal = 1;            // Red off
  //   grnVal = 256 - analogValue; // Green from full to off
  //   bluVal = analogValue;       // Blue from off to full
  // }
  // else  // Upper third of potentiometer"s range (682-1023)
  // {
  //   analogValue = ( (analogValue-683) * 3) / 4; // Normalize to 0-255

  //   redVal = analogValue;       // Red from off to full
  //   grnVal = 1;            // Green off
  //   bluVal = 256 - analogValue; // Blue from full to off
  // }
  // analogWrite(RED_LED, redVal);   // Write values to LED pins
  // analogWrite(GREEN_LED, grnVal); 
  // analogWrite(BLUE_LED, bluVal);  
}

void handlerLED_v2() {

//  0     128     256     384     512     640     768     896     1024
//  G              GB              B               BR               R
//  G ++++++++++++++----------------
//  B               ++++++++++++++++----------------
//  R                                                 +++++++++++++++


//  0     171     341     512     682     850     1024
//  G ++++++--------
//  B       ++++++++--------
//  R               ++++++++-------



  unsigned short greenVal;
  unsigned short blueVal;
  unsigned short redVal;

  if ((analogValue >= 0) && (analogValue < 256)) {

    greenVal = map(analogValue, 0, 256, 0, 255);
    blueVal = 0;
    redVal = 0;

  } else if ((analogValue >= 256) && (analogValue < 512)) {

    greenVal = map(analogValue, 256, 384, 255, 0);
    blueVal = map(analogValue, 256, 512, 0, 255);
    redVal = 0;

  } else if ((analogValue >= 512) && (analogValue < 768)) {

    greenVal = 0;
    blueVal = map(analogValue, 512, 640, 255, 0);
    redVal = map(analogValue, 512, 768, 0, 255);

  } else if (analogValue >= 768) {

    greenVal = 0;
    blueVal = 0;
    redVal = map(analogValue, 0, 256, 0, 255);

  } else {

    redVal = 0;
    blueVal = 0;
    greenVal = 0;
  }

  analogWrite(RED_LED, redVal);
  analogWrite(GREEN_LED, greenVal); 
  analogWrite(BLUE_LED, blueVal);

  delay(5);

  // digitalWrite(GREEN_LED, LOW);
  // digitalWrite(BLUE_LED, LOW);
  // digitalWrite(RED_LED, LOW);



  // if (analogValue < 341)  // Lowest third of the potentiometer's range (0-340)
  // {                  
  //   analogValue = (analogValue * 3) / 4; // Normalize to 0-255

  //   redVal = 256 - analogValue;  // Red from full to off
  //   grnVal = analogValue;        // Green from off to full
  //   bluVal = 1;             // Blue off
  // }
  // else if (analogValue < 682) // Middle third of potentiometer's range (341-681)
  // {
  //   analogValue = ( (analogValue-341) * 3) / 4; // Normalize to 0-255

  //   redVal = 1;            // Red off
  //   grnVal = 256 - analogValue; // Green from full to off
  //   bluVal = analogValue;       // Blue from off to full
  // }
  // else  // Upper third of potentiometer"s range (682-1023)
  // {
  //   analogValue = ( (analogValue-683) * 3) / 4; // Normalize to 0-255

  //   redVal = analogValue;       // Red from off to full
  //   grnVal = 1;            // Green off
  //   bluVal = 256 - analogValue; // Blue from full to off
  // }
  // analogWrite(RED_LED, redVal);   // Write values to LED pins
  // analogWrite(GREEN_LED, grnVal); 
  // analogWrite(BLUE_LED, bluVal);  
}

// Get the time
void pullNTPtime(bool printData) {
  timeClient.update();
  formatedTime = timeClient.getFormattedTime();

  if (printData) {
    // Serial.print(daysOfTheWeek[timeClient.getDay()]);
    // Serial.print(", ");
    // Serial.print(timeClient.getHours());
    // Serial.print(":");
    // Serial.print(timeClient.getMinutes());
    // Serial.print(":");
    // Serial.println(timeClient.getSeconds());
    Serial.println(timeClient.getFormattedTime()); // format time like 23:05:00
  }
}

// Serial print data
void serialPrintAll() {
  Serial.println(timeClient.getFormattedTime());
  Serial.print("Temperature: ");
  Serial.print(String(temperature));
  Serial.println("°C");
  Serial.print("Humidity: ");
  Serial.print(String(humidity));
  Serial.println("%");
  Serial.print("IR value: ");
  Serial.print(String(analogValue));
  Serial.println(" [0-1024]");
  Serial.println();
}


void loop(){
  // Handle OTA firmware updates
  ArduinoOTA.handle();

  unsigned long currentMillis = millis();

  // check IR level every 100ms
  if (currentMillis % 10 == 0) {
    analogValue = analogRead(ANLG_IN);
    analogValue = map(analogValue, 0, 1024, 1024, 0);

    if (analogValue > 768) {
      digitalWrite(PCBLED, LOW);
    }
    else {
      digitalWrite(PCBLED, HIGH);
    }
    if ((analogValue > 768) && allowFlamePrint) {
      Serial.print("WARNING: flame detected! (");
      Serial.print(analogValue);
      Serial.println(")");
      sendToAutoRemote("WARNING_flame-detected", autoRemotePlus6, autoRemotePass);
      allowFlamePrint = false;
    }
    if ((analogValue < 768) && !allowFlamePrint) {
      allowFlamePrint = true;
    }
  }

  // handlerLED();
  handlerLED_v2();

  // pull the time
  if ((currentMillis % ntpInterval == 0) && (allowNtp)) {
    // Serial.println("Pulling NTP...");
    pullNTPtime(false);
    allowNtp = false;
  }

  // pull sensor data
  if (currentMillis % sensorsInterval == 0) {
    // Serial.println("Reading sensor data...");
    getSensorData();
  }

  // upload data to ThingSpeak
  if (currentMillis % uploadInterval == 0) {
    // Serial.println("Uploading to thingspeak...");
    digitalWrite(ESPLED, LOW);
    getSensorData();

    // Upload data to thingSpeak
    thingSpeakRequest();

    // Upload data to beehive
    thingSpeakRequestBeeHive();\

    serialPrintAll();
    digitalWrite(ESPLED, HIGH);
  }

  // debounce per second
  if (currentMillis % secondInterval == 0) {
    // debounce for NTP calls
    allowNtp = true;
  }

  // handle HTTP connections
  server.handleClient();
}