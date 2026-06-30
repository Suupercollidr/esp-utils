
/**
 * @brief Logs events to InfluxDB, a CSV file, and prints them to serial
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
                const char *logFileName = "/system.log");

    void log(const String &message,
             LogLevel level = LogLevel::ERROR,
             bool alwaysReport = false);

private:
    int8_t sdDetectPin;
    int lastSdDetectState = -1;
    bool sdAvailable = false;
    const char *logFileName;
    struct LogEntry
    {
        unsigned long lastTime; // Tidpunkt för senaste loggningen
        int count;              // Antal upprepningar
    };
    std::unordered_map<uint32_t, LogEntry> logHistory; // Map för att lagra logghistorik
    const std::vector<int> suppressionThresholds = {10, 100, 1000};
    const unsigned long suppressionPeriod = 86400000; // 1d i millisekunder

    InfluxDBClient &influxClient;

    bool checkSDStatus();

    bool logToFile(const char *timestamp,
                   const String &message,
                   LogLevel level);

    bool logToInfluxDB(const char *timestamp,
                       const String &message,
                       LogLevel level);

    const char *levelToString(LogLevel level);

    uint16_t shouldReport(const String &message);

    uint32_t simpleHash(const String &message); // Hash-funktion
};