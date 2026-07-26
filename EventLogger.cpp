#include "EventLogger.h"
#include <WiFi.h>
#include <SD.h>
#include <time.h>

EventLogger::EventLogger(InfluxDBClient &client,
                         int8_t sdDetectPin,
                         const char *logFileName,
                         const String deviceName)

    : influxClient(client),
      sdDetectPin(sdDetectPin),
      logFileName(logFileName),
      deviceName(deviceName)
{
    if (sdDetectPin >= 0)
        pinMode(sdDetectPin, INPUT_PULLUP);

    //    sdAvailable = SD.begin();
    sdAvailable = false; // Turn off b/c broken
    if (!sdAvailable)
        Serial.println("Inget SD-kort");
}

void EventLogger::log(const String &originalMessage, LogLevel level, bool alwaysReport)
{
    time_t nowTime;
    time(&nowTime);

    auto logCount = alwaysReport ? 1 : shouldReport(originalMessage);
    if (logCount == 0)
        return;

    String message = originalMessage;
    if (logCount > 1)
        message = "Inträffat " + String(logCount) + " gånger: " + message;

    char timestamp[30];
    strftime(timestamp, sizeof(timestamp),
             "%Y-%m-%d\t%H:%M:%S", localtime(&nowTime));

    static const char *levelStr = levelToString(level);

    bool fileSuccess = false;
    bool influxSuccess = false;

    if (checkSDStatus())
        fileSuccess = logToFile(timestamp, message, level);

    Point logPoint = makePoint(timestamp, message, level);

    if (WiFi.status() == WL_CONNECTED)
        influxSuccess = writePoint(logPoint);

    if (!influxSuccess)
        savePointToLittleFS(logPoint, nowTime);

    Serial.print(timestamp);
    Serial.print("\t");
    Serial.print(levelStr);
    Serial.print("\t");
    Serial.print(message);
    Serial.print(influxSuccess ? " (sent to InfluxDB," : " (not sent to InfluxDB,");
    Serial.print(fileSuccess ? " written to file)" : " not written to file)");
    Serial.println();
}

bool EventLogger::logToFile(const char *timestamp,
                            const String &message,
                            LogLevel level)
{
    File file = SD.open(logFileName, FILE_APPEND);
    if (!file)
        return false;

    file.print(timestamp);
    file.print(",");
    file.print(levelToString(level));
    file.print(",");
    file.println(message);
    file.close();
    return true;
}

Point EventLogger::makePoint(const char *timestamp,
                             const String &message,
                             LogLevel level)
{
    Point logPoint("EventLog");
    logPoint.addTag("level", levelToString(level));
    logPoint.addTag("device", deviceName);
    logPoint.addField("message", message);
    //logPoint.addField("timestamp", timestamp);
    return logPoint;
}

bool EventLogger::writePoint(Point pointToWrite)
{
    bool pointSentSuccessfully = influxClient.writePoint(pointToWrite);

    if (!pointSentSuccessfully)
        Serial.println("InfluxDB error: " + influxClient.getLastErrorMessage());

    return pointSentSuccessfully;
}

void EventLogger::savePointToLittleFS(Point &logPoint, time_t &nowTime)
{
    File file = LittleFS.open("/pending.log", "a");

    unsigned long long pointTimestampNs = (unsigned long long)nowTime * 1000000000ULL;
    logPoint.setTime(pointTimestampNs);

    String lineProtocol = logPoint.toLineProtocol();

    if (file)
    {
        file.println(lineProtocol);
        file.close();
        return;
    }

    Serial.println("Fel: Kunde inte spara till LittleFS");
}

void EventLogger::sendPendingPoints()
{
    if (!LittleFS.exists("/pending.log"))
        return;

    File file = LittleFS.open("/pending.log", "r");
    if (!file)
        return;

    File tempFile = LittleFS.open("/pending_temp.log", "w");
    if (!tempFile)
    {
        file.close();
        return;
    }

    uint32_t logLineCount = 0;

    while (file.available())
    {
        String line = file.readStringUntil('\n');
        if (line.length() > 0)
        {
            // Skicka direkt som raw Line Protocol
            if (influxClient.writeRecord(line))
            {
                Serial.println("Skickad sparad punkt: " + line);
                logLineCount++;
            }
            else
            {
                // Om skickning misslyckas, spara tillbaka
                tempFile.println(line);
                Serial.println("Misslyckades, sparar om: " + line);
            }
        }
    }

    file.close();
    tempFile.close();

    // Ersätt originalfilen med tempfilen (innehåller bara misslyckade)
    LittleFS.remove("/pending.log");
    LittleFS.rename("/pending_temp.log", "/pending.log");

    log("Skickade " + String(logLineCount) + " väntande meddelanden vid uppstart", EventLogger::LogLevel::INFO);
}

const char *EventLogger::levelToString(LogLevel level)
{
    switch (level)
    {
    case LogLevel::INFO:
        return "INFO";
    case LogLevel::WARNING:
        return "WARNING";
    case LogLevel::ERROR:
        return "ERROR";
    case LogLevel::DATA:
        return "DATA";
    default:
        return "UNKNOWN";
    }
}

bool EventLogger::checkSDStatus()
{
    // Om ingen pin används → returnera nuvarande status
    if (sdDetectPin < 0)
        return sdAvailable;

    // Läs aktuell pin-status
    int currentState = digitalRead(sdDetectPin);

    // Om pinnen inte ändrats → gör inget
    if (currentState == lastSdDetectState)
        return sdAvailable;

    // Uppdatera senaste kända status
    lastSdDetectState = currentState;

    // Någon har pillat på kortet → testa att initiera igen
    sdAvailable = SD.begin();

    if (sdAvailable)
        Serial.println("SD-kort isatt");
    else
        Serial.println("SD-kort borttaget");

    return sdAvailable;
}

uint16_t EventLogger::shouldReport(const String &message)
{
    uint32_t hash = simpleHash(message); // Skapa en hash av meddelandet
    uint32_t now = millis();

    auto it = logHistory.find(hash); // Hitta hashen i logHistory

    if (it == logHistory.end()) // Hittade inte hashen, första gången meddelandet kommer
    {
        logHistory[hash] = {now, 0}; // Lägg till i logHistory
        return 1;
    }

    it->second.count++;
    uint16_t count = it->second.count;

    if (now - it->second.lastTime > suppressionPeriod) // Om suppressionPeriod passerats
    {
        it->second.lastTime = now;
        return count;
    }

    for (int threshold : suppressionThresholds) // Kolla om vi nått något av våra definerade tröskelvärden
    {
        if (count == threshold || count % 1000 == 0)
        {
            it->second.lastTime = now;
            return count;
        }
    }
    return 0;
}

uint32_t EventLogger::simpleHash(const String &str)
{
    uint32_t hash = 5381;
    for (size_t i = 0; i < str.length(); i++)
    {
        hash = ((hash << 5) + hash) + str[i]; // hash * 33 + c
    }
    return hash;
}