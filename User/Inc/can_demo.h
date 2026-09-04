#ifndef CAN_DEMO_H
#define CAN_DEMO_H

#include <stdint.h>
#include "stm32g4xx_hal.h"

#define CAN_MAX_DATA_LENGTH 8U

typedef struct
{
  uint16_t id;                       /* 11位标准ID：0x000~0x7FF。 */
  uint8_t length;                    /* 有效数据长度：0~8字节。 */
  uint8_t data[CAN_MAX_DATA_LENGTH]; /* Classic CAN数据区。 */
} CAN_Frame;

/* 中断和主循环都会访问部分状态，因此使用volatile防止编译器缓存旧值。 */
extern volatile uint32_t CAN_TxCount;
extern volatile uint32_t CAN_RxCount;
extern volatile uint32_t CAN_TxFailed;
extern volatile uint8_t CAN_Started;
extern volatile uint8_t CAN_BusOff;

/* 拉低TCAN3413 STB、配置过滤器、打开通知并启动FDCAN1。 */
HAL_StatusTypeDef CAN_Init(void);
/* 发送11位标准数据帧；id不得大于0x7FF，length不得大于8。 */
HAL_StatusTypeDef CAN_SendStd(uint16_t id, const uint8_t *data,
                              uint8_t length);
/* 已经收到过报文时复制最新一帧并返回1，否则返回0。 */
uint8_t CAN_GetLastFrame(CAN_Frame *frame);
/* 在主循环调用，用于更新Bus-Off和HAL错误状态。 */
void CAN_Process(void);
uint32_t CAN_GetError(void);

#endif
