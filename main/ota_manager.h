// ota_manager.h
#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Lanza la OTA en una tarea aparte.
// url_firmware: URL completa del .bin (http://... o https://...)
// id_peticion: opcional, se reenvía en el retorno MQTT
bool ota_start_async(const char *url_firmware, const char *id_peticion);

#ifdef __cplusplus
}
#endif
