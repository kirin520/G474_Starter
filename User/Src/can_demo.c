/*
 * FDCAN1经典CAN演示
 *
 * PD0=RX，PD1=TX，PD4=TCAN3413 STB（低电平进入Normal）。
 * CubeMX负责24 MHz内核时钟和500 kbit/s位时序；本文件只配置过滤器、
 * 启动控制器，并提供标准帧收发接口。
 */
#include "can_demo.h"

#include <string.h>
#include "fdcan.h"
#include "main.h"

/* 回调和主循环都可能访问这些状态，volatile 保证每次都读取真实内存值。 */
volatile uint32_t CAN_TxCount;
volatile uint32_t CAN_RxCount;
volatile uint32_t CAN_TxFailed;
volatile uint8_t CAN_Started;
volatile uint8_t CAN_BusOff;

static CAN_Frame can_last_frame;
static volatile uint8_t can_last_frame_valid;
static uint32_t can_last_error;

static uint32_t CAN_LengthToDlc(uint8_t length)
{
  /* HAL 的 DataLength 字段不是直接填 0~8，而要填 FDCAN_DLC_BYTES_x 宏。 */
  static const uint32_t dlc[9] =
  {
    FDCAN_DLC_BYTES_0, FDCAN_DLC_BYTES_1, FDCAN_DLC_BYTES_2,
    FDCAN_DLC_BYTES_3, FDCAN_DLC_BYTES_4, FDCAN_DLC_BYTES_5,
    FDCAN_DLC_BYTES_6, FDCAN_DLC_BYTES_7, FDCAN_DLC_BYTES_8
  };

  return dlc[length];
}

HAL_StatusTypeDef CAN_Init(void)
{
  FDCAN_FilterTypeDef filter = {0};
  HAL_StatusTypeDef result;

  CAN_TxCount = 0U;
  CAN_RxCount = 0U;
  CAN_TxFailed = 0U;
  CAN_Started = 0U;
  CAN_BusOff = 0U;
  can_last_frame_valid = 0U;
  can_last_error = 0U;

  /* TCAN3413 的 STB 为高时待机、为低时 Normal；先唤醒收发器再启动控制器。 */
  HAL_GPIO_WritePin(CAN_STB_GPIO_Port, CAN_STB_Pin, GPIO_PIN_RESET);
  HAL_Delay(1U);

  /* 掩码为0，所有11位标准ID都进入RX FIFO0。 */
  filter.IdType = FDCAN_STANDARD_ID;
  filter.FilterIndex = 0U;
  filter.FilterType = FDCAN_FILTER_MASK;
  filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
  filter.FilterID1 = 0U;
  filter.FilterID2 = 0U;
  result = HAL_FDCAN_ConfigFilter(&hfdcan1, &filter);
  if (result != HAL_OK)
  {
    return result;
  }

  /* 未被标准过滤器接收的帧、扩展帧和远程帧全部拒绝。 */
  result = HAL_FDCAN_ConfigGlobalFilter(&hfdcan1,
                                         FDCAN_REJECT, FDCAN_REJECT,
                                         FDCAN_REJECT_REMOTE,
                                         FDCAN_REJECT_REMOTE);
  if (result != HAL_OK)
  {
    return result;
  }

  /* 新报文和 Bus-Off 通过中断通知；中断处理函数在本文件末尾。 */
  result = HAL_FDCAN_ActivateNotification(
      &hfdcan1, FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_BUS_OFF, 0U);
  if (result != HAL_OK)
  {
    return result;
  }

  /* 过滤器必须在 Start 之前配置；Start 后控制器才真正参与 CAN 总线。 */
  result = HAL_FDCAN_Start(&hfdcan1);
  CAN_Started = (result == HAL_OK);
  return result;
}

HAL_StatusTypeDef CAN_SendStd(uint16_t id, const uint8_t *data,
                              uint8_t length)
{
  FDCAN_TxHeaderTypeDef header = {0};
  uint8_t payload[CAN_MAX_DATA_LENGTH] = {0};
  HAL_StatusTypeDef result;

  /* 标准 ID 只有 11 位，经典 CAN 单帧最多携带 8 字节。 */
  if (!CAN_Started || CAN_BusOff || (id > 0x7FFU) ||
      (length > CAN_MAX_DATA_LENGTH) ||
      ((data == NULL) && (length != 0U)))
  {
    ++CAN_TxFailed;
    return HAL_ERROR;
  }
  /* 发送 FIFO 没有空位时返回 BUSY，让上层决定稍后重试。 */
  if (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan1) == 0U)
  {
    ++CAN_TxFailed;
    return HAL_BUSY;
  }
  if (length != 0U)
  {
    memcpy(payload, data, length);
  }

  /* 明确关闭 BRS 并选择 CLASSIC_CAN，保证 PCAN 按经典 CAN 解码。 */
  header.Identifier = id;
  header.IdType = FDCAN_STANDARD_ID;
  header.TxFrameType = FDCAN_DATA_FRAME;
  header.DataLength = CAN_LengthToDlc(length);
  header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
  header.BitRateSwitch = FDCAN_BRS_OFF;
  header.FDFormat = FDCAN_CLASSIC_CAN;
  header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
  header.MessageMarker = 0U;

  result = HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &header, payload);
  if (result == HAL_OK)
  {
    ++CAN_TxCount;
  }
  else
  {
    ++CAN_TxFailed;
  }
  return result;
}

uint8_t CAN_GetLastFrame(CAN_Frame *frame)
{
  uint32_t primask;

  if ((frame == NULL) || !can_last_frame_valid)
  {
    return 0U;
  }

  /* 中断只在复制这11字节期间关闭，防止OLED读取到一半更新的数据。 */
  primask = __get_PRIMASK();
  __disable_irq();
  *frame = can_last_frame;
  if (primask == 0U)
  {
    __enable_irq();
  }
  return 1U;
}

void CAN_Process(void)
{
  FDCAN_ProtocolStatusTypeDef protocol = {0};

  /*
   * 没有其他节点发送 ACK 时，错误计数会增加并可能进入 Bus-Off。
   * 这里只读取状态，不等待总线恢复，所以不会卡住 OLED 菜单。
   */
  if (CAN_Started &&
      (HAL_FDCAN_GetProtocolStatus(&hfdcan1, &protocol) == HAL_OK))
  {
    CAN_BusOff = (protocol.BusOff != 0U);
  }
  can_last_error = HAL_FDCAN_GetError(&hfdcan1);
}

uint32_t CAN_GetError(void)
{
  return can_last_error;
}

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *fdcan,
                               uint32_t interrupt_flags)
{
  FDCAN_RxHeaderTypeDef header;
  uint8_t data[CAN_MAX_DATA_LENGTH];

  if ((fdcan->Instance != FDCAN1) ||
      ((interrupt_flags & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) == 0U))
  {
    return;
  }

  /*
   * 回调运行在中断上下文：高频报文到来时持续读空 FIFO，但只保存最新
   * 一帧。这里不刷新 OLED、不 printf，也不做任何长时间等待。
   */
  while (HAL_FDCAN_GetRxFifoFillLevel(fdcan, FDCAN_RX_FIFO0) > 0U)
  {
    uint8_t length;

    if (HAL_FDCAN_GetRxMessage(fdcan, FDCAN_RX_FIFO0, &header, data) != HAL_OK)
    {
      break;
    }

    /* 对经典 CAN 的 0~8 字节 DLC，HAL 编码的低 4 位就是实际长度。 */
    length = (uint8_t)(header.DataLength & 0x0FU);
    if ((header.IdType != FDCAN_STANDARD_ID) ||
        (header.RxFrameType != FDCAN_DATA_FRAME) || (length > 8U))
    {
      continue;
    }

    can_last_frame.id = (uint16_t)header.Identifier;
    can_last_frame.length = length;
    memset(can_last_frame.data, 0, sizeof(can_last_frame.data));
    memcpy(can_last_frame.data, data, length);
    can_last_frame_valid = 1U;
    ++CAN_RxCount;
  }
}

void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef *fdcan,
                                   uint32_t error_status_flags)
{
  if ((fdcan->Instance == FDCAN1) &&
      ((error_status_flags & FDCAN_IT_BUS_OFF) != 0U))
  {
    /* Bus-Off 是总线错误累计后的保护状态，交给主循环显示给用户。 */
    CAN_BusOff = 1U;
  }
}
