// mqtt_manager.h
#pragma once

#include "esp_err.h"
#include <stdbool.h>

bool mqtt_enqueue(const char *topic,
                  const char *payload,
                  int qos,
                  int retain);

void mqtt_start(void);
void mqtt_start_tasks(void);

bool rc522_last_in_ok(void);
bool rc522_last_out_ok(void);
