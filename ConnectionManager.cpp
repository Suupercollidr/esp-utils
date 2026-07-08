#include "ConnectionManager.h"

ConnectionManager::ConnectionManager(EventLogger &logger,
                                     IPAddress primaryDNS, IPAddress secondaryDNS,
                                     uint32_t reconnectIntervalMs,
                                     uint32_t healthCheckIntervalMs,
                                     uint8_t maxPingFails)
    : _logger(logger), _primaryDNS(primaryDNS), _secondaryDNS(secondaryDNS),
      _reconnectInterval(reconnectIntervalMs),
      _healthCheckInterval(healthCheckIntervalMs),
      _initialConnectTimeout(30000),
      _maxPingFails(maxPingFails)
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
        return;
    }

    if (_healthCheckInterval.ready())
        checkHealth();
}

void ConnectionManager::checkHealth()
{
    if (Ping.ping(_primaryDNS) || Ping.ping(_secondaryDNS))
    {
        _pingFails = 0;
        return;
    }

    _logger.log("Inget svar från DNS-servrar", EventLogger::LogLevel::WARNING);
    if (++_pingFails > _maxPingFails)
        restart("Upprepade DNS-fel");
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