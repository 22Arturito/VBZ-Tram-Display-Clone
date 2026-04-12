#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <vbzfont.h>
#include <Adafruit_GFX.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

class Display
{

public:

    int maxDestinationPixels = 68; // max length of pixels on display for destination

    void begin(int r1_pin,
               int g1_pin,
               int b1_pin,
               int r2_pin,
               int g2_pin,
               int b2_pin,
               int a_pin,
               int b_pin,
               int c_pin,
               int d_pin,
               int e_pin,
               int lat_pin,
               int oe_pin,
               int clk_pin,
               int panel_res_x,
               int panel_res_y,
               int panel_chain);

    int getRightAlignStartingPoint(const char *str, int16_t width);

    void printLine(String line, String lineRef, String destination, bool accessible, int timeToArrival, bool liveData, bool isLate, int position);
    void printLines(JsonArray data);

    void showIpAddress(const char *ipAddress);
    void connectingMsg();
    void connectionMsg(String apName, String password);
    void printError(String apiError);

    void displaySetBrightness(int brightness);
    void setNightMode(bool enabled);
    void turnOff();
    void showClock(int h, int m, int s, String date, String weather = "", bool rain = false, float uvMax = -1.0f, const float* hourlyRain = nullptr);
    void testPattern();

    void showSplash();

    uint16_t getVbzFontColor(String lineRef);
    uint16_t getVbzBackgroundColor(String lineRef);

    String cropDestination(String destination);
    int getTextUsedLength(String text);

private:
    MatrixPanel_I2S_DMA *dma_display = nullptr;
    uint16_t myBLACK = dma_display->color565(0, 0, 0);
    uint16_t vbzYellow = dma_display->color565(255, 255, 255); // 252, 249, 110
    uint16_t vbzWhite = dma_display->color565(255, 255, 255);
    uint16_t vbzBlack = dma_display->color565(0, 0, 0);
    uint16_t vbzRed = dma_display->color565(255, 0, 0);

    int total_width = 0;
    int total_height = 0;
    int dest_start_x = 27;
    int access_x = 97;
    int tta_area_x = 108;
    int live_marker_x = 125;
    int tta_area_w = 16;

    bool nightMode = false;

    void drawLineBackground(String lineRef, int x, int y, int w, int h);
    int getLinRefId(String lineRef);

};

#endif
