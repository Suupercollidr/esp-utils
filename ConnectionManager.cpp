#include "ConnectionManager.h"

ConnectionManager::ConnectionManager(EventLogger &logger,
                                     IPAddress primaryDNS, IPAddress secondaryDNS,
                                     uint32_t allowedDowntimeBeforeRestartS,
                                     uint32_t reconnectIntervalS,
                                     uint32_t healthCheckIntervalS)
    : _logger(logger), _primaryDNS(primaryDNS), _secondaryDNS(secondaryDNS),
      _allowedDowntimeBeforeRestartMs(allowedDowntimeBeforeRestartS * 1000),
      _reconnectInterval(reconnectIntervalS * 1000),
      _healthCheckInterval(healthCheckIntervalS * 1000),
      _initialConnectTimeout(30000)
{
}

void ConnectionManager::begin(const char *ssid, const char *password, const char *hostname, const int32_t channel)
{
    _logger.log("Ansluter till WiFi " + String(ssid), EventLogger::LogLevel::INFO);

    WiFi.setHostname(hostname);
    WiFi.begin(ssid, password, channel);

    while (!WiFi.isConnected())
    {
        if (_initialConnectTimeout.ready())
            restart("Kunde inte ansluta till WiFi vid uppstart");
        delay(100);
        Serial.print("🛜  ");
    }
    Serial.println();
    logConnectionInfo();
}

void ConnectionManager::loop()
{
    if (!WiFi.isConnected())
    {
        if (_reconnectInterval.ready())
        {
            _logger.log("Återansluter till WiFi", EventLogger::LogLevel::INFO);
            WiFi.reconnect();
        }
        registerFailure();
        return;
    }

    if (!_healthCheckInterval.ready())
        return;

    if (Ping.ping(_primaryDNS, 1) || Ping.ping(_secondaryDNS, 1))
    {
        _disconnectedSinceMs = 0;
        return;
    }

    registerFailure();
}

void ConnectionManager::registerFailure()
{
    if (_disconnectedSinceMs == 0)
        _disconnectedSinceMs = millis();

    if (millis() - _disconnectedSinceMs > _allowedDowntimeBeforeRestartMs)
        restart("Ingen fungerande anslutning under en längre period");
}

void ConnectionManager::restart(const String &reason)
{
    _logger.log(reason + ", startar om", EventLogger::LogLevel::ERROR);
    delay(1000);
    ESP.restart();
}

void ConnectionManager::logConnectionInfo()
{
    _logger.log("Ansluten till WiFi", EventLogger::LogLevel::INFO);

    Point netStat("Network");
    netStat.addField("Channel", WiFi.channel());
    netStat.addField("Hostname", WiFi.getHostname());
    netStat.addField("IP address", WiFi.localIP().toString());
    netStat.addField("Gateway", WiFi.gatewayIP().toString());
    netStat.addField("MAC address", WiFi.macAddress());

    _logger.writePoint(netStat);
}