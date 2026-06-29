#pragma once

#include <Arduino.h>
#include <esp_system.h>   // esp_reset_reason_t
#include <nvs.h>
#include <nvs_flash.h>

// Init NVS (ska köras i setup)
inline void initNvs()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        nvs_flash_init();
    }
}

// Spara en sträng till NVS
inline void storeDataToNvs(const char *key, const char *value)
{
    nvs_handle_t handle;
    if (nvs_open("storage", NVS_READWRITE, &handle) == ESP_OK)
    {
        nvs_set_str(handle, key, value);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

// Läs en sträng från NVS
inline String readDataFromNvs(const char *key)
{
    nvs_handle_t handle;
    size_t required_size = 0;

    if (nvs_open("storage", NVS_READONLY, &handle) != ESP_OK)
        return "";

    // Först hämta storlek
    if (nvs_get_str(handle, key, nullptr, &required_size) != ESP_OK)
    {
        nvs_close(handle);
        return "";
    }

    char buffer[required_size];
    nvs_get_str(handle, key, buffer, &required_size);
    nvs_close(handle);

    return String(buffer);
}

// Returnerar senaste resetorsak som sträng
inline const char* getResetReason(esp_reset_reason_t reason)
{
    switch (reason)
    {
        case ESP_RST_UNKNOWN:     return "Unknown";
        case ESP_RST_POWERON:     return "Power on";
        case ESP_RST_EXT:         return "External reset";
        case ESP_RST_SW:          return "Software reset";
        case ESP_RST_PANIC:       return "Software panic";
        case ESP_RST_INT_WDT:     return "Interrupt watchdog";
        case ESP_RST_TASK_WDT:    return "Task watchdog";
        case ESP_RST_WDT:         return "Other watchdogs";
        case ESP_RST_DEEPSLEEP:   return "Deep sleep";
        case ESP_RST_BROWNOUT:    return "Brownout";
        case ESP_RST_SDIO:        return "SDIO";
        default:                  return "Unknown reset reason";
    }
}
