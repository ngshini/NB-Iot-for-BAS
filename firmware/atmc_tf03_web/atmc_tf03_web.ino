#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <HardwareSerial.h>
#include <driver/gpio.h>
#include <esp_system.h>
#include "tf03_parser.h"
#include "parser_checks.h"
#include "web_page.h"
#if __has_include("config.h")
#include "config.h"
#else
#include "config.example.h"
#endif

static WebServer server(80);
static HardwareSerial laser(1);
static portMUX_TYPE sampleLock = portMUX_INITIALIZER_UNLOCKED;
static int rxPin = -1;
static uint32_t baud = 115200, overRange = 18000;
static uint16_t distanceCm = 0, strength = 0;
static uint32_t lastFrame = 0, frames = 0;
static bool uartReady = false;
static String stationSsid, stationPass, adminPass, csrfToken, apName;
static uint32_t rebootAt = 0;
static const uint32_t BAUDS[] = {9600,19200,38400,57600,115200,230400,460800};

static bool validPin(int p) {
  return p == -1 || (p >= 0 && GPIO_IS_VALID_GPIO(p) && p != 1 && p != 3 && !(p >= 6 && p <= 11));
}

static void readLaser(void *) {
  Tf03Parser parser;
  for (;;) {
    for (unsigned i=0; i<4096 && laser.available(); ++i) {
      if (parser.feed(uint8_t(laser.read()))) {
        portENTER_CRITICAL(&sampleLock);
        distanceCm = parser.distance; strength = parser.strength;
        lastFrame = millis(); ++frames;
        portEXIT_CRITICAL(&sampleLock);
      }
    }
    delay(1);
  }
}

static void sendData() {
  portENTER_CRITICAL(&sampleLock);
  uint16_t d=distanceCm, s=strength;
  uint32_t at=lastFrame, count=frames;
  portEXIT_CRITICAL(&sampleLock);
  uint32_t age=millis()-at;
  const char *status=rxPin<0?"pin_not_configured":!uartReady?"uart_error":!count?"no_data":
    age>2000?"stale":(s<40 || d==0 || d>=overRange)?"invalid":"ok";
  char distance[24], ageText[24], strengthText[16], json[400];
  if (!strcmp(status,"ok")) snprintf(distance,sizeof distance,"%.2f",d/100.0);
  else strcpy(distance,"null");
  if (count) {snprintf(ageText,sizeof ageText,"%lu",(unsigned long)age);snprintf(strengthText,sizeof strengthText,"%u",s);}
  else {strcpy(ageText,"null");strcpy(strengthText,"null");}
  bool connected=WiFi.status()==WL_CONNECTED;
  String ip=connected?WiFi.localIP().toString():WiFi.softAPIP().toString();
  snprintf(json,sizeof json,"{\"distance\":%s,\"unit\":\"m\",\"status\":\"%s\",\"strength\":%s,\"age_ms\":%s,\"frames\":%lu,\"wifi_connected\":%s,\"ip\":\"%s\",\"rx_gpio\":%d}",
    distance,status,strengthText,ageText,(unsigned long)count,connected?"true":"false",ip.c_str(),rxPin);
  server.sendHeader("Cache-Control","no-store");
  server.send(200,"application/json",json);
}

static bool authenticate() {
  if(server.authenticate("admin",adminPass.c_str()))return true;
  server.requestAuthentication(); return false;
}
static bool authorizeSave() {
  if(!authenticate())return false;
  if(server.arg("token")!=csrfToken){server.send(403,"text/plain","Invalid form token; reload settings.");return false;}
  return true;
}
static bool numberArg(const char *name,long &value) {
  String raw=server.arg(name); if(raw.isEmpty())return false;
  char *end=nullptr; value=strtol(raw.c_str(),&end,10);
  return end!=raw.c_str() && *end=='\0';
}
static void saved() {
  server.send(200,"text/html; charset=utf-8","<h2>Đã lưu. Bo sẽ khởi động lại.</h2><p>Kết nối lại Wi-Fi của bo hoặc mở IP mới được in trên Serial.</p><a href='/'>Về trang chính</a>");
  rebootAt=millis()+1500;
}

void setup() {
  Serial.begin(115200);
  Preferences prefs;
  prefs.begin("atmc-tf03",false);
  rxPin=prefs.getInt("rx",-1); baud=prefs.getUInt("baud",115200); overRange=prefs.getUInt("range",18000);
  if(!validPin(rxPin))rxPin=-1;
  stationSsid=prefs.getString("ssid",WIFI_SSID); stationPass=prefs.getString("pass",WIFI_PASS);
  adminPass=prefs.getString("admin","");
  if(adminPass.length()<12) {
    char pass[25]; snprintf(pass,sizeof pass,"%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random());
    adminPass=pass; prefs.putString("admin",adminPass);
  }
  prefs.end();
  char token[25];snprintf(token,sizeof token,"%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random());csrfToken=token;
  if(rxPin>=0) {
    laser.setRxBufferSize(4096); laser.begin(baud,SERIAL_8N1,rxPin,-1);
    uartReady=bool(laser);
    if(uartReady && xTaskCreate(readLaser,"tf03",4096,nullptr,2,nullptr)!=pdPASS){laser.end();uartReady=false;}
  }
  WiFi.mode(WIFI_AP_STA);
  apName="ATMC-TF03-"+WiFi.macAddress().substring(12); apName.replace(":","");
  bool apOk=WiFi.softAP(apName.c_str(),adminPass.c_str());
  Serial.printf("\nATMC TF03 WEB (Wi-Fi only)\nSetup Wi-Fi: %s\nSetup password / admin password: %s\nAP: %s http://%s/\n",
    apName.c_str(),adminPass.c_str(),apOk?"OK":"FAILED",WiFi.softAPIP().toString().c_str());
  if(stationSsid.length() && stationSsid!="CHANGE_ME") {
    WiFi.setAutoReconnect(true);WiFi.begin(stationSsid.c_str(),stationPass.c_str());
  }
  server.on("/",HTTP_GET,[]{server.send_P(200,"text/html; charset=utf-8",WEB_PAGE);});
  server.on("/api/data",HTTP_GET,sendData);
  server.on("/settings",HTTP_GET,[]{
    if(!authenticate())return;
    String page=FPSTR(SETTINGS_PAGE), options;
    for(auto b:BAUDS)options+="<option value='"+String(b)+"'"+(b==baud?" selected":"")+">"+String(b)+"</option>";
    page.replace("%TOKEN%",csrfToken);page.replace("%PIN%",String(rxPin));page.replace("%RANGE%",String(overRange));page.replace("%BAUDS%",options);
    server.sendHeader("Cache-Control","no-store");server.send(200,"text/html; charset=utf-8",page);
  });
  server.on("/sensor",HTTP_POST,[]{
    if(!authorizeSave())return;
    long p,b,r;bool ok=numberArg("pin",p)&&numberArg("baud",b)&&numberArg("range",r);
    bool baudOk=false;if(ok)for(auto v:BAUDS)if(b==long(v))baudOk=true;
    if(!ok||p< -1||p>39||!validPin(int(p))||!baudOk||r<1||r>65535){server.send(400,"text/plain","Invalid GPIO, baud or range.");return;}
    Preferences prefs;prefs.begin("atmc-tf03",false);
    bool stored=prefs.putInt("rx",p)>0 && prefs.putUInt("baud",b)>0 && prefs.putUInt("range",r)>0;prefs.end();
    if(!stored){server.send(500,"text/plain","Could not save settings.");return;}saved();
  });
  server.on("/wifi",HTTP_POST,[]{
    if(!authorizeSave())return;
    String s=server.arg("ssid"),p=server.arg("password");
    if(s.isEmpty()||s.length()>32||p.length()>63||(p.length()>0&&p.length()<8)){server.send(400,"text/plain","Invalid SSID or password length.");return;}
    Preferences prefs;prefs.begin("atmc-tf03",false);prefs.putString("ssid",s);prefs.putString("pass",p);
    bool stored=prefs.getString("ssid")==s && prefs.getString("pass")==p;prefs.end();
    if(!stored){server.send(500,"text/plain","Could not save Wi-Fi.");return;}saved();
  });
  server.onNotFound([]{server.send(404,"text/plain","Not found");});server.begin();
  Serial.printf("RX GPIO=%d, baud=%lu. Settings user: admin\n",rxPin,(unsigned long)baud);
}

void loop() {
  server.handleClient();
  if(rebootAt && int32_t(millis()-rebootAt)>=0)ESP.restart();
  static bool wasConnected=false;
  bool connected=WiFi.status()==WL_CONNECTED;
  if(connected&&!wasConnected)Serial.printf("Open website: http://%s/\n",WiFi.localIP().toString().c_str());
  wasConnected=connected;
  static uint32_t retry=0;
  if(!connected && stationSsid.length() && stationSsid!="CHANGE_ME" && millis()-retry>=30000){retry=millis();WiFi.reconnect();}
  delay(2);
}
