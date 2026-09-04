/*
 * MB85RC64T FRAM驱动
 *
 * 硬件连接：I2C4，7位地址0x50，容量8 KB。
 * 最小用法：
 *   FRAM_Init();
 *   FRAM_Write(0x0100, data, sizeof(data));
 *   FRAM_Read(0x0100, data, sizeof(data));
 *
 * FRAM_SelfTest()会暂时使用最后16字节，但会先备份并在结束前恢复。
 */
#include "fram.h"

#include <string.h>
#include "i2c.h"

#define FRAM_TIMEOUT_MS   100U
#define FRAM_TEST_LENGTH  16U
#define FRAM_TEST_ADDRESS (FRAM_SIZE_BYTES - FRAM_TEST_LENGTH)

uint8_t FRAM_Ready;

static uint8_t FRAM_RangeIsValid(uint16_t address, uint16_t length)
{
  /* MB85RC64T 可访问地址为 0x0000~0x1FFF，先检查可避免越界绕回。 */
  return (length > 0U) &&
         (((uint32_t)address + (uint32_t)length) <= FRAM_SIZE_BYTES);
}

HAL_StatusTypeDef FRAM_Init(void)
{
  /* IsDeviceReady 只发送地址并等待 ACK，不会修改 FRAM 中的数据。 */
  FRAM_Ready = (HAL_I2C_IsDeviceReady(&hi2c4, FRAM_ADDRESS << 1U,
                                      3U, FRAM_TIMEOUT_MS) == HAL_OK);
  return FRAM_Ready ? HAL_OK : HAL_ERROR;
}

HAL_StatusTypeDef FRAM_Read(uint16_t address, uint8_t *data, uint16_t length)
{
  if (!FRAM_Ready || (data == NULL) || !FRAM_RangeIsValid(address, length))
  {
    return HAL_ERROR;
  }

  /* 芯片内部地址有 16 位，HAL 会先发送高字节、再发送低字节。 */
  return HAL_I2C_Mem_Read(&hi2c4, FRAM_ADDRESS << 1U, address,
                          I2C_MEMADD_SIZE_16BIT, data, length,
                          FRAM_TIMEOUT_MS);
}

HAL_StatusTypeDef FRAM_Write(uint16_t address, const uint8_t *data,
                             uint16_t length)
{
  if (!FRAM_Ready || (data == NULL) || !FRAM_RangeIsValid(address, length))
  {
    return HAL_ERROR;
  }

  /* FRAM 没有 EEPROM 的页写等待时间，写函数返回后可立即读取验证。 */
  return HAL_I2C_Mem_Write(&hi2c4, FRAM_ADDRESS << 1U, address,
                           I2C_MEMADD_SIZE_16BIT, (uint8_t *)data, length,
                           FRAM_TIMEOUT_MS);
}

uint8_t FRAM_SelfTest(void)
{
  uint8_t backup[FRAM_TEST_LENGTH];
  uint8_t pattern[FRAM_TEST_LENGTH];
  uint8_t readback[FRAM_TEST_LENGTH];
  uint8_t test_ok = 0U;
  uint32_t index;

  /* 第 1 步：确认设备应答，并备份测试区域原有的 16 字节。 */
  if (FRAM_Init() != HAL_OK ||
      FRAM_Read(FRAM_TEST_ADDRESS, backup, sizeof(backup)) != HAL_OK)
  {
    return 0U;
  }

  /* 第 2 步：生成不全为 0/1 的测试图案，更容易发现数据线问题。 */
  for (index = 0U; index < sizeof(pattern); ++index)
  {
    pattern[index] = (uint8_t)(0xA5U ^ index ^ (index << 1U));
  }

  /* 第 3 步：写入、读回并逐字节比较。 */
  if ((FRAM_Write(FRAM_TEST_ADDRESS, pattern, sizeof(pattern)) == HAL_OK) &&
      (FRAM_Read(FRAM_TEST_ADDRESS, readback, sizeof(readback)) == HAL_OK) &&
      (memcmp(pattern, readback, sizeof(pattern)) == 0))
  {
    test_ok = 1U;
  }

  /* 第 4 步：即使测试失败也尽力恢复，并再次读回确认恢复成功。 */
  if ((FRAM_Write(FRAM_TEST_ADDRESS, backup, sizeof(backup)) != HAL_OK) ||
      (FRAM_Read(FRAM_TEST_ADDRESS, readback, sizeof(readback)) != HAL_OK) ||
      (memcmp(backup, readback, sizeof(backup)) != 0))
  {
    FRAM_Ready = 0U;
    return 0U;
  }

  return test_ok;
}
