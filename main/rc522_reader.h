// rc522_reader.h
#pragma once

#include "esp_err.h"

// Inicializa bus SPI + dispositivos RC522 (2 lectores)
esp_err_t pn532_reader_init(void);

// Arranca la task que va leyendo los dos lectores RC522
void pn532_reader_start_task(void);

void rc522_access_gate_release(void);

bool rc522_write_card_out_block8(const char *user_text,
                                 char *uid_hex_out,
                                 size_t uid_hex_out_size,
                                 uint32_t timeout_ms);