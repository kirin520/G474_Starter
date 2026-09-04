/*
 * FatFs 只理解“第几个逻辑盘、从第几个扇区读写多少个扇区”，并不知道
 * STM32 使用 SPI4。这个文件是文件系统与 sdcard.c 原始块读写之间的桥梁：
 *
 *   f_read/f_write -> FatFs -> disk_read/disk_write -> SD_Read/WriteBlock
 *
 * 本工程只有一张卡，所以合法的物理盘号 pdrv 只有 0。
 */
#include "diskio.h"
#include "sdcard.h"
#include <stddef.h>

DSTATUS disk_status(BYTE pdrv)
{
  if (pdrv != 0U)
  {
    return STA_NOINIT;
  }
  return (SD_State == SD_STATE_READY) ? 0U : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv)
{
  if (pdrv != 0U)
  {
    return STA_NOINIT;
  }
  /* 已就绪时不重复初始化，否则让底层重新执行完整 SD 初始化流程。 */
  if (SD_State == SD_STATE_READY)
  {
    return 0U;
  }
  return (SD_Reinitialize() == SD_STATE_READY) ? 0U : disk_status(pdrv);
}

DRESULT disk_read(BYTE pdrv, BYTE *buffer, DWORD sector, UINT count)
{
  UINT i;

  if ((pdrv != 0U) || (buffer == NULL) || (count == 0U))
  {
    return RES_PARERR;
  }
  /* FatFs 可能一次请求多个连续扇区，底层当前用单块命令逐块完成。 */
  for (i = 0U; i < count; ++i)
  {
    if (!SD_ReadBlock((uint32_t)sector + i, &buffer[i * SD_BLOCK_SIZE]))
    {
      return RES_ERROR;
    }
  }
  return RES_OK;
}

#if _FS_READONLY == 0
DRESULT disk_write(BYTE pdrv, const BYTE *buffer, DWORD sector, UINT count)
{
  UINT i;

  if ((pdrv != 0U) || (buffer == NULL) || (count == 0U))
  {
    return RES_PARERR;
  }
  for (i = 0U; i < count; ++i)
  {
    if (!SD_WriteBlock((uint32_t)sector + i, &buffer[i * SD_BLOCK_SIZE]))
    {
      return RES_ERROR;
    }
  }
  return RES_OK;
}
#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE command, void *buffer)
{
  if (pdrv != 0U)
  {
    return RES_PARERR;
  }
  if (SD_State != SD_STATE_READY)
  {
    return RES_NOTRDY;
  }

  /* ioctl 向 FatFs 报告同步状态、容量、扇区大小和擦除块大小。 */
  switch (command)
  {
    case CTRL_SYNC:
      return SD_Sync() ? RES_OK : RES_ERROR;
    case GET_SECTOR_COUNT:
      if (buffer == NULL) return RES_PARERR;
      *(DWORD *)buffer = SD_BlockCount;
      return RES_OK;
    case GET_SECTOR_SIZE:
      if (buffer == NULL) return RES_PARERR;
      *(WORD *)buffer = SD_BLOCK_SIZE;
      return RES_OK;
    case GET_BLOCK_SIZE:
      if (buffer == NULL) return RES_PARERR;
      *(DWORD *)buffer = 1U;
      return RES_OK;
    default:
      return RES_PARERR;
  }
}
