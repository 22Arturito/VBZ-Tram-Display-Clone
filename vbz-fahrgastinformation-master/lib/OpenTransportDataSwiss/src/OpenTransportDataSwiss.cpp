#include <OpenTransportDataSwiss.h>

OpenTransportDataSwiss::OpenTransportDataSwiss(String stopPointBPUIC,
                                               String direction,
                                               String openDataUrl,
                                               String apiKey,
                                               String numResults)
{

    OpenTransportDataSwiss::numResultsString = numResults;
    OpenTransportDataSwiss::stopPointBPUIC = stopPointBPUIC;
    OpenTransportDataSwiss::direction = direction;
    OpenTransportDataSwiss::openDataUrl = openDataUrl;
    OpenTransportDataSwiss::apiKey = apiKey;
}

int OpenTransportDataSwiss::getWebData(NTPClient timeClient)
{
    WiFiClientSecure *client = new WiFiClientSecure;
    if (client)
    {
        HTTPClient https;
        // If we only one one direction we need to get the double amount of results in order to fill the display
        int resultsToGet = OpenTransportDataSwiss::numResultsString.toInt();
        if (OpenTransportDataSwiss::direction != "A")
        {
            resultsToGet = resultsToGet * 2;
        }

        // client->setCACert(rootCACertificate);
        client->setInsecure();

        // get a timestamp to "now"
       // get a timestamp to "now"
String formattedDate = timeClient.getFormattedDate();

// Precompute timestamps (saves String churn + ensures consistent values)
String reqTs = OpenTransportDataSwiss::FormatTimeStamp(formattedDate, "RequestTimestamp");
String depArrTs = OpenTransportDataSwiss::FormatTimeStamp(formattedDate, "DepArrTime");

// Build OJP 2.0 StopEventRequest XML
String PostData;
PostData.reserve(900); // helps avoid heap fragmentation on ESP32/ESP8266

PostData  = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";
PostData += "<OJP xmlns=\"http://www.vdv.de/ojp\" xmlns:siri=\"http://www.siri.org.uk/siri\" version=\"2.0\">";
PostData += "  <OJPRequest>";
PostData += "    <siri:ServiceRequest>";
PostData += "      <siri:ServiceRequestContext>";
PostData += "        <siri:Language>de</siri:Language>";
PostData += "      </siri:ServiceRequestContext>";
PostData += "      <siri:RequestTimestamp>" + reqTs + "</siri:RequestTimestamp>";
PostData += "      <siri:RequestorRef>tramdisplay</siri:RequestorRef>";
PostData += "      <OJPStopEventRequest>";
PostData += "        <siri:RequestTimestamp>" + reqTs + "</siri:RequestTimestamp>";
PostData += "        <siri:MessageIdentifier>SER-1</siri:MessageIdentifier>";
PostData += "        <Location>";
PostData += "          <PlaceRef>";
PostData += "            <siri:StopPointRef>" + OpenTransportDataSwiss::stopPointBPUIC + "</siri:StopPointRef>";
PostData += "            <Name><Text>" + OpenTransportDataSwiss::stopPointBPUIC + "</Text></Name>";
PostData += "          </PlaceRef>";
PostData += "          <DepArrTime>" + depArrTs + "</DepArrTime>";
PostData += "        </Location>";
PostData += "        <Params>";
PostData += "          <NumberOfResults>" + String(resultsToGet) + "</NumberOfResults>";
PostData += "          <StopEventType>departure</StopEventType>";
PostData += "          <UseRealtimeData>full</UseRealtimeData>";
PostData += "        </Params>";
PostData += "      </OJPStopEventRequest>";
PostData += "    </siri:ServiceRequest>";
PostData += "  </OJPRequest>";
PostData += "</OJP>";


        // Serial.println(url);
        // Serial.println(PostData);

        String authHeader = OpenTransportDataSwiss::apiKey;
        if (!authHeader.startsWith("Bearer "))
        {
            authHeader = "Bearer " + authHeader;
        }

        // Serial.println(https.getString());

#ifdef DEBUG
        Serial.print("[HTTPS] begin...\n");
#endif
        if (https.begin(*client, OpenTransportDataSwiss::openDataUrl))
        { // HTTPS
            // Headers must be added AFTER begin() — begin() calls clear() internally
            https.addHeader("Authorization", authHeader);
            https.addHeader("Content-Type", "application/xml");
            https.setTimeout(10000);
#ifdef DEBUG
            Serial.print("[HTTPS] POST...\n");
#endif
            // start connection and send HTTP header
            int httpCode = https.POST(PostData);

            // httpCode will be negative on error
            if (httpCode > 0)
            {
// HTTP header has been send and Server response header has been handled
#ifdef DEBUG
                Serial.printf("[HTTPS] POST... code: %d\n", httpCode);
#endif

                // file found at server
                if (httpCode == HTTP_CODE_OK)
                {
                    // Parse
#ifdef DEBUG
                    Serial.printf("[HTTPS] Parse\n");
#endif

                    // get length of document (is -1 when Server sends no Content-Length header)
                    // int len = https.getSize();
                    // Serial.printf("len: %d\n", len);

                    String result = https.getString();

                    Serial.println("---- OJP XML START ----");
                    Serial.println(result);
                    Serial.println("---- OJP XML END ----");

                    int index = result.indexOf("</ojp:StopEvent>");
                    if (index == -1)
                    {
                        index = result.indexOf("</StopEvent>");
                    }
                    int counter = 0;

                    JsonArray data = doc.to<JsonArray>();

                    // Serial.printf("xml: %s\n", result.c_str());

                    while (index != -1)
                    {
                        String part = OpenTransportDataSwiss::getXmlValue("<ojp:StopEvent>", "</ojp:StopEvent>", result);
                        if (part.length() == 0)
                        {
                            part = OpenTransportDataSwiss::getXmlValue("<StopEvent>", "</StopEvent>", result);
                        }

                        // quick & dirty: normalize whitespace to make tag matching robust
                        part.replace("\r", "");
                        part.replace("\n", "");
                        part.replace("\t", "");
                        while (part.indexOf("> <") != -1) part.replace("> <", "><");
                        while (part.indexOf(">  <") != -1) part.replace(">  <", "><");

                        if (result.indexOf("</ojp:StopEvent>") != -1)
                        {
                            result.remove(0, result.indexOf("</ojp:StopEvent>") + 15);
                            index = result.indexOf("</ojp:StopEvent>");
                        }
                        else
                        {
                            result.remove(0, result.indexOf("</StopEvent>") + 11);
                            index = result.indexOf("</StopEvent>");
                        }

                        // Serial.printf("xml %d: %s\n", index,  part.c_str());

                        // find live dep time <ojp:EstimatedTime>2022-11-04T16:02:00Z</ojp:EstimatedTime>
                        // find sched dep time <ojp:TimetabledTime>2022-11-05T19:43:00Z</ojp:TimetabledTime>
                        String departureTime;
                        bool liveData = false;
                        bool isLate = false;
                        if (part.indexOf("<ojp:EstimatedTime>") != -1 || part.indexOf("<EstimatedTime>") != -1)
                        {
                            // Has live data
                            liveData = true;
                            departureTime = OpenTransportDataSwiss::getXmlValue(
                                "<ojp:EstimatedTime>",
                                "</ojp:EstimatedTime>",
                                part);
                            if (departureTime.length() == 0)
                            {
                                departureTime = OpenTransportDataSwiss::getXmlValue(
                                    "<EstimatedTime>",
                                    "</EstimatedTime>",
                                    part);
                            }

                            // check if late
                            String scheduledTime = OpenTransportDataSwiss::getXmlValue(
                                "<ojp:TimetabledTime>",
                                "</ojp:TimetabledTime>",
                                part);
                            if (scheduledTime.length() == 0)
                            {
                                scheduledTime = OpenTransportDataSwiss::getXmlValue(
                                    "<TimetabledTime>",
                                    "</TimetabledTime>",
                                    part);
                            }

                            uint32_t drift = OpenTransportDataSwiss::GetTimeToDeparture(scheduledTime, departureTime);
                            // Serial.printf("drift: %d\n", drift);

                            if (drift >= OpenTransportDataSwiss::lateMinCutoff) {
                                isLate = true;
                            }

                        }
                        else
                        {
                            // Has no live data, use scheduled
                            departureTime = OpenTransportDataSwiss::getXmlValue(
                                "<ojp:TimetabledTime>",
                                "</ojp:TimetabledTime>",
                                part);
                            if (departureTime.length() == 0)
                            {
                                departureTime = OpenTransportDataSwiss::getXmlValue(
                                    "<TimetabledTime>",
                                    "</TimetabledTime>",
                                    part);
                            }
                        }

                        // Serial.printf("departureTime: %d: %s\n", index, departureTime.c_str());

                        // find destination <ojp:DestinationText><ojp:Text>ZÃ¼rich, Rehalp</ojp:Text>...</ojp:DestinationText>
                        String destination = "";

                        String serviceBlock = OpenTransportDataSwiss::getXmlValue(
                            "<ojp:Service>",
                            "</ojp:Service>",
                            part);
                        if (serviceBlock.length() == 0)
                        {
                            serviceBlock = OpenTransportDataSwiss::getXmlValue(
                                "<Service>",
                                "</Service>",
                                part);
                        }
                        String destSource = (serviceBlock.length() > 0) ? serviceBlock : part;

                        String destBlock = OpenTransportDataSwiss::getXmlValue(
                            "<ojp:DestinationText>",
                            "</ojp:DestinationText>",
                            destSource);
                        if (destBlock.length() == 0)
                        {
                            destBlock = OpenTransportDataSwiss::getXmlValue(
                                "<DestinationText>",
                                "</DestinationText>",
                                destSource);
                        }

                        if (destBlock.length() > 0)
                        {
                            destination = OpenTransportDataSwiss::getXmlValueFlexible("ojp:Text", destBlock);
                            if (destination.length() == 0)
                            {
                                destination = OpenTransportDataSwiss::getXmlValueFlexible("Text", destBlock);
                            }
                        }

                        if (destination.length() == 0)
                        {
                            String dirBlock = OpenTransportDataSwiss::getXmlValue(
                                "<ojp:DirectionText>",
                                "</ojp:DirectionText>",
                                destSource);
                            if (dirBlock.length() == 0)
                            {
                                dirBlock = OpenTransportDataSwiss::getXmlValue(
                                    "<DirectionText>",
                                    "</DirectionText>",
                                    destSource);
                            }

                            if (dirBlock.length() > 0)
                            {
                                destination = OpenTransportDataSwiss::getXmlValueFlexible("ojp:Text", dirBlock);
                                if (destination.length() == 0)
                                {
                                    destination = OpenTransportDataSwiss::getXmlValueFlexible("Text", dirBlock);
                                }
                            }
                        }

                        // Serial.printf("destination: %d: %s\n", index, destination.c_str());

                        // find Line (prefer PublicCode, then PublishedServiceName)
                        String line = "";

                        String serviceBlockLine = OpenTransportDataSwiss::getXmlValue(
                            "<ojp:Service>",
                            "</ojp:Service>",
                            part);
                        if (serviceBlockLine.length() == 0)
                        {
                            serviceBlockLine = OpenTransportDataSwiss::getXmlValue(
                                "<Service>",
                                "</Service>",
                                part);
                        }
                        String lineSource = (serviceBlockLine.length() > 0) ? serviceBlockLine : part;

                        // 1) PublicCode (with/without namespace)
                        line = OpenTransportDataSwiss::getXmlValue(
                            "<ojp:PublicCode>",
                            "</ojp:PublicCode>",
                            lineSource);
                        if (line.length() == 0)
                        {
                            line = OpenTransportDataSwiss::getXmlValue(
                                "<PublicCode>",
                                "</PublicCode>",
                                lineSource);
                        }

                        // 2) PublishedServiceName -> Text (handles Text attributes)
                        if (line.length() == 0)
                        {
                            String psn = OpenTransportDataSwiss::getXmlValue(
                                "<ojp:PublishedServiceName>",
                                "</ojp:PublishedServiceName>",
                                lineSource);
                            if (psn.length() == 0)
                            {
                                psn = OpenTransportDataSwiss::getXmlValue(
                                    "<PublishedServiceName>",
                                    "</PublishedServiceName>",
                                    lineSource);
                            }

                            if (psn.length() > 0)
                            {
                                line = OpenTransportDataSwiss::getXmlValueFlexible("ojp:Text", psn);
                                if (line.length() == 0)
                                {
                                    line = OpenTransportDataSwiss::getXmlValueFlexible("Text", psn);
                                }
                            }
                        }

                        // 3) Fallback: PublishedLineName -> Text (legacy)
                        if (line.length() == 0)
                        {
                            String pln = OpenTransportDataSwiss::getXmlValue(
                                "<ojp:PublishedLineName>",
                                "</ojp:PublishedLineName>",
                                lineSource);
                            if (pln.length() == 0)
                            {
                                pln = OpenTransportDataSwiss::getXmlValue(
                                    "<PublishedLineName>",
                                    "</PublishedLineName>",
                                    lineSource);
                            }

                            if (pln.length() > 0)
                            {
                                line = OpenTransportDataSwiss::getXmlValueFlexible("ojp:Text", pln);
                                if (line.length() == 0)
                                {
                                    line = OpenTransportDataSwiss::getXmlValueFlexible("Text", pln);
                                }
                            }
                        }

                        // Serial.printf("line: %d: %s\n", index, line.c_str());

                        // find NF <ojp:Code>A__NF</ojp:Code>
                        bool isNF = false;
                        if (part.indexOf("<ojp:Code>A__NF</ojp:Code>") != -1 || part.indexOf("<Code>A__NF</Code>") != -1)
                        {
                            isNF = true;
                        }

                        String lineRef = OpenTransportDataSwiss::getXmlValue(
                            "<ojp:LineRef>",
                            "</ojp:LineRef>",
                            part);
                        if (lineRef.length() == 0)
                        {
                            lineRef = OpenTransportDataSwiss::getXmlValue(
                                "<siri:LineRef>",
                                "</siri:LineRef>",
                                part);
                        }
                        if (lineRef.length() == 0)
                        {
                            lineRef = OpenTransportDataSwiss::getXmlValue(
                                "<LineRef>",
                                "</LineRef>",
                                part);
                        }

                        Serial.printf("Parsed lineRef: %s\n", lineRef.c_str());

                        // Serial.printf("NF: %d: %s\n", index, isNF);

                        // match direction if set and skip if it doesn't match
                        String refDirection = OpenTransportDataSwiss::getXmlValue(
                            "<siri:DirectionRef>",
                            "</siri:DirectionRef>",
                            part);
                        if (refDirection.length() == 0)
                        {
                            refDirection = OpenTransportDataSwiss::getXmlValue(
                                "<DirectionRef>",
                                "</DirectionRef>",
                                part);
                        }
                        if (refDirection.length() == 0 && lineRef.indexOf(":") != -1)
                        {
                            refDirection = lineRef.substring(lineRef.lastIndexOf(":") + 1, lineRef.length());
                        }

                        Serial.printf(
                            "FILTER? cfgDir=%s refDir=%s lineRef=%s\n",
                            OpenTransportDataSwiss::direction.c_str(),
                            refDirection.c_str(),
                            lineRef.c_str());

                        if (OpenTransportDataSwiss::direction != "A" &&
                            refDirection.length() > 0 &&
                            refDirection != OpenTransportDataSwiss::direction)
                        {
                            continue;
                        }

                        StaticJsonDocument<200> doc2;
                        JsonObject item = doc2.to<JsonObject>();

                        item["departureTime"] = departureTime;
                        item["ttl"] = OpenTransportDataSwiss::GetTimeToDeparture(OpenTransportDataSwiss::FormatTimeStamp(formattedDate, "RequestTimestamp"), departureTime);
                        item["liveData"] = liveData;
                        item["line"] = line;
                        item["lineRef"] = lineRef;
                        item["isNF"] = isNF;
                        item["isLate"] = isLate;
                        item["destination"] = destination;
                        item["dir"] = refDirection;

                        data.add(item);

                        Serial.printf(
                            "dep=%s ttl=%d live=%d line=%s lineRef=%s late=%d dest=%s\n",
                            departureTime.c_str(),
                            item["ttl"].as<int>(),
                            liveData ? 1 : 0,
                            line.c_str(),
                            lineRef.c_str(),
                            isLate ? 1 : 0,
                            destination.c_str());

                        counter++;
                    }

                    if (data.isNull())
                    {
                        Serial.printf("No data: %s\n", result.c_str());
                    }
                    https.end();
                    delete client;
                    return 0;
                }
                else
                {
                    Serial.printf("[HTTPS] POST... failed, http code error: %s\n", https.errorToString(httpCode).c_str());

                    if (httpCode == 403)
                    {
                        OpenTransportDataSwiss::httpLastError = "ERROR: Authentication Failed. API key may be incorrect or expired. Code: " + (String)httpCode;
                    }
                    else
                    {
                        OpenTransportDataSwiss::httpLastError = https.errorToString(httpCode) + "(" + httpCode + ")";
                    }
                }
            }
            else
            {
                Serial.printf("[HTTPS] POST... failed, error: %s\n", https.errorToString(httpCode).c_str());
                OpenTransportDataSwiss::httpLastError = https.errorToString(httpCode) + "(" + httpCode + ")";
            }

            https.end();
        }
        else
        {
            Serial.printf("[HTTPS] Unable to connect\n");
            OpenTransportDataSwiss::httpLastError = "Unable to connect";
        }

        // End extra scoping block

        delete client;
    }
    else
    {
        Serial.println("Unable to create client or no stations set");
        OpenTransportDataSwiss::httpLastError = "Unable to create client or no stations set";
    }

    return 1;
}

String OpenTransportDataSwiss::FormatTimeStamp(String formattedDate, String format)
{
    // RequestTimestamp>2022-11-04T15:38:26.611Z
    // DepArrTime>2022-11-04T16:38:26

    if (format == "RequestTimestamp")
    {
        return formattedDate;
    }

    return formattedDate.substring(0, formattedDate.lastIndexOf("."));
}

uint32_t OpenTransportDataSwiss::GetTimeToDeparture(String apiCallTime, String departureTime)
{

    // Serial.printf("apiCallTime: %s\n", apiCallTime.c_str());
    // Serial.printf("departureTime: %s\n", departureTime.c_str());

    uint32_t now = OpenTransportDataSwiss::GetEpochTime(apiCallTime);
    uint32_t dep = OpenTransportDataSwiss::GetEpochTime(departureTime);

    // Serial.printf("now: %d\n", now);
    // Serial.printf("dep: %d\n", dep);

    if ((dep <= now) || (dep - now) <= 60)
    {
        return 0;
    }

    // Serial.printf("res: %d\n", round((dep - now) / 60));

    return round((dep - now) / 60);
}

uint32_t OpenTransportDataSwiss::GetEpochTime(String dateTimeStamp)
{

    String dayStamp = dateTimeStamp.substring(0, dateTimeStamp.indexOf("T"));
    int year = dayStamp.substring(0, dayStamp.indexOf("-")).toInt();
    int month = dayStamp.substring(dayStamp.indexOf("-") + 1, dayStamp.lastIndexOf("-")).toInt();
    int day = dayStamp.substring(dayStamp.lastIndexOf("-") + 1).toInt();

    int timeEnd = dateTimeStamp.indexOf("Z");
    if (timeEnd == -1)
    {
        timeEnd = dateTimeStamp.length();
    }
    String timeStamp = dateTimeStamp.substring(dateTimeStamp.indexOf("T") + 1, timeEnd);
    int hour = timeStamp.substring(0, timeStamp.indexOf(":")).toInt();
    int minute = timeStamp.substring(timeStamp.indexOf(":") + 1, timeStamp.lastIndexOf(":")).toInt();
    int seconds = timeStamp.substring(timeStamp.lastIndexOf(":") + 1).toInt();

    UnixTime stamp(0);

    stamp.setDateTime(year, month, day, hour, minute, seconds);

    return stamp.getUnix();
}

String OpenTransportDataSwiss::getXmlValue(String xmlStartElement, String xmlEndElement, String xmlDocument)
{
    int start = xmlDocument.indexOf(xmlStartElement);
    int end = xmlDocument.indexOf(xmlEndElement);
    if (start == -1 || end == -1)
    {
        return "";
    }
    start += xmlStartElement.length();
    if (start >= end)
    {
        return "";
    }
    return xmlDocument.substring(start, end);
}

String OpenTransportDataSwiss::getXmlValueFlexible(String tagName, String xmlDocument)
{
    String openTagPrefix = "<" + tagName;
    String closeTag = "</" + tagName + ">";
    int searchFrom = 0;

    while (true)
    {
        int start = xmlDocument.indexOf(openTagPrefix, searchFrom);
        if (start == -1)
        {
            return "";
        }

        int nameEnd = start + openTagPrefix.length();
        if (nameEnd >= xmlDocument.length())
        {
            return "";
        }

        char nextChar = xmlDocument.charAt(nameEnd);
        if (nextChar == '>' || nextChar == ' ' || nextChar == '\t' || nextChar == '\r' || nextChar == '\n')
        {
            int openEnd = xmlDocument.indexOf(">", nameEnd);
            if (openEnd == -1)
            {
                return "";
            }
            int end = xmlDocument.indexOf(closeTag, openEnd + 1);
            if (end == -1)
            {
                return "";
            }
            return xmlDocument.substring(openEnd + 1, end);
        }

        searchFrom = nameEnd;
    }
}
