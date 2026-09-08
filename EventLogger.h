/**
 * @brief Logs events to InfluxDB, a CSV file, and (when connectivity allows) reports state.
 *
 * Skickningar mot InfluxDB skyddas av en CircuitBreaker: efter ett par
 * misslyckade försök i rad slutar vi helt att försöka i några minuter
 * (istället för att blockera huvudloopen med upprepade timeouts), och
 * sparar allt lokalt på LittleFS under tiden. sendPendingPoints() körs
 * periodiskt via maintain() - inte bara vid uppstart - så kön töms så
 * fort anslutningen är tillbaka.
 *
 * @param client InfluxDBClient object where we can send the event
 * @param sdDetectPin (optional) GPIO pin that is low when an SD card is inserted
 * @param logFileName (optional) Name of the file
 *
 */

#pragma once

#include <Arduino.h>
#include <InfluxDbClient.h>
#include <FS.h>
#include <LittleFS.h>
#include <ping.h>
#include <deque>
#include <vector>
#include <unordered_map>
#include "AmIOnline.h"

class EventLogger
{
public:
    enum class LogLevel
    {
        INFO,
        WARNING,
        ERROR,
        DATA
    };

    EventLogger(InfluxDBClient &client,
                int8_t sdDetectPin = -1,
                const char *logFileName = "/system.log",
                const String deviceName = "",
                uint8_t influxFailureThreshold = 3,
                unsigned long influxCooldownMs = 5UL * 60UL * 1000UL);

    void log(const String &message,
             LogLevel level = LogLevel::ERROR,
             bool alwaysReport = false);

    bool writePoint(Point pointToWrite);

    void sendPendingPoints();

    /**
     * Anropa denna regelbundet från loop() (t.ex. varje varv, eller minst
     * varje sekund). Den är billig att anropa ofta - den gör bara ett
     * riktigt nätverksförsök när circuit breakern faktiskt tillåter det.
     * Hanterar både periodisk tömning av kön och rapportering av
     * online/offline-övergångar.
     */
    void maintain();

    AmIOnline::State getInfluxConnectionState() const { return influxBreaker.getState(); }
    uint32_t getDroppedPointsCount() const { return droppedPointsCount; }

private:
    int8_t sdDetectPin;
    int lastSdDetectState = -1;
    bool sdAvailable = false;
    bool littleFsAvailable = false;
    bool littleFsErrorReported = false;
    const char *logFileName;
    const char *pendingLogFileName = "/pending.log";
    const String deviceName;
    struct LogEntry
    {
        unsigned long lastTime; // Tidpunkt för senaste loggningen
        int count;              // Antal upprepningar
    };
    std::unordered_map<uint32_t, LogEntry> logHistory; // Map för att lagra logghistorik
    const std::vector<int> suppressionThresholds = {10, 100, 1000};
    const unsigned long suppressionPeriod = 86400000; // 1d i millisekunder

    InfluxDBClient &influxClient;
    AmIOnline influxBreaker;

    // Skydd mot att fylla hela flashen om nätverket är nere länge.
    static const size_t maxPendingFileSizeBytes = 200UL * 1024UL; // 200 KB
    static const size_t maxLinesPerBatch = 200;
    uint32_t droppedPointsCount = 0;

    bool checkSDStatus();

    bool logToFile(const char *timestamp,
                   const String &message,
                   LogLevel level);

    Point makePoint(const char *timestamp,
                    const String &message,
                    LogLevel level);

    void savePointToLittleFS(Point &logPoint,
                             time_t &nowTime);

    void reportInfluxStateChangeIfAny();

    const char *levelToString(LogLevel level);

    uint16_t shouldReport(const String &message);

    uint32_t simpleHash(const String &message); // Hash-funktion
};