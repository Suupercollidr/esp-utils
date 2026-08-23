#include "EventLogger.h"
#include <WiFi.h>
#include <SD.h>
#include <time.h>

EventLogger::EventLogger(InfluxDBClient &client,
                         int8_t sdDetectPin,
                         const char *logFileName,
                         const String deviceName,
                         uint8_t influxFailureThreshold,
                         unsigned long influxCooldownMs)

    : influxClient(client),
      sdDetectPin(sdDetectPin),
      logFileName(logFileName),
      deviceName(deviceName),
      influxBreaker(influxFailureThreshold, influxCooldownMs, "InfluxDB")
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

    const char *levelStr = levelToString(level);

    bool fileSuccess = false;
    bool influxSuccess = false;

    if (checkSDStatus())
        fileSuccess = logToFile(timestamp, message, level);

    Point logPoint = makePoint(timestamp, message, level);

    influxSuccess = writePoint(logPoint);

    if (!influxSuccess)
        savePointToLittleFS(logPoint, nowTime);

    reportInfluxStateChangeIfAny();

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
    // logPoint.addField("timestamp", timestamp);
    return logPoint;
}

bool EventLogger::writePoint(Point pointToWrite)
{
    // Circuit breaker: om vi redan vet att InfluxDB är nere, gör inte ens
    // försöket. Det här är kärnfixen mot "eviga time-outs" - istället för
    // att varje enskilt loggmeddelande blockerar loopen i flera sekunder
    // under en nätverksstörning, ger vi upp direkt tills cooldown gått ut.
    if (!influxBreaker.canAttempt())
        return false;

    if (!influxClient.writePoint(pointToWrite))
    {
        influxBreaker.recordFailure();
        Serial.println("InfluxDB error: " + influxClient.getLastErrorMessage());
        return false;
    }

    influxBreaker.recordSuccess();
    return true;
}

void EventLogger::savePointToLittleFS(Point &logPoint, time_t &nowTime)
{
    File file = LittleFS.open(pendingLogFileName, "a");

    if (!file)
    {
        Serial.println("Fel: Kunde inte spara till LittleFS");
        return;
    }
    
    if (file.size() > maxPendingFileSizeBytes)
    {
        file.close();
        Serial.println("Fel: Bufferten är full, loggmeddelandet kastades");
        return;
    }

    unsigned long long pointTimestampNs = (unsigned long long)nowTime * 1000000000ULL;
    logPoint.setTime(pointTimestampNs);

    String lineProtocol = logPoint.toLineProtocol();

    file.println(lineProtocol);
    file.close();
}

void EventLogger::sendPendingPoints()
{
    if (!LittleFS.exists(pendingLogFileName))
        return;

    // Fråga breakern istället för att bara pröva blint - annars gör vi ett
    // blockerande nätverksförsök varje gång maintain() råkar anropas medan
    // vi redan vet att vi är nere.
    if (!influxBreaker.canAttempt())
        return;

    File file = LittleFS.open(pendingLogFileName, "r");
    if (!file)
    {
        log("Kunde inte öppna filen med sparade loggmeddelanden", EventLogger::LogLevel::ERROR);
        return;

    // Läs igenom HELA filen, men dela upp i en batch (som vi försöker
    // skicka nu) och en rest (som skrivs tillbaka om batchen lyckas).
    // Detta ersätter den gamla logiken som tystlåtet kastade bort allt
    // utöver de sista 200 raderna så fort filen väl skickades.
    std::vector<String> batchLines;
    std::vector<String> remainingLines;

    while (file.available())
    {
        String line = file.readStringUntil('\n');
        if (line.length() == 0)
            continue;

        if (batchLines.size() < maxLinesPerBatch)
            batchLines.push_back(line);
        else
            remainingLines.push_back(line);
    }
    file.close();

    if (batchLines.empty())
    {
        LittleFS.remove(pendingLogFileName);
        return;
    }

    String batch;
    for (auto &l : batchLines)
    {
        batch += l;
        batch += "\n";
    }

    bool ok = influxClient.writeRecord(batch);

    if (ok)
        influxBreaker.recordSuccess();
    else
        influxBreaker.recordFailure();

    if (!ok)
        return; // Filen orörd - försök igen nästa gång maintain() kallas

    if (remainingLines.empty())
    {
        LittleFS.remove(pendingLogFileName);
    }
    else
    {
        // Fortfarande kö kvar (fler än maxLinesPerBatch rader väntade) -
        // skriv tillbaka resten så den skickas vid nästa anrop.
        File out = LittleFS.open(pendingLogFileName, "w");
        if (out)
        {
            for (auto &l : remainingLines)
                out.println(l);
            out.close();
        }
    }

    log("Skickade " + String(batchLines.size()) + " väntande meddelanden (" + String(remainingLines.size()) + " kvar i kö)",
        LogLevel::INFO, true);
}

void EventLogger::maintain()
{
    // Billig att anropa varje loop-varv: sendPendingPoints() returnerar
    // omedelbart om det inte finns någon kö, eller om breakern säger att
    // vi ändå inte får försöka just nu.
    sendPendingPoints();
    reportInfluxStateChangeIfAny();
}

void EventLogger::reportInfluxStateChangeIfAny()
{
    if (!influxBreaker.consumeStateChangeFlag())
        return;

    if (influxBreaker.getState() == AmIOnline::State::ONLINE)
    {
        log("InfluxDB-anslutning återställd", LogLevel::INFO, true);
    }
    else
    {
        unsigned long retryMinutes = influxBreaker.getMsUntilRetry() / 60000UL;
        log("InfluxDB otillgänglig efter " + String(influxBreaker.getConsecutiveFailures()) + " misslyckade försök - försöker igen om ca " + String(retryMinutes) + " min",
            LogLevel::WARNING, true);
    }
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