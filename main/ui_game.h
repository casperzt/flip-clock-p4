#pragma once

#include <stdbool.h>
#include <stdint.h>

void ui_game_init(void);
void ui_game_show(bool on);
bool ui_game_visible(void);

/* 由 OSD（nofrendo）每帧调用：256x240 8-bit 索引 + 256 色调色板（RGB565） */
void ui_game_blit(const uint8_t *fb, const uint16_t *pal);

/* 当前按键位图（1=按下）：0 select,1 start,2 up,3 right,4 down,5 left,6 A,7 B */
int ui_game_input(void);
