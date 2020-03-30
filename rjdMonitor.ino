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
#define DHTPIN D1
#define PIRIN D6
#define PCBLED D0 // 16 , LED_BUILTIN
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
char autoRemoteTTL[] = "15";

char otaAuthPin[] = OTA_AUTH_PIN;

// ~~~~ Constants and variables
String httpHeader;
String serverReply;
String localIPaddress;
String formatedTime;
String dayToday;
String lastMovementDay;
String lastMovementTime;

float temperature;
float humidity;
int analogValue = 0;
bool movement = false;
bool tempMove = false;
bool allowLEDs = true;
bool allowFlamePrint = true;
bool currentBedLEDstate = false;

unsigned long flameMillis = 0;

const int uploadInterval = 15000;
const int sensorsInterval = 6000;
const int ntpInterval = 2000;
const int secondInterval = 1000;

unsigned long lastNTPtime = 0;
unsigned long lastSensorTime = 0;
unsigned long lastUploadTime = 0;

const char* thinkSpeakAPIurl = "api.thingspeak.com"; // "184.106.153.149" or api.thingspeak.com
const char* autoRemoteURL = "autoremotejoaomgcd.appspot.com";

// Network Time Protocol
const long utcOffsetInSeconds = 7200; // 1H (3600) for winter time / 2H (7200) for summer time
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

ESP8266WebServer server(80);
WiFiClient client;
HTTPClient clientXmasLEDs;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);

DHT dht(DHTPIN, DHTTYPE);


void setup() {
  pinMode(DHTPIN, INPUT);
  pinMode(PIRIN, INPUT);
  pinMode(PCBLED, OUTPUT);
  pinMode(ESPLED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);

  digitalWrite(PCBLED, HIGH);
  digitalWrite(ESPLED, HIGH);
  // analogWrite(GREEN_LED, 0);
  // analogWrite(BLUE_LED, 0);
  // analogWrite(RED_LED, 0);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(BLUE_LED, LOW);
  digitalWrite(RED_LED, LOW);

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
  server.on("/LEDon", handle_OnConnectLEDon);
  server.on("/LEDoff", handle_OnConnectLEDoff);
  server.on("/BedLedOn", handle_OnConnectbedLedOn);
  server.on("/BedLedOff", handle_OnConnectbedLedOff);
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

  delay(5000);
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
    url += "&ttl=";
    url += (String)autoRemoteTTL;

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
    postStr += String(tempMove);
    postStr +="&field6=";
    postStr += String(currentBedLEDstate);
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
    //int weight = random(65, 76);

    String postStr = apiKeyBeehive;
    postStr += "&field1=";
    postStr += String(temperature);
    postStr += "&field2=";
    postStr += String(humidity);
    postStr += "&field3=";
    // postStr += String(weight);
    postStr += "0";
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

// Sending movement info to XmasLEDs handler
void movementReport() {
  clientXmasLEDs.setTimeout(8);
  clientXmasLEDs.begin("http://192.168.1.31/movement");
  int httpCode = clientXmasLEDs.GET();
  // if (httpCode > 0) {
  //   String payload = clientXmasLEDs.getString();
  //   Serial.println(payload);
  // }
  clientXmasLEDs.end();
}

void handle_OnConnect() {
  if (allowLEDs){ digitalWrite(ESPLED, LOW);}
  getSensorData();
  server.send(200, "text/html", HTMLpresentData());
  digitalWrite(ESPLED, HIGH);
}

void handle_OnConnectLEDon() {
  if (allowLEDs){ digitalWrite(ESPLED, LOW);}
  allowLEDs = true;
  refreshToRoot();
  digitalWrite(ESPLED, HIGH);
}

void handle_OnConnectLEDoff() {
  if (allowLEDs){ digitalWrite(ESPLED, LOW);}
  allowLEDs = false;
  refreshToRoot();
  digitalWrite(ESPLED, HIGH);
}

void handle_OnConnectbedLedOn() {
  if (allowLEDs){ digitalWrite(ESPLED, LOW);}
  currentBedLEDstate = true;
  // refreshToRoot();
  digitalWrite(ESPLED, HIGH);
}

void handle_OnConnectbedLedOff() {
  if (allowLEDs){ digitalWrite(ESPLED, LOW);}
  currentBedLEDstate = false;
  // refreshToRoot();
  digitalWrite(ESPLED, HIGH);
}

void handle_OnConnectAbout() {
  if (allowLEDs){ digitalWrite(ESPLED, LOW);}
  server.send(200, "text/plain", "A smart home automation! (C) Apostolos Smyrnakis");
  digitalWrite(ESPLED, HIGH);
}

void handle_NotFound(){
  server.send(404, "text/html", HTMLnotFound());
}

void refreshToRoot() {
  String rfr = "<HEAD>";
  rfr += "<meta http-equiv=\"refresh\" content=\"0;url=/\">";
  rfr += "</head>";
  server.send(200, "text/html", rfr);
}

String HTMLpresentData(){
  String ptr = "<!DOCTYPE html> <html>\n";
  ptr += "<meta http-equiv=\"refresh\" content=\"5\" >\n";
  ptr += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr += "<title>RJD Monitor</title>\n";
  ptr += "<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n";
  ptr += "body{margin-top: 50px;} h1 {color: #B4F9F3;margin: 50px auto 30px;}\n";
  ptr += "body {color: white; background: black;}\n";
  ptr += "p {font-size: 24px;color: #B4F9F3;margin-bottom: 10px;}\n";
  ptr += ".button { background-color: #195B6A; border: none; color: white; padding: 16px 50px;\n";
  ptr += "text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}\n";
  ptr += ".button2 { background-color: #77878A;}\n";
  ptr += "</style>\n";
  ptr += "</head>\n";
  ptr += "<body>\n";
  ptr += "<div id=\"webpage\">\n";
  ptr += "<h1>RJD Monitor</h1>\n";
  
  ptr += "<p><b>Local IP:</b> ";
  ptr += (String)localIPaddress;
  ptr += "</p>";
  ptr += "<p><b>Timestamp:</b> ";
  ptr += (String)formatedTime;
  ptr += "</p>";
  ptr += "<p><b>Current millis:</b> ";
  ptr += (String)millis();
  ptr += "</p>";

  ptr += "<p><b>Temperature:</b> ";
  ptr += (String)temperature;
  ptr += "&#176C</p>"; // '°' is '&#176' in HTML
  ptr += "<p><b>Humidity:</b> ";
  ptr += (String)humidity;
  ptr += "%</p>";
  ptr += "<p><b>IR sensor:</b> ";
  // ptr += (String)analogValue;
  // ptr += " [0-1024]</p>";
  float tempAnalogValuePerCent = 0.0;
  tempAnalogValuePerCent = (analogValue * 100) / 1024;
  ptr += (String)tempAnalogValuePerCent;
  ptr += "%</p>";
  ptr += "<p><b>Bedroom LEDs:</b> ";
  ptr += (String)currentBedLEDstate;
  ptr += " [0/1]</p>";
  ptr += "<p><b>Movement:</b> ";
  // ptr += (String)movement;
  ptr += (String)tempMove;
  ptr += " [0/1]</p>";
  if ((lastMovementDay != NULL) || (lastMovementTime != NULL)) {
    ptr += "<p><b>Last movement:</b> ";
    ptr += (String)lastMovementDay;
    ptr += ", ";
    ptr += (String)lastMovementTime;
    ptr += "</p>";
  }
  ptr += "<p></p>";

  if (allowLEDs) {
    ptr += "<th colspan=\"2\"><p><a href=\"/LEDoff\"><button class=\"button\">LEDs allowed</button></a></p></th>";
  } else {
    ptr += "<th colspan=\"2>\"<p><a href=\"/LEDon\"><button class=\"button button2\">LEDs disallowed</button></a></p></th>";
  }
  
  ptr += "</div>\n";
  ptr += "</body>\n";
  ptr += "</html>\n";
  return ptr;
}

String HTMLnotFound(){
  String ptr = "<!DOCTYPE html> <html>\n";
  ptr += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr += "<title>RJD Monitor</title>\n";
  ptr += "<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: left;}\n";
  ptr += "body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;}\n";
  ptr += "p {font-size: 24px;color: #444444;margin-bottom: 10px;}\n";
  ptr += "</style>\n";
  ptr += "</head>\n";
  ptr += "<body>\n";
  ptr += "<div id=\"webpage\">\n";
  ptr += "<h1>You know this 404 thing ?</h1>\n";
  ptr += "<p>What you asked can not be found... :'( </p>";
  ptr += "</div>\n";
  ptr += "</body>\n";
  ptr += "</html>\n";
  return ptr;
}

void getSensorData() {
  temperature = dht.readTemperature();
  humidity = dht.readHumidity();
  // movement = digitalRead(PIRIN);
  // analogValue = analogRead(ANLG_IN);
  // analogValue = map(analogValue, 0, 1024, 1024, 0);
  lastSensorTime = millis();
}

// LED color for IR level
void handlerLED() {
  if ((analogValue >= 50) && (analogValue < 150)) {
    digitalWrite(GREEN_LED, HIGH);
    digitalWrite(BLUE_LED, LOW);
    digitalWrite(RED_LED, LOW);
  } else if ((analogValue >= 150) && (analogValue < 300)) {
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

  // +++++++
  // ********
  // -------

  //     0    170     341    510    682     854 1024
  //                  300    480    610    810
  // G   +++++++********-------
  // B                 +++++++********-------
  // R                              +++++++*******

  //  Green 0 - 170-341 - 510
  //  Blue  300 - 480-682 - 854
  //  Red   610 - 810-1024  

  unsigned short greenVal;
  unsigned short blueVal;
  unsigned short redVal;

  if (analogValue = 0) {
    greenVal = 0;
    blueVal = 0;
    redVal = 0;
  } else {

    // GREEN LED
    if (analogValue <= 170) {
      greenVal = map(analogValue, 0, 170, 0, 255);
    }
    if ((analogValue > 170) && (analogValue <= 341)) {
      greenVal = 255;
    }
    if (analogValue > 341) {
      greenVal = map(analogValue, 341, 510, 255, 0);
    }
    // Turn GREEN off
    if (analogValue > 510) {
      greenVal = 0;
    }

    // BLUE LED
    if ((analogValue >= 300) && (analogValue <= 480)) {
      blueVal = map(analogValue, 300, 480, 0, 255);
    }
    if ((analogValue > 480) && (analogValue <= 682)) {
      blueVal = 255;
    }
    if (analogValue > 682) {
      blueVal = map(analogValue, 682, 880, 255, 0);   // 880 --> 854
    }
    // Turn BLUE off
    if ((analogValue < 300) && (analogValue > 880)) { // 880 --> 854
      blueVal = 0;
    }
    
    // RED LED
    if ((analogValue >= 610) && (analogValue <= 840)) { // 840 --> 810
      redVal = map(analogValue, 610, 840, 0, 255);      // 840 --> 810
    }
    if ((analogValue > 480) && (analogValue <= 682)) {
      redVal = 255;
    }
    if (analogValue > 840) {
      redVal = 255;                                     // 840 --> 810
    }
    // Turn RED off
    if (analogValue < 610) {
      redVal = 0;
    }


  }


  // if (analogValue > 510) {
  //   greenVal = 0;
  // }
  // if ((analogValue < 300) && (analogValue > 854)) {
  //   blueVal = 0;
  // }
  // if (analogValue < 610) {
  //   redVal = 0;
  // }




  // if ((analogValue >= 0) && (analogValue < 170)) {


  // } else if ((analogValue >= 256) && (analogValue < 512)) {


  // } else if ((analogValue >= 512) && (analogValue < 768)) {

  //   greenVal = 0;
  //   blueVal = map(analogValue, 512, 640, 255, 0);
  //   redVal = map(analogValue, 512, 768, 0, 255);

  // } else if (analogValue >= 768) {

  //   greenVal = 0;
  //   blueVal = 0;
  //   redVal = map(analogValue, 0, 256, 0, 255);

  // } else {

  //   redVal = 0;
  //   blueVal = 0;
  //   greenVal = 0;
  // }

  analogWrite(RED_LED, redVal);
  analogWrite(GREEN_LED, greenVal); 
  analogWrite(BLUE_LED, blueVal);

  // delay(5);

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

void handlerLED_v3 () {

  unsigned int greenVal;
  unsigned int blueVal;
  unsigned int redVal;

  if (analogValue = 0) {
    greenVal = 0;
    blueVal = 0;
    redVal = 0;
  }
  else {
    if (analogValue <= 350) {
      redVal = 0;
      blueVal = 0;
      greenVal = map(analogValue, 0, 350, 0, 255);
    }
    else if (analogValue <= 700) {
      redVal = 0;
      greenVal = 0;
      blueVal = map(analogValue, 351, 700, 0, 255);
    }
    else {
      greenVal = 0;
      blueVal = 0;
      redVal = map(analogValue, 701, 1024, 0, 255);
    }
  }

  analogWrite(RED_LED, redVal);
  analogWrite(GREEN_LED, greenVal); 
  analogWrite(BLUE_LED, blueVal);
}

void pullNTPtime(bool printData) {
  timeClient.update();
  formatedTime = timeClient.getFormattedTime();
  dayToday = daysOfTheWeek[timeClient.getDay()];

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
  lastNTPtime = millis();
}

void serialPrintAll() {
  String tempDayTime;
  tempDayTime = dayToday;
  tempDayTime += ", ";
  tempDayTime += formatedTime;
  Serial.print(String(tempDayTime));
  // Serial.println(timeClient.getFormattedTime());
  Serial.print("Temperature: ");
  Serial.print(String(temperature));
  Serial.println("°C");
  Serial.print("Humidity: ");
  Serial.print(String(humidity));
  Serial.println("%");
  Serial.print("IR value: ");
  Serial.print(String(analogValue));
  Serial.println(" [0-1024]");
  Serial.print("Movement: ");
  Serial.print(String(movement));
  Serial.println(" [0/1]");
  Serial.println();
}


void loop() {
  // Handle OTA firmware updates
  ArduinoOTA.handle();

  // check IR level every 100ms
  if (millis() % 100 == 0) {
    analogValue = analogRead(ANLG_IN);
    analogValue = map(analogValue, 0, 1024, 1024, 0);
    movement = digitalRead(PIRIN);
  }

  if (movement) {
    tempMove = true;
    lastMovementTime = formatedTime;
    lastMovementDay = dayToday;
  }

  if (allowLEDs){ (movement) ? digitalWrite(PCBLED, LOW) : digitalWrite(PCBLED, HIGH);}

  if ((analogValue > 850) && allowFlamePrint) {
    Serial.print("WARNING: flame detected! (");
    Serial.print(analogValue);
    Serial.println(")");
    sendToAutoRemote("WARNING_flame-detected", autoRemotePlus6, autoRemotePass);
    allowFlamePrint = false;
    flameMillis = millis();
  }
  if ((analogValue < 850) && !allowFlamePrint && (millis() >= flameMillis+60000)) {
    allowFlamePrint = true;
  }

  if (allowLEDs) { 
    handlerLED();
    // handlerLED_v2();
    // handlerLED_v3();
  } else {
    digitalWrite(GREEN_LED, LOW);
    digitalWrite(BLUE_LED, LOW);
    digitalWrite(RED_LED, LOW);
    digitalWrite(ESPLED, HIGH);
    digitalWrite(PCBLED, HIGH);
  }

  // pull the time
  if (millis() > lastNTPtime + ntpInterval) {
    pullNTPtime(false);
  }

  // pull sensor data
  if (millis() > lastSensorTime + sensorsInterval) {
    getSensorData();
  }

  // upload data to ThingSpeak
  if (millis() > lastUploadTime + uploadInterval) {
    if (allowLEDs){ digitalWrite(ESPLED, LOW);}
    getSensorData();
    serialPrintAll();

    thingSpeakRequest();
    // thingSpeakRequestBeeHive();

    // report movement to XmasLEDs handler
    if (tempMove) {
      movementReport();
    }

    tempMove = false;
    lastUploadTime = millis();
    digitalWrite(ESPLED, HIGH);
  }

  // handle HTTP connections
  server.handleClient();
}