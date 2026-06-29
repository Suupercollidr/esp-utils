#pragma once
#include <Arduino.h>

class Debounce
{
public:
    Debounce(uint32_t delayMs = 50) : _delayMs(delayMs), _lastTrigger(0) {}

    bool ready()
    {
        if (millis() - _lastTrigger >= _delayMs)
        {
            _lastTrigger = millis();
            return true;
        }
        return false;
    }

    bool ready(uint32_t now) // ISR-säker variant om du skickar in en cachad tid
    {
        if (now - _lastTrigger >= _delayMs)
        {
            _lastTrigger = now;
            return true;
        }
        return false;
    }

private:
    uint32_t _delayMs;
    volatile uint32_t _lastTrigger;
};