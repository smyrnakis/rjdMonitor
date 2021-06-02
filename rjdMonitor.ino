#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include <NTPClient.h>
#include "DHT.h"
#include <WiFiUdp.h>
#include <ErriezMHZ19B.h>
#include <SoftwareSerial.h>

#include "secrets.h"

// #include "htmlPages.h"
// #include <DNSServer.h>
// #include "amazonCertificate.h"

#define DHTTYPE DHT11
#define DHTPIN D1
#define PIRIN D6
#define PCBLED D0 // 16 , LED_BUILTIN
#define ESPLED D4 // 2
#define ANLG_IN A0
#define GREEN_LED D2
#define BLUE_LED D3
#define RED_LED D5
#define MHZ19B_TX_PIN D7
#define MHZ19B_RX_PIN D8

char defaultSSID[] = WIFI_DEFAULT_SSID;
char defaultPASS[] = WIFI_DEFAULT_PASS;

char thingSpeakApiKey[] = THINGSP_WR_APIKEY;
char autoRemotePlus6[] = AUTOREM_PLUS6;
char autoRemotePass[] = AUTOREM_PASS;
char autoRemoteTTL[] = "15";

char otaAuthPin[] = OTA_AUTH_PIN;

String macAddr;
String wifiSSID;
String wifiSignal;
String localIPaddress;

String httpHeader;
String serverReply;
String formatedTime;
String dayToday;
String lastMovementDay;
String lastMovementTime;

float temperature;
float humidity;
int16_t co2Read;
int analogValue = 0;
bool movement = false;
bool movementFlag = false;
bool allowLEDs = true;
bool allowFlamePrint = true;
bool currentBedLEDstate = false;

bool wifiAvailable = false;
bool allowTalos = false;
bool allowAutoRemote = true;
bool allowThingSpeak = true;

unsigned long flameMillis = 0;
unsigned short flameThreshold = 850;

const int sensorsInterval = 6000;       // 6 seconds
const int PirIrInterval = 500;          // 500 ms
const int thingSpeakInterval = 15000;   // 15 seconds
const int ntpInterval = 2000;           // 2 seconds

unsigned long lastNTPtime = 0;
unsigned long lastPirIrTime = 0;
unsigned long lastSensorTime = 0;
unsigned long lastUploadTime = 0;

// Network Time Protocol
// GR: 2H (7200) for winter time / 3H (10800) for summer time
// CH: 1H (3600) for winter time / 2H (7200) for summer time
const long utcOffsetInSeconds = 7200;
char daysOfTheWeek[7][12] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
// char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

// SERVER
ESP8266WebServer server(80);

// HTTP requests
WiFiClient clientHttp;

// HTTPS requests
WiFiClientSecure clientHttps;

// clients
HTTPClient remoteClient;

// NTP configuration
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);

// DHT sensor initialisation
DHT dht(DHTPIN, DHTTYPE);

// MH-Z19B CO2 sensor
SoftwareSerial mhzSerial(MHZ19B_TX_PIN, MHZ19B_RX_PIN);
ErriezMHZ19B mhz19b(&mhzSerial);


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
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(BLUE_LED, LOW);
  digitalWrite(RED_LED, LOW);

  Serial.begin(115200);
  delay(200);

  WiFiManager wifiManager;
  //wifiManager.resetSettings();
  wifiManager.setConfigPortalTimeout(180);                // 3 min timeout for WiFi configuration
  wifiManager.autoConnect(defaultSSID, defaultPASS);

  Serial.println();
  delay(200);

  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    wifiAvailable = false;
    Serial.println("[ERROR] connecting on WiFi. Operating 'OFFLINE'");
  }
  else {
    wifiAvailable = true;
    localIPaddress = (WiFi.localIP()).toString();
    wifiSignal = String(WiFi.RSSI());
    wifiSSID = String(WiFi.SSID());
    macAddr = String(WiFi.macAddress());

    Serial.print("[SUCCESS] connected on WiFi: ");
    Serial.println(wifiSSID);
    Serial.print("Local IP address: ");
    Serial.println(localIPaddress);
  }

  // connection handlers
  server.on("/", onConnect_default);
  server.on("/restart", onConnect_restart);
  server.on("/settings", onConnect_settings);
  server.on("/about", onConnect_about);
  server.on("/toggleLED", onConnect_toggleLEDs);
  server.on("/toggleThSp", onConnect_toggleThingSpeak);
  server.on("/toggleAR", onConnect_toggleAutoRemote);
  server.on("/BedLedOn", onConnect_bedLedOn);
  server.on("/BedLedOff", onConnect_bedLedOff);
  server.onNotFound(onConnect_notFound);

  server.begin();
  Serial.println("[SUCCESS] HTTP server started on port 80");
  delay(200);

  timeClient.begin();
  Serial.println("[SUCCESS] NTP client started");
  delay(200);

  dht.begin();
  Serial.println("[SUCCESS] DHT started");
  delay(200);

  mhzSerial.begin(9600);
  // Serial.print(F("\nDetecting MH-Z19B sensor..."));
  while ( !mhz19b.detect() ) {
      delay(500);
      // Serial.print(F("."));
      delay(500);
  };
  Serial.println(F("[SUCCESS] MH-Z19B detected"));

  Serial.print(F("[INFO] MH-Z19B warming up "));
  short tempWarmUp20s = 0;  // 9 max
  while (mhz19b.isWarmingUp()) {
      short tempWarmUpPerCent = ( 100 * tempWarmUp20s ) / 9;
      Serial.print(tempWarmUpPerCent);
      Serial.print(F("%  "));
      tempWarmUp20s += 1;
      delay(20000);
      // Serial.print(F("."));
      // delay(5000);
  };
  Serial.println(F(" DONE"));
  delay(200);

  handler_OTA();
  delay(1000);
}


String millisToTime(bool calcDays) {

  char outString[16];

  unsigned long millisecondsNow = millis();
  unsigned long tempTime = millisecondsNow / 1000;

  unsigned long seconds = tempTime % 60;

  tempTime = (tempTime - seconds) / 60;
  unsigned long minutes = tempTime % 60;

  tempTime = (tempTime - minutes) / 60;
  unsigned long hours = tempTime % 24;

  unsigned long days = (tempTime - hours) / 24;

  // ~~~~~~~~~~ another algorithm ~~~~~~~~~~
  // int days = n / (24 * 3600);

  // n = n % (24 * 3600);
  // int hours = n / 3600;

  // n %= 3600;
  // int minutes = n / 60 ;

  // n %= 60;
  // int seconds = n;

  if (calcDays) {
    // output:  1d 03h 42' 04"  (d HH MM SS)
    sprintf(outString, "%dd %02dh %02d' %02d\"", days,hours,minutes,seconds);
  }
  else {
    // output:  03:42:04        (HH:MM:SS)
    sprintf(outString, "%02d:%02d:%02d" ,hours,minutes,seconds);
  }

  return outString;
}


String HTML_REFRESH_TO_SETTINGS() {
    String html_page = "<HEAD>";
    html_page += "<meta http-equiv=\"refresh\" content=\"0;url=/settings\">";
    html_page += "</head>";

    return html_page;
}


String HTML_REFRESH_TO_ROOT() {
    String html_page = "<HEAD>";
    html_page += "<meta http-equiv=\"refresh\" content=\"0;url=/\">";
    html_page += "</head>";

    return html_page;
}


String HTML_NOT_FOUND() {
  String html_page = "<!DOCTYPE html> <html>\n";
  html_page += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  html_page += "<title>RJD Monitor</title>\n";
  html_page += "<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: left;}\n";
  html_page += "body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;}\n";
  html_page += "p {font-size: 24px;color: #444444;margin-bottom: 10px;}\n";
  html_page += "</style>\n";
  html_page += "</head>\n";
  html_page += "<body>\n";
  html_page += "<div id=\"webpage\">\n";
  html_page += "<h1>You know this 404 thing ?</h1>\n";
  html_page += "<p>What you asked can not be found... :'( </p>";
  html_page += "</div>\n";
  html_page += "</body>\n";
  html_page += "</html>\n";

  return html_page;
}


String HTML_LANDING_PAGE() {
  String html_page = "<!DOCTYPE html> <html>\n";
  html_page += "<meta http-equiv=\"refresh\" content=\"15\" >\n";
  html_page += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  html_page += "<link rel=\"icon\" href=\"data:,\">\n";
  html_page += "<title>RJD Monitor</title>\n";
  html_page += "<style>\n";
  html_page += "html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center; }\n";
  html_page += "body { margin-top: 50px; color: white; background: black;}\n";
  // html_page += "h1 { color: #B4F9F3; margin: 50px auto 30px; }\n";
  html_page += "p { font-size: 24px; }\n";
  // html_page += "p { font-size: 24px; color: #B4F9F3; margin-bottom: 10px; }\n";
  // html_page += "table, table td { border: 0px solid #cccccc; }\n";
  html_page += "table, table td { text-align: center; vertical-align: middle; padding-top: 5px; padding-bottom: 5px; }\n";
  // html_page += ".button { background-color: #195B6A; border: none; color: white; padding: 16px 50px;\n";
  // html_page += ".button { background-color: #195B6A; border: none; color: white; padding: 16px 50px; text-decoration: none; text-align: center; font-size: 30px; margin: 2px; cursor: pointer; }\n";
  // html_page += ".button { background-color: #195B6A; border: none; color: white; height: 50px; width: 130px;\n";
  html_page += ".button { background-color: #195B6A; border: none; color: white; height: 50px; width: auto;\n";
  html_page += "text-decoration: none; text-align: center; font-size: 20px; margin: 2px; cursor: pointer; }\n";
  html_page += ".button2 { background-color: #77878A; }\n";
  html_page += ".button3 { background-color: #ff3300; }\n";
  
  html_page += "</style>\n";
  html_page += "</head>\n";

  html_page += "<body>\n";
  html_page += "<div id=\"webpage\">\n";
  html_page += "<h1>RJD Monitor</h1>\n";
  // html_page += "<br />\n";
  html_page += "<h2>";
  html_page += String(formatedTime);
  html_page += "</h2>\n";
  // html_page += "<br />\n";

  html_page += "<table style=\"margin-left:auto; margin-right:auto; allign:center; font-size:20px\">\n";

  html_page += "<tr>\n";
  html_page += "<td>Temperature:</td>";
  html_page += "<td style=\"padding-left: 10px;\">";
  html_page += (String)temperature;
  html_page += " &#176C</td>"; // '°' is '&#176' in HTML
  html_page += "</tr>\n";

  html_page += "<tr>\n";
  html_page += "<td>Humidity:</td>";
  html_page += "<td style=\"padding-left: 10px;\">";
  html_page += (String)humidity;
  html_page += " %</td>";
  html_page += "</tr>\n";

  html_page += "<tr>\n";
  html_page += "<td>IR radiation:</td>";
  html_page += "<td style=\"padding-left: 10px;\">";
  float tempAnalogValuePerCent = 0.0;
  tempAnalogValuePerCent = (float)(((float)analogValue * 100.0) / 1024.0);
  html_page += (float)tempAnalogValuePerCent;
  html_page += " %</td>";
  html_page += "</tr>\n";

  html_page += "<tr>\n";
  html_page += "<td>CO2:</td>";
  html_page += "<td style=\"padding-left: 10px;\">";
  html_page += (String)co2Read;
  html_page += " [400-5000]</td>";
  html_page += "</tr>\n";

  html_page += "<tr>\n";
  html_page += "<td>Movement:</td>";
  html_page += "<td style=\"padding-left: 10px;\">";
  String tempYesNo;
  if (movementFlag) {
      tempYesNo = "yes";
    }
  else {
    tempYesNo = "no";
  }
  html_page += tempYesNo;
  html_page += "</td>";
  html_page += "</tr>\n";

  if ((lastMovementDay != NULL) || (lastMovementTime != NULL)) {
    html_page += "<tr>\n";
    html_page += "<td>Last movement:</td>";
    html_page += "<td style=\"padding-left: 10px;\">";
    html_page += (String)lastMovementDay;
    html_page += ", ";
    html_page += (String)lastMovementTime;
    html_page += "</td>";
    html_page += "</tr>\n";
  }

  html_page += "<tr>\n";
  html_page += "<td>Bedroom LEDs:</td>";
  html_page += "<td style=\"padding-left: 10px;\">";
  if (currentBedLEDstate) {
      tempYesNo = "on";
    }
  else {
    tempYesNo = "off";
  }
  html_page += tempYesNo;
  html_page += "</td>";
  html_page += "</tr>\n";

  html_page += "<tr>\n";
  // html_page += "<td colspan=\"2\"><p><a href=\"/settings\"><button class=\"button\">SETTINGS</button></a></p></td>";
  html_page += "<td colspan=\"2\"><a href=\"/settings\"><button class=\"button\">SETTINGS</button></a></td>";
  html_page += "</tr>\n";

  html_page += "</table>\n";

  html_page += "<br />\n";

  html_page += "</div>\n";
  html_page += "</body>\n";
  html_page += "</html>\n";

  return html_page;
}


String HTML_SETTINGS_PAGE() {
  String html_page = "<!DOCTYPE html> <html>\n";
  html_page += "<meta http-equiv=\"refresh\" content=\"5\" >\n";
  html_page += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  html_page += "<link rel=\"icon\" href=\"data:,\">\n";
  html_page += "<title>RJD Monitor</title>\n";
  html_page += "<style>\n";
  html_page += "html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center; }\n";
  html_page += "body { margin-top: 50px; color: white; background: black;}\n";
  // html_page += "h1 { color: #B4F9F3; margin: 50px auto 30px; }\n";
  html_page += "p { font-size: 18px; }\n";
  // html_page += "p { font-size: 24px; color: #B4F9F3; margin-bottom: 10px; }\n";
  // html_page += "table, table td { border: 0px solid #cccccc; }\n";
  html_page += "table, table td { text-align: center; vertical-align: middle; padding-top: 1px; padding-bottom: 1px; }\n";
  // html_page += ".button { background-color: #195B6A; border: none; color: white; padding: 16px 50px;\n";
  // html_page += ".button { background-color: #195B6A; border: none; color: white; padding: 16px 50px; text-decoration: none; text-align: center; font-size: 30px; margin: 2px; cursor: pointer; }\n";
  // html_page += ".button { background-color: #195B6A; border: none; color: white; height: 50px; width: 130px;\n";
  html_page += ".button { background-color: #195B6A; border: none; color: white; height: 50px; width: auto;\n";
  html_page += "text-decoration: none; text-align: center; font-size: 18px; margin: 2px; cursor: pointer; }\n";
  html_page += ".button2 { background-color: #77878A; }\n";
  html_page += ".button3 { background-color: #ff3300; }\n";
  
  html_page += "</style>\n";
  html_page += "</head>\n";

  html_page += "<body>\n";
  html_page += "<div id=\"webpage\">\n";
  // html_page += "<h1>RJD Monitor</h1>\n";
  // html_page += "<br />\n";
  html_page += "<h2>Debugging Page (auto refresh: 5\")</h2>\n";
  // html_page += "<br />\n";

  html_page += "<table style=\"margin-left:auto; margin-right:auto; allign:center; font-size:16px\">\n";

  html_page += "<tr>\n";
  html_page += "<td colspan=\"2\">INPUTS</td>";
  html_page += "</tr>\n";

  html_page += "<tr>\n";
  html_page += "<td>Temperature:</td>";
  html_page += "<td style=\"padding-left: 10px;\">";
  html_page += (String)temperature;
  html_page += " &#176C</td>"; // '°' is '&#176' in HTML
  html_page += "</tr>\n";
  html_page += "<tr>\n";
  html_page += "<td>Humidity:</td>";
  html_page += "<td style=\"padding-left: 10px;\">";
  html_page += (String)humidity;
  html_page += " %</td>";
  html_page += "</tr>\n";
  html_page += "<tr>\n";
  html_page += "<td>Analog IN (IR radiation):</td>";
  html_page += "<td style=\"padding-left: 10px;\">";
  html_page += (String)analogValue;
  html_page += " [0-1024]</td>";
  html_page += "</tr>\n";
  html_page += "<tr>\n";
  html_page += "<td>IR radiation:</td>";
  html_page += "<td style=\"padding-left: 10px;\">";
  float tempAnalogValuePerCent = 0.0;
  tempAnalogValuePerCent = (float)(((float)analogValue * 100.0) / 1024.0);
  html_page += (float)tempAnalogValuePerCent;
  html_page += " %</td>";
  html_page += "</tr>\n";
  html_page += "<tr>\n";
  html_page += "<td>CO2:</td>";
  html_page += "<td style=\"padding-left: 10px;\">";
  html_page += (String)co2Read;
  html_page += " [400-5000]</td>";
  html_page += "</tr>\n";
  html_page += "<tr>\n";
  html_page += "<td>Movement:</td>";
  html_page += "<td style=\"padding-left: 10px;\">";
  String tempYesNo;
  if (movementFlag) {
      tempYesNo = "yes";
    }
  else {
    tempYesNo = "no";
  }
  html_page += tempYesNo;
  html_page += "</td>";
  html_page += "</tr>\n";
  if ((lastMovementDay != NULL) || (lastMovementTime != NULL)) {
    html_page += "<tr>\n";
    html_page += "<td>Last movement:</td>";
    html_page += "<td style=\"padding-left: 10px;\">";
    html_page += (String)lastMovementDay;
    html_page += ", ";
    html_page += (String)lastMovementTime;
    html_page += "</td>";
    html_page += "</tr>\n";
  }
  html_page += "<tr>\n";
  html_page += "<td>Bedroom LEDs:</td>";
  html_page += "<td style=\"padding-left: 10px;\">";
  if (currentBedLEDstate) {
      tempYesNo = "on";
    }
  else {
    tempYesNo = "off";
  }
  html_page += tempYesNo;
  html_page += "</td>";
  html_page += "</tr>\n";

  html_page += "<tr><td colspan=\"2\"> </td></tr>\n";

  html_page += "<tr>\n";
  html_page += "<td colspan=\"2\">NETWORK</td>";
  html_page += "</tr>\n";  
  html_page += "<tr>\n";
  html_page += "<td>SSID:</td>";
  html_page += "<td style=\"padding-left: 10px;\">";
  html_page += (String)wifiSSID;
  html_page += "</td>";
  html_page += "</tr>\n";
  html_page += "<tr>\n";
  html_page += "<td>Signal:</td>";
  html_page += "<td style=\"padding-left: 10px;\">";
  html_page += (String)wifiSignal;
  html_page += "</td>";
  html_page += "</tr>\n";
  html_page += "<tr>\n";
  html_page += "<td>Local IP:</td>";
  html_page += "<td style=\"padding-left: 10px;\">";
  html_page += (String)localIPaddress;
  html_page += "</td>";
  html_page += "</tr>\n";
  html_page += "<tr>\n";
  html_page += "<td>MAC:</td>";
  html_page += "<td style=\"padding-left: 10px;\">";
  html_page += (String)macAddr;
  html_page += "</td>";
  html_page += "</tr>\n";

  html_page += "<tr><td colspan=\"2\"> </td></tr>\n";

  html_page += "<tr>\n";
  html_page += "<td colspan=\"2\">RUNTIME</td>";
  html_page += "</tr>\n";  
  html_page += "<tr>\n";
  html_page += "<td>Time:</td>";
  html_page += "<td style=\"padding-left: 10px;\">";
  html_page += (String)formatedTime;
  html_page += "</td>";
  html_page += "</tr>\n";
  html_page += "<tr>\n";
  html_page += "<td>millis():</td>";
  html_page += "<td style=\"padding-left: 10px;\">";
  html_page += String(millis());
  html_page += "</td>";
  html_page += "</tr>\n";
  html_page += "<tr>\n";
  html_page += "<td>Up time (millis):</td>";
  html_page += "<td style=\"padding-left: 10px;\">";
  html_page += millisToTime(true);
  html_page += "</td>";
  html_page += "</tr>\n";
  html_page += "<tr>\n";
  html_page += "<td>UTC offset:</td>";
  html_page += "<td style=\"padding-left: 10px;\">";
  html_page += (String)utcOffsetInSeconds;
  html_page += "</td>";
  html_page += "</tr>\n";

  html_page += "<tr><td colspan=\"2\"> </td></tr>\n";

  html_page += "<tr>\n";
  html_page += "<td colspan=\"2\">FLAGS</td>";
  html_page += "</tr>\n";  
  html_page += "<tr>\n";
  html_page += "<td>wifiAvailable:</td>";
  html_page += "<td style=\"padding-left: 10px;\">";
  html_page += (String)wifiAvailable;
  html_page += "</td>";
  html_page += "</tr>\n";
  html_page += "<tr>\n";
  html_page += "<td>allowLEDs:</td>";
  html_page += "<td style=\"padding-left: 10px;\">";
  html_page += (String)allowLEDs;
  html_page += "</td>";
  html_page += "</tr>\n";
  html_page += "<tr>\n";
  html_page += "<td>allowAutoRemote:</td>";
  html_page += "<td style=\"padding-left: 10px;\">";
  html_page += (String)allowAutoRemote;
  html_page += "</td>";
  html_page += "</tr>\n";
  html_page += "<tr>\n";
  html_page += "<td>allowThingSpeak:</td>";
  html_page += "<td style=\"padding-left: 10px;\">";
  html_page += (String)allowThingSpeak;
  html_page += "</td>";
  html_page += "</tr>\n";
  html_page += "<tr>\n";
  html_page += "<td>allowFlamePrint:</td>";
  html_page += "<td style=\"padding-left: 10px;\">";
  html_page += (String)allowFlamePrint;
  html_page += "</td>";
  html_page += "</tr>\n";
  html_page += "<tr>\n";
  html_page += "<td>currentBedLEDstate:</td>";
  html_page += "<td style=\"padding-left: 10px;\">";
  html_page += (String)currentBedLEDstate;
  html_page += "</td>";
  html_page += "</tr>\n";

  html_page += "<tr>\n";
  if (allowThingSpeak) {
    html_page += "<td><a href=\"/toggleThSp\"><button style=\"width:100%\" class=\"button\">ThingSpeak</button></a></td>";
  } else {
    html_page += "<td><a href=\"/toggleThSp\"><button style=\"width:100%\" class=\"button button2\">ThingSpeak</button></a></td>";
  }
  if (allowAutoRemote) {
    html_page += "<td><a href=\"/toggleAR\"><button style=\"width:100%\" class=\"button\">AutoRemote</button></a></td>";
  } else {
    html_page += "<td><a href=\"/toggleAR\"><button style=\"width:100%\" class=\"button button2\">AutoRemote</button></a></td>";
  }
  html_page += "</tr>\n";
  html_page += "<tr>\n";
  if (allowLEDs) {
    html_page += "<td><a href=\"/toggleLED\"><button style=\"width:100%\" class=\"button\">LEDs</button></a></td>";
  } else {
    html_page += "<td><a href=\"/toggleLED\"><button style=\"width:100%\" class=\"button button2\">LEDs</button></a></td>";
  }
  html_page += "<td><a href=\"/restart\"><button style=\"width:100%\" class=\"button button3\">RESTART</button></a></td>";
  html_page += "</tr>\n";
  html_page += "<tr><td colspan=\"2\"><a href=\"/\"><button style=\"width:100%\" class=\"button\">back</button></a></td></tr>\n";

  html_page += "</table>\n";

  html_page += "<br />\n";

  html_page += "</div>\n";
  html_page += "</body>\n";
  html_page += "</html>\n";

  return html_page;
}


void onConnect_restart() {
  refreshToRoot();
  delay(3000);
  ESP.restart();
}


void onConnect_toggleLEDs() {
  if (allowLEDs){ digitalWrite(ESPLED, LOW);}
  allowLEDs = !allowLEDs;
  // refreshToRoot();
  refreshToSettings();
  digitalWrite(ESPLED, HIGH);
}


void onConnect_toggleThingSpeak() {
  if (allowLEDs){ digitalWrite(ESPLED, LOW);}
  allowThingSpeak = !allowThingSpeak;
  // refreshToRoot();
  refreshToSettings();
  digitalWrite(ESPLED, HIGH);
}


void onConnect_toggleAutoRemote() {
  if (allowLEDs){ digitalWrite(ESPLED, LOW);}
  allowAutoRemote = !allowAutoRemote;
  // refreshToRoot();
  refreshToSettings();
  digitalWrite(ESPLED, HIGH);
}


void onConnect_bedLedOn() {
  if (allowLEDs){ digitalWrite(ESPLED, LOW);}
  currentBedLEDstate = true;
  // refreshToRoot();
  digitalWrite(ESPLED, HIGH);
}


void onConnect_bedLedOff() {
  if (allowLEDs){ digitalWrite(ESPLED, LOW);}
  currentBedLEDstate = false;
  // refreshToRoot();
  digitalWrite(ESPLED, HIGH);
}


void onConnect_default() {
  if (allowLEDs){ digitalWrite(ESPLED, LOW);}
  get_sensorData();
  server.send(200, "text/html", HTML_LANDING_PAGE());
  digitalWrite(ESPLED, HIGH);
}


void onConnect_settings() {
  if (allowLEDs){ digitalWrite(ESPLED, LOW);}
  get_sensorData();
  server.send(200, "text/html", HTML_SETTINGS_PAGE());
  digitalWrite(ESPLED, HIGH);
}


void onConnect_about() {
  if (allowLEDs){ digitalWrite(ESPLED, LOW);}
  server.send(200, "text/plain", "A smart home automation! (C) Apostolos Smyrnakis");
  digitalWrite(ESPLED, HIGH);
}


void onConnect_notFound(){
  if (allowLEDs){ digitalWrite(ESPLED, LOW);}
  server.send(404, "text/html", HTML_NOT_FOUND());
  digitalWrite(ESPLED, HIGH);
}


void refreshToRoot() {
  server.send(200, "text/html", HTML_REFRESH_TO_ROOT());
}


void refreshToSettings() {
  server.send(200, "text/html", HTML_REFRESH_TO_SETTINGS());
}


void handler_OTA() {
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


void handler_LED() {
  if (!allowLEDs) {
    digitalWrite(GREEN_LED, LOW);
    digitalWrite(BLUE_LED, LOW);
    digitalWrite(RED_LED, LOW);
    digitalWrite(ESPLED, HIGH);
    digitalWrite(PCBLED, HIGH);
  }
  else {
    if ((analogValue >= 50) && (analogValue < 300)) {
      digitalWrite(GREEN_LED, HIGH);
      digitalWrite(BLUE_LED, LOW);
      digitalWrite(RED_LED, LOW);
    } else if ((analogValue >= 300) && (analogValue < 550)) {
      digitalWrite(GREEN_LED, HIGH);
      digitalWrite(BLUE_LED, LOW);
      digitalWrite(RED_LED, HIGH);
    } else if ((analogValue >= 550) && (analogValue < 750)) {
      digitalWrite(GREEN_LED, LOW);
      digitalWrite(BLUE_LED, HIGH);
      digitalWrite(RED_LED, HIGH);
    } else if (analogValue >= 750) {
      digitalWrite(GREEN_LED, LOW);
      digitalWrite(BLUE_LED, LOW);
      digitalWrite(RED_LED, HIGH);
    } else {
      digitalWrite(GREEN_LED, LOW);
      digitalWrite(BLUE_LED, LOW);
      digitalWrite(RED_LED, LOW);
    }
  }
}


int call_AutoRemote(char message[], char deviceKey[], char password[]) {
  if (!allowAutoRemote) {
    return -1;
  }

  // https://autoremotejoaomgcd.appspot.com/sendmessage?key=XXXX&message=TEST&password=XXXX&ttl=10
  String autoRemoteURL = "https://autoremotejoaomgcd.appspot.com";
  autoRemoteURL += "/sendmessage?key=";
  autoRemoteURL += (String)deviceKey;
  autoRemoteURL += "&message=";
  autoRemoteURL += (String)message;
  autoRemoteURL += "&sender=";
  autoRemoteURL += "SmyESP-1";
  autoRemoteURL += "&password=";
  autoRemoteURL += (String)password;
  autoRemoteURL += "&ttl=";
  autoRemoteURL += (String)autoRemoteTTL;

  clientHttps.setInsecure();
  clientHttps.connect("https://autoremotejoaomgcd.appspot.com", 443);
  remoteClient.begin(clientHttps, autoRemoteURL);

  int httpCode = remoteClient.GET();
  String payload = remoteClient.getString();

  if (httpCode == 200) {
    Serial.println("[SUCCESS] contacting AutoRemote");
  }
  else {
    Serial.print("[DEB] AutoRemote response ");
    Serial.print(String(httpCode));
    Serial.print(" | ");
    Serial.println(String(payload));
  }

  return httpCode;
}

// TESTS - TO REMOVE
int call_talos(bool isMovement) {
  if (!allowTalos) {
    return -1;
  }

  clientHttps.setInsecure();

  if (isMovement) {
    clientHttps.connect(TALOS_URL_MOVEMENT, 443);
    remoteClient.begin(clientHttps, TALOS_URL_MOVEMENT);
  }
  else {
    clientHttps.connect(TALOS_URL_FIRE, 443);
    remoteClient.begin(clientHttps, TALOS_URL_FIRE);
  }

  int httpCode = remoteClient.GET();
  String payload = remoteClient.getString();

  if (httpCode == 200) {
    Serial.println("[SUCCESS] contacting TALOS");
  }
  else {
    Serial.println("[ERROR] contacting TALOS");
    Serial.print("[DEB] ");
    Serial.print(String(httpCode));
    Serial.print(" ");
    Serial.println(String(payload));
  }

  // clientHttps.close();

  return httpCode;
}


int call_thingSpeak() {
  if (!allowThingSpeak) {
    return -1;
  }

  // GET https://api.thingspeak.com/update?api_key=XXXXX&field1=X     // "184.106.153.149" or api.thingspeak.com
  String thinkSpeakAPIurl = "https://api.thingspeak.com";
  thinkSpeakAPIurl += "/update?api_key=";
  thinkSpeakAPIurl += (String)thingSpeakApiKey;
  if (!isnan(temperature)) {
    thinkSpeakAPIurl +="&field1=";
    thinkSpeakAPIurl += String(temperature);
  }
  if (!isnan(humidity)) {
    thinkSpeakAPIurl +="&field2=";
    thinkSpeakAPIurl += String(humidity);
  }
  thinkSpeakAPIurl +="&field3=";
  thinkSpeakAPIurl += String(analogValue);
  thinkSpeakAPIurl +="&field4=";
  thinkSpeakAPIurl += String(movementFlag);
  if ((co2Read >= 400) && (co2Read <= 4999)) {
    thinkSpeakAPIurl +="&field5=";
    thinkSpeakAPIurl += String(co2Read);
  }
  thinkSpeakAPIurl +="&field6=";
  thinkSpeakAPIurl += String(currentBedLEDstate);

  clientHttps.setInsecure();
  clientHttps.connect("https://api.thingspeak.com", 443);
  remoteClient.begin(clientHttps, thinkSpeakAPIurl);

  int httpCode = remoteClient.GET();
  String payload = remoteClient.getString();

  if (httpCode == 200) {
    Serial.println("[SUCCESS] contacting ThingSpeak");
  }
  else {
    Serial.print("[DEB] ThingSpeak response ");
    Serial.print(String(httpCode));
    Serial.print(" | ");
    Serial.println(String(payload));
  }

  return httpCode;
}


void call_movementReport_XmasLEDs() {
  // notify XmasLEDs that there's movement (postpone auto turn off)
  remoteClient.setTimeout(8);
  remoteClient.begin(clientHttp, "http://192.168.1.31/movement");
  int httpCode = remoteClient.GET();
  // if (httpCode > 0) {
  //   String payload = remoteClient.getString();
  //   Serial.println(payload);
  // }
  remoteClient.end();
}


void call_NTP(bool printData) {
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


void get_sensorData() {
  temperature = dht.readTemperature();
  humidity = dht.readHumidity();
  if (mhz19b.isReady()) {
    co2Read =  get_MHZ19B();
  }
  lastSensorTime = millis();
}


int get_MHZ19B() {
  int16_t result = mhz19b.readCO2();

  if (result < 0) {
    // Print error code
    switch (result) {
      case MHZ19B_RESULT_ERR_CRC:
        Serial.println(F("[ERROR] MH-Z19B CRC error"));
        break;
      case MHZ19B_RESULT_ERR_TIMEOUT:
        Serial.println(F("[ERROR] MH-Z19B RX timeout"));
        break;
      default:
        Serial.print(F("[ERROR] MH-Z19B : "));
        Serial.println(result);
        break;
    }
    return result;
  }
  // Serial.print(result);
  // Serial.println(F(" ppm"));
  return result;
}


void mhz19b_settings() {
  char firmwareVersion[5];

  // Optional: Print firmware version
  Serial.print(F("  Firmware: "));
  mhz19b.getVersion(firmwareVersion, sizeof(firmwareVersion));
  Serial.println(firmwareVersion);

  // Optional: Set CO2 range 2000ppm or 5000ppm (default) once
  // Serial.print(F("Set range..."));
  // mhz19b.setRange2000ppm();
  // mhz19b.setRange5000ppm();

  // Optional: Print operating range
  Serial.print(F("  Range: "));
  Serial.print(mhz19b.getRange());
  Serial.println(F("ppm"));

  // Optional: Print Automatic Baseline Calibration status
  Serial.print(F("  Auto calibrate: "));
  Serial.println(mhz19b.getAutoCalibration() ? F("On") : F("Off"));
}


void serial_printAll() {
  Serial.println();
  String tempDayTime;
  tempDayTime = dayToday;
  tempDayTime += ", ";
  tempDayTime += formatedTime;
  Serial.println(String(tempDayTime));
  // Serial.println(timeClient.getFormattedTime());
  Serial.print("Temperature: ");
  Serial.print(String(temperature));
  Serial.println(" °C");
  Serial.print("Humidity: ");
  Serial.print(String(humidity));
  Serial.println(" %");
  Serial.print("IR value: ");
  Serial.print(String(analogValue));
  Serial.println(" [0-1024]");
  Serial.print("CO2: ");
  Serial.print(String(co2Read));
  Serial.println(" ppm [400-5000]");
  Serial.print("Movement: ");
  Serial.print(String(movement));
  Serial.println(" [0/1]");
  Serial.println();
}


void loop() {
  ArduinoOTA.handle();


  if (millis() > lastNTPtime + ntpInterval) {
    call_NTP(false);
  }

  if (millis() > lastPirIrTime + PirIrInterval) {
    analogValue = analogRead(ANLG_IN);
    analogValue = map(analogValue, 0, 1024, 1024, 0);
    movement = digitalRead(PIRIN);

    lastPirIrTime = millis();
  }

  if (millis() > lastSensorTime + sensorsInterval) {
    get_sensorData();
  }

  if (movement) {
    movementFlag = true;
    lastMovementTime = formatedTime;
    lastMovementDay = dayToday;
  }

  if (allowLEDs){ (movement) ? digitalWrite(PCBLED, LOW) : digitalWrite(PCBLED, HIGH);}


  if ((analogValue > flameThreshold) && allowFlamePrint) {
    Serial.print("WARNING: flame detected! (");
    Serial.print(analogValue);
    Serial.println(")");
    call_AutoRemote("WARNING_flame-detected", autoRemotePlus6, autoRemotePass);
    // call_talos(false);
    allowFlamePrint = false;
    flameMillis = millis();
  }
  // debouncing flame alarm
  if ((analogValue < flameThreshold) && !allowFlamePrint && (millis() >= flameMillis+60000)) {
    allowFlamePrint = true;
  }


  handler_LED();


  if (millis() > lastUploadTime + thingSpeakInterval) {
    if (allowLEDs) { digitalWrite(ESPLED, LOW);}

    get_sensorData();
    serial_printAll();

    call_thingSpeak();

    if (movementFlag) {
      call_AutoRemote("WARNING_movement-detected", autoRemotePlus6, autoRemotePass);
      // call_talos(true);
      call_movementReport_XmasLEDs();
    }

    movementFlag = false;
    lastUploadTime = millis();
    digitalWrite(ESPLED, HIGH);
  }

  server.handleClient();
}