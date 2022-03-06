#include "Arduino.h"
#include <WiFi.h>
#include "CommonWifiManager.h"
#include "ThermistorSensor.h"
#include <TFT_eSPI.h> // Hardware-specific library
#include <SPI.h>
#include "temperatureImages.h"
#include <SimpleTimer.h>
// #include "NotoSansBold15.h"
// #include "NotoSansBold36.h"
/*
screen pinout
DIN -> d23
CLk -> d18
CS -> d5
DC -> d22 
RST -> d21
BUSY -> d4
*/


 /* #region Hot Tub Controller Variables */
// which analog pin to connect
#define THERMISTORPIN 34         
#define NUMSAMPLES 30

//---Relay Control Setup Start---//
#define tempUpButton 13 //27
#define tempDownButton 14 //26
#define pump1Button 36 //25
#define pump2Button 39 //33
#define ledButton 35 //32

#define SHORT_BUTTON_PRESS_DELAY 300
#define LONG_BUTTON_PRESS_DELAY 1000
unsigned long lastTempUpButtonPress = 0;
unsigned long lastTempDownButtonPress = 0;
unsigned long lastPump1ButtonPress = 0;
unsigned long lastPump2ButtonPress = 0;
unsigned long lastLedButtonPress = 0;

#define pump1LowPin 26 // 19 //
#define pump1HighPin 25 //21
#define heaterPin 32 // 23 
#define pump2Pin 33 // 22
//#define ledRelayPin 5
//---Relay Control Setup End ---//

//---Preferred Settings---//
#define DEFAULTTEMPERATURE 100;

//---Preferred Settings End

int setTemperature;
int restartHeaterTemperature;
float currentTemperature;
int lastDisplayedTemperature = 0;
int lastSetTemperature = 0;
bool heaterAndPump1LowOn;
bool pump1HighOn;
bool pump2On;
bool ledOn;
char degreesSymbol = char(176);
bool wifiConnected = false;
uint16_t tempIcon[434];
int iconState = 0;
int yPosCurrentTemp = 25;
int yPosSetTemp = 70;
enum iconStates { iconTempOff, iconTempLow, iconTemp1, iconTemp2, iconTemp3, iconTempHigh }; 

int maxTemp = 104;
int minTemp = 60;

ThermistorSensor thermistor(THERMISTORPIN);
TFT_eSPI tft = TFT_eSPI();
/* #endregion */

/* #region Wifi Variables*/
String localServerSsid = "Hot Tub Access Point";
String hostname = "HotTub";
WiFiClass scanWifi;
CommonWifiManager wifiManager(localServerSsid, WiFi, scanWifi);
WebServer server(80);

String ssid;
String password;
/* #endregion*/

TaskHandle_t hotTubControllerTask;
TaskHandle_t wifiTask;
SimpleTimer timer;

/* #region Hot Tub Controller Interupts*/
bool checkLastButtonPush(unsigned long lastPressTime, int delay) {
  if ((millis() - lastPressTime) > delay) {
    return true;
  }
  return false;
}

void IRAM_ATTR tempUp() {
  if (digitalRead(tempUpButton) == HIGH) {
    if (checkLastButtonPush(lastTempUpButtonPress, SHORT_BUTTON_PRESS_DELAY)) {
      Serial.println("TempUp");
      if (setTemperature + 1 <= maxTemp) {
        setTemperature += 1;
        restartHeaterTemperature = setTemperature;
      }
      lastTempUpButtonPress = millis();
    }
  }
}

void IRAM_ATTR tempDown() {
  if (digitalRead(tempDownButton) == HIGH) {
    if (checkLastButtonPush(lastTempDownButtonPress, SHORT_BUTTON_PRESS_DELAY)) {
      Serial.println("TempDown");
      if (setTemperature - 1 >= minTemp) {
        setTemperature -= 1;
        restartHeaterTemperature = setTemperature;
      }
      lastTempDownButtonPress = millis();
    }
  }
}

void IRAM_ATTR pump1Toggle() {
  if (digitalRead(pump1Button) == HIGH) {
    if (checkLastButtonPush(lastPump1ButtonPress, LONG_BUTTON_PRESS_DELAY)) {
      Serial.println("pump1Toggle");
      lastPump1ButtonPress = millis();

      if (pump1HighOn) {
        digitalWrite(pump1HighPin, LOW);
        pump1HighOn = false;
      } else {
        digitalWrite(pump1HighPin, HIGH);
        pump1HighOn = true;
      }
    }
  }
}

void IRAM_ATTR pump2Toggle() {
  if (digitalRead(pump2Button) == HIGH) {
    if (checkLastButtonPush(lastPump2ButtonPress, LONG_BUTTON_PRESS_DELAY)) {
      Serial.println("pump2Toggle");
      lastPump2ButtonPress = millis();

      if (pump2On) {
        digitalWrite(pump2Pin, LOW);
        pump2On = false;
      } else {
        digitalWrite(pump2Pin, HIGH);
        pump2On = true;
      }
    }
  }
}

void IRAM_ATTR ledToggle() {
  if (checkLastButtonPush(lastLedButtonPress, LONG_BUTTON_PRESS_DELAY)) {
    Serial.println("ledToggle");
    lastLedButtonPress = millis();

    // if (ledOn) {
    //   digitalWrite(ledRelayPin, LOW);
    //   ledOn = false;
    // } else {
    //   digitalWrite(ledRelayPin, HIGH);
    //   ledOn = true;
    // }
  }
}
/* #endregion*/

void setup(void) {
  Serial.begin(9600);
  WiFi.setHostname(hostname.c_str());

  setTemperature = DEFAULTTEMPERATURE;
  restartHeaterTemperature = setTemperature;
  currentTemperature = 0;
  heaterAndPump1LowOn = false;
  pump1HighOn = false;
  pump2On = false;
  ledOn = false;

  pinMode(tempUpButton, INPUT);
  pinMode(tempDownButton, INPUT);
  pinMode(pump1Button, INPUT);
  pinMode(pump2Button, INPUT);
  pinMode(ledButton, INPUT);

  pinMode(pump1LowPin, OUTPUT);
  pinMode(pump1HighPin, OUTPUT);
  pinMode(heaterPin, OUTPUT);
  pinMode(pump2Pin, OUTPUT);
  //pinMode(ledRelay, OUTPUT);

  digitalWrite(pump1LowPin, LOW);
  digitalWrite(pump1HighPin, LOW);
  digitalWrite(heaterPin, LOW);
  digitalWrite(pump2Pin, LOW);
  //digitalWrite(ledRelay, LOW);

  attachInterrupt(tempUpButton, tempUp, RISING);
  attachInterrupt(tempDownButton, tempDown, RISING);
  attachInterrupt(pump1Button, pump1Toggle, RISING);
  attachInterrupt(pump2Button, pump2Toggle, RISING);
  attachInterrupt(ledButton, ledToggle, RISING);

  tft.init();
  tft.setRotation(3);
  tft.setTextWrap(false);
  tft.fillScreen(TFT_BLACK);
  // tft.fillScreen(TFT_WHITE);
  tft.setSwapBytes(true);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  timer.setInterval(300, changeTempIcon); // 300 = .3 sec
  timer.setInterval(1000, updateTemp); // 1 sec

  xTaskCreatePinnedToCore(
    hotTubControllerTaskCode,   /* Task function. */
    "hotTubControllerTask",     /* name of task. */
    10000,       /* Stack size of task */
    NULL,        /* parameter of the task */
    1,           /* priority of the task */
    &hotTubControllerTask,      /* Task handle to keep track of created task */
    0);          /* pin task to core 0 */  
  
  xTaskCreatePinnedToCore(
    wifiTaskCode,   /* Task function. */
    "wifiTask",     /* name of task. */
    10000,       /* Stack size of task */
    NULL,        /* parameter of the task */
    1,           /* priority of the task */
    &wifiTask,      /* Task handle to keep track of created task */
    1);          /* pin task to core 0 */ 
}

void loop(void) {
  delay(2);
}

/* #region Hot Tub Controller */ 
void hotTubControllerTaskCode( void * pvParameters ){
  for (;;) {
    timer.run();
    delay(10);
  }
}

void updateTemp() {
  checkTemps();
  readTemp();
  checkPumpAndHeater();

  bool lastTempChanged = false;

  if (lastDisplayedTemperature != (int)currentTemperature) {
    int xPosCurrentTemp = 110;
    tft.setTextFont(4);
    tft.setTextSize(1);
    tft.setCursor(xPosCurrentTemp - 5, yPosCurrentTemp); 
    tft.print(" `F");

    tft.setTextDatum(TR_DATUM);
    int temp = currentTemperature;
    char tempString [5];
    tft.setTextSize(2);
    tft.fillRect(xPosCurrentTemp - 90, yPosCurrentTemp, 80, 52, TFT_BLACK);
    // tft.drawString("100", xPosCurrentTemp, yPosCurrentTemp);
    tft.drawString(itoa(temp, tempString, 10), xPosCurrentTemp, yPosCurrentTemp);
    lastDisplayedTemperature = temp;
    lastTempChanged = true;
  }
  if (lastSetTemperature != (int)setTemperature || lastTempChanged) {
    int xPosSetTemp = 90;
    tft.setTextFont(4);
    tft.setTextSize(1);
    tft.setCursor(xPosSetTemp - 2, yPosSetTemp);
    tft.print(" `F");

    tft.setTextDatum(TR_DATUM);
    int temp = setTemperature;
    char tempString [5];
    tft.fillRect(xPosSetTemp - 67, yPosSetTemp, 60, 26, TFT_BLACK);
    tft.drawString(itoa(temp, tempString, 10), xPosSetTemp, yPosSetTemp);
    lastDisplayedTemperature = temp;
  }

  // Serial.print("Temperature "); 
  // Serial.print(currentTemperature);
  // Serial.println(" *F");
  // Serial.print("Set Temp: "); 
  // Serial.print(setTemperature);
  // Serial.println(" *F");
  // Serial.print("Restart Heater Temp: "); 
  // Serial.print(restartHeaterTemperature);
  // Serial.println(" *F");
}

void checkTemps() {
  if (maxTemp < setTemperature) {
    setTemperature = maxTemp;
  }
  if (minTemp > setTemperature) {
    setTemperature = minTemp;
  }
}

void changeTempIcon() {
  int xPos = 135;
  int yPos = 33;
  int width = 14;
  int height = 31;

  if (heaterAndPump1LowOn) {
    switch (iconState) {
      case iconTempOff:
        iconState++;
      case iconTempLow:
        tft.pushImage(xPos, yPos, width, height, tempLow);
        break;
      case iconTemp1:
        tft.pushImage(xPos, yPos, width, height, temp1);
        break;
      case iconTemp2:
        tft.pushImage(xPos, yPos, width, height, temp2);
        break;
      case iconTemp3:
        tft.pushImage(xPos, yPos, width, height, temp3);
        break;
      case iconTempHigh:
        tft.pushImage(xPos, yPos, width, height, tempHigh);
        iconState = iconTempOff;
        break;
    } 
    iconState++;
  } else {
    if (iconState != iconTempOff) {
      tft.fillRect(xPos, yPos, width, height, TFT_BLACK);
    }
  }
}

void checkPumpAndHeater() {
  if (restartHeaterTemperature + 1 > currentTemperature) {
    if (!heaterAndPump1LowOn) {
      digitalWrite(pump1LowPin, HIGH);
      digitalWrite(heaterPin, HIGH);
      heaterAndPump1LowOn = true;
    }
  } else {
    if (heaterAndPump1LowOn) {
      digitalWrite(pump1LowPin, LOW);
      digitalWrite(heaterPin, LOW);
      restartHeaterTemperature = setTemperature - 3;
      heaterAndPump1LowOn = false;
    }
  }
}

void readTemp(void) {
  float temp = thermistor.readTemp();
  currentTemperature = temp;
}
/* #endregion */

/* #region wifi functionality */
void wifiTaskCode( void * pvParameters ){
  wifiManager.connectToNetwork();
  
  server.on("/", handleLandingPage);
  server.on("/update/temp", HTTP_POST, handleUpdateTemp);
  server.on("/get/temp", HTTP_GET, handleGetTemp);
  
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      wifiConnected = false;
      printIpOnDisplay(wifiConnected);
      Serial.println("Server Close");
      server.close();
      wifiManager.connectToNetwork();
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Server Begin");
      server.begin();
      if (wifiConnected == false) {
        wifiConnected = true;
        printIpOnDisplay(wifiConnected);
      }
      for (;;) {
        if (WiFi.status() != WL_CONNECTED) {
          wifiConnected = false;
          printIpOnDisplay(wifiConnected);
          break;
        }   
        delay(2); 
        server.handleClient();
      } 
    }
    delay(2);
  }
}

void printIpOnDisplay(bool wifiConnected)
{
  tft.fillRect (45, 120, 100, 8, TFT_BLACK);
  if (wifiConnected) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(TR_DATUM);
    tft.setTextSize(1);
    tft.setTextFont(1);
    tft.drawString("IP: " + WiFi.localIP().toString(), 160, 120);
  }
}

void handleGetTemp() {
  String strReturn = "{\"setTemp\": \"";
  strReturn += setTemperature;
  strReturn += "\", \"waterTemp\": \"";
  strReturn += currentTemperature;
  strReturn += "\"}";
  server.send(200, "application/json", strReturn);
}

void handleUpdateTemp() {
  int numArgs = server.args();
  if (numArgs == 0) {
    Serial.println("BAD ARGS");
  } else {
    for (int i = 0; i < numArgs; i++) {
      Serial.print(server.argName(i));
      Serial.print(": ");
      Serial.println(server.arg(i));
      if (server.argName(i) == "newSetTemp") {
        setTemperature = server.arg(i).toInt();
        restartHeaterTemperature = server.arg(i).toInt();
        Serial.print("New Set Temperature: ");
        Serial.println(setTemperature);
        Serial.print("New Restart Temperature: ");
        Serial.println(restartHeaterTemperature);
      }
    }
  }
  
  String strReturn = "{\"setTemp\": \"";
  strReturn += setTemperature;
  strReturn += "\", \"message\": \"Temp Updated\"}";
  server.send(200, "application/json", strReturn);
}

void handleLandingPage() {
  server.send(200, "text/html", landingPageHtml());
}

void returnOk(String msg) {
  String strReturn = "{\"message\": \"" + msg + "\"}";
  server.send(200, "application/json", strReturn);
}

String landingPageHtml()
{
  String html = "<!DOCTYPE html><html>\n";
  html += "<head>\n";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n"; 
  html += "<title>Hot Tub Controller</title>\n";
  html += "<style>\n";
  html += "html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center; }\n";
  html += ".container { text-align: right; width: fit-content; margin-left: auto; margin-right: auto; }\n";
  html += ".container__adjustTemp { margin-top: 20px; text-align: center; }\n";
  html += ".arrow { max-width: 30px; }\n";
  html += "</style>\n";
  html += "<script>\n";
  html += "const changeTemp = (adjustment) => { var el = document.querySelector('#adjustTemp'); var temp = parseInt(el.innerHTML); adjustment = parseInt(adjustment);\n";
  html += "if (temp + adjustment <= ";
  html += maxTemp;
  html += " && temp + adjustment >= ";
  html += minTemp;
  html += ") { el.innerHTML = temp + adjustment }; };\n";
  html += "const ajaxPost = (url, send, onSuccess, onFail) => { var xhr = new XMLHttpRequest(); xhr.open('POST', url); xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded'); xhr.onload = () => { if (xhr.status === 200) { var response = JSON.parse(xhr.responseText); onSuccess(response); return; } onFail(); }; xhr.send(send); };\n";
  html += "const ajaxGet = (url, onSuccess) => { var xhr = new XMLHttpRequest(); xhr.open('GET', url); xhr.setRequestHeader('Content-Type', 'application/json'); xhr.onload = () => { if (xhr.status === 200) { var response = JSON.parse(xhr.responseText); onSuccess(response); } }; xhr.send(); };\n";
  html += "const updateTemp = () => { console.log('Update Temp'); var el = document.querySelector('#adjustTemp');  var temp = parseInt(el.innerHTML); var url = '/update/temp'; var send = 'newSetTemp=' + temp; const onSuccess = (response) => { setTemp(response.setTemp); }, onFail = () => { console.log('update failed'); }; ajaxPost(url, send, onSuccess, onFail); };\n";
  html += "const getTemp = () => { const onSuccess = (response) => { waterTemp(response.waterTemp); setTemp(response.setTemp); }; ajaxGet('/get/temp', onSuccess); };\n";
  html += "const setTemp = (temp) => { document.querySelector('#setTemp').innerHTML = temp; };\n";
  html += "const waterTemp = (temp) => { document.querySelector('#waterTemp').innerHTML = temp; };\n";
  html += "setInterval(getTemp, 10000);\n";
  html += "</script>\n";
  html += "</head>\n";
  html += "<body>\n";
  html += "<h1>Hot Tub Controls</h1>\n";
  html += "<div class=\"container\">\n";
  html += "<span>Water Temp: </span><span id=\"waterTemp\">";
  html += (int)currentTemperature;
  html += "</span><span>";
  html += degreesSymbol;
  html += "F</span><br/>\n";
  html += "<span>Current Set Temp: </span><span id=\"setTemp\">";
  html += setTemperature;
  html += "</span><span>";
  html += degreesSymbol;
  html += "F</span>\n";
  html += "<div class=\"container__adjustTemp\">\n";
  html += "<span>Adjust Temperature: </span><span id=\"adjustTemp\">";
  html += setTemperature;
  html += "</span><span>";
  html += degreesSymbol;
  html += "F</span><br/>\n";
  html += "<svg class=\"arrow\" onclick=\"changeTemp(1)\" version=\"1.1\" id=\"Capa_1\" xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" x=\"0px\" y=\"0px\" viewBox=\"0 0 49.656 49.656\" style=\"enable-background:new 0 0 49.656 49.656;\" xml:space=\"preserve\"><g><polygon style=\"fill:#00AD97;\" points=\"48.242,35.122 45.414,37.95 24.828,17.364 4.242,37.95 1.414,35.122 24.828,11.707 	\"/><path style=\"fill:#00AD97;\" d=\"M45.414,39.363L24.828,18.778L4.242,39.363L0,35.121l24.828-24.828l24.828,24.828L45.414,39.363z M24.828,15.95l20.586,20.585l1.414-1.414l-22-22l-22,22l1.414,1.414L24.828,15.95z\"/></g></svg>\n";
  html += "<svg class=\"arrow\" onclick=\"changeTemp(-1)\" version=\"1.1\" id=\"Capa_1\" xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" x=\"0px\" y=\"0px\" viewBox=\"0 0 49.656 49.656\" style=\"enable-background:new 0 0 49.656 49.656;\" xml:space=\"preserve\"><g><polygon style=\"fill:#00AD97;\" points=\"1.414,14.535 4.242,11.707 24.828,32.292 45.414,11.707 48.242,14.535 24.828,37.95 \"/><path style=\"fill:#00AD97;\" d=\"M24.828,39.364L0,14.536l4.242-4.242l20.586,20.585l20.586-20.586l4.242,4.242L24.828,39.364z M2.828,14.536l22,22l22-22.001l-1.414-1.414L24.828,33.707L4.242,13.122L2.828,14.536z\"/></g></svg><br>\n";
  html += "<input type=\"button\" value=\"Set Temp\" onclick=\"updateTemp()\">\n";
  html += "</div>\n";
  html += "</div>\n";
  html += "</body>\n";
  html += "</html>\n";

  return html;
}
/* #endregion */
