/*
 * SSD1306兼容OLED驱动
 * 硬件：128x64，I2C4，7位地址0x3C。
 * 最小用法：OLED_Init(); OLED_Clear(); OLED_ShowString(...); OLED_Refresh();
 */
#include "oled.h"

#include <string.h>
#include "font.h"
#include "i2c.h"

#define OLED_PAGE_COUNT      (OLED_HEIGHT / 8U)
#define OLED_BUFFER_SIZE     (OLED_WIDTH * OLED_PAGE_COUNT)
#define OLED_TIMEOUT_MS      100U
#define OLED_CONTROL_COMMAND 0x00U
#define OLED_CONTROL_DATA    0x40U

uint8_t OLED_Ready;

/*
 * SSD1306 把 128x64 像素分成 8 个 Page，每个 Page 高 8 像素、宽 128 字节，
 * 所以完整画面需要 8*128=1024 字节。
 * oled_buffer 是准备显示的新画面；oled_shadow 是上次已经发到屏幕的画面。
 */
static uint8_t oled_buffer[OLED_BUFFER_SIZE];
static uint8_t oled_shadow[OLED_BUFFER_SIZE];
static uint8_t oled_shadow_valid;

static uint8_t OLED_WriteCommand(uint8_t command)
{
  /* 控制字节 0x00 表示后一个字节是命令；发送像素数据时使用 0x40。 */
  uint8_t packet[2] = {OLED_CONTROL_COMMAND, command};

  /* HAL 接口要求传入左移一位后的地址，OLED_ADDRESS 本身仍写 7 位地址。 */
  if (HAL_I2C_Master_Transmit(&hi2c4, OLED_ADDRESS << 1U, packet,
                              sizeof(packet), OLED_TIMEOUT_MS) != HAL_OK)
  {
    OLED_Ready = 0U;
    return 0U;
  }
  return 1U;
}

void OLED_Init(void)
{
  /*
   * 初始化命令依次设置：关闭显示、时钟、扫描范围、偏移、电荷泵、
   * 页寻址、扫描方向、COM 引脚、对比度和显示模式。最后再发送 0xAF 开屏。
   */
  static const uint8_t commands[] =
  {
    0xAE, 0xD5,0x80, 0xA8,0x3F, 0xD3,0x00, 0x40,
    0x8D,0x14, 0x20,0x02, 0xA1, 0xC8, 0xDA,0x12,
    0x81,0xCF, 0xD9,0xF1, 0xDB,0x30, 0xA4, 0xA6
  };
  uint32_t i;

  OLED_Ready = 0U;
  oled_shadow_valid = 0U;
  if (HAL_I2C_IsDeviceReady(&hi2c4, OLED_ADDRESS << 1U, 3U,
                            OLED_TIMEOUT_MS) != HAL_OK)
  {
    return;
  }

  OLED_Ready = 1U;
  HAL_Delay(100U); /* 模块上电后需要等待内部电源稳定。 */
  for (i = 0U; i < sizeof(commands); ++i)
  {
    if (!OLED_WriteCommand(commands[i]))
    {
      return;
    }
  }

  OLED_Clear();
  if (!OLED_Refresh())
  {
    return;
  }
  (void)OLED_WriteCommand(0xAFU);
}

void OLED_Clear(void)
{
  OLED_Fill(0x00U);
}

void OLED_Fill(uint8_t value)
{
  memset(oled_buffer, value, sizeof(oled_buffer));
}

void OLED_DrawPixel(uint8_t x, uint8_t y, uint8_t on)
{
  uint32_t index;
  uint8_t mask;

  if ((x >= OLED_WIDTH) || (y >= OLED_HEIGHT))
  {
    return;
  }
  /* y/8 选择 Page，y%8 选择该字节中的一位。 */
  index = ((uint32_t)y / 8U) * OLED_WIDTH + x;
  mask = (uint8_t)(1U << (y & 7U));
  if (on)
  {
    oled_buffer[index] |= mask;
  }
  else
  {
    oled_buffer[index] &= (uint8_t)~mask;
  }
}

void OLED_ShowString(uint8_t x, uint8_t y, const char *text)
{
  if (text == NULL)
  {
    return;
  }

  while ((*text != '\0') && (x <= (OLED_WIDTH - FONT6X8_WIDTH)) &&
         (y <= (OLED_HEIGHT - FONT6X8_HEIGHT)))
  {
    /* 6x8 字模按列存放，一字节的 bit0 对应这一列最上面的像素。 */
    const uint8_t *glyph = Font6x8_GetGlyph(*text++);
    uint8_t column;
    uint8_t row;

    for (column = 0U; column < FONT6X8_WIDTH; ++column)
    {
      for (row = 0U; row < FONT6X8_HEIGHT; ++row)
      {
        OLED_DrawPixel((uint8_t)(x + column), (uint8_t)(y + row),
                       (uint8_t)((glyph[column] & (1U << row)) != 0U));
      }
    }
    x = (uint8_t)(x + FONT6X8_WIDTH);
  }
}

void OLED_ShowNumber16(uint8_t x, uint8_t y, uint32_t value, uint8_t digits)
{
  uint32_t divisor = 1U;
  uint8_t digit_index;

  if ((digits == 0U) || (digits > 10U))
  {
    return;
  }
  for (digit_index = 1U; digit_index < digits; ++digit_index)
  {
    divisor *= 10U;
  }

  for (digit_index = 0U; digit_index < digits; ++digit_index)
  {
    /* 8x16 数字字模按行存放，最高位对应这一行最左边的像素。 */
    const uint8_t *glyph =
        Font8x16_GetDigit((uint8_t)((value / divisor) % 10U));
    uint8_t row;
    uint8_t column;

    for (row = 0U; row < FONT8X16_HEIGHT; ++row)
    {
      for (column = 0U; column < FONT8X16_WIDTH; ++column)
      {
        OLED_DrawPixel((uint8_t)(x + column), (uint8_t)(y + row),
                       (uint8_t)((glyph[row] & (0x80U >> column)) != 0U));
      }
    }
    x = (uint8_t)(x + FONT8X16_WIDTH);
    divisor = (divisor > 1U) ? divisor / 10U : 1U;
  }
}

void OLED_ShowTestPattern(void)
{
  uint8_t x;
  uint8_t y;

  OLED_Clear();
  for (x = 0U; x < OLED_WIDTH; ++x)
  {
    OLED_DrawPixel(x, 0U, 1U);
    OLED_DrawPixel(x, OLED_HEIGHT - 1U, 1U);
  }
  for (y = 0U; y < OLED_HEIGHT; ++y)
  {
    OLED_DrawPixel(0U, y, 1U);
    OLED_DrawPixel(OLED_WIDTH - 1U, y, 1U);
  }
  OLED_ShowString(14U, 8U, "ASCII 6x8");
  OLED_ShowString(8U, 24U, "G474 STARTER");
  OLED_ShowNumber16(24U, 40U, 1234567890U, 10U);
}

uint8_t OLED_Refresh(void)
{
  uint8_t packet[OLED_WIDTH + 1U];
  uint8_t page;

  if (!OLED_Ready)
  {
    return 0U;
  }
  packet[0] = OLED_CONTROL_DATA;

  /*
   * 逐 Page 比较新旧缓冲，只发送变化的 128 字节。这样菜单静止时不会
   * 反复占用 I2C 总线，也能减轻整屏刷新造成的闪烁。
   */
  for (page = 0U; page < OLED_PAGE_COUNT; ++page)
  {
    uint32_t offset = (uint32_t)page * OLED_WIDTH;

    if (oled_shadow_valid &&
        (memcmp(&oled_buffer[offset], &oled_shadow[offset], OLED_WIDTH) == 0))
    {
      continue;
    }
    /* 0xB0 选择 Page，后两条命令把列地址设置为 0。 */
    if (!OLED_WriteCommand((uint8_t)(0xB0U | page)) ||
        !OLED_WriteCommand(0x00U) || !OLED_WriteCommand(0x10U))
    {
      return 0U;
    }
    memcpy(&packet[1], &oled_buffer[offset], OLED_WIDTH);
    if (HAL_I2C_Master_Transmit(&hi2c4, OLED_ADDRESS << 1U, packet,
                                sizeof(packet), OLED_TIMEOUT_MS) != HAL_OK)
    {
      OLED_Ready = 0U;
      return 0U;
    }
    memcpy(&oled_shadow[offset], &oled_buffer[offset], OLED_WIDTH);
  }
  oled_shadow_valid = 1U;
  return 1U;
}
