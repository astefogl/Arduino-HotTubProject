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

// led colors
#define ledRedPin 22
#define ledGreenPin 21
#define ledBluePin 19
#define ledRedChannel 2
#define ledGreenChannel 3
#define ledBlueChannel 1
#define defaultWhiteRedValue 50
#define defaultWhiteGreenValue 255
#define defaultWhiteBlueValue 100

int ledRedValue = 0;
int ledBlueValue = 0;
int ledGreenValue = 0;

//---Relay Control Setup End ---//

//---Preferred Settings---//
#define DEFAULTTEMPERATURE 100;

//---Preferred Settings End

int setTemperature;
int restartHeaterTemperature;
float currentTemperature;
int lastDisplayedTemperature = 0;
int lastSetTemperature = 0;
bool heaterOn;
bool pump1LowOn;
bool pump1HighOn;
bool pump2On;
bool heaterToggled = false;
bool pump1LowToggled = false;
bool pump1HighToggled = false;
bool pump2Toggled = false;
bool ledOn;
char degreesSymbol = char(176);
bool wifiConnected = false;
uint16_t tempIcon[434];
int iconState = 0;
int yPosCurrentTemp = 25;
int yPosSetTemp = 70;
enum iconStates { iconTempOff, iconTempLow, iconTemp1, iconTemp2, iconTemp3, iconTempHigh }; 

int maxTemp = 107;
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

void togglePump1Low(bool state) {
  if (state) {
    pump1LowToggled = true;
    digitalWrite(pump1LowPin, HIGH);
  } else {
    pump1LowToggled = false;
    digitalWrite(pump1LowPin, LOW);
  }
}

void togglePump1High(bool state) {
  if (state) {
    pump1HighToggled = true;
    digitalWrite(pump1HighPin, HIGH);
  } else {
    pump1HighToggled = false;
    digitalWrite(pump1HighPin, LOW);
  }
}

void togglePump2(bool state) {
  if (state) {
    pump2Toggled = true;
    digitalWrite(pump2Pin, HIGH);
  } else {
    pump2Toggled = false;
    digitalWrite(pump2Pin, LOW);
  }
}

void toggleHeater(bool state) {
  if (state) {
    heaterToggled = true;
    digitalWrite(heaterPin, HIGH);
  } else {
    heaterToggled = false;
    digitalWrite(heaterPin, LOW);
  }
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
        togglePump1High(false);
        pump1HighOn = false;
      } else {
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
        togglePump2(false);
        pump2On = false;
      } else {
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
  heaterOn = false;
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

  ledcAttachPin(ledBluePin, 1);
  ledcAttachPin(ledRedPin, 2);
  ledcAttachPin(ledGreenPin, 3);

  ledcSetup(1, 12000, 8); // 12 kHz PWM, 8-bit resolution
  ledcSetup(2, 12000, 8);
  ledcSetup(3, 12000, 8);

  updateLedColors();

  togglePump1Low(false);
  togglePump1High(false);
  togglePump2(false);
  toggleHeater(false);

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
  timer.setInterval(1000, toggleRelay); // 1 sec

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

void toggleRelay() {
  if (!pump1LowOn)
    togglePump1Low(false);
  if (!heaterOn)
    toggleHeater(false);
  if (!pump1HighOn)
    togglePump1High(false);
  if (!pump2On)
    togglePump2(false);
    
  // Serial.print("pump1Low On: ");
  // Serial.print(pump1LowOn);
  // Serial.print(" Toggled: ");
  // Serial.println(pump1LowToggled);
  // Serial.print("pump1High On: ");
  // Serial.print(pump1HighOn);
  // Serial.print(" Toggled: ");
  // Serial.println(pump1HighToggled);
  // Serial.print("pump2 On: ");
  // Serial.print(pump2On);
  // Serial.print(" Toggled: ");
  // Serial.println(pump2Toggled);
  // Serial.print("heater On: ");
  // Serial.print(heaterOn);
  // Serial.print(" Toggled: ");
  // Serial.println(heaterToggled);

  if (pump1LowOn && !pump1LowToggled) {
    // Serial.print("pump1 On, Toggled: ");
    // Serial.println(pump1LowOn, pump1LowToggled);
    togglePump1Low(true);
    return;
  }
  if (heaterOn && !heaterToggled) {
    toggleHeater(true);
    return;
  }
  if (pump1HighOn && !pump1HighToggled) {
    togglePump1High(true);
    return;
  }
  if (pump2On && !pump2Toggled) {
    togglePump2(true);
    return;
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

  if (heaterOn) {
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
    if (!heaterOn || !pump1LowOn) {
      heaterOn = true;
      pump1LowOn = true;
    }
  } else {
    if (heaterOn || pump1LowOn) {
      togglePump1Low(false);
      toggleHeater(false);
      restartHeaterTemperature = setTemperature - 3;
      heaterOn = false;
      pump1LowOn = false;
    }
  }
}

void readTemp(void) {
  float temp = thermistor.readTemp();
  currentTemperature = temp;
}

void updateLedColors() {
  ledcWrite(ledBlueChannel, ledBlueValue);
  ledcWrite(ledRedChannel, ledRedValue);
  ledcWrite(ledGreenChannel, ledGreenValue);
}

int getLedStatus() {
  if (ledBlueValue > 0 || ledRedValue > 0 || ledGreenValue > 0) {
    return 1;
  }
  return 0;
}
/* #endregion */

/* #region wifi functionality */
void wifiTaskCode( void * pvParameters ){
  wifiManager.connectToNetwork();
  
  server.on("/", handleLandingPage);
  server.on("/update/temp", HTTP_POST, handleUpdateTemp);
  server.on("/update/pump/status", HTTP_POST, handlePumpStatus);
  server.on("/update/ledColor", HTTP_POST, handleColorUpdate);
  server.on("/update/led/status", HTTP_POST, handleLedStatus);
  server.on("/get/status", HTTP_GET, handleGetStatus);
  
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
        
        /*
        long rssi = 0;
        for (int i=0;i < 10; i++){
          rssi += WiFi.RSSI();
          delay(20);
        }

        long averageRSSI = rssi/10;
        Serial.print("Signal Strength");
        Serial.println(averageRSSI);
        */

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

void handleGetStatus() {
  String strReturn = "{\"setTemp\": \"";
  strReturn += setTemperature;
  strReturn += "\", \"waterTemp\": \"";
  strReturn += currentTemperature;
  strReturn += "\", \"heaterStatus\": \"";
  strReturn += heaterOn ? "On" : "Off";
  strReturn += "\", \"ledStatus\": \"";
  strReturn += getLedStatus();
  strReturn += "\"}";
  server.send(200, "application/json", strReturn);
}

void handleUpdateTemp() {
  int numArgs = server.args();
  String strReturn;
  int returnCode = 200;
  if (numArgs == 0) {
    Serial.println("BAD ARGS");
    strReturn = "{\"message\": \"Temp update failed\"}";
    returnCode = 500;
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
    strReturn = "{\"setTemp\": \"";
    strReturn += setTemperature;
    strReturn += "\", \"message\": \"Temp Updated\"}";
  }

  server.send(returnCode, "application/json", strReturn);
}

void handlePumpStatus() {
  int numArgs = server.args();
  int pumpNum = 0;
  int pumpStatus = 0;
  String strReturn;
  int returnCode = 200;

  if (numArgs == 0) {
    Serial.println("BAD ARGS");
  } else {
    for (int i = 0; i < numArgs; i++) {
      if (server.argName(i) == "pumpNum") {
        pumpNum = server.arg(i).toInt();
      }
      if (server.argName(i) == "pumpStatus") {
        pumpStatus = server.arg(i).toInt();
      }
    }
    switch (pumpNum) {
      case 1: 
        pump1HighOn = pumpStatus;
        break;
      case 2:
        pump2On = pumpStatus;
        break;
    }

    strReturn = "{\"pumpNum\": \"";
    strReturn += pumpNum;
    strReturn += "\", \"status\": \"";
    strReturn += pumpStatus;
    strReturn += "\", \"message\": \"Pump Status Updated\"}";
  }

  server.send(returnCode, "application/json", strReturn);
}

void handleLedStatus() {
  int numArgs = server.args();
  String strReturn;
  int returnCode = 200;

  if (numArgs == 0) {
    Serial.println("BAD ARGS");
    strReturn = "{\"message\": \"Temp update failed\"}";
    returnCode = 500;
  } else {
    for (int i = 0; i < numArgs; i++) {
      Serial.print(server.argName(i));
      Serial.print(": ");
      Serial.println(server.arg(i));
      if (server.argName(i) == "ledStatus") {
        int ledStatus = server.arg(i).toInt();
        if (ledStatus == 1) {
          ledBlueValue = defaultWhiteBlueValue;
          ledRedValue = defaultWhiteRedValue;
          ledGreenValue = defaultWhiteGreenValue;
          updateLedColors();
          Serial.println("Led default White Color On");
        }
        if (ledStatus == 0) {
          ledBlueValue = 0;
          ledRedValue = 0;
          ledGreenValue = 0;
          updateLedColors();
          Serial.println("Leds off");
        }
      }
    }
  }

  strReturn = "{\"blueValue\": \"";
  strReturn += ledBlueValue;
  strReturn += "\", \"redValue\": \"";
  strReturn += ledRedValue;
  strReturn += "\", \"greenValue\": \"";
  strReturn += ledGreenValue;
  strReturn += "\", \"status\": \"";
  strReturn += getLedStatus();
  strReturn += "\", \"message\": \"Led Status Updated\"}";

  server.send(returnCode, "application/json", strReturn);
}

void handleColorUpdate() {
  int returnCode = 200;
  String strReturn;

  strReturn = "{\"setLedColor\": \"";
  strReturn += "some color combo";
  strReturn += "\", \"message\": \"Led Color Updated Updated\"}";

  server.send(returnCode, "application/json", strReturn);
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
  html += ".container__center { margin-top: 20px; text-align: center; }\n";
  html += ".arrow { max-width: 30px; }\n";
  html += "</style>\n";
  html += "<script>\n";
  html += "var pump1Status = ";
  html += pump1HighOn ? 1 : 0;
  html += "; var pump2Status = ";
  html += pump2On ? 1 : 0;
  html += "; var ledStatus = ";
  html += getLedStatus();
  html += "; \n";
  html += "const changeTemp = (adjustment) => { var el = document.querySelector('#adjustTemp'); var temp = parseInt(el.innerHTML); adjustment = parseInt(adjustment);\n";
  html += "if (temp + adjustment <= ";
  html += maxTemp;
  html += " && temp + adjustment >= ";
  html += minTemp;
  html += ") { el.innerHTML = temp + adjustment }; };\n";
  html += "const ajaxPost = (url, send, onSuccess, onFail) => { var xhr = new XMLHttpRequest(); xhr.open('POST', url); xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded'); xhr.onload = () => { if (xhr.status === 200) { var response = JSON.parse(xhr.responseText); onSuccess(response); return; } onFail(); }; xhr.send(send); };\n";
  html += "const ajaxGet = (url, onSuccess) => { var xhr = new XMLHttpRequest(); xhr.open('GET', url); xhr.setRequestHeader('Content-Type', 'application/json'); xhr.onload = () => { if (xhr.status === 200) { var response = JSON.parse(xhr.responseText); onSuccess(response); } }; xhr.send(); };\n";
  html += "const updateTemp = () => { var el = document.querySelector('#adjustTemp');  var temp = parseInt(el.innerHTML); var url = '/update/temp'; var send = 'newSetTemp=' + temp; const onSuccess = (response) => { setTemp(response.setTemp); }, onFail = () => { console.log('update failed'); }; ajaxPost(url, send, onSuccess, onFail); };\n";
  html += "const getStatus = () => { const onSuccess = (response) => { waterTemp(response.waterTemp); setTemp(response.setTemp); updateHeaterStatus(response.heaterStatus); updateLedStatus(response.ledStatus); }; ajaxGet('/get/status', onSuccess); };\n";
  html += "const setTemp = (temp) => { document.querySelector('#setTemp').innerHTML = temp; };\n";
  html += "const waterTemp = (temp) => { document.querySelector('#waterTemp').innerHTML = temp; };\n";
  html += "const togglePump = (pumpNum) => { var pumpStatus = pumpNum == 1 ? pump1Status : pump2Status; pumpStatus = pumpStatus == 1 ? 0 : 1; var url = '/update/pump/status'; var send = 'pumpNum=' + pumpNum + '&pumpStatus=' + pumpStatus; const onSuccess = (response) => { updateButton('Pump ' + response.pumpNum, response.status, '#button-pump' + response.pumpNum); }, onFail = () => { console.log('pump ' + pumpNum + ' toggle failed'); }; ajaxPost(url, send, onSuccess, onFail); };\n";
  html += "const updateButton = (pump, status, button) => { var statusString = status == 0 ? 'Turn On ' : 'Turn Off '; var value = statusString + pump; switch (pump) { case 'Pump 1': pump1Status = status; break; case 'Pump 2': pump2Status = status; break; case 'Leds': ledStatus = status; break; }; document.querySelector(button).value = value; };\n";
  html += "const updateHeaterStatus = (status) => { document.querySelector('#heaterStatusValue').innerHTML = status; };\n";
  html += "const updateLedStatus = (status) => { document.querySelector('#ledStatusValue').innerHTML = status == 1 ? 'On' : 'Off'; };\n";
  html += "const toggleLeds = () => { var url = '/update/led/status'; ledStatus = ledStatus == 1 ? 0 : 1; var send = 'ledStatus=' + ledStatus; const onSuccess = (response) => { updateButton('Leds', response.status, '#button-led'); updateLedStatus(response.status); }, onFail = () => { console.log('led toggle failed'); }; ajaxPost(url, send, onSuccess, onFail); };\n";
  html += "setInterval(getStatus, 10000);\n";
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
  html += "<div class=\"container__center\">\n";
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
  html += "<div class=\"container__center\">\n";
  html += "<span>Heater Status: </span><span id=\"heaterStatusValue\">";
  html += heaterOn ? "On" : "Off";
  html += "</span><br><br>\n";
  html += "<input type=\"button\" id=\"button-pump1\" value=\"Turn ";
  html += pump1HighOn ? "Off" : "On"; 
  html += " Pump 1\" onclick=\"togglePump(1)\">\n";
  html += "<input type=\"button\" id=\"button-pump2\" value=\"Turn ";
  html += pump2On ? "Off" : "On"; 
  html += " Pump 2\" onclick=\"togglePump(2)\">\n";
  html += "</div>\n";
  html += "<div class=\"container__center\">\n";
  html += "<span>Led Status: </span><span id=\"ledStatusValue\">";
  html += getLedStatus() ? "On" : "Off";
  html += "</span><br><br>\n";
  html += "<input type=\"button\" id=\"button-led\" value=\"Turn ";
  html += getLedStatus() ? "Off" : "On";
  html += " Leds\" onclick=\"toggleLeds()\">\n";
  html += "</div>\n";
  html += "</body>\n";
  html += "</html>\n";

  return html;
}
/* #endregion */
