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
  // ArduinoOTA.setHostname("myesp8266");

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
  // server.send(200, "text/html", SendHTML(temperature,humidity,formatedTime)); 
  server.send(200, "text/html", SendHTML());
  digitalWrite(ESPLED, HIGH);
}

void handle_NotFound(){
  server.send(404, "text/plain", "404 - Page not found");
}

// HTML page structure
String SendHTML(){
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

  if (currentMillis % 100 == 0) {
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
  


  // if ((currentMillis % eepromSaveInterval == 0) && (allowEeprom)) {
  //   Serial.println("Saving data in eeprom...");

  //   allowEeprom = false;
  // }

  if ((currentMillis % ntpInterval == 0) && (allowNtp)) {
    // Serial.println("Pulling NTP...");
    pullNTPtime(false);
    allowNtp = false;
  }

  if (currentMillis % sensorsInterval == 0) {
    // Serial.println("Reading sensor data...");
    getSensorData();
  }

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

  // Repeat every 1 second
  if (currentMillis % secondInterval == 0) {
    // debounce for NTP calls
    allowNtp = true;
    // allowEeprom = true;
  }

  server.handleClient();
}