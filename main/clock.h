#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

void clock_init(void);
bool clock_is_set(void);
void clock_set_epoch(uint32_t epoch);
int clock_get(struct tm *out);
