#ifndef FONT_H
#define FONT_H

#include <stdint.h>

/*
 * OLED使用的两个小字库。6x8字模按列存储，8x16数字按行存储。
 * 字模数据放在font.c，使用OLED接口时不需要直接访问数组。
 */
#define FONT6X8_WIDTH   6U
#define FONT6X8_HEIGHT  8U
#define FONT8X16_WIDTH  8U
#define FONT8X16_HEIGHT 16U

/* 返回字符或数字对应字模的只读指针。 */
const uint8_t *Font6x8_GetGlyph(char character);
const uint8_t *Font8x16_GetDigit(uint8_t digit);

#endif
