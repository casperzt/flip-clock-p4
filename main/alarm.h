#pragma once

#include <stdbool.h>

void alarm_init(void);

/* 设置每日闹钟，成功返回 true */
bool alarm_set(int hour, int minute);
void alarm_clear(void);
bool alarm_get(int *hour, int *minute);

bool alarm_is_ringing(void);
void alarm_stop(void);
