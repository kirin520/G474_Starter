#ifndef FRAM_H
#define FRAM_H

#include <stdint.h>
#include "stm32g4xx_hal.h"

#define FRAM_SIZE_BYTES 8192U
#define FRAM_ADDRESS    0x50U

/*
 * 使用顺序：FRAM_Init -> FRAM_Read/FRAM_Write。
 * address范围为0x0000~0x1FFF，length单位为字节，接口会检查越界。
 */

/* 为1表示地址0x50的器件已经应答。 */
extern uint8_t FRAM_Ready;

/* 只探测器件，不改写FRAM内容。 */
HAL_StatusTypeDef FRAM_Init(void);
/* 从address开始读取length字节，成功返回HAL_OK。 */
HAL_StatusTypeDef FRAM_Read(uint16_t address, uint8_t *data, uint16_t length);
/* 从address开始写入length字节；FRAM不需要等待内部写周期。 */
HAL_StatusTypeDef FRAM_Write(uint16_t address, const uint8_t *data,
                             uint16_t length);
/* 备份、测试并恢复最后16字节；全部成功返回1。 */
uint8_t FRAM_SelfTest(void);

#endif
