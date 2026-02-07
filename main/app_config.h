// app_config.h
#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    bool enable_cards;      // habilitar lector RC522
    int  version;           // para futuras migraciones de config
    bool enable_qr;
    // aquí puedes ir añadiendo cosas por dispositivo:
    // int  sitio_id;
    // char zona[32];
} app_config_t;

extern app_config_t g_app_config;

esp_err_t app_config_load(void);
esp_err_t app_config_save(void);
void      app_config_set_defaults(void);
