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

#define LONG_PRESS_TIME 500 // 1 sec
#define SHORT_BUTTON_PRESS_DELAY 300
#define LONG_BUTTON_PRESS_DELAY 1000
#define LED_AUTO_TURN_OFF_DELAY 1000 * 60 * 30 // 30 minutes
#define PUMP_AUTO_TURN_OFF_DELAY 1000 * 60 * 15  // 15 minutes
unsigned long autoRunPumpDelay = 1000 * 30; //1000 * 60 * 60 * 3; // 3 hours
unsigned long lastTempUpButtonPress = 0;
unsigned long lastTempDownButtonPress = 0;
unsigned long lastPump1ButtonPress = 0;
unsigned long lastPump2ButtonPress = 0;
unsigned long lastLedButtonPress = 0;
unsigned long ledButtonPressedTime = 0;
unsigned long pumpTurnOnTime = 0;
unsigned long ledTurnOnTime = 0;
unsigned long lastPumpRun = 0;
unsigned long pump1LowTurnOnTime = 0;

#define pump1LowPin 26 // 19 //
#define pump1HighPin 25 //21
#define heaterPin 32 // 23 
#define pump2Pin 33 // 22
#define ledRelayPin 5

// led colors
#define ledRedPin 22
#define ledGreenPin 21
#define ledBluePin 19
#define ledRedChannel 2
#define ledGreenChannel 3
#define ledBlueChannel 1
#define defaultWhiteRedValue 255
#define defaultWhiteGreenValue 255
#define defaultWhiteBlueValue 255

int ledRedValue = 0;
int ledBlueValue = 0;
int ledGreenValue = 0;

//---Relay Control Setup End ---//

//---Preferred Settings---//
#define DEFAULTTEMPERATURE 100;

//---Preferred Settings End
bool ledButtonPressed = false;
bool ledButtonToggled = false;

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
bool checkTimer(unsigned long lastPressTime, int delay) {
  if ((millis() - lastPressTime) > delay) {
    return true;
  }
  return false;
}

// bool longPress(int pin) {
//   unsigned long initialButtonPress = millis();
//   do {
//     if (!digitalRead(pin)) {
//       Serial.println("long Press False");
//       return false;
//     }
//     if (checkTimer(initialButtonPress, LONG_PRESS_TIMER)) {
//       Serial.println("long Press True");
//       return true;
//     }
//     Serial.println(digitalRead(pin));
//   } while(true);
//   return false;
// }

void togglePump1Low(bool state) {
  if (state) {
    if (pump1HighToggled) {
      state = false;
    } else {
      pump1LowToggled = true;
      pump1LowTurnOnTime = millis();
      digitalWrite(pump1LowPin, HIGH);
    }
  }
  if (!state) {
    pump1LowOn = false;
    pump1LowToggled = false;
    digitalWrite(pump1LowPin, LOW);
  }
}

void togglePump1High(bool state) {
  if (state) {
    Serial.println("togglePump1High");
    //if (pump1LowToggled) {
    togglePump1Low(false);
    //}
    pumpTurnOnTime = millis();
    pump1HighToggled = true;
    digitalWrite(pump1HighPin, HIGH);
  } else {
    pump1HighOn = false;
    pump1HighToggled = false;
    digitalWrite(pump1HighPin, LOW);
    if (heaterOn) {
      togglePump1Low(true);
    }
  }
}

void togglePump2(bool state) {
  if (state) {
    pump2Toggled = true;
    pumpTurnOnTime = millis();
    digitalWrite(pump2Pin, HIGH);
  } else {
    pump2On = false;
    pump2Toggled = false;
    digitalWrite(pump2Pin, LOW);
  }
}

void toggleHeater(bool state) {
  if (state) {
    if (!pump1LowToggled && !pump1HighToggled) {
      togglePump1Low(true);
    }
    heaterToggled = true;
    digitalWrite(heaterPin, HIGH);
  } else {
    heaterToggled = false;
    digitalWrite(heaterPin, LOW);
  }
}

void toggleLed(bool state) {
  if (state) {
    ledTurnOnTime = millis();

    ledBlueValue = 255;
    updateLedColors();
    ledOn = true;
  } else {
    ledBlueValue = 0;
    ledRedValue = 0;
    ledGreenValue = 0;
    updateLedColors();
    //digitalWrite(ledRelayPin, LOW);
    ledOn = false;
  }
}

void IRAM_ATTR tempUp() {
  if (digitalRead(tempUpButton) == HIGH) {
    if (checkTimer(lastTempUpButtonPress, SHORT_BUTTON_PRESS_DELAY)) {
      //Serial.println("TempUp");
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
    if (checkTimer(lastTempDownButtonPress, SHORT_BUTTON_PRESS_DELAY)) {
      //Serial.println("TempDown");
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
    if (checkTimer(lastPump1ButtonPress, LONG_BUTTON_PRESS_DELAY)) {
      //Serial.println("pump1Toggle");
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
    if (checkTimer(lastPump2ButtonPress, LONG_BUTTON_PRESS_DELAY)) {
      //Serial.print("pump2Toggle: ");
      lastPump2ButtonPress = millis();

      if (pump2On) {
        togglePump2(false);
        pump2On = false;
      } else {
        pump2On = true;
      }
      //Serial.println(pump2On);
    }
  }
}

void IRAM_ATTR ledDown() {
  Serial.println("LED RISING");
  if (checkTimer(lastLedButtonPress, SHORT_BUTTON_PRESS_DELAY)) {
    Serial.println("LED Down timerchecked");
    lastLedButtonPress = millis();
    ledButtonPressed = true;
  }
}

void IRAM_ATTR ledUp() {
  Serial.println("LED UP"); 
  Serial.println(checkTimer(lastLedButtonPress, LONG_PRESS_TIME));
  Serial.println(ledButtonPressed);
  Serial.println(!checkTimer(lastLedButtonPress, 1000 * 10));

  if (checkTimer(lastLedButtonPress, LONG_PRESS_TIME) && ledButtonPressed && !checkTimer(lastLedButtonPress, 1000 * 10)) {
  //if ((millis() - lastLedButtonPress) > LONG_PRESS_TIME) {
    if (ledOn) {
      toggleLed(false);
    } else {
      toggleLed(true);
    }
  }
  ledButtonPressed = false;
}

// void IRAM_ATTR ledToggle() {
//   Serial.println("LED RISING");
//   if (checkTimer(lastLedButtonPress, LONG_BUTTON_PRESS_DELAY)) {
//       lastLedButtonPress = millis();

//       if (ledOn) {
//         toggleLed(false);
//       } else {
//         toggleLed(true);
//       }
//     // }
//   }
// }
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

  pinMode(tempUpButton, INPUT_PULLDOWN);
  pinMode(tempDownButton, INPUT_PULLDOWN);
  pinMode(pump1Button, INPUT_PULLDOWN);
  pinMode(pump2Button, INPUT_PULLDOWN);
  pinMode(ledButton, INPUT_PULLUP);

  pinMode(pump1LowPin, OUTPUT);
  pinMode(pump1HighPin, OUTPUT);
  pinMode(heaterPin, OUTPUT);
  pinMode(pump2Pin, OUTPUT);
  pinMode(ledRelayPin, OUTPUT);

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
  // attachInterrupt(ledButton, ledToggle, RISING);
  // attachInterrupt(ledButton, ledDown, RISING);
  // attachInterrupt(ledButton, ledToggle, FALLING);

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
  timer.setInterval(1000, autoTurnOff); // 1 sec
  // timer.setInterval(1000, checkPumpRun); // 1 minute

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
    
    checkLedButton();
    delay(10);
  }
}

void checkLedButton()
{
  int buttonPressed = digitalRead(ledButton);
  if (buttonPressed) {
    if (checkTimer(lastLedButtonPress, SHORT_BUTTON_PRESS_DELAY)) {
      lastLedButtonPress = millis();
      
      if (!ledButtonPressed) {
        ledButtonPressedTime = millis();
        ledButtonPressed = true;
      }
    }
    if (ledButtonPressed && !ledButtonToggled && checkTimer(ledButtonPressedTime, LONG_PRESS_TIME)) {
      ledButtonToggled = true;
      if (ledOn) {
        toggleLed(false);
      } else {
        toggleLed(true);
      }
    }
  }
  if (!buttonPressed && ledButtonPressed) {
    ledButtonPressed = false;
    ledButtonToggled = false;
  }
}

void checkPumpRun() {
  Serial.println(autoRunPumpDelay);
  if (checkTimer(lastPumpRun, autoRunPumpDelay)) {
    // (millis() - lastPumpRun) > autoRunPumpDelay) {
    // Serial.println("Pump1LowOn");
    togglePump1Low(true);
    // pump1LowOn = true;
    // lastPumpRun = millis();
  }
}

void autoTurnOff() {
  if ((pump1HighToggled || pump2Toggled) && checkTimer(pumpTurnOnTime, PUMP_AUTO_TURN_OFF_DELAY)) {
    togglePump1High(false);
    togglePump2(false);
  }
  if (ledOn && checkTimer(ledTurnOnTime, LED_AUTO_TURN_OFF_DELAY)) {
    toggleLed(false);
  }
  if (pump1LowToggled && checkTimer(pump1LowTurnOnTime, PUMP_AUTO_TURN_OFF_DELAY)) {
    if (!heaterToggled) {
      togglePump1Low(false);
    }
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
    //Serial.println("turn on pump1 low");
    // Serial.print("pump1 On, Toggled: ");
    // Serial.println(pump1LowOn, pump1LowToggled);
    togglePump1Low(true);
    return;
  }
  if (heaterOn && !heaterToggled) {
    //Serial.println('turn on heater');
    toggleHeater(true);
    return;
  }
  if (pump1HighOn && !pump1HighToggled) {
    //Serial.println('turn on pump1high');
    togglePump1High(true);
    return;
  }
  if (pump2On && !pump2Toggled) {
    //Serial.println('turn on pump2');
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
  // Serial.print("RestartTemp: ");
  // Serial.println(restartHeaterTemperature);
  // Serial.print("CurrentTemp: ");
  // Serial.println(currentTemperature);
  // Serial.print("setTemp: ");
  // Serial.println(setTemperature);

  if (restartHeaterTemperature + 1.0 > currentTemperature) {
    restartHeaterTemperature = setTemperature;
    if (!heaterOn) {
      heaterOn = true;
      if (!pump1HighOn) {
        pump1LowOn = true;
      }
    }
  } else {
      togglePump1Low(false);
      toggleHeater(false);
      restartHeaterTemperature = setTemperature - 2;
      heaterOn = false;
      pump1LowOn = false;
  }
  if (heaterOn && !pump1LowOn && !pump1HighOn) 
  {
    pump1LowOn = true;
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
  // Serial.println('UpdateColors');
  if (getLedStatus()) {
    digitalWrite(ledRelayPin, HIGH);
  } else {
    digitalWrite(ledRelayPin, LOW);
  }
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
    // Serial.println("BAD ARGS");
    strReturn = "{\"message\": \"Temp update failed\"}";
    returnCode = 500;
  } else {
    for (int i = 0; i < numArgs; i++) {
      // Serial.print(server.argName(i));
      // Serial.print(": ");
      // Serial.println(server.arg(i));
      if (server.argName(i) == "newSetTemp") {
        setTemperature = server.arg(i).toInt();
        restartHeaterTemperature = server.arg(i).toInt();
        // Serial.print("New Set Temperature: ");
        // Serial.println(setTemperature);
        // Serial.print("New Restart Temperature: ");
        // Serial.println(restartHeaterTemperature);
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
    // Serial.println("BAD ARGS");
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
    // Serial.println("BAD ARGS");
    strReturn = "{\"message\": \"Temp update failed\"}";
    returnCode = 500;
  } else {
    for (int i = 0; i < numArgs; i++) {
      // Serial.print(server.argName(i));
      // Serial.print(": ");
      // Serial.println(server.arg(i));
      if (server.argName(i) == "ledStatus") {
        int ledStatus = server.arg(i).toInt();
        if (ledStatus == 1) {
          ledBlueValue = defaultWhiteBlueValue;
          ledRedValue = defaultWhiteRedValue;
          ledGreenValue = defaultWhiteGreenValue;
          updateLedColors();
          // Serial.println("Led default White Color On");
        }
        if (ledStatus == 0) {
          ledBlueValue = 0;
          ledRedValue = 0;
          ledGreenValue = 0;
          updateLedColors();
          // Serial.println("Leds off");
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
  int numArgs = server.args();
  String strReturn;
  int returnCode = 200;

  if (numArgs == 0) {
    // Serial.println("BAD ARGS");
    strReturn = "{\"message\": \"Temp update failed\"}";
    returnCode = 500;
  } else {
    for (int i = 0; i < numArgs; i++) {
      // Serial.print(server.argName(i));
      // Serial.print(": ");
      // Serial.println(server.arg(i));
      if (server.argName(i) == "red") {
        ledRedValue = server.arg(i).toInt();
      }
      if (server.argName(i) == "green") {
        ledGreenValue = server.arg(i).toInt();
      }
      if (server.argName(i) == "blue") {
        ledBlueValue = server.arg(i).toInt();
      }
    }
    updateLedColors();
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
  html += "const setTemp = ( temp) => { document.querySelector('#setTemp').innerHTML = temp; };\n";
  html += "const waterTemp = (temp) => { document.querySelector('#waterTemp').innerHTML = temp; };\n";
  html += "const togglePump = (pumpNum) => { var pumpStatus = pumpNum == 1 ? pump1Status : pump2Status; pumpStatus = pumpStatus == 1 ? 0 : 1; var url = '/update/pump/status'; var send = 'pumpNum=' + pumpNum + '&pumpStatus=' + pumpStatus; const onSuccess = (response) => { updateButton('Pump ' + response.pumpNum, response.status, '#button-pump' + response.pumpNum); }, onFail = () => { console.log('pump ' + pumpNum + ' toggle failed'); }; ajaxPost(url, send, onSuccess, onFail); };\n";
  html += "const updateButton = (pump, status, button) => { var statusString = status == 0 ? 'Turn On ' : 'Turn Off '; var value = statusString + pump; switch (pump) { case 'Pump 1': pump1Status = status; break; case 'Pump 2': pump2Status = status; break; case 'Leds': ledStatus = status; if (status == 0) value = statusString + 'White ' + pump; break; }; document.querySelector(button).value = value; };\n";
  html += "const updateHeaterStatus = (status) => { document.querySelector('#heaterStatusValue').innerHTML = status; };\n";
  html += "const updateLedStatus = (status) => { document.querySelector('#ledStatusValue').innerHTML = status == 1 ? 'On' : 'Off'; };\n";
  html += "const toggleLeds = () => { var url = '/update/led/status'; ledStatus = ledStatus == 1 ? 0 : 1; var send = 'ledStatus=' + ledStatus; const onSuccess = (response) => { updateButton('Leds', response.status, '#button-led'); updateLedStatus(response.status); }, onFail = () => { console.log('led toggle failed'); }; ajaxPost(url, send, onSuccess, onFail); };\n";
  html += "const updateLedColor = () => { var url = '/update/ledColor'; var ledContainer = document.querySelector('.container--leds'); var red = ledContainer.querySelector('#redled').value; var green = ledContainer.querySelector('#greenled').value; var blue = ledContainer.querySelector('#blueled').value; var send = 'red=' + red + '&green=' + green + '&blue=' + blue; const onSuccess = (response) => { updateButton('Leds', response.status, '#button-led'); updateLedStatus(response.status); }, onFail = () => { console.log('Led color update failed'); }; ajaxPost(url, send, onSuccess, onFail); };\n";
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
  html += "</div><br>\n";
  html += "<div class=\"container__center\">\n";
  html += "<span>Led Status: </span><span id=\"ledStatusValue\">";
  html += getLedStatus() ? "On" : "Off";
  html += "</span><br><br>\n";
  html += "<input type=\"button\" id=\"button-led\" value=\"Turn ";
  html += getLedStatus() ? "Off" : "On";
  html += " White Leds\" onclick=\"toggleLeds()\">\n";
  html += "<br><br>\n";
  html += "<div class=\"container container--leds\">\n";
  html += "<span>Red: </span><input type=\"range\" id=\"redled\" name=\"red\" min=\"0\" max=\"255\" value=\"";
  html += ledRedValue;
  html += "\"><br>\n";
  html += "<span>Green: </span><input type=\"range\" id=\"greenled\" name=\"green\" min=\"0\" max=\"255\" value=\"";
  html += ledGreenValue;
  html += "\"><br>\n";
  html += "<span>Blue: </span><input type=\"range\" id=\"blueled\" name=\"blue\" min=\"0\" max=\"255\" value=\"";
  html += ledBlueValue;
  html += "\">\n";
  html += "</div><br>\n";
  html += "<input type=\"button\" id=\"button-led-update\" value=\"Update Color Leds\" onclick=\"updateLedColor()\">\n";
  html += "</div>\n";
  html += "</body>\n";
  html += "</html>\n";

  return html;
}

// char* landingPageHtml()
// {
//   char *buf = (char *) malloc(10000);
//   char ledStatusStr[1];
//   char maxTempStr[3];
//   char minTempStr[3];
//   char currentTemperatureStr[4];
//   char setTempStr[3];
//   char ledRedValueStr[4];
//   char ledGreenValueStr[4];
//   char ledBlueValueStr[4];

//   itoa(getLedStatus(), ledStatusStr, 10);
//   itoa(maxTemp, maxTempStr, 10);
//   itoa(minTemp, minTempStr, 10);
//   itoa(minTemp, minTempStr, 10);
//   itoa(currentTemperature, currentTemperatureStr, 10);
//   itoa(setTemperature, setTempStr, 10);
//   itoa(ledRedValue, ledRedValueStr, 10);
//   itoa(ledGreenValue, ledGreenValueStr, 10);
//   itoa(ledBlueValue, ledBlueValueStr, 10);

//   strcpy(buf, "<!DOCTYPE html><html>\n");
//   strcat(buf, "<head>\n");
//   strcat(buf, "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n");
//   strcat(buf, "<title>Hot Tub Controller</title>\n");
//   strcat(buf, "<style>\n");
//   strcat(buf, "html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center; }\n");
//   strcat(buf, ".container { text-align: right; width: fit-content; margin-left: auto; margin-right: auto; }\n");
//   strcat(buf, ".container__center { margin-top: 20px; text-align: center; }\n");
//   strcat(buf, ".arrow { max-width: 30px; }\n");
//   strcat(buf, "</style>\n");
//   strcat(buf, "<script>\n");
//   strcat(buf, "var pump1Status = ");
//   strcat(buf, pump1HighOn ? "1" : "0");
//   strcat(buf, "; var pump2Status = ");
//   strcat(buf, pump2On ? "1" : "0");
//   strcat(buf, "; var ledStatus = ");
//   strcat(buf, ledStatusStr);
//   strcat(buf, "; \n");
//   strcat(buf, "const changeTemp = (adjustment) => { var el = document.querySelector('#adjustTemp'); var temp = parseInt(el.innerHTML); adjustment = parseInt(adjustment);\n");
//   strcat(buf, "if (temp + adjustment <= ");
//   strcat(buf, maxTempStr);
//   strcat(buf, " && temp + adjustment >= ");
//   strcat(buf, ") { el.innerHTML = temp + adjustment }; };\n");
//   strcat(buf, "const ajaxPost = (url, send, onSuccess, onFail) => { var xhr = new XMLHttpRequest(); xhr.open('POST', url); xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded'); xhr.onload = () => { if (xhr.status === 200) { var response = JSON.parse(xhr.responseText); onSuccess(response); return; } onFail(); }; xhr.send(send); };\n");
//   strcat(buf, "const ajaxGet = (url, onSuccess) => { var xhr = new XMLHttpRequest(); xhr.open('GET', url); xhr.setRequestHeader('Content-Type', 'application/json'); xhr.onload = () => { if (xhr.status === 200) { var response = JSON.parse(xhr.responseText); onSuccess(response); } }; xhr.send(); };\n");
//   strcat(buf, "const updateTemp = () => { var el = document.querySelector('#adjustTemp');  var temp = parseInt(el.innerHTML); var url = '/update/temp'; var send = 'newSetTemp=' + temp; const onSuccess = (response) => { setTemp(response.setTemp); }, onFail = () => { console.log('update failed'); }; ajaxPost(url, send, onSuccess, onFail); };\n");
//   strcat(buf, "const getStatus = () => { const onSuccess = (response) => { waterTemp(response.waterTemp); setTemp(response.setTemp); updateHeaterStatus(response.heaterStatus); updateLedStatus(response.ledStatus); }; ajaxGet('/get/status', onSuccess); };\n");
//   strcat(buf, "const setTemp = ( temp) => { document.querySelector('#setTemp').innerHTML = temp; };\n");
//   strcat(buf, "const waterTemp = (temp) => { document.querySelector('#waterTemp').innerHTML = temp; };\n");
//   strcat(buf, "const togglePump = (pumpNum) => { var pumpStatus = pumpNum == 1 ? pump1Status : pump2Status; pumpStatus = pumpStatus == 1 ? 0 : 1; var url = '/update/pump/status'; var send = 'pumpNum=' + pumpNum + '&pumpStatus=' + pumpStatus; const onSuccess = (response) => { updateButton('Pump ' + response.pumpNum, response.status, '#button-pump' + response.pumpNum); }, onFail = () => { console.log('pump ' + pumpNum + ' toggle failed'); }; ajaxPost(url, send, onSuccess, onFail); };\n");
//   strcat(buf, "const updateButton = (pump, status, button) => { var statusString = status == 0 ? 'Turn On ' : 'Turn Off '; var value = statusString + pump; switch (pump) { case 'Pump 1': pump1Status = status; break; case 'Pump 2': pump2Status = status; break; case 'Leds': ledStatus = status; if (status == 0) value = statusString + 'White ' + pump; break; }; document.querySelector(button).value = value; };\n");
//   strcat(buf, "const updateHeaterStatus = (status) => { document.querySelector('#heaterStatusValue').innerHTML = status; };\n");
//   strcat(buf, "const updateLedStatus = (status) => { document.querySelector('#ledStatusValue').innerHTML = status == 1 ? 'On' : 'Off'; };\n");
//   strcat(buf, "const toggleLeds = () => { var url = '/update/led/status'; ledStatus = ledStatus == 1 ? 0 : 1; var send = 'ledStatus=' + ledStatus; const onSuccess = (response) => { updateButton('Leds', response.status, '#button-led'); updateLedStatus(response.status); }, onFail = () => { console.log('led toggle failed'); }; ajaxPost(url, send, onSuccess, onFail); };\n");
//   strcat(buf, "const updateLedColor = () => { var url = '/update/ledColor'; var ledContainer = document.querySelector('.container--leds'); var red = ledContainer.querySelector('#redled').value; var green = ledContainer.querySelector('#greenled').value; var blue = ledContainer.querySelector('#blueled').value; var send = 'red=' + red + '&green=' + green + '&blue=' + blue; const onSuccess = (response) => { updateButton('Leds', response.status, '#button-led'); updateLedStatus(response.status); }, onFail = () => { console.log('Led color update failed'); }; ajaxPost(url, send, onSuccess, onFail); };\n");
//   strcat(buf, "setInterval(getStatus, 10000);\n");
//   strcat(buf, "</script>\n");
//   strcat(buf, "</head>\n");
//   strcat(buf, "<body>\n");
//   strcat(buf, "<h1>Hot Tub Controls</h1>\n");
//   strcat(buf, "<div class=\"container\">\n");
//   strcat(buf, "<span>Water Temp: </span><span id=\"waterTemp\">");
//   strcat(buf, currentTemperatureStr);
//   strcat(buf, "</span><span>");
//   strcat(buf, &degreesSymbol);
//   strcat(buf, "F</span><br/>\n");
//   strcat(buf, "<span>Current Set Temp: </span><span id=\"setTemp\">");
//   strcat(buf, setTempStr);
//   strcat(buf, "</span><span>");
//   strcat(buf, &degreesSymbol);
//   strcat(buf, "F</span>\n");
//   strcat(buf, "<div class=\"container__center\">\n");
//   strcat(buf, "<span>Adjust Temperature: </span><span id=\"adjustTemp\">");
//   strcat(buf, setTempStr);
//   strcat(buf, "</span><span>");
//   strcat(buf, &degreesSymbol);
//   strcat(buf, "F</span><br/>\n");
//   strcat(buf, "<svg class=\"arrow\" onclick=\"changeTemp(1)\" version=\"1.1\" id=\"Capa_1\" xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" x=\"0px\" y=\"0px\" viewBox=\"0 0 49.656 49.656\" style=\"enable-background:new 0 0 49.656 49.656;\" xml:space=\"preserve\"><g><polygon style=\"fill:#00AD97;\" points=\"48.242,35.122 45.414,37.95 24.828,17.364 4.242,37.95 1.414,35.122 24.828,11.707 	\"/><path style=\"fill:#00AD97;\" d=\"M45.414,39.363L24.828,18.778L4.242,39.363L0,35.121l24.828-24.828l24.828,24.828L45.414,39.363z M24.828,15.95l20.586,20.585l1.414-1.414l-22-22l-22,22l1.414,1.414L24.828,15.95z\"/></g></svg>\n");
//   strcat(buf, "<svg class=\"arrow\" onclick=\"changeTemp(-1)\" version=\"1.1\" id=\"Capa_1\" xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" x=\"0px\" y=\"0px\" viewBox=\"0 0 49.656 49.656\" style=\"enable-background:new 0 0 49.656 49.656;\" xml:space=\"preserve\"><g><polygon style=\"fill:#00AD97;\" points=\"1.414,14.535 4.242,11.707 24.828,32.292 45.414,11.707 48.242,14.535 24.828,37.95 \"/><path style=\"fill:#00AD97;\" d=\"M24.828,39.364L0,14.536l4.242-4.242l20.586,20.585l20.586-20.586l4.242,4.242L24.828,39.364z M2.828,14.536l22,22l22-22.001l-1.414-1.414L24.828,33.707L4.242,13.122L2.828,14.536z\"/></g></svg><br>\n");
//   strcat(buf, "<input type=\"button\" value=\"Set Temp\" onclick=\"updateTemp()\">\n");
//   strcat(buf, "</div>\n");
//   strcat(buf, "</div>\n");
//   strcat(buf, "<div class=\"container__center\">\n");
//   strcat(buf, "<span>Heater Status: </span><span id=\"heaterStatusValue\">");
//   strcat(buf, heaterOn ? "On" : "Off");
//   strcat(buf, "</span><br><br>\n");
//   strcat(buf, "<input type=\"button\" id=\"button-pump1\" value=\"Turn ");
//   strcat(buf, pump1HighOn ? "Off" : "On");
//   strcat(buf, " Pump 1\" onclick=\"togglePump(1)\">\n");
//   strcat(buf, "<input type=\"button\" id=\"button-pump2\" value=\"Turn ");
//   strcat(buf, pump2On ? "Off" : "On");
//   strcat(buf, " Pump 2\" onclick=\"togglePump(2)\">\n");
//   strcat(buf, "</div><br>\n");
//   strcat(buf, "<div class=\"container__center\">\n");
//   strcat(buf, "<span>Led Status: </span><span id=\"ledStatusValue\">");
//   strcat(buf, getLedStatus() ? "On" : "Off");
//   strcat(buf, "</span><br><br>\n");
//   strcat(buf, "<input type=\"button\" id=\"button-led\" value=\"Turn ");
//   strcat(buf, getLedStatus() ? "Off" : "On");
//   strcat(buf, " White Leds\" onclick=\"toggleLeds()\">\n");
//   strcat(buf, "<br><br>\n");
//   strcat(buf, "<div class=\"container container--leds\">\n");
//   strcat(buf, "<span>Red: </span><input type=\"range\" id=\"redled\" name=\"red\" min=\"0\" max=\"255\" value=\"");
//   strcat(buf, ledRedValueStr);
//   strcat(buf, "\"><br>\n");
//   strcat(buf, "<span>Green: </span><input type=\"range\" id=\"greenled\" name=\"green\" min=\"0\" max=\"255\" value=\"");
//   strcat(buf, ledGreenValueStr);
//   strcat(buf, "\"><br>\n");
//   strcat(buf, "<span>Blue: </span><input type=\"range\" id=\"blueled\" name=\"blue\" min=\"0\" max=\"255\" value=\"");
//   strcat(buf, ledBlueValueStr);
//   strcat(buf, "\">\n");
//   strcat(buf, "</div><br>\n");
//   strcat(buf, "<input type=\"button\" id=\"button-led-update\" value=\"Update Color Leds\" onclick=\"updateLedColor()\">\n");
//   strcat(buf, "</div>\n");
//   strcat(buf, "</body>\n");
//   strcat(buf, "</html>\n");

//   return buf;
// }
/* #endregion */
