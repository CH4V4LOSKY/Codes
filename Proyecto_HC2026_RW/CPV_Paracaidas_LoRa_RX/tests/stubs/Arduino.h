#pragma once
#include <stdint.h>
#include <cstdlib>
inline void vTaskDelay(unsigned) {}
#define pdMS_TO_TICKS(ms) (ms)
