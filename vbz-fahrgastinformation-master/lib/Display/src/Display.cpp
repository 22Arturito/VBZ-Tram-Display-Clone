#include <Display.h>
#include <Config.h>

void Display::begin(int r1_pin,
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
                    int panel_chain)
{

    HUB75_I2S_CFG mxConfig;

    mxConfig.mx_width = panel_res_x;
    mxConfig.mx_height = panel_res_y;    
    mxConfig.chain_length = panel_chain; 
    mxConfig.gpio.a = a_pin;
    mxConfig.gpio.b = b_pin;
    mxConfig.gpio.c = c_pin;
    mxConfig.gpio.d = d_pin;
    mxConfig.gpio.e = e_pin;
    mxConfig.gpio.lat = lat_pin;
    mxConfig.gpio.oe = oe_pin;
    mxConfig.gpio.clk = clk_pin;

    total_width = panel_res_x * panel_chain;
    total_height = panel_res_y;
    dest_start_x = 27;
    access_x = total_width - 31;
    tta_area_x = total_width - 20;
    live_marker_x = total_width - 3;
    tta_area_w = 16;
    maxDestinationPixels = access_x - dest_start_x - 2;
    if (maxDestinationPixels < 0)
    {
        maxDestinationPixels = 0;
    }

    mxConfig.gpio.r1 = r1_pin;
    mxConfig.gpio.g1 = g1_pin;
    mxConfig.gpio.b1 = b1_pin;
    mxConfig.gpio.r2 = r2_pin;
    mxConfig.gpio.g2 = g2_pin;
    mxConfig.gpio.b2 = b2_pin;

    // --- DIE WICHTIGEN FIXES (Behalten!) ---
    mxConfig.double_buff = true;               // Kein schwarzes Flackern
    mxConfig.i2sspeed = HUB75_I2S_CFG::HZ_10M; // Sauberes Signal (kein blaues Rauschen)
    mxConfig.latch_blanking = 4;               // Kein Ghosting
    mxConfig.clkphase = false;
    // ---------------------------------------

    Display::dma_display = new MatrixPanel_I2S_DMA(mxConfig);
    Display::dma_display->begin();
    Display::dma_display->setTextWrap(false);
    Display::dma_display->setBrightness8(64); 
    Display::dma_display->clearScreen();
    Display::dma_display->fillScreen(Display::myBLACK);
    
    // Screen initialisieren
    Display::dma_display->flipDMABuffer();
}

void Display::showSplash()
{
    Display::dma_display->clearScreen();
    Display::dma_display->fillScreen(Display::myBLACK);

    // 'ch', 34x34px
    const unsigned char epd_bitmap_ch[] PROGMEM = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xff, 0xff, 0xff, 0x80, 0x7f, 0xff, 0xff, 0xff, 0x80, 0x7f,
        0xff, 0xff, 0xff, 0x80, 0x7f, 0xff, 0xff, 0xff, 0x80, 0x7f, 0xff, 0xff, 0xff, 0x80, 0x7f, 0xff,
        0xff, 0xff, 0x80, 0x7f, 0xfc, 0x0f, 0xff, 0x80, 0x7f, 0xfc, 0x0f, 0xff, 0x80, 0x7f, 0xfc, 0x0f,
        0xff, 0x80, 0x7f, 0xfc, 0x0f, 0xff, 0x80, 0x7f, 0xfc, 0x0f, 0xff, 0x80, 0x7f, 0xfc, 0x0f, 0xff,
        0x80, 0x7f, 0xfc, 0x0f, 0xff, 0x80, 0x7e, 0x00, 0x00, 0x1f, 0x80, 0x7e, 0x00, 0x00, 0x1f, 0x80,
        0x7e, 0x00, 0x00, 0x1f, 0x80, 0x7e, 0x00, 0x00, 0x1f, 0x80, 0x7e, 0x00, 0x00, 0x1f, 0x80, 0x7e,
        0x00, 0x00, 0x1f, 0x80, 0x7f, 0xfc, 0x0f, 0xff, 0x80, 0x7f, 0xfc, 0x0f, 0xff, 0x80, 0x7f, 0xfc,
        0x0f, 0xff, 0x80, 0x7f, 0xfc, 0x0f, 0xff, 0x80, 0x7f, 0xfc, 0x0f, 0xff, 0x80, 0x7f, 0xfc, 0x0f,
        0xff, 0x80, 0x7f, 0xfc, 0x0f, 0xff, 0x80, 0x7f, 0xff, 0xff, 0xff, 0x80, 0x7f, 0xff, 0xff, 0xff,
        0x80, 0x7f, 0xff, 0xff, 0xff, 0x80, 0x7f, 0xff, 0xff, 0xff, 0x80, 0x7f, 0xff, 0xff, 0xff, 0x80,
        0x7f, 0xff, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00};

    int splash_w = total_width > 0 ? total_width : 128;
    int splash_h = total_height > 0 ? total_height : 64;

    Display::dma_display->fillRect((splash_w - 34) / 2, (splash_h - 34) / 2, 34, 34, Display::vbzWhite);
    Display::dma_display->drawBitmap((splash_w - 34) / 2, (splash_h - 34) / 2, epd_bitmap_ch, 34, 34, Display::vbzRed);

    Display::dma_display->setFont();
    Display::dma_display->setTextSize(1);
    Display::dma_display->setCursor(0, 50);
    Display::dma_display->println("opentransportdata.swiss");

    Display::dma_display->flipDMABuffer();
}

void Display::displaySetBrightness(int brightness)
{
    Display::dma_display->setBrightness8(brightness);
}

void Display::setNightMode(bool enabled)
{
    nightMode = enabled;
}

void Display::turnOff()
{
    Display::dma_display->fillScreen(myBLACK);
    Display::dma_display->flipDMABuffer();
    Display::dma_display->setBrightness8(0);
}

void Display::showClock(int h, int m, int s, String date, String weather, bool rain, float uvMax, const float* hourlyRain)
{
    Display::dma_display->fillScreen(myBLACK);
    Display::dma_display->setFont();
    Display::dma_display->setTextWrap(false);

    uint16_t col = nightMode
        ? dma_display->color565(220, 100, 0)
        : dma_display->color565(255, 255, 255);
    Display::dma_display->setTextColor(col);

    // Time "HH:MM:SS" at size 2 — each char 12×16px, 8 chars = 96px wide
    char timeBuf[9];
    sprintf(timeBuf, "%02d:%02d:%02d", h, m, s);
    const int timeW = 8 * 12;
    const int timeH = 16;
    const int dateW = date.length() * 6;
    const int dateH = 8;

    bool hasDate = date.length() > 0;
    bool hasWx   = weather.length() > 0;
    bool hasUv   = (uvMax >= 0.0f && hasWx);
    bool hasRain = (hourlyRain != nullptr);

    const int gap    = 4;
    const int uvBarH = 4;
    const int rainDiagH = 15; // reserved pixels at bottom for rain diagram

    int totalH = timeH;
    if (hasDate) totalH += gap + dateH;
    if (hasWx)   totalH += gap + 8;
    if (hasUv)   totalH += gap + uvBarH;

    // Center clock content in the area above the rain diagram (if present)
    // Clamp to 1 so the top display row stays black (border)
    const int clockAreaH = hasRain ? (total_height - rainDiagH) : total_height;
    const int yStart = max(1, (clockAreaH - totalH) / 2);

    Display::dma_display->setTextSize(2);
    Display::dma_display->setCursor((total_width - timeW) / 2, yStart);
    Display::dma_display->print(timeBuf);

    Display::dma_display->setTextSize(1);
    if (hasDate) {
        Display::dma_display->setCursor((total_width - dateW) / 2, yStart + timeH + gap);
        Display::dma_display->print(date);
    }

    if (hasWx) {
        uint16_t wxCol = nightMode
            ? dma_display->color565(220, 100, 0)
            : dma_display->color565(180, 180, 180);
        int wxY = yStart + timeH + gap + (hasDate ? dateH + gap : 0);

        if (rain) {
            static const uint8_t umbrellaIcon[] PROGMEM = {0x3C, 0x7E, 0xFF, 0x18, 0x18, 0x30, 0x00, 0x00};
            const int iconW = 8, iconGap = 3;
            int totalW = iconW + iconGap + (int)weather.length() * 6;
            int startX = (total_width - totalW) / 2;
            dma_display->drawBitmap(startX, wxY, umbrellaIcon, 8, 8, wxCol);
            dma_display->setTextColor(wxCol);
            dma_display->setCursor(startX + iconW + iconGap, wxY);
        } else {
            dma_display->setTextColor(wxCol);
            dma_display->setCursor((total_width - (int)weather.length() * 6) / 2, wxY);
        }
        Display::dma_display->print(weather);

        if (hasUv) {
            int uvY = wxY + 8 + gap;
            const int segW = 8, segGap = 2;
            const int uvBarW = 10 * segW + 9 * segGap; // 98px
            int barX = (total_width - uvBarW) / 2;
            int uvFilled = (int)uvMax;
            if (uvFilled > 10) uvFilled = 10;

            for (int i = 0; i < 10; i++) {
                uint16_t segColor;
                if (i < uvFilled) {
                    uint8_t r, g, b;
                    if      (i < 2) { r=0;   g=180; b=0;   }
                    else if (i < 5) { r=220; g=220; b=0;   }
                    else if (i < 7) { r=255; g=100; b=0;   }
                    else            { r=220; g=0;   b=0;   }
                    if (nightMode) { r=(uint8_t)(r/2); g=(uint8_t)(g/2); b=(uint8_t)(b/2); }
                    segColor = dma_display->color565(r, g, b);
                } else {
                    segColor = dma_display->color565(35, 35, 35);
                }
                dma_display->fillRect(barX + i * (segW + segGap), uvY, segW, uvBarH, segColor);
            }
        }
    }

    // --- Ensemble precipitation diagram (bottom, fixed) ---
    // 24 hourly bars of ECMWF ensemble median (mm/h), scaled 0–5 mm/h = full bar.
    // Centered with 1px side borders. Bottom row (y=63) stays black.
    if (hasRain) {
        const int rainAreaY  = total_height - rainDiagH; // y=49
        const int slotW      = 6;   // 3px bar + 3px gap
        const int bW         = 3;
        const int availW     = total_width - 2;
        const int diagX      = 1 + (availW - 24 * slotW) / 2;
        const int yBottom    = total_height - 2;
        const int effectiveH = yBottom - rainAreaY + 1;  // =14

        const float maxMmh = 5.0f;

        uint8_t cr=30, cg=110, cb=220;  // solid blue
        if (nightMode) { cr=(uint8_t)(cr/2); cg=(uint8_t)(cg/2); cb=(uint8_t)(cb/2); }
        uint16_t rainColor = dma_display->color565(cr, cg, cb);
        uint16_t stubColor = dma_display->color565(22, 22, 22);

        for (int idx = 0; idx < 24; idx++) {
            float val = hourlyRain[idx];
            int bx    = diagX + idx * slotW;

            if (val < 0.05f) {
                dma_display->fillRect(bx, yBottom, bW, 1, stubColor);
                continue;
            }

            int totalBarH = (int)(val * effectiveH / maxMmh + 0.5f);
            if (totalBarH > effectiveH) totalBarH = effectiveH;
            if (totalBarH < 1) totalBarH = 1;

            dma_display->fillRect(bx, yBottom - totalBarH + 1, bW, totalBarH, rainColor);
        }

        // Short ticks every 6h (4 ticks for 24h)
        uint16_t tickColor = nightMode
            ? dma_display->color565(110, 50, 0)
            : dma_display->color565(200, 200, 200);
        for (int g = 0; g < 4; g++) {
            int tx = diagX + g * 6 * slotW - 2;
            dma_display->drawFastVLine(tx, yBottom - 2, 3, tickColor);
        }
    }

    Display::dma_display->flipDMABuffer();
}

void Display::testPattern()
{
    uint16_t colors[4] = {
        dma_display->color565(255, 0,   0),
        dma_display->color565(0,   255, 0),
        dma_display->color565(0,   0,   255),
        dma_display->color565(255, 255, 255)
    };
    for (int i = 0; i < 4; i++) {
        dma_display->fillScreen(colors[i]);
        dma_display->flipDMABuffer();
        delay(500);
    }
    dma_display->fillScreen(myBLACK);
    dma_display->flipDMABuffer();
}

void Display::showIpAddress(const char *ipAddress)
{
    Display::dma_display->fillScreen(Display::myBLACK);
    Display::dma_display->setFont(&vbzfont);
    Display::dma_display->setTextSize(1);
    Display::dma_display->setCursor(0, 0);
    Display::dma_display->println("configure at:");
    Display::dma_display->println(ipAddress);
    Display::dma_display->flipDMABuffer();
}

void Display::connectingMsg()
{
    Display::dma_display->fillScreen(Display::myBLACK);
    Display::dma_display->setFont(&vbzfont);
    Display::dma_display->setTextSize(1);
    Display::dma_display->setCursor(0, 0);
    Display::dma_display->println("connecting...");
    Display::dma_display->flipDMABuffer();
}

void Display::connectionMsg(String apName, String password)
{
    Display::dma_display->fillScreen(Display::myBLACK);
    Display::dma_display->setFont(&vbzfont);
    Display::dma_display->setTextSize(1);
    Display::dma_display->setCursor(0, 0);
    Display::dma_display->println("connect to:");
    Display::dma_display->println(apName);
    Display::dma_display->println("pwd: " + password);
    Display::dma_display->flipDMABuffer();
}

void Display::printError(String apiError)
{
    Display::dma_display->clearScreen();
    Display::dma_display->fillScreen(Display::vbzBlack);
    Display::dma_display->setFont();
    Display::dma_display->setTextWrap(true);
    Display::dma_display->setTextSize(1);
    Display::dma_display->setTextColor(Display::vbzRed);
    Display::dma_display->setCursor(0, 0);
    Display::dma_display->println(apiError);
    Display::dma_display->setTextWrap(false);
    Display::dma_display->flipDMABuffer();
}

void Display::printLines(JsonArray data)
{
    Display::dma_display->fillScreen(Display::myBLACK);

    int maxLines = (total_height - 11) / 13 + 1;
    int index = 0;
    for (const auto &value : data)
    {
        if (index >= maxLines) break;
        if (value["ttl"].as<int>() >= 0)
        {
            Display::printLine(
                value["line"].as<String>(),
                value["lineRef"].as<String>(),
                value["destination"].as<String>(),
                value["isNF"].as<bool>(),
                value["ttl"].as<int>(),
                value["liveData"].as<bool>(),
                value["isLate"].as<bool>(),
                index);
            index++;
        }
    }
    
    // Zeig das fertige Bild
    Display::dma_display->flipDMABuffer();
}

int Display::getRightAlignStartingPoint(const char *str, int16_t width)
{
    GFXcanvas1 canvas(512, 16);
    canvas.setFont(&vbzfont);
    canvas.setTextSize(1);
    canvas.setCursor(0, 0);
    canvas.print(str);
    int advance = canvas.getCursorX() + 1;
    return (width - advance);
}

void Display::printLine(String line, String lineRef, String destination, bool accessible, int timeToArrival, bool liveData, bool isLate, int position)
{
    Display::dma_display->setFont(&vbzfont);
    Display::dma_display->setTextSize(1);
    Display::dma_display->setTextWrap(false);

    String NF = "";
    if (accessible) NF = "NF";

    String LD = "S";
    if (liveData) LD = "L";

    char infoLine[50];
    char lineCh[24];
    char destinationCh[48];
    char accessCh[2];
    char ttlCh[6];

    int lineNumber = position * 13;

    // reset cursor
    dma_display->setCursor(0, 0);

    // Line
    line.replace(" ", "");
    line.trim();
    line.toCharArray(lineCh, sizeof(lineCh));
    sprintf(infoLine, "%s", lineCh);

    int xPos = Display::getRightAlignStartingPoint(line.c_str(), 23);

    uint16_t nightAmber = dma_display->color565(220, 100, 0);
    uint16_t textColor = nightMode ? nightAmber : Display::vbzYellow;

    Display::drawLineBackground(lineRef, 0, lineNumber, 24, 11);
    Display::dma_display->setTextColor(nightMode ? nightAmber : Display::getVbzFontColor(lineRef));
    Display::dma_display->setCursor(xPos, lineNumber);
    Display::dma_display->print(infoLine);

    // Direction
    destination = Display::cropDestination(destination);
    destination.toCharArray(destinationCh, sizeof(destinationCh));
    sprintf(infoLine, "%s", destinationCh);

    Display::dma_display->setTextColor(textColor);
    Display::dma_display->setCursor(dest_start_x, lineNumber);
    
    // Spezial-Fix für Kerning-Probleme (Nordstrasse / Wyss Platz)
    for (int i = 0; i < strlen(infoLine); i++) {
        char c = infoLine[i];
        char nextC = infoLine[i+1]; 

        Display::dma_display->print(c);

        // Gezielter Abstandshalter für überlappende Buchstaben
        if ((c == 'N' && nextC == 'o') || (c == 'y' && nextC == 's')) {
             Display::dma_display->setCursor(Display::dma_display->getCursorX() + 1, lineNumber);
        }
    }

    // Accessibility
    if (accessible)
    {
        String acces = "\x1F";
        acces.toCharArray(accessCh, sizeof(accessCh));
        sprintf(infoLine, "%s", accessCh);

        Display::dma_display->setTextColor(textColor);
        Display::dma_display->setCursor(access_x, lineNumber);
        Display::dma_display->setTextWrap(false);
        Display::dma_display->print(infoLine);
    }

    // TTA
    if (timeToArrival <= 0)
    {
        sprintf(ttlCh, "\x1E");
        xPos = tta_area_w - 8;
    }
    else
    {
        if (isLate) {
            sprintf(ttlCh, ">%d", timeToArrival);
        } else {
            sprintf(ttlCh, "%d", timeToArrival);
        }
        xPos = Display::getRightAlignStartingPoint(ttlCh, tta_area_w);
    }

    Display::dma_display->setTextColor(textColor);
    Display::dma_display->setCursor(tta_area_x + xPos, lineNumber);
    Display::dma_display->setTextWrap(false);
    Display::dma_display->print(ttlCh);

    if (timeToArrival > 0)
    {
        String liveDataMarker = "'";
        if (liveData) liveDataMarker = "`";

        Display::dma_display->setTextColor(textColor);
        Display::dma_display->setCursor(live_marker_x, lineNumber);
        Display::dma_display->setTextWrap(false);
        Display::dma_display->print(liveDataMarker);
    }
}

void Display::drawLineBackground(String lineRef, int x, int y, int w, int h)
{
    uint16_t bg = nightMode ? myBLACK : Display::getVbzBackgroundColor(lineRef);
    Display::dma_display->fillRect(x, y, w, h, bg);

#if BLACK_LINE_STRIPES_ENABLE
    int lineId = Display::getLinRefId(lineRef);
    if (lineId == 91007)
    {
        uint16_t stripeColor = Display::dma_display->color565(
            BLACK_LINE_STRIPE_R,
            BLACK_LINE_STRIPE_G,
            BLACK_LINE_STRIPE_B);

        const int spacing = 4;
        for (int offset = -h; offset < w; offset += spacing)
        {
            int x0 = x + offset;
            int y0 = y;
            int x1 = x + offset + h;
            int y1 = y + h;
            Display::dma_display->drawLine(x0, y0, x1, y1, stripeColor);
        }
    }
#endif
}

uint16_t Display::getVbzFontColor(String lineRef)
{
    int lineId = Display::getLinRefId(lineRef);
    uint8_t r, g, b;

    switch (lineId)
    {
    case 91008:
    case 91013:
        r = 0; g = 0; b = 0;
        break;
    default:
        r = 255; g = 255; b = 255;
        break;
    }
    return dma_display->color565(r, g, b);
}

uint16_t Display::getVbzBackgroundColor(String lineRef)
{
    int lineId = Display::getLinRefId(lineRef);
    uint8_t r = 0, g = 0, b = 0;

    switch (lineId)
    {
    case 91002: r = 229; g = 0; b = 0; break;
    case 91003: r = 0; g = 138; b = 41; break;
    case 91004: r = 14; g = 37; b = 110; break;
    case 91005: r = 116; g = 69; b = 30; break;
    case 91006: r = 204; g = 128; b = 52; break;
    case 91007: r = 0; g = 0; b = 0; break;
    case 91008: r = 137; g = 183; b = 0; break;
    case 91009: r = 15; g = 38; b = 113; break;
    case 91010: r = 228; g = 29; b = 113; break;
    case 91011: r = 0; g = 138; b = 41; break;
    case 91013: r = 255; g = 194; b = 0; break;
    case 91014: r = 0; g = 140; b = 199; break;
    case 91015: r = 228; g = 0; b = 0; break;
    case 91017: r = 144; g = 29; b = 77; break;
    default: r = 0; g = 0; b = 0; break;
    }
    return dma_display->color565(r, g, b);
}

int Display::getLinRefId(String lineRef)
{
    int last = lineRef.lastIndexOf(':');
    String noDir = (last >= 0) ? lineRef.substring(0, last) : lineRef;
    int prev = noDir.lastIndexOf(':');
    String idStr = (prev >= 0) ? noDir.substring(prev + 1) : noDir;
    return idStr.toInt();
}

String Display::cropDestination(String destination)
{
    destination.replace("Zürich,", "");
    destination.replace("Winterthur,", "");
    destination.replace("Bahnhof ", "");
    destination.replace("ä", "\x7B");
    destination.replace("ö", "\x7C");
    destination.replace("ü", "\x7D");
    destination.trim();

    bool textWasTooLong = (Display::getTextUsedLength(destination) >= Display::maxDestinationPixels);
    while (Display::getTextUsedLength(destination) >= Display::maxDestinationPixels)
    {
        destination = destination.substring(0, destination.length() - 1);
    }
    if (textWasTooLong) destination = destination + ".";
    
    return destination;
}

int Display::getTextUsedLength(String text)
{
    GFXcanvas1 canvas(Display::dma_display->width(), 16);
    canvas.setFont(&vbzfont);
    canvas.setTextSize(1);
    canvas.setTextWrap(false);
    canvas.setCursor(0, 0);
    canvas.print(text);
    return canvas.getCursorX() + 1;
}