#pragma once
#include <WiFi.h>
#include <ESP32Ping.h>
#include "EventLogger.h"
#include "debounce.h"

class ConnectionManager {
public:
    ConnectionManager(EventLogger& logger,
                       IPAddress primaryDNS, IPAddress secondaryDNS,
                       uint32_t allowedDowntimeBeforeRestartS = 600,
                       uint32_t reconnectIntervalS = 30,
                       uint32_t healthCheckIntervalS = 30);

    void begin(const char* ssid, const char* password, const char* hostname, const int32_t channel = 0);
    void loop();
    bool isConnected() const { return WiFi.isConnected(); }

private:
    void restart(const String& reason);
    void logConnectionInfo();
    void registerFailure();

    EventLogger& _logger;
    IPAddress _primaryDNS, _secondaryDNS;
    uint32_t _allowedDowntimeBeforeRestartMs;
    Debounce _reconnectInterval;
    Debounce _healthCheckInterval;
    Debounce _initialConnectTimeout;
    uint32_t _disconnectedSinceMs = 0;
};