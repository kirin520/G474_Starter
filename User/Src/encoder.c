/*
 * 旋转编码器驱动
 *
 * PC0=TIM1_CH1，PC1=TIM1_CH2。TIM1完成AB相译码，主程序只需周期调用
 * Encoder_Update()，无需为两相信号编写GPIO中断。
 */
#include "encoder.h"

#include "tim.h"

#define ENCODER_COUNTS_PER_DETENT 2

static uint16_t encoder_last_counter;
static int32_t encoder_raw_count;

void Encoder_Init(void)
{
  /* TIM1 计数器从 0 开始；硬件随后根据 A/B 两相的先后顺序自动加减。 */
  encoder_raw_count = 0;
  __HAL_TIM_SET_COUNTER(&htim1, 0U);
  encoder_last_counter = 0U;
  (void)HAL_TIM_Encoder_Start(&htim1, TIM_CHANNEL_ALL);
}

void Encoder_Update(void)
{
  uint16_t current = (uint16_t)__HAL_TIM_GET_COUNTER(&htim1);

  /*
   * TIM1 是 16 位计数器。差值转成 int16_t 后，65535->0 会得到 +1，
   * 0->65535 会得到 -1，从而把硬件计数无缝扩展为软件 32 位位置。
   * 前提是两次 Update 之间的实际变化不超过 32767，本主循环远快于人手。
   */
  encoder_raw_count += (int16_t)(current - encoder_last_counter);
  encoder_last_counter = current;
}

int32_t Encoder_GetCount(void)
{
  /* 当前编码器每个机械档位产生 2 个有效计数，因此除以 2 得到“格数”。 */
  return encoder_raw_count / ENCODER_COUNTS_PER_DETENT;
}

int32_t Encoder_GetRawCount(void)
{
  return encoder_raw_count;
}

void Encoder_Reset(void)
{
  __HAL_TIM_SET_COUNTER(&htim1, 0U);
  encoder_last_counter = 0U;
  encoder_raw_count = 0;
}
