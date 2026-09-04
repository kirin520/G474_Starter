#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>

/*
 * PC0/PC1由TIM1 Encoder Interface计数，每个机械格默认对应2个计数。
 * 使用顺序：Encoder_Init一次，然后在主循环反复调用Encoder_Update。
 */

/* 清零TIM1并启动两个编码器通道。 */
void Encoder_Init(void);
/* 读取16位TIM1计数器，并累加为软件32位位置。 */
void Encoder_Update(void);
/* 返回按机械格换算后的位置。 */
int32_t Encoder_GetCount(void);
/* 返回尚未除以每格计数数值的原始位置。 */
int32_t Encoder_GetRawCount(void);
/* 同时清零硬件计数器和软件累计值。 */
void Encoder_Reset(void);

#endif
