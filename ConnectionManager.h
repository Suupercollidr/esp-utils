#pragma once
#include <WiFi.h>
#include <ESP32Ping.h>
#include "EventLogger.h"
#include "debounce.h"

class ConnectionManager
{
public:
    enum class ConnState
    {
        DISCONNECTED, // Ingen WiFi
        WIFI_ONLY,    // WiFi anslutet, men DNS-servrar svarar inte på ping
        CONNECTED     // WiFi uppe OCH DNS svarar
    };

    using StateCallback = std::function<void()>;

    ConnectionManager(EventLogger &logger,
                      IPAddress primaryDNS, IPAddress secondaryDNS,
                      uint32_t allowedDowntimeBeforeRestartS = 600,
                      uint32_t reconnectIntervalS = 30,
                      uint32_t healthCheckIntervalS = 30);

    void begin(const char *ssid, const char *password, const char *hostname, const int32_t channel = 0);
    void loop();
    bool isConnected() const { return WiFi.isConnected(); }
    void onInternetAvailable(StateCallback callBack) { _onInternetUp = callBack; }
    void onInternetDown(StateCallback callBack) { _onInternetDown = callBack; }
    bool connectedToWifi() const { return WiFi.isConnected(); }
    bool connectedToInternet() const { return _state == ConnState::CONNECTED; }
    ConnState state() const { return _state; }

private:
    void restart(const String &reason);
    void logConnectionInfo();
    void registerFailure();
    void transitionTo(ConnState newState, const String &reason);

    EventLogger &_logger;
    IPAddress _primaryDNS, _secondaryDNS;
    uint32_t _allowedDowntimeBeforeRestartMs;
    Debounce _reconnectInterval;
    Debounce _healthCheckInterval;
    Debounce _initialConnectTimeout;
    Debounce _downtimeTimeout;
    ConnState _state = ConnState::DISCONNECTED;
    StateCallback _onInternetUp;
    StateCallback _onInternetDown;
};