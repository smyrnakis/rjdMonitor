// #include "rjdMonitor.ino"

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


String HTML_REFRESH_TO_ROOT() {
    String html_page = "<HEAD>";
    html_page += "<meta http-equiv=\"refresh\" content=\"0;url=/\">";
    html_page += "</head>";

    return html_page;
}


String HTML_LANDING_PAGE() {
  String html_page = "<!DOCTYPE html> <html>\n";
  html_page += "<meta http-equiv=\"refresh\" content=\"5\" >\n";
  html_page += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  html_page += "<title>RJD Monitor</title>\n";
  html_page += "<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n";
  html_page += "body{margin-top: 50px;} h1 {color: #B4F9F3;margin: 50px auto 30px;}\n";
  html_page += "body {color: white; background: black;}\n";
  html_page += "p {font-size: 24px;color: #B4F9F3;margin-bottom: 10px;}\n";
  html_page += ".button { background-color: #195B6A; border: none; color: white; padding: 16px 50px;\n";
  html_page += "text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}\n";
  html_page += ".button2 { background-color: #77878A;}\n";
  html_page += "</style>\n";
  html_page += "</head>\n";
  html_page += "<body>\n";
  html_page += "<div id=\"webpage\">\n";
  html_page += "<h1>RJD Monitor</h1>\n";
  
  html_page += "<p><b>Local IP:</b> ";
  html_page += (String)localIPaddress;
  html_page += "</p>";
  html_page += "<p><b>Timestamp:</b> ";
  html_page += (String)formatedTime;
  html_page += "</p>";
  html_page += "<p><b>Current millis:</b> ";
  html_page += (String)millis();
  html_page += "</p>";
  html_page += "<p><b>Up time (millis):</b> ";
  html_page += millisToTime(true);
  html_page += "</p>";
  html_page += "<p></p>";

  html_page += "<p><b>Temperature:</b> ";
  html_page += (String)temperature;
  html_page += "&#176C</p>"; // '°' is '&#176' in HTML
  html_page += "<p><b>Humidity:</b> ";
  html_page += (String)humidity;
  html_page += "%</p>";
  html_page += "<p><b>IR sensor:</b> ";
  // html_page += (String)analogValue;
  // html_page += " [0-1024]</p>";
  float tempAnalogValuePerCent = 0.0;
  tempAnalogValuePerCent = (float)(((float)analogValue * 100.0) / 1024.0);
  html_page += (float)tempAnalogValuePerCent;
  // html_page += (String)tempAnalogValuePerCent;
  html_page += "%</p>";
  html_page += "<p></p>";
  html_page += "<p><b>Bedroom LEDs:</b> ";
  html_page += (String)currentBedLEDstate;
  html_page += " [0/1]</p>";
  html_page += "<p><b>Movement:</b> ";
  // html_page += (String)movement;
  html_page += (String)tempMove;
  html_page += " [0/1]</p>";
  if ((lastMovementDay != NULL) || (lastMovementTime != NULL)) {
    html_page += "<p><b>Last movement:</b> ";
    html_page += (String)lastMovementDay;
    html_page += ", ";
    html_page += (String)lastMovementTime;
    html_page += "</p>";
  }
  html_page += "<p></p>";

  if (allowLEDs) {
    html_page += "<th colspan=\"2\"><p><a href=\"/LEDoff\"><button class=\"button\">LEDs allowed</button></a></p></th>";
  } else {
    html_page += "<th colspan=\"2>\"<p><a href=\"/LEDon\"><button class=\"button button2\">LEDs disallowed</button></a></p></th>";
  }
  
  html_page += "</div>\n";
  html_page += "</body>\n";
  html_page += "</html>\n";

  return html_page;
}