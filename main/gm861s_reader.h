// gm861s_reader.h
#pragma once
#include "esp_err.h"

esp_err_t gm861s_reader_init(void);
void gm861s_reader_start_task(void);