#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

const int ALT_PIN = D2;
const int GLOW_RELAY = D9;
const int DASH_LIGHT = D3;
const int TEMP_PIN = A0;

const unsigned long AFTER_GLOW_DURATION_MS = 3000;
const int AFTER_GLOW_COLD_THRESHOLD = 700;

const char* FIRMWARE_VERSION = "1.0.0";

Preferences prefs;
WebServer server(80);

int t8,t7,t6,t5,t4,t3,t2;

String homeSsid;
String homePass;

String lastOtaState;

unsigned long preGlowStart = 0;
unsigned long afterGlowStart = 0;

bool preGlowFinished = false;
bool afterGlowActive = false;
bool glowComplete = false;

int activeTarget = 0;
int bootTempReading = 0;

String htmlAttr(String s) {
  s.replace("&", "&amp;");
  s.replace("\"", "&quot;");
  return s;
}

void handleRoot() {

  int sensor = analogRead(TEMP_PIN);

  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'><style>";
  html += "body{background:#121212;color:#eee;font-family:sans-serif;text-align:center;padding:20px;}";
  html += ".card{background:#1e1e1e;padding:20px;border-radius:15px;border:1px solid #333;max-width:400px;margin:auto;}";
  html += "input{width:60px;padding:10px;margin:5px;background:#2a2a2a;color:#fff;border:1px solid #444;border-radius:8px;text-align:center;}";
  html += ".btn{background:#e67e22;color:white;border:none;padding:15px;border-radius:8px;cursor:pointer;width:100%;font-size:16px;margin-top:10px;}";
  html += ".status{color:#e67e22;font-weight:bold;font-size:1.2em;}</style></head><body><div class='card'>";

  html += "<h2>105 GLOW CONTROL</h2>";
  html += "<p>A0 Reading: <span class='status'>" + String(sensor) + "</span></p>";
  html += "<p>Target: <span class='status'>" + String(activeTarget) + "s</span></p>";

  html += "<form action='/save' method='POST'>";
  html += "A0 > 800: <input type='number' name='t8' value='"+String(t8)+"'>s<br>";
  html += "A0 > 700: <input type='number' name='t7' value='"+String(t7)+"'>s<br>";
  html += "A0 > 600: <input type='number' name='t6' value='"+String(t6)+"'>s<br>";
  html += "A0 > 500: <input type='number' name='t5' value='"+String(t5)+"'>s<br>";
  html += "A0 > 400: <input type='number' name='t4' value='"+String(t4)+"'>s<br>";
  html += "A0 > 300: <input type='number' name='t3' value='"+String(t3)+"'>s<br>";
  html += "A0 > 200: <input type='number' name='t2' value='"+String(t2)+"'>s<br>";
  html += "<label>Home Wi-Fi SSID</label><br>";
  html += "<input type='text' name='wifi_ssid' value=\""+htmlAttr(homeSsid)+"\"><br>";
  html += "<label>Home Wi-Fi password</label><br>";
  html += "<input type='password' name='wifi_pass' value=\""+htmlAttr(homePass)+"\"><br>";
  html += "<input type='submit' class='btn' value='SAVE SETTINGS'></form></div></body></html>";

  server.send(200,"text/html",html);
}

void handleSave() {

  t8 = server.arg("t8").toInt(); prefs.putInt("t8",t8);
  t7 = server.arg("t7").toInt(); prefs.putInt("t7",t7);
  t6 = server.arg("t6").toInt(); prefs.putInt("t6",t6);
  t5 = server.arg("t5").toInt(); prefs.putInt("t5",t5);
  t4 = server.arg("t4").toInt(); prefs.putInt("t4",t4);
  t3 = server.arg("t3").toInt(); prefs.putInt("t3",t3);
  t2 = server.arg("t2").toInt(); prefs.putInt("t2",t2);

  homeSsid = server.arg("wifi_ssid"); prefs.putString("wifi_ssid", homeSsid);
  homePass = server.arg("wifi_pass"); prefs.putString("wifi_pass", homePass);

  server.sendHeader("Location","/");
  server.send(303);
}

void setup() {

  pinMode(GLOW_RELAY,OUTPUT);
  pinMode(DASH_LIGHT,OUTPUT);
  pinMode(ALT_PIN,INPUT);
  pinMode(TEMP_PIN,INPUT);

  digitalWrite(GLOW_RELAY,HIGH);
  digitalWrite(DASH_LIGHT,HIGH);

  Serial.begin(115200);

  prefs.begin("glow_final",false);

  t8 = prefs.getInt("t8",5);
  t7 = prefs.getInt("t7",4);
  t6 = prefs.getInt("t6",3);
  t5 = prefs.getInt("t5",2);
  t4 = prefs.getInt("t4",1);
  t3 = prefs.getInt("t3",1);
  t2 = prefs.getInt("t2",1);

  homeSsid = prefs.getString("wifi_ssid", "");
  homePass = prefs.getString("wifi_pass", "");

  String prevFw = prefs.getString("prev_fw", "");
  if (prevFw.length() > 0 && prevFw != FIRMWARE_VERSION) {
    lastOtaState = "updated from v" + prevFw + " → v" + String(FIRMWARE_VERSION);
  } else {
    lastOtaState = "up to date";
  }
  prefs.putString("prev_fw", FIRMWARE_VERSION);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("GlowPlugController","password123");
  if (homeSsid.length() > 0) {
    WiFi.begin(homeSsid.c_str(), homePass.c_str());
  }

  server.on("/",handleRoot);
  server.on("/save",HTTP_POST,handleSave);
  server.begin();

  delay(200);

  bootTempReading = analogRead(TEMP_PIN);

  if(bootTempReading > 800) activeTarget = t8;
  else if(bootTempReading > 700) activeTarget = t7;
  else if(bootTempReading > 600) activeTarget = t6;
  else if(bootTempReading > 500) activeTarget = t5;
  else if(bootTempReading > 400) activeTarget = t4;
  else if(bootTempReading > 300) activeTarget = t3;
  else if(bootTempReading > 200) activeTarget = t2;
  else activeTarget = 0;

  preGlowStart = millis();
}

void loop() {

  server.handleClient();

  unsigned long now = millis();

  // PRE-GLOW TIMER
  if(!preGlowFinished) {

    if(now - preGlowStart >= (unsigned long)activeTarget * 1000) {

      digitalWrite(DASH_LIGHT,LOW);
      preGlowFinished = true;
    }
  }

  // ENGINE START DETECT
  if(preGlowFinished && !afterGlowActive && !glowComplete && digitalRead(ALT_PIN)==HIGH) {

    if(bootTempReading < AFTER_GLOW_COLD_THRESHOLD) {

      digitalWrite(GLOW_RELAY,LOW);
      glowComplete = true;
      return;
    }

    afterGlowStart = millis();
    afterGlowActive = true;
  }

  // AFTER-GLOW TIMER
  if(afterGlowActive && !glowComplete) {

    if(now - afterGlowStart >= AFTER_GLOW_DURATION_MS) {

      digitalWrite(GLOW_RELAY,LOW);
      glowComplete = true;
    }
  }
}
