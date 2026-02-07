# Multi-Device Configuration System - Developer Guide

## Quick Start

### Accessing Configuration (Thread-Safe)

```c
#include "app_config.h"

// Always use lock/unlock when reading or writing g_app_config
if (app_config_lock() == ESP_OK) {
    // Read configuration values
    bool cards_enabled = g_app_config.enable_cards;
    int gpio_pin = g_app_config.rc522_pin_mosi;
    
    app_config_unlock();
    
    // Use the values
    if (cards_enabled) {
        gpio_set_level(gpio_pin, 1);
    }
}
```

### Modifying Configuration

```c
if (app_config_lock() == ESP_OK) {
    // Modify configuration
    g_app_config.enable_cards = true;
    g_app_config.rc522_pin_mosi = 11;
    
    app_config_unlock();
    
    // Save to NVS (this will re-lock internally)
    esp_err_t err = app_config_save();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save config");
    }
}
```

### Safe String Copy

```c
// DON'T do this:
strncpy(g_app_config.device_name, "New Name", sizeof(g_app_config.device_name) - 1);

// DO this instead:
app_config_safe_str_copy(g_app_config.device_name, "New Name", 
                         sizeof(g_app_config.device_name));
```

### Initialization in main()

```c
void app_main(void) {
    // 1. Init NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    
    // 2. Init config mutex BEFORE loading
    app_config_init_mutex();
    
    // 3. Load config from NVS
    ESP_ERROR_CHECK(app_config_load());
    
    // 4. Now safe to use configuration
    // ...
}
```

## MQTT Commands

### getConfig
**Request:**
```json
{
  "action": "getConfig",
  "idPeticion": "req-12345"
}
```

**Response:**
```json
{
  "action": "retornoConfig",
  "id": "SFTCLUB_DEVICE",
  "idPeticion": "req-12345",
  "version": 2,
  "deviceId": "SFTCLUB_DEVICE",
  "deviceName": "Default Device",
  "enableCards": false,
  "enableQr": true,
  "enableWifi": true,
  "enableMqtt": true,
  "wifiSsid": "MyNetwork",
  "mqttHost": "mqtt.example.com",
  "mqttPort": 1883,
  "mqttUser": "admin",
  "mqttTopicRoot": "/var/deploys/topics",
  "gpioRc522": {
    "mosi": 11, "miso": 13, "sck": 12,
    "ss1": 10, "rst1": 16,
    "ss2": 15, "rst2": 17
  },
  "tornInPin": 19,
  "tornOutPin": 20,
  "buzzerPin": 21,
  "gpioQr": {
    "tx": 17, "rx": 18, "uartNum": 1, "baudRate": 9600
  }
}
```

### setConfig
**Request:**
```json
{
  "action": "setConfig",
  "idPeticion": "req-12346",
  "config": {
    "deviceName": "Entry Gate",
    "enableCards": true,
    "wifiSsid": "NewNetwork",
    "wifiPass": "SecurePassword123",
    "mqttHost": "mqtt.newserver.com",
    "mqttPort": 1883,
    "gpioRc522": {
      "mosi": 11,
      "ss1": 10
    }
  }
}
```

**Response:**
```json
{
  "action": "retornoSetConfig",
  "ok": true,
  "message": "Config saved",
  "needsRestart": true,
  "enableCards": true,
  "enableQr": true,
  "idPeticion": "req-12346",
  "id": "SFTCLUB_DEVICE"
}
```

**Note:** If `needsRestart` is `true`, a device restart is required for changes to take effect.

### resetConfig
**Request:**
```json
{
  "action": "resetConfig",
  "idPeticion": "req-12347"
}
```

**Response:**
```json
{
  "action": "retornoResetConfig",
  "ok": true,
  "message": "Config reset to defaults",
  "idPeticion": "req-12347",
  "id": "SFTCLUB_DEVICE"
}
```

## Input Validation

### Valid Ranges
- **GPIO pins**: 0-48 (ESP32-S3)
- **MQTT port**: 1-65535
- **UART number**: 0-2
- **Baud rate**: > 0

### Invalid Values
Invalid values are **silently rejected** with a warning log. The field retains its previous value.

**Example:**
```
W (12345) CMD: Invalid MQTT port: 99999 (ignored)
```

## Thread Safety Rules

### ✅ DO
- Always use `app_config_lock()` / `app_config_unlock()` when accessing `g_app_config`
- Keep lock duration short (< 100ms)
- Use `app_config_safe_str_copy()` for all string copies to config
- Check lock return value

### ❌ DON'T
- Don't access `g_app_config` without locking
- Don't hold the lock across blocking operations (network I/O, long delays)
- Don't call `app_config_save()` while holding the lock (it locks internally)
- Don't use raw `strncpy()` - use `app_config_safe_str_copy()`

## Common Pitfalls

### 1. Reading config without lock
```c
// ❌ WRONG
if (g_app_config.enable_cards) {
    init_rc522();
}

// ✅ CORRECT
bool enable_cards = false;
if (app_config_lock() == ESP_OK) {
    enable_cards = g_app_config.enable_cards;
    app_config_unlock();
}
if (enable_cards) {
    init_rc522();
}
```

### 2. Holding lock too long
```c
// ❌ WRONG
app_config_lock();
do_network_request();  // Long operation
g_app_config.value = response;
app_config_unlock();

// ✅ CORRECT
int response = do_network_request();
app_config_lock();
g_app_config.value = response;
app_config_unlock();
```

### 3. Forgetting null termination
```c
// ❌ WRONG
strncpy(dest, src, sizeof(dest) - 1);

// ✅ CORRECT
app_config_safe_str_copy(dest, src, sizeof(dest));
```

## Configuration Persistence

### When Config is Saved
- On first boot (default config)
- After `setConfig` MQTT command
- After `resetConfig` MQTT command
- After version migration

### When Config is Loaded
- Once during `app_main()` startup
- After migration, saved immediately

### Flash Wear Concern
- Each save writes to NVS (flash)
- Flash has limited write cycles (~10K-100K)
- **Recommendation**: Don't call setConfig in tight loops or repeatedly

## Version Migration

### Current Version: 2

When config structure changes:
1. Increment `CFG_VERSION` in `app_config.c`
2. Update migration logic in `app_config_load()` to preserve user fields
3. Test migration path from previous version

### Migration Behavior
- Old version detected → migrate to new version
- Preserved fields: device ID/name, WiFi, MQTT, feature enables
- New fields: Set to defaults
- Auto-saves migrated config

## Debugging

### Enable Config Logs
```
idf.py menuconfig
→ Component config → Log output → Default log level → Info/Debug
```

### Key Log Messages
```
I (123) APP_CFG: Config mutex initialized
I (234) APP_CFG: Config loaded from NVS (version 2)
I (345) APP_CFG: Config saved to NVS
W (456) APP_CFG: Config version mismatch (stored:1, expected:2), migrating...
I (567) APP_CFG: Config migrated to version 2
```

### Lock Timeout
If you see:
```
E (789) APP_CFG: Failed to acquire config lock
```
This means:
- Another task is holding the lock for > 1 second (deadlock?)
- Or `app_config_init_mutex()` was not called

## Testing

### Unit Test Template
```c
void test_config_thread_safety(void) {
    app_config_init_mutex();
    
    // Spawn 2 tasks that read/write config
    xTaskCreate(task_reader, "reader", 2048, NULL, 5, NULL);
    xTaskCreate(task_writer, "writer", 2048, NULL, 5, NULL);
    
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    // Verify no corruption
    assert(strlen(g_app_config.device_id) < 32);
}
```

### Integration Test Checklist
- [ ] Save config → reboot → verify loaded correctly
- [ ] Set invalid GPIO → verify rejected
- [ ] Concurrent reads from multiple tasks
- [ ] WiFi credential change + restart → verify connection
- [ ] MQTT broker change + restart → verify connection

## Performance Notes

### Lock Overhead
- Mutex take/give: ~1-2 μs
- Minimal impact on system performance
- Stack usage: ~32 bytes

### NVS Write Time
- ~10-50 ms depending on partition size
- Blocking operation
- Should not be called from ISR

### Memory Usage
- Config struct: ~600 bytes (global)
- Mutex handle: ~100 bytes
- No dynamic allocation

## Future Enhancements

### Planned
1. **Write coalescing**: Delay NVS writes by 5-10s to reduce flash wear
2. **Auto-restart**: Automatically reboot after critical config changes
3. **Config validation API**: Public validation functions for all fields

### Under Consideration
1. **TLS for MQTT**: Secure credential transmission
2. **Wi-Fi provisioning**: BLE/SoftAP based setup instead of hardcoded credentials
3. **Config backup**: Save previous version before migration
4. **Config checksum**: CRC32 to detect corruption

## Support

For questions or issues:
1. Check logs for error messages
2. Verify mutex initialization
3. Check NVS partition size (should be ≥ 4KB)
4. Review thread safety rules above

