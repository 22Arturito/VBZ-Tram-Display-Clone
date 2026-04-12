#ifndef CONFIG_H
#define CONFIG_H

#define TIME_SERVER "0.ch.pool.ntp.org"
#define AP_NAME "vbz-anzeige"
#define AP_PASSWORD "123456"

// Api
#define OPEN_DATA_API_KEY "eyJvcmciOiI2NDA2NTFhNTIyZmEwNTAwMDEyOWJiZTEiLCJpZCI6ImQ0Njk3NmUyNmUwYTQ5ZTM4NTZiNjk4ZWVhYjY0MzAzIiwiaCI6Im11cm11cjEyOCJ9"
#define OPEN_DATA_URL "https://api.opentransportdata.swiss/ojp20"
#define OPEN_DATA_STATION "8591324" // Zürich Röslistrasse
#define OPEN_DATA_RESULTS "5"
#define OPEN_DATA_DIRECTION "A" // H = Outward, R = Return, A = All

// Display (angepasst für ESP32-S3 DevKit / S3-N16R8)
// (nur dieser Teil wurde geändert)

// RGB Daten
#define R1_PIN  4
#define G1_PIN  5
#define B1_PIN  6
#define R2_PIN  7
#define G2_PIN  15
#define B2_PIN  16

// Adress-Leitungen
#define A_PIN   17
#define B_PIN   18
#define C_PIN   8
#define D_PIN   9
#define E_PIN   10  // nötig bei 64x64 (1/32 scan)

// Steuerung
#define LAT_PIN 11
#define OE_PIN  12
#define CLK_PIN 13

#define PANEL_RES_X 128 // Number of pixels wide of each INDIVIDUAL panel module.
#define PANEL_RES_Y 64  // Number of pixels tall of each INDIVIDUAL panel module.
#define PANEL_CHAIN 3   // Total number of panels chained one to another

#endif
