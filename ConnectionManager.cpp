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
}

void ConnectionManager::loop()
{
    if (!WiFi.isConnected())
    {
        if (_state != ConnState::DISCONNECTED)
            transitionTo(ConnState::DISCONNECTED, "WiFi frånkopplad");

        if (_reconnectInterval.ready())
        {
            _logger.log("Återansluter till WiFi", EventLogger::LogLevel::INFO);
            WiFi.reconnect();
        }
        registerFailure();
        return;
    }

    if (_state == ConnState::DISCONNECTED)
        transitionTo(ConnState::WIFI_ONLY, "WiFi ansluten, försöker kontakta DNS-server");

    if (!_healthCheckInterval.ready())
        return;

    if (Ping.ping(_primaryDNS, 1) || Ping.ping(_secondaryDNS, 1))
    {
        _downtimeTimeout.reset();
        if (_state != ConnState::CONNECTED)
            transitionTo(ConnState::CONNECTED, "Svar från DNS-server");

        return;
    }

    registerFailure();
}

void ConnectionManager::transitionTo(ConnState newState, const String &reason)
{
    _state = newState;
    _logger.log(reason, EventLogger::LogLevel::INFO);

    if (newState == ConnState::CONNECTED)
    {
        logConnectionInfo();
        if (_onInternetUp)
            _onInternetUp();
        return;
    }

    if (_onInternetDown)
        _onInternetDown();
}

void ConnectionManager::registerFailure()
{
    if (_downtimeTimeout.ready())
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
    netStat.addTag("Hostname", WiFi.getHostname());
    netStat.addField("Channel", WiFi.channel());
    netStat.addField("IP address", WiFi.localIP().toString());
    netStat.addField("Gateway", WiFi.gatewayIP().toString());
    netStat.addField("MAC address", WiFi.macAddress());

    _logger.writePoint(netStat);
}