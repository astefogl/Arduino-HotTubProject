#include "Arduino.h"
#include <WiFi.h>
#include "CommonWifiManager.h"

/* #region wifi functionality */
// void wifiTaskCode( void * pvParameters ){
//   wifiManager.connectToNetwork();
  
//   server.on("/", handleLandingPage);
//   server.on("/update/temp", HTTP_POST, handleUpdateTemp);
//   server.on("/get/temp", HTTP_GET, handleGetTemp);
  
//   for (;;) {
//     if (WiFi.status() != WL_CONNECTED) {
//       Serial.println("Server Close");
//       server.close();
//       wifiManager.connectToNetwork();
//     }
//     if (WiFi.status() == WL_CONNECTED) {
//       Serial.println("Server Begin");
//       server.begin();
//       for (;;) {
//         if (WiFi.status() != WL_CONNECTED) {
//           break;
//         }
//         delay(2); 
//         server.handleClient();
//       } 
//     }

//     delay(2);
//   }
// }