#ifndef OLED_H
#define OLED_H

#include <stdint.h>

#define OLED_WIDTH   128U
#define OLED_HEIGHT  64U
#define OLED_ADDRESS 0x3CU

/*
 * 使用顺序：OLED_Init -> OLED_Clear/绘图 -> OLED_Refresh。
 * 绘图函数只修改RAM中的显存，必须调用OLED_Refresh后屏幕才会变化。
 */

/* 为1表示地址0x3C已经应答，并且初始化命令发送成功。 */
extern uint8_t OLED_Ready;

/* 初始化SSD1306；失败时OLED_Ready保持为0。 */
void OLED_Init(void);
/* 清空RAM显存，不会立刻刷新物理屏幕。 */
void OLED_Clear(void);
/* 用同一个字节填充显存，0x00全灭、0xFF全亮。 */
void OLED_Fill(uint8_t value);
/* 设置或清除一个像素；坐标原点位于屏幕左上角。 */
void OLED_DrawPixel(uint8_t x, uint8_t y, uint8_t on);
/* 使用6x8 ASCII字库绘制以'\0'结尾的字符串。 */
void OLED_ShowString(uint8_t x, uint8_t y, const char *text);
/* 使用8x16数字字库显示无符号数，digits范围为1~10。 */
void OLED_ShowNumber16(uint8_t x, uint8_t y, uint32_t value, uint8_t digits);
void OLED_ShowTestPattern(void);
/* 把发生变化的页面发送到OLED；成功返回1，失败返回0。 */
uint8_t OLED_Refresh(void);

#endif
