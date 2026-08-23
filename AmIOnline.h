/**
 * @brief Generisk circuit breaker för nätverksintegrationer på inbyggda system.
 *
 * Håller reda på om en extern tjänst (t.ex. InfluxDB) verkar vara nåbar,
 * baserat på faktiska resultat av skickningsförsök - inte på länkstatus
 * (WiFi.status()). Syftet är att undvika att upprepade gånger blockera
 * huvudloOFFLINE med anrop som ändå kommer tima ut när tjänsten är nere.
 *
 * Tillstånd:
 *  - ONLINE:     Allt fungerar, försök skicka som vanligt.
 *  - OFFLINE:       Vi har sett tillräckligt många fel i rad. Vi vägrar nya
 *                försök helt tills cooldown-perioden har passerat.
 *  - DUNNO:  Cooldown har passerat - vi tillåter ETT provförsök för
 *                att se om tjänsten är tillbaka.
 *
 * Klassen är fristående (bara beroende av millis()) och kan återanvändas
 * för flera olika nätverksintegrationer - instansiera en per extern tjänst.
 */

#pragma once

#include <Arduino.h>

class AmIOnline
{
public:
    enum class State
    {
        ONLINE,
        OFFLINE,
        DUNNO
    };

    /**
     * @param failureThreshold Antal misslyckanden i rad innan vi går till OFFLINE.
     * @param cooldownMs       Hur länge vi väntar i OFFLINE innan vi tillåter ett nytt provförsök.
     * @param name             Valfritt namn för diagnostik (t.ex. "InfluxDB").
     */
    AmIOnline(uint8_t failureThreshold = 3,
                   unsigned long cooldownMs = 5UL * 60UL * 1000UL,
                   const char *name = "")
        : failureThreshold(failureThreshold),
          cooldownMs(cooldownMs),
          name(name)
    {
    }

    /**
     * Fråga om det är okej att försöka skicka just nu.
     * Anropa denna INNAN du gör det faktiska (blockerande) nätverksanropet.
     */
    bool canAttempt()
    {
        if (state == State::ONLINE)
            return true;

        if (state == State::OFFLINE)
        {
            if (millis() - lastFailureTime >= cooldownMs)
            {
                state = State::DUNNO;
                return true;
            }
            return false;
        }

        // DUNNO: vi har redan gett grönt ljus för provförsöket.
        return true;
    }

    /** Anropas efter ett LYCKAT skickningsförsök. */
    void recordSuccess()
    {
        if (state != State::ONLINE)
            stateChanged = true;

        state = State::ONLINE;
        consecutiveFailures = 0;
    }

    /** Anropas efter ett MISSLYCKAT skickningsförsök. */
    void recordFailure()
    {
        consecutiveFailures++;
        lastFailureTime = millis();

        if (state == State::DUNNO)
        {
            // Provförsöket misslyckades - vänta ännu en cooldown-period.
            state = State::OFFLINE;
            stateChanged = true;
            return;
        }

        if (state == State::ONLINE && consecutiveFailures >= failureThreshold)
        {
            state = State::OFFLINE;
            stateChanged = true;
        }
    }

    State getState() const { return state; }

    /**
     * Har tillståndet ändrats sen sist detta anropades? Returnerar sant
     * en gång per övergång (flaggan nollställs vid läsning), så att
     * anroparen kan logga/rapportera övergångar utan att spamma.
     */
    bool consumeStateChangeFlag()
    {
        bool v = stateChanged;
        stateChanged = false;
        return v;
    }

    /** Hur många ms kvar tills nästa provförsök tillåts (0 om det redan är okej). */
    unsigned long getMsUntilRetry() const
    {
        if (state != State::OFFLINE)
            return 0;
        unsigned long elapsed = millis() - lastFailureTime;
        return elapsed >= cooldownMs ? 0 : cooldownMs - elapsed;
    }

    uint8_t getConsecutiveFailures() const { return consecutiveFailures; }
    const char *getName() const { return name; }

private:
    uint8_t failureThreshold;
    unsigned long cooldownMs;
    const char *name;

    State state = State::ONLINE;
    uint8_t consecutiveFailures = 0;
    unsigned long lastFailureTime = 0;
    bool stateChanged = false;
};
