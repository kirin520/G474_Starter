/*
 * SD卡SPI模式驱动
 * SPI4: PE2=SCK, PE5=MISO, PE6=MOSI, PE4=CS。
 * 不使用卡槽检测脚。初始化阶段约332 kHz，识别成功后约5.31 MHz。
 *
 * SPI 是全双工总线：主机每发送 1 字节，也同时收到 1 字节。只想读取时，
 * 主机仍需发送 0xFF 来提供时钟；代码中的 dummy byte 就是这个用途。
 */
#include "sdcard.h"

#include <stdbool.h>
#include <string.h>
#include "ff.h"
#include "main.h"
#include "spi.h"

/* SPI 数据 Token、R1 响应和 ACMD 标记。 */
#define SD_DUMMY_BYTE 0xFFU
#define SD_TOKEN_START_BLOCK 0xFEU
#define SD_DATA_ACCEPTED 0x05U
#define SD_R1_IDLE 0x01U
#define SD_CMD_APP_FLAG 0x80U

/* 本示例实际用到的 SD 命令编号。ACMD41 必须先发送 CMD55。 */
#define SD_CMD0 0U
#define SD_CMD1 1U
#define SD_CMD8 8U
#define SD_CMD9 9U
#define SD_CMD16 16U
#define SD_CMD17 17U
#define SD_CMD24 24U
#define SD_CMD55 55U
#define SD_CMD58 58U
#define SD_ACMD41 (SD_CMD_APP_FLAG | 41U)

#define SD_COMMAND_TIMEOUT_MS 100U
#define SD_INIT_TIMEOUT_MS 2000U
#define SD_DATA_TIMEOUT_MS 750U

uint8_t SD_State;
uint8_t SD_HighCapacity;
uint8_t SD_LastR1;
uint32_t SD_BlockCount;
uint8_t SD_FileResult;

/* 512 字节固定缓冲区避免动态内存；读/写整扇区时用于产生或接收时钟。 */
static uint8_t sd_dummy_tx[SD_BLOCK_SIZE];
static uint8_t sd_dummy_rx[SD_BLOCK_SIZE];

static HAL_StatusTypeDef SD_SetSpiPrescaler(uint32_t prescaler)
{
  /* 只改 SPI 分频并重新初始化 SPI4，引脚和 Mode 0 等配置仍来自 CubeMX。 */
  if (hspi4.State == HAL_SPI_STATE_BUSY)
  {
    return HAL_BUSY;
  }
  hspi4.Init.BaudRatePrescaler = prescaler;
  return HAL_SPI_Init(&hspi4);
}

uint32_t SD_GetSpiClock(void)
{
  uint32_t divider;

  switch (hspi4.Init.BaudRatePrescaler)
  {
    case SPI_BAUDRATEPRESCALER_2:   divider = 2U; break;
    case SPI_BAUDRATEPRESCALER_4:   divider = 4U; break;
    case SPI_BAUDRATEPRESCALER_8:   divider = 8U; break;
    case SPI_BAUDRATEPRESCALER_16:  divider = 16U; break;
    case SPI_BAUDRATEPRESCALER_32:  divider = 32U; break;
    case SPI_BAUDRATEPRESCALER_64:  divider = 64U; break;
    case SPI_BAUDRATEPRESCALER_128: divider = 128U; break;
    case SPI_BAUDRATEPRESCALER_256: divider = 256U; break;
    default: return 0U;
  }
  return HAL_RCC_GetPCLK2Freq() / divider;
}

static bool SdCard_TransferByte(uint8_t output, uint8_t *input)
{
  uint8_t received = SD_DUMMY_BYTE;

  if (HAL_SPI_TransmitReceive(&hspi4, &output, &received, 1U,
                              SD_COMMAND_TIMEOUT_MS) != HAL_OK)
  {
    SD_State = SD_STATE_SPI_ERROR;
    return false;
  }

  if (input != NULL)
  {
    *input = received;
  }
  return true;
}

static void SdCard_Deselect(void)
{
  uint8_t ignored;
  /* 拉高 CS 后再提供 8 个时钟，让卡完成本次命令并释放 MISO。 */
  HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);
  (void)SdCard_TransferByte(SD_DUMMY_BYTE, &ignored);
}

static bool SdCard_WaitReady(uint32_t timeout_ms)
{
  uint32_t start = HAL_GetTick();
  uint8_t value = 0U;

  do
  {
    if (!SdCard_TransferByte(SD_DUMMY_BYTE, &value))
    {
      return false;
    }
    /* 写卡期间 MISO 会保持忙；再次读到 0xFF 表示卡已就绪。 */
    if (value == SD_DUMMY_BYTE)
    {
      return true;
    }
  } while ((HAL_GetTick() - start) < timeout_ms);

  SD_State = SD_STATE_TIMEOUT;
  return false;
}

static bool SdCard_Select(void)
{
  uint8_t ignored;

  HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_RESET);
  if (!SdCard_TransferByte(SD_DUMMY_BYTE, &ignored))
  {
    return false;
  }
  if (SdCard_WaitReady(SD_DATA_TIMEOUT_MS))
  {
    return true;
  }
  SdCard_Deselect();
  return false;
}

static uint8_t SdCard_SendCommand(uint8_t command, uint32_t argument)
{
  uint8_t response = 0xFFU;
  uint8_t crc = 0x01U;
  uint8_t attempts;
  uint8_t ignored;

  if ((command & SD_CMD_APP_FLAG) != 0U)
  {
    /* ACMD 不是独立命令：协议规定先 CMD55，再发送去掉标记后的命令号。 */
    command &= (uint8_t)~SD_CMD_APP_FLAG;
    response = SdCard_SendCommand(SD_CMD55, 0U);
    if (response > SD_R1_IDLE)
    {
      return response;
    }
  }

  SdCard_Deselect();
  if (!SdCard_Select())
  {
    return 0xFFU;
  }

  /* SPI 模式关闭 CRC 后通常只需合法停止位；CMD0/CMD8 的 CRC 必须正确。 */
  if (command == SD_CMD0)
  {
    crc = 0x95U;
  }
  else if (command == SD_CMD8)
  {
    crc = 0x87U;
  }

  /* 命令包固定 6 字节：0x40|CMD、32位参数（高字节先发）、CRC。 */
  (void)SdCard_TransferByte((uint8_t)(0x40U | command), &ignored);
  (void)SdCard_TransferByte((uint8_t)(argument >> 24U), &ignored);
  (void)SdCard_TransferByte((uint8_t)(argument >> 16U), &ignored);
  (void)SdCard_TransferByte((uint8_t)(argument >> 8U), &ignored);
  (void)SdCard_TransferByte((uint8_t)argument, &ignored);
  (void)SdCard_TransferByte(crc, &ignored);

  /* R1 的最高位清零时响应有效；最多读取 10 字节，避免永久等待。 */
  attempts = 10U;
  do
  {
    if (!SdCard_TransferByte(SD_DUMMY_BYTE, &response))
    {
      response = 0xFFU;
      break;
    }
    --attempts;
  } while (((response & 0x80U) != 0U) && (attempts != 0U));

  SD_LastR1 = response;
  return response;
}

static bool SdCard_ReadData(uint8_t *data, uint16_t length)
{
  uint32_t start = HAL_GetTick();
  uint8_t token = 0xFFU;
  uint8_t ignored;

  do
  {
    if (!SdCard_TransferByte(SD_DUMMY_BYTE, &token))
    {
      return false;
    }
  } while ((token == 0xFFU) &&
           ((HAL_GetTick() - start) < SD_DATA_TIMEOUT_MS));

  /* 0xFE 是单块数据开始 Token；其他值说明协议或卡状态异常。 */
  if (token != SD_TOKEN_START_BLOCK)
  {
    SD_State = SD_STATE_PROTOCOL;
    return false;
  }

  if (length == SD_BLOCK_SIZE)
  {
    if (HAL_SPI_TransmitReceive(&hspi4, sd_dummy_tx, data, length,
                               SD_DATA_TIMEOUT_MS) != HAL_OK)
    {
      SD_State = SD_STATE_SPI_ERROR;
      return false;
    }
  }
  else
  {
    uint16_t index;
    for (index = 0U; index < length; ++index)
    {
      if (!SdCard_TransferByte(SD_DUMMY_BYTE, &data[index]))
      {
        return false;
      }
    }
  }

  /* 数据块后有 2 字节 CRC；本入门示例未启用数据 CRC，仍必须把它读走。 */
  (void)SdCard_TransferByte(SD_DUMMY_BYTE, &ignored);
  (void)SdCard_TransferByte(SD_DUMMY_BYTE, &ignored);
  return true;
}

static bool SdCard_ReadCsd(void)
{
  uint8_t csd[16];
  uint32_t c_size;

  if ((SdCard_SendCommand(SD_CMD9, 0U) != 0U) ||
      !SdCard_ReadData(csd, sizeof(csd)))
  {
    SdCard_Deselect();
    return false;
  }
  SdCard_Deselect();

  /* CSD v2（SDHC/SDXC）和 CSD v1（SDSC）的容量字段位置不同。 */
  if ((csd[0] >> 6U) == 1U)
  {
    c_size = ((uint32_t)(csd[7] & 0x3FU) << 16U) |
             ((uint32_t)csd[8] << 8U) | csd[9];
    SD_BlockCount = (c_size + 1U) * 1024U;
  }
  else
  {
    uint32_t read_block_length = 1UL << (csd[5] & 0x0FU);
    uint32_t size_multiplier;
    uint64_t capacity;

    c_size = ((uint32_t)(csd[6] & 0x03U) << 10U) |
             ((uint32_t)csd[7] << 2U) | ((csd[8] >> 6U) & 0x03U);
    size_multiplier = 1UL << ((((uint32_t)csd[9] & 0x03U) << 1U) |
                              ((csd[10] >> 7U) & 0x01U));
    size_multiplier <<= 2U;
    capacity = (uint64_t)(c_size + 1U) * size_multiplier * read_block_length;
    SD_BlockCount = (uint32_t)(capacity / SD_BLOCK_SIZE);
  }

  return SD_BlockCount != 0U;
}

uint8_t SD_Init(void)
{
  /* SD_Init 是公开入口：重置状态后完整执行一次卡初始化。 */
  memset(sd_dummy_tx, SD_DUMMY_BYTE, sizeof(sd_dummy_tx));
  SD_State = SD_STATE_NOT_READY;
  SD_HighCapacity = 0U;
  SD_LastR1 = 0xFFU;
  SD_BlockCount = 0U;
  SD_FileResult = SD_FILE_NOT_TESTED;
  return SD_Reinitialize();
}

uint8_t SD_Reinitialize(void)
{
  uint32_t start;
  uint8_t response;
  uint8_t data[4];
  uint8_t index;
  uint8_t ignored;

  SD_HighCapacity = 0U;
  SD_BlockCount = 0U;
  SD_LastR1 = 0xFFU;
  SD_State = SD_STATE_NOT_READY;
  /* 第 1 步：低速、CS 高，发送至少 74 个空闲时钟，令 SD 进入 SPI 模式。 */
  HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);
  if (SD_SetSpiPrescaler(SPI_BAUDRATEPRESCALER_256) != HAL_OK)
  {
    SD_State = SD_STATE_SPI_ERROR;
    return SD_State;
  }
  HAL_Delay(2U);
  for (index = 0U; index < 10U; ++index)
  {
    if (!SdCard_TransferByte(SD_DUMMY_BYTE, &ignored))
    {
      return SD_State;
    }
  }

  /* 第 2 步：重复 CMD0，直到卡返回 Idle 状态 R1=0x01。 */
  start = HAL_GetTick();
  do
  {
    response = SdCard_SendCommand(SD_CMD0, 0U);
  } while ((response != SD_R1_IDLE) &&
           ((HAL_GetTick() - start) < SD_INIT_TIMEOUT_MS));
  if (response != SD_R1_IDLE)
  {
    SdCard_Deselect();
    SD_State = SD_STATE_TIMEOUT;
    return SD_State;
  }

  /* 第 3 步：CMD8 检查新版本卡和 2.7~3.6 V 电压范围。 */
  response = SdCard_SendCommand(SD_CMD8, 0x000001AAUL);
  if (response == SD_R1_IDLE)
  {
    for (index = 0U; index < 4U; ++index)
    {
      (void)SdCard_TransferByte(SD_DUMMY_BYTE, &data[index]);
    }
    if ((data[2] != 0x01U) || (data[3] != 0xAAU))
    {
      SdCard_Deselect();
      SD_State = SD_STATE_PROTOCOL;
      return SD_State;
    }

    /* 第 4 步：ACMD41(HCS=1) 轮询，等待新版本卡完成上电初始化。 */
    start = HAL_GetTick();
    do
    {
      response = SdCard_SendCommand(SD_ACMD41, 0x40000000UL);
    } while ((response != 0U) &&
             ((HAL_GetTick() - start) < SD_INIT_TIMEOUT_MS));
    if (response != 0U)
    {
      SdCard_Deselect();
      SD_State = SD_STATE_TIMEOUT;
      return SD_State;
    }

    /* 第 5 步：CMD58 读取 OCR，CCS=1 表示 SDHC/SDXC 块寻址。 */
    if (SdCard_SendCommand(SD_CMD58, 0U) != 0U)
    {
      SdCard_Deselect();
      SD_State = SD_STATE_PROTOCOL;
      return SD_State;
    }
    for (index = 0U; index < 4U; ++index)
    {
      (void)SdCard_TransferByte(SD_DUMMY_BYTE, &data[index]);
    }
    SD_HighCapacity = (uint8_t)((data[0] & 0x40U) != 0U);
  }
  else
  {
    /* CMD8 不受支持时兼容旧 SDSC：尝试 ACMD41，必要时再用 CMD1。 */
    start = HAL_GetTick();
    response = SdCard_SendCommand(SD_ACMD41, 0U);
    if (response <= SD_R1_IDLE)
    {
      do
      {
        response = SdCard_SendCommand(SD_ACMD41, 0U);
      } while ((response != 0U) &&
               ((HAL_GetTick() - start) < SD_INIT_TIMEOUT_MS));
    }
    else
    {
      do
      {
        response = SdCard_SendCommand(SD_CMD1, 0U);
      } while ((response != 0U) &&
               ((HAL_GetTick() - start) < SD_INIT_TIMEOUT_MS));
    }
    if (response != 0U)
    {
      SdCard_Deselect();
      SD_State = SD_STATE_TIMEOUT;
      return SD_State;
    }
  }

  /* SDSC 使用字节地址，先用 CMD16 固定块长为 512 字节。 */
  if (!SD_HighCapacity &&
      (SdCard_SendCommand(SD_CMD16, SD_BLOCK_SIZE) != 0U))
  {
    SdCard_Deselect();
    SD_State = SD_STATE_PROTOCOL;
    return SD_State;
  }
  SdCard_Deselect();

  /* 第 6 步：初始化完成后提速，再读 CSD 得到可用扇区数。 */
  if (SD_SetSpiPrescaler(SPI_BAUDRATEPRESCALER_16) != HAL_OK)
  {
    SD_State = SD_STATE_SPI_ERROR;
    return SD_State;
  }
  if (!SdCard_ReadCsd())
  {
    SD_State = SD_STATE_PROTOCOL;
    return SD_State;
  }

  SD_State = SD_STATE_READY;
  return SD_State;
}

uint8_t SD_ReadBlock(uint32_t block, uint8_t *data)
{
  uint32_t argument;
  bool ok;

  if ((data == NULL) || (SD_State != SD_STATE_READY) ||
      (block >= SD_BlockCount))
  {
    return 0U;
  }

  /* SDHC/SDXC 参数是扇区号；旧 SDSC 参数是字节地址。 */
  argument = SD_HighCapacity ? block : block * SD_BLOCK_SIZE;
  if (SdCard_SendCommand(SD_CMD17, argument) != 0U)
  {
    SdCard_Deselect();
    SD_State = SD_STATE_IO_ERROR;
    return 0U;
  }
  ok = SdCard_ReadData(data, SD_BLOCK_SIZE);
  SdCard_Deselect();
  if (!ok)
  {
    SD_State = SD_STATE_IO_ERROR;
  }
  return (uint8_t)ok;
}

uint8_t SD_WriteBlock(uint32_t block, const uint8_t *data)
{
  uint32_t argument;
  uint8_t response;
  uint8_t ignored;

  if ((data == NULL) || (SD_State != SD_STATE_READY) ||
      (block >= SD_BlockCount))
  {
    return 0U;
  }

  /* CMD24 写一个 512 字节块，寻址规则与读取相同。 */
  argument = SD_HighCapacity ? block : block * SD_BLOCK_SIZE;
  if (SdCard_SendCommand(SD_CMD24, argument) != 0U)
  {
    SdCard_Deselect();
    SD_State = SD_STATE_IO_ERROR;
    return 0U;
  }

  /* 发送 0xFE Token、512 字节数据和两个占位 CRC 字节。 */
  (void)SdCard_TransferByte(SD_DUMMY_BYTE, &ignored);
  (void)SdCard_TransferByte(SD_TOKEN_START_BLOCK, &ignored);
  if (HAL_SPI_TransmitReceive(&hspi4, (uint8_t *)data, sd_dummy_rx,
                             SD_BLOCK_SIZE,
                             SD_DATA_TIMEOUT_MS) != HAL_OK)
  {
    SdCard_Deselect();
    SD_State = SD_STATE_SPI_ERROR;
    return 0U;
  }
  (void)SdCard_TransferByte(SD_DUMMY_BYTE, &ignored);
  (void)SdCard_TransferByte(SD_DUMMY_BYTE, &ignored);
  /* 卡返回 Data Response；0x05 表示接收成功，随后还要等待内部写入结束。 */
  if (!SdCard_TransferByte(SD_DUMMY_BYTE, &response) ||
      ((response & 0x1FU) != SD_DATA_ACCEPTED) ||
      !SdCard_WaitReady(SD_DATA_TIMEOUT_MS))
  {
    SdCard_Deselect();
    SD_State = SD_STATE_IO_ERROR;
    return 0U;
  }

  SdCard_Deselect();
  SD_State = SD_STATE_READY;
  return 1U;
}

uint8_t SD_Sync(void)
{
  bool ready;

  if (SD_State != SD_STATE_READY)
  {
    return 0U;
  }
  /* FatFs 在关闭/同步文件时调用这里，确保卡已经结束内部写操作。 */
  if (!SdCard_Select())
  {
    return 0U;
  }
  ready = SdCard_WaitReady(SD_DATA_TIMEOUT_MS);
  SdCard_Deselect();
  return (uint8_t)ready;
}

uint8_t SD_FileTest(void)
{
  static FATFS file_system;
  static const char test_text[] = "STM32G474 SD CARD TEST\r\n";
  FIL file;
  char readback[sizeof(test_text)] = {0};
  UINT bytes_written = 0U;
  UINT bytes_read = 0U;
  FRESULT result;
  FRESULT close_result;

  /*
   * 文件测试流程：初始化 -> 挂载 -> 覆盖写入 -> 关闭 -> 重新打开 ->
   * 读回比较 -> 卸载。注意：FA_CREATE_ALWAYS 会覆盖 G474_DEMO.TXT。
   */
  SD_FileResult = SD_FILE_NOT_TESTED;
  if (SD_Init() != SD_STATE_READY)
  {
    SD_FileResult = SD_FILE_INIT_ERROR;
    return 0U;
  }

  /* opt=1 要求 FatFs 现在就挂载并读取文件系统，而不是延迟到首次访问。 */
  result = f_mount(&file_system, "", 1U);
  if (result != FR_OK)
  {
    SD_FileResult = SD_FILE_MOUNT_ERR;
    return 0U;
  }

  /* 先写并关闭，关闭动作会把目录项和缓存真正同步到卡中。 */
  result = f_open(&file, "G474_DEMO.TXT", FA_CREATE_ALWAYS | FA_WRITE);
  if (result != FR_OK)
  {
    SD_FileResult = SD_FILE_WRITE_ERR;
    (void)f_mount(NULL, "", 0U);
    return 0U;
  }

  result = f_write(&file, test_text, sizeof(test_text) - 1U, &bytes_written);
  close_result = f_close(&file);
  if ((result != FR_OK) || (bytes_written != (sizeof(test_text) - 1U)) ||
      (close_result != FR_OK))
  {
    SD_FileResult = SD_FILE_WRITE_ERR;
    (void)f_mount(NULL, "", 0U);
    return 0U;
  }

  /* 重新打开而不是直接比较 RAM，才能验证从 SD 卡实际读回的数据。 */
  result = f_open(&file, "G474_DEMO.TXT", FA_READ);
  if (result == FR_OK)
  {
    result = f_read(&file, readback, sizeof(test_text) - 1U, &bytes_read);
    (void)f_close(&file);
  }
  if ((result != FR_OK) || (bytes_read != (sizeof(test_text) - 1U)) ||
      (memcmp(test_text, readback, sizeof(test_text) - 1U) != 0))
  {
    SD_FileResult = SD_FILE_VERIFY_ERR;
    (void)f_mount(NULL, "", 0U);
    return 0U;
  }

  (void)f_mount(NULL, "", 0U);
  SD_FileResult = SD_FILE_PASS;
  return 1U;
}
