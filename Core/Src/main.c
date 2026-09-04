/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "fdcan.h"
#include "i2c.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include "can_demo.h"
#include "encoder.h"
#include "fram.h"
#include "oled.h"
#include "sdcard.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/*
 * 一个枚举只允许 current_mode 同时取一个值，比“每个页面一个标志位”更安全：
 * 不会出现 LED 页面和 CAN 页面两个标志同时为 1 的冲突状态。
 */
typedef enum
{
  MODE_MENU,
  MODE_SYSTEM,
  MODE_LED,
  MODE_KEY,
  MODE_ENCODER,
  MODE_SWITCH,
  MODE_FRAM,
  MODE_SD,
  MODE_UART,
  MODE_CAN,
  MODE_OLED
} DemoMode;

/*
 * PC2 按键消抖状态机。机械触点在按下/松开瞬间会抖动数毫秒，不能把每个
 * GPIO 跳变都当成一次按键。主循环按时间依次确认“稳定按下”和“稳定松开”。
 */
typedef enum
{
  KEY_IDLE,
  KEY_PRESS_DEBOUNCE,
  KEY_PRESSED,
  KEY_RELEASE_DEBOUNCE
} KeyState;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define MENU_ITEM_COUNT       10U
#define MENU_VISIBLE_ITEMS     6U
#define KEY_DEBOUNCE_MS       20U
#define KEY_LONG_PRESS_MS    800U
#define OLED_REFRESH_MS      100U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* 菜单文字和目标页面一一对应；增加菜单项时必须同时修改这两个数组。 */
static const char *const menu_text[MENU_ITEM_COUNT] =
{
  "System Status", "LED Test", "Key Test", "Encoder Test", "DIP Switch",
  "FRAM Test", "SD Card Test", "UART Test", "CAN Test", "OLED Test"
};

static const DemoMode menu_mode[MENU_ITEM_COUNT] =
{
  MODE_SYSTEM, MODE_LED, MODE_KEY, MODE_ENCODER, MODE_SWITCH,
  MODE_FRAM, MODE_SD, MODE_UART, MODE_CAN, MODE_OLED
};

static DemoMode current_mode = MODE_MENU;
static uint8_t menu_index;
/* mode_first_entry 用于执行页面的一次性动作；screen_dirty 请求提前刷新。 */
static uint8_t mode_first_entry = 1U;
static uint8_t screen_dirty = 1U;
static uint32_t screen_tick;

/*
 * key_irq_flag 在中断和主循环之间共享，因此必须用 volatile，防止编译器
 * 假设它不会突然改变。中断只置位，消抖、计时和页面操作都留在主循环。
 */
static volatile uint8_t key_irq_flag;
static uint8_t key_short_event;
static uint8_t key_long_event;
static KeyState key_state = KEY_IDLE;
static uint8_t key_long_sent;
static uint32_t key_change_tick;
static uint32_t key_press_tick;

static int32_t menu_encoder_position;
static uint32_t led_tick;
static uint32_t led_toggle_count;
static uint32_t key_press_count;
static int32_t encoder_previous_raw;
static char encoder_direction = '-';
static uint8_t fram_test_result;
static uint8_t oled_pattern;

/* UART2 回调只保存最新字节；耗时的回显发送在主循环中完成。 */
static uint8_t uart2_rx_byte;
static volatile uint8_t uart2_last_byte;
static volatile uint8_t uart2_rx_pending;
static volatile uint32_t uart2_rx_count;
static uint32_t uart2_tx_count;
static uint32_t uart3_tx_count;
static uint32_t uart3_tick;

static uint32_t can_payload_count;
static uint8_t can_send_result;
static uint8_t system_oled_detected;
static uint8_t system_fram_detected;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void Key_Update(void);
static void Mode_Enter(DemoMode mode);
static uint8_t Display_IsDue(void);
static void Display_Footer(void);
static void Menu_Process(void);
static void System_TestProcess(void);
static void LED_TestProcess(void);
static void Key_TestProcess(void);
static void Encoder_TestProcess(void);
static void Switch_TestProcess(void);
static void FRAM_TestProcess(void);
static void SD_TestProcess(void);
static void UART_TestProcess(void);
static void CAN_TestProcess(void);
static void OLED_TestProcess(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/*
 * 非阻塞周期判断：函数不会停在 HAL_Delay() 等待，而是立即返回。
 * 无符号减法在 HAL_GetTick() 回绕时仍能正确计算较短的时间间隔。
 */
static uint8_t Display_IsDue(void)
{
  uint32_t now = HAL_GetTick();

  if (screen_dirty || ((now - screen_tick) >= OLED_REFRESH_MS))
  {
    screen_dirty = 0U;
    screen_tick = now;
    return 1U;
  }
  return 0U;
}

static void Display_Footer(void)
{
  OLED_ShowString(0U, 56U, "Hold KEY = Back");
}

static void Mode_Enter(DemoMode mode)
{
  /* PB11低电平点亮；离开LED测试时确保灯关闭。 */
  if ((current_mode == MODE_LED) && (mode != MODE_LED))
  {
    HAL_GPIO_WritePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin, GPIO_PIN_SET);
  }

  current_mode = mode;
  /* 新页面在下一次主循环中执行一次初始化，并立即重画。 */
  mode_first_entry = 1U;
  screen_dirty = 1U;
  key_short_event = 0U;

  if (mode == MODE_MENU)
  {
    menu_encoder_position = Encoder_GetCount();
  }
}

static void Key_Update(void)
{
  uint32_t now = HAL_GetTick();
  uint8_t pressed =
      (HAL_GPIO_ReadPin(KEY_GPIO_Port, KEY_Pin) == GPIO_PIN_RESET);

  switch (key_state)
  {
    case KEY_IDLE:
      /* EXTI 下降沿只负责把状态机从空闲状态唤醒。 */
      if (key_irq_flag)
      {
        key_irq_flag = 0U;
        key_change_tick = now;
        key_state = KEY_PRESS_DEBOUNCE;
      }
      break;

    case KEY_PRESS_DEBOUNCE:
      /* 20 ms 后仍为低电平，才承认一次真正的按下。 */
      if (!pressed)
      {
        key_state = KEY_IDLE;
      }
      else if ((now - key_change_tick) >= KEY_DEBOUNCE_MS)
      {
        key_press_tick = now;
        key_long_sent = 0U;
        key_state = KEY_PRESSED;
      }
      break;

    case KEY_PRESSED:
      /* 持续 800 ms 只产生一次长按事件；短按事件要等松手后才产生。 */
      if (!pressed)
      {
        key_change_tick = now;
        key_state = KEY_RELEASE_DEBOUNCE;
      }
      else if (!key_long_sent &&
               ((now - key_press_tick) >= KEY_LONG_PRESS_MS))
      {
        key_long_event = 1U;
        key_long_sent = 1U;
      }
      break;

    case KEY_RELEASE_DEBOUNCE:
      /* 松开也要消抖，避免一次操作被识别成多次短按。 */
      if (pressed)
      {
        key_state = KEY_PRESSED;
      }
      else if ((now - key_change_tick) >= KEY_DEBOUNCE_MS)
      {
        if (!key_long_sent)
        {
          key_short_event = 1U;
        }
        key_state = KEY_IDLE;
      }
      break;

    default:
      key_state = KEY_IDLE;
      break;
  }
}

static void Menu_Process(void)
{
  int32_t encoder_position;
  int32_t movement;
  uint8_t first_item;
  uint8_t row;

  if (mode_first_entry)
  {
    menu_encoder_position = Encoder_GetCount();
    mode_first_entry = 0U;
  }

  /* 只关心本次位置相对上次位置的变化，不要求编码器计数从 0 开始。 */
  encoder_position = Encoder_GetCount();
  movement = encoder_position - menu_encoder_position;
  while (movement > 0)
  {
    menu_index = (uint8_t)((menu_index + 1U) % MENU_ITEM_COUNT);
    --movement;
    screen_dirty = 1U;
  }
  while (movement < 0)
  {
    menu_index = (menu_index == 0U) ?
                 (MENU_ITEM_COUNT - 1U) : (uint8_t)(menu_index - 1U);
    ++movement;
    screen_dirty = 1U;
  }
  menu_encoder_position = encoder_position;

  if (key_short_event)
  {
    Mode_Enter(menu_mode[menu_index]);
    return;
  }

  if (!Display_IsDue())
  {
    return;
  }

  /* 128x64 屏幕一次显示 6 个菜单项，整页切换起始下标。 */
  first_item = (uint8_t)((menu_index / MENU_VISIBLE_ITEMS) *
                         MENU_VISIBLE_ITEMS);
  OLED_Clear();
  OLED_ShowString(0U, 0U, "G474 DEMO MENU");
  for (row = 0U; row < MENU_VISIBLE_ITEMS; ++row)
  {
    uint8_t item = (uint8_t)(first_item + row);
    char line[22];

    if (item >= MENU_ITEM_COUNT)
    {
      break;
    }
    (void)snprintf(line, sizeof(line), "%c %s",
                   (item == menu_index) ? '>' : ' ', menu_text[item]);
    OLED_ShowString(0U, (uint8_t)(8U + row * 8U), line);
  }
  OLED_ShowString(0U, 56U, "Rotate / Press");
  (void)OLED_Refresh();
}

static void System_TestProcess(void)
{
  char line[22];

  /* 首次进入或短按时重新探测；空闲显示时不持续占用 I2C 总线。 */
  if (mode_first_entry || key_short_event)
  {
    system_oled_detected =
        (HAL_I2C_IsDeviceReady(&hi2c4, OLED_ADDRESS << 1U, 2U, 50U) == HAL_OK);
    system_fram_detected = (FRAM_Init() == HAL_OK);
    CAN_Process();
    mode_first_entry = 0U;
    screen_dirty = 1U;
  }

  if (!Display_IsDue())
  {
    return;
  }

  CAN_Process();
  OLED_Clear();
  OLED_ShowString(0U, 0U, "SYSTEM STATUS");
  (void)snprintf(line, sizeof(line), "SYSCLK:%lu MHz",
                 (unsigned long)(HAL_RCC_GetSysClockFreq() / 1000000U));
  OLED_ShowString(0U, 8U, line);
  (void)snprintf(line, sizeof(line), "OLED:%s FRAM:%s",
                 system_oled_detected ? "OK" : "ERR",
                 system_fram_detected ? "OK" : "ERR");
  OLED_ShowString(0U, 16U, line);
  (void)snprintf(line, sizeof(line), "SPI4:%lu kHz",
                 (unsigned long)(SD_GetSpiClock() / 1000U));
  OLED_ShowString(0U, 24U, line);
  OLED_ShowString(0U, 32U, CAN_BusOff ? "CAN:BUS-OFF 500K" :
                                      (CAN_Started ? "CAN:OK 500K" :
                                                     "CAN:INIT ERR"));
  if (SD_FileResult == SD_FILE_NOT_TESTED)
  {
    OLED_ShowString(0U, 40U, "SD:NOT TESTED");
  }
  else
  {
    OLED_ShowString(0U, 40U,
                    (SD_FileResult == SD_FILE_PASS) ? "SD:PASS" : "SD:FAIL");
  }
  OLED_ShowString(0U, 48U, "Press=Rescan");
  Display_Footer();
  (void)OLED_Refresh();
}

static void LED_TestProcess(void)
{
  char line[22];
  uint32_t now = HAL_GetTick();

  if (mode_first_entry)
  {
    HAL_GPIO_WritePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin, GPIO_PIN_SET);
    led_toggle_count = 0U;
    led_tick = now;
    mode_first_entry = 0U;
  }

  /* PB11 低电平点亮。用 Tick 调度闪烁，主循环仍可继续扫描按键。 */
  if ((now - led_tick) >= 500U)
  {
    led_tick = now;
    HAL_GPIO_TogglePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin);
    ++led_toggle_count;
    screen_dirty = 1U;
  }

  if (!Display_IsDue())
  {
    return;
  }

  OLED_Clear();
  OLED_ShowString(0U, 0U, "LED TEST - PB11");
  OLED_ShowString(0U, 16U,
      (HAL_GPIO_ReadPin(STATUS_LED_GPIO_Port, STATUS_LED_Pin) == GPIO_PIN_RESET) ?
      "LED STATE: ON" : "LED STATE: OFF");
  (void)snprintf(line, sizeof(line), "TOGGLE:%lu",
                 (unsigned long)led_toggle_count);
  OLED_ShowString(0U, 32U, line);
  OLED_ShowString(0U, 40U, "Period:500 ms");
  Display_Footer();
  (void)OLED_Refresh();
}

static void Key_TestProcess(void)
{
  char line[22];

  if (mode_first_entry)
  {
    key_press_count = 0U;
    mode_first_entry = 0U;
  }
  if (key_short_event)
  {
    ++key_press_count;
    screen_dirty = 1U;
  }

  if (!Display_IsDue())
  {
    return;
  }

  OLED_Clear();
  OLED_ShowString(0U, 0U, "KEY TEST - PC2");
  OLED_ShowString(0U, 16U, "Active level:LOW");
  (void)snprintf(line, sizeof(line), "Short press:%lu",
                 (unsigned long)key_press_count);
  OLED_ShowString(0U, 32U, line);
  OLED_ShowString(0U, 40U, "Press to count");
  Display_Footer();
  (void)OLED_Refresh();
}

static void Encoder_TestProcess(void)
{
  char line[22];
  int32_t raw;

  if (mode_first_entry)
  {
    encoder_previous_raw = Encoder_GetRawCount();
    encoder_direction = '-';
    mode_first_entry = 0U;
  }
  if (key_short_event)
  {
    Encoder_Reset();
    encoder_previous_raw = 0;
    encoder_direction = '-';
    screen_dirty = 1U;
  }

  raw = Encoder_GetRawCount();
  if (raw != encoder_previous_raw)
  {
    encoder_direction = (raw > encoder_previous_raw) ? '+' : '-';
    encoder_previous_raw = raw;
    screen_dirty = 1U;
  }

  if (!Display_IsDue())
  {
    return;
  }

  OLED_Clear();
  OLED_ShowString(0U, 0U, "ENCODER - TIM1");
  (void)snprintf(line, sizeof(line), "RAW:%ld", (long)raw);
  OLED_ShowString(0U, 16U, line);
  (void)snprintf(line, sizeof(line), "DETENT:%ld",
                 (long)Encoder_GetCount());
  OLED_ShowString(0U, 24U, line);
  (void)snprintf(line, sizeof(line), "DIRECTION:%c", encoder_direction);
  OLED_ShowString(0U, 32U, line);
  OLED_ShowString(0U, 40U, "Press=Zero");
  Display_Footer();
  (void)OLED_Refresh();
}

static void Switch_TestProcess(void)
{
  char line[22];
  uint8_t sw1;
  uint8_t sw2;

  if (mode_first_entry)
  {
    mode_first_entry = 0U;
  }
  /* 板上有外部上拉，因此拨到 ON 时引脚被接地，读到低电平。 */
  sw1 = (HAL_GPIO_ReadPin(SW_1_GPIO_Port, SW_1_Pin) == GPIO_PIN_RESET);
  sw2 = (HAL_GPIO_ReadPin(SW_2_GPIO_Port, SW_2_Pin) == GPIO_PIN_RESET);

  if (!Display_IsDue())
  {
    return;
  }

  OLED_Clear();
  OLED_ShowString(0U, 0U, "DIP SWITCH TEST");
  (void)snprintf(line, sizeof(line), "SW_1:%s", sw1 ? "ON" : "OFF");
  OLED_ShowString(0U, 16U, line);
  (void)snprintf(line, sizeof(line), "SW_2:%s", sw2 ? "ON" : "OFF");
  OLED_ShowString(0U, 24U, line);
  (void)snprintf(line, sizeof(line), "VALUE:0b%c%c",
                 sw2 ? '1' : '0', sw1 ? '1' : '0');
  OLED_ShowString(0U, 40U, line);
  Display_Footer();
  (void)OLED_Refresh();
}

static void FRAM_TestProcess(void)
{
  if (mode_first_entry)
  {
    mode_first_entry = 0U;
  }
  /* 写测试由用户短按触发，上电不会自动改写 FRAM 内容。 */
  if (key_short_event)
  {
    fram_test_result = FRAM_SelfTest() ? 1U : 2U;
    screen_dirty = 1U;
  }

  if (!Display_IsDue())
  {
    return;
  }

  OLED_Clear();
  OLED_ShowString(0U, 0U, "FRAM TEST - 0x50");
  OLED_ShowString(0U, 16U, FRAM_Ready ? "DEVICE:READY" : "DEVICE:ERROR");
  if (fram_test_result == 0U)
  {
    OLED_ShowString(0U, 32U, "RESULT:NOT TESTED");
  }
  else
  {
    OLED_ShowString(0U, 32U,
                    (fram_test_result == 1U) ? "RESULT:PASS" : "RESULT:FAIL");
  }
  OLED_ShowString(0U, 40U, "Press=Run safe test");
  OLED_ShowString(0U, 48U, "Backup then restore");
  Display_Footer();
  (void)OLED_Refresh();
}

static const char *SD_StepText(uint8_t step)
{
  uint8_t failed_step;

  if (SD_FileResult == SD_FILE_NOT_TESTED)
  {
    return "--";
  }
  if (SD_FileResult == SD_FILE_PASS)
  {
    return "PASS";
  }

  failed_step = (uint8_t)(SD_FileResult - 1U);
  if (step < failed_step)
  {
    return "PASS";
  }
  return (step == failed_step) ? "ERR" : "--";
}

static void SD_TestProcess(void)
{
  char line[22];

  if (mode_first_entry)
  {
    mode_first_entry = 0U;
  }
  /* 此操作会覆盖根目录中的 G474_DEMO.TXT，故只在用户短按后执行。 */
  if (key_short_event)
  {
    OLED_Clear();
    OLED_ShowString(0U, 0U, "SD CARD TEST");
    OLED_ShowString(0U, 24U, "Running...");
    (void)OLED_Refresh();
    (void)SD_FileTest();
    screen_dirty = 1U;
  }

  if (!Display_IsDue())
  {
    return;
  }

  OLED_Clear();
  OLED_ShowString(0U, 0U, "SD CARD TEST");
  (void)snprintf(line, sizeof(line), "Init  :%s", SD_StepText(1U));
  OLED_ShowString(0U, 8U, line);
  (void)snprintf(line, sizeof(line), "Mount :%s", SD_StepText(2U));
  OLED_ShowString(0U, 16U, line);
  (void)snprintf(line, sizeof(line), "Write :%s", SD_StepText(3U));
  OLED_ShowString(0U, 24U, line);
  (void)snprintf(line, sizeof(line), "Verify:%s", SD_StepText(4U));
  OLED_ShowString(0U, 32U, line);
  OLED_ShowString(0U, 40U, "File:G474_DEMO.TXT");
  OLED_ShowString(0U, 48U, "Press=Run");
  Display_Footer();
  (void)OLED_Refresh();
}

static void UART_TestProcess(void)
{
  char line[32];
  uint32_t now = HAL_GetTick();

  if (mode_first_entry)
  {
    uart2_rx_pending = 0U;
    uart2_rx_count = 0U;
    uart2_tx_count = 0U;
    uart3_tx_count = 0U;
    uart3_tick = now;
    mode_first_entry = 0U;
  }

  /*
   * 回调和主循环同时访问 pending/last_byte，复制时短暂关中断，避免刚读
   * 完标志却读到下一个字节。这里只关几个指令的时间，不影响正常接收。
   */
  if (uart2_rx_pending)
  {
    uint8_t value;
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    value = uart2_last_byte;
    uart2_rx_pending = 0U;
    if (primask == 0U)
    {
      __enable_irq();
    }
    if (HAL_UART_Transmit(&huart2, &value, 1U, 20U) == HAL_OK)
    {
      ++uart2_tx_count;
    }
    screen_dirty = 1U;
  }

  /* UART3 用阻塞发送演示最基础的 HAL API，但每秒只发一行，等待很短。 */
  if ((now - uart3_tick) >= 1000U)
  {
    int length;

    uart3_tick = now;
    ++uart3_tx_count;
    length = snprintf(line, sizeof(line), "USART3 TEST: %lu\r\n",
                      (unsigned long)uart3_tx_count);
    if (length > 0)
    {
      (void)HAL_UART_Transmit(&huart3, (uint8_t *)line,
                             (uint16_t)length, 100U);
    }
    screen_dirty = 1U;
  }

  if (!Display_IsDue())
  {
    return;
  }

  OLED_Clear();
  OLED_ShowString(0U, 0U, "UART TEST 115200");
  OLED_ShowString(0U, 8U, "UART2:RX + ECHO");
  (void)snprintf(line, sizeof(line), "RX:%lu TX:%lu",
                 (unsigned long)uart2_rx_count,
                 (unsigned long)uart2_tx_count);
  OLED_ShowString(0U, 16U, line);
  (void)snprintf(line, sizeof(line), "LAST:0x%02X '%c'", uart2_last_byte,
                 ((uart2_last_byte >= 0x20U) && (uart2_last_byte <= 0x7EU)) ?
                 (char)uart2_last_byte : '.');
  OLED_ShowString(0U, 24U, line);
  OLED_ShowString(0U, 32U, "UART3:TX EACH 1S");
  (void)snprintf(line, sizeof(line), "TX:%lu", (unsigned long)uart3_tx_count);
  OLED_ShowString(0U, 40U, line);
  Display_Footer();
  (void)OLED_Refresh();
}

static void CAN_TestProcess(void)
{
  char line[32];
  CAN_Frame frame;

  if (mode_first_entry)
  {
    can_send_result = 0U;
    mode_first_entry = 0U;
  }

  /* 周期读取协议状态，使无 ACK、Bus-Off 等错误能显示出来而不阻塞菜单。 */
  CAN_Process();
  if (key_short_event)
  {
    uint8_t data[8];
    HAL_StatusTypeDef result;

    /* 8 字节经典 CAN 数据：ASCII "G474" + 32 位小端递增计数。 */
    data[0] = 'G';
    data[1] = '4';
    data[2] = '7';
    data[3] = '4';
    data[4] = (uint8_t)can_payload_count;
    data[5] = (uint8_t)(can_payload_count >> 8U);
    data[6] = (uint8_t)(can_payload_count >> 16U);
    data[7] = (uint8_t)(can_payload_count >> 24U);
    result = CAN_SendStd(0x123U, data, sizeof(data));
    can_send_result = (result == HAL_OK) ? 1U :
                      ((result == HAL_BUSY) ? 2U : 3U);
    ++can_payload_count;
    screen_dirty = 1U;
  }

  if (!Display_IsDue())
  {
    return;
  }

  OLED_Clear();
  OLED_ShowString(0U, 0U, "CAN TEST - 500K");
  OLED_ShowString(0U, 8U, CAN_BusOff ? "STATUS:BUS-OFF" :
                                     (CAN_Started ? "STATUS:OK" :
                                                    "STATUS:INIT ERR"));
  (void)snprintf(line, sizeof(line), "TX:%lu RX:%lu",
                 (unsigned long)CAN_TxCount, (unsigned long)CAN_RxCount);
  OLED_ShowString(0U, 16U, line);
  if (CAN_GetLastFrame(&frame))
  {
    (void)snprintf(line, sizeof(line), "LAST ID:0x%03X", frame.id);
    OLED_ShowString(0U, 24U, line);
    (void)snprintf(line, sizeof(line), "DLC:%u", frame.length);
    OLED_ShowString(0U, 32U, line);
    (void)snprintf(line, sizeof(line), "%02X %02X %02X %02X",
                   frame.data[0], frame.data[1],
                   frame.data[2], frame.data[3]);
    OLED_ShowString(0U, 40U, line);
  }
  else
  {
    OLED_ShowString(0U, 24U, "LAST RX:NONE");
  }
  if (can_send_result == 0U)
  {
    OLED_ShowString(0U, 48U, "Press=Send 0x123");
  }
  else if (can_send_result == 1U)
  {
    OLED_ShowString(0U, 48U, "SEND:OK");
  }
  else if (can_send_result == 2U)
  {
    OLED_ShowString(0U, 48U, "SEND:FIFO FULL");
  }
  else
  {
    OLED_ShowString(0U, 48U, "SEND:ERROR");
  }
  Display_Footer();
  (void)OLED_Refresh();
}

static void OLED_TestProcess(void)
{
  uint8_t x;

  if (mode_first_entry)
  {
    oled_pattern = 0U;
    mode_first_entry = 0U;
    screen_dirty = 1U;
  }
  if (key_short_event)
  {
    oled_pattern = (uint8_t)((oled_pattern + 1U) % 3U);
    screen_dirty = 1U;
  }

  if (!Display_IsDue())
  {
    return;
  }

  if (oled_pattern == 0U)
  {
    OLED_ShowTestPattern();
  }
  else if (oled_pattern == 1U)
  {
    OLED_Fill(0xAAU);
    OLED_ShowString(20U, 24U, "PATTERN 2/3");
    OLED_ShowString(14U, 40U, "Press=Next");
  }
  else
  {
    OLED_Clear();
    for (x = 0U; x < 64U; ++x)
    {
      OLED_DrawPixel(x, x, 1U);
      OLED_DrawPixel((uint8_t)(127U - x), x, 1U);
    }
    OLED_ShowString(20U, 24U, "PATTERN 3/3");
    OLED_ShowString(14U, 40U, "Press=Next");
  }
  (void)OLED_Refresh();
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_FDCAN1_Init();
  MX_I2C4_Init();
  MX_SPI4_Init();
  MX_TIM1_Init();
  MX_USART2_UART_Init();
  MX_USART3_UART_Init();
  /* USER CODE BEGIN 2 */
  /*
   * 上面 MX_* 是 CubeMX 生成的底层初始化；下面是用户驱动初始化。
   * 上电只探测设备并启动接口，不自动执行 FRAM/SD 写测试。
   */
  OLED_Init();
  (void)FRAM_Init();
  Encoder_Init();
  (void)CAN_Init();

  /* UART2只接收一个字节；每次回调后立即重新启动下一次接收。 */
  if (HAL_UART_Receive_IT(&huart2, &uart2_rx_byte, 1U) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  /*
   * 协作式单主循环：每次循环只做一点工作，没有页面内部的死循环。
   * 因此无论在哪个页面，编码器、按键和 CAN 状态都能持续得到处理。
   */
  while (1)
  {
    Encoder_Update();
    Key_Update();

    if (key_long_event)
    {
      key_long_event = 0U;
      if (current_mode != MODE_MENU)
      {
        Mode_Enter(MODE_MENU);
      }
    }

    switch (current_mode)
    {
      case MODE_MENU:    Menu_Process();        break;
      case MODE_SYSTEM:  System_TestProcess();  break;
      case MODE_LED:     LED_TestProcess();     break;
      case MODE_KEY:     Key_TestProcess();     break;
      case MODE_ENCODER: Encoder_TestProcess(); break;
      case MODE_SWITCH:  Switch_TestProcess();  break;
      case MODE_FRAM:    FRAM_TestProcess();    break;
      case MODE_SD:      SD_TestProcess();      break;
      case MODE_UART:    UART_TestProcess();    break;
      case MODE_CAN:     CAN_TestProcess();      break;
      case MODE_OLED:    OLED_TestProcess();     break;
      default:           Mode_Enter(MODE_MENU); break;
    }

    /* 每个短按事件只允许当前页面使用一次。 */
    key_short_event = 0U;
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV6;
  RCC_OscInitStruct.PLL.PLLN = 85;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == KEY_Pin)
  {
    /* 中断里不消抖、不刷新OLED，只通知主循环有一次下降沿。 */
    key_irq_flag = 1U;
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2)
  {
    /*
     * HAL 的单字节中断接收完成后会停止，必须再次调用 Receive_IT 才能
     * 接收下一个字节。回调内不做阻塞发送，避免中断占用时间过长。
     */
    uart2_last_byte = uart2_rx_byte;
    uart2_rx_pending = 1U;
    ++uart2_rx_count;
    (void)HAL_UART_Receive_IT(&huart2, &uart2_rx_byte, 1U);
  }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
