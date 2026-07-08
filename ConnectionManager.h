#pragma once
#include <ESP32Ping.h>
#include "EventLogger.h"
#include "debounce.h"

class ConnectionManager {
public:
    ConnectionManager(EventLogger& logger,
                       IPAddress primaryDNS, IPAddress secondaryDNS,
                       uint32_t reconnectIntervalMs = 30000,
                       uint32_t healthCheckIntervalMs = 30000,
                       uint8_t maxPingFails = 5);

    void begin(const char* ssid, const char* password, const char* hostname);
    void loop();
    bool isConnected() const { return WiFi.isConnected(); }

private:
    void checkHealth();
    void restart(const String& reason);
    void logConnectionInfo();

    EventLogger& _logger;
    IPAddress _primaryDNS, _secondaryDNS;
    Debounce _reconnectInterval;
    Debounce _healthCheckInterval;
    Debounce _initialConnectTimeout;
    uint8_t _pingFails = 0;
    uint8_t _maxPingFails;
};