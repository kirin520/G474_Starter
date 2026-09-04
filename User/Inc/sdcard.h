#ifndef SDCARD_H
#define SDCARD_H

#include <stdint.h>

#define SD_BLOCK_SIZE 512U

/*
 * 使用顺序：直接调用SD_FileTest完成教学文件测试；需要学习底层块读写时，
 * 先调用SD_Init，成功后再调用SD_ReadBlock/SD_WriteBlock。
 * 本驱动没有使用卡槽插入检测脚。
 */

/* SD_State 的取值。状态公开是为了让 main.c 可以直接显示故障原因。 */
#define SD_STATE_NOT_READY  0U
#define SD_STATE_READY      1U
#define SD_STATE_NO_CARD    2U
#define SD_STATE_SPI_ERROR  3U
#define SD_STATE_TIMEOUT    4U
#define SD_STATE_PROTOCOL   5U
#define SD_STATE_IO_ERROR   6U

/* SD_FileResult用于指出文件测试停在哪一步。 */
#define SD_FILE_NOT_TESTED 0U
#define SD_FILE_PASS       1U
#define SD_FILE_INIT_ERROR 2U
#define SD_FILE_MOUNT_ERR  3U
#define SD_FILE_WRITE_ERR  4U
#define SD_FILE_VERIFY_ERR 5U

extern uint8_t SD_State;
extern uint8_t SD_HighCapacity;
extern uint8_t SD_LastR1;
extern uint32_t SD_BlockCount;
extern uint8_t SD_FileResult;

/* 以低速初始化SD卡，成功后自动切换到约5.31 MHz。返回SD_STATE_xxx。 */
uint8_t SD_Init(void);
/* 在FatFs要求重新初始化磁盘时使用。 */
uint8_t SD_Reinitialize(void);
/* block是512字节逻辑块号；成功返回1。 */
uint8_t SD_ReadBlock(uint32_t block, uint8_t *data);
uint8_t SD_WriteBlock(uint32_t block, const uint8_t *data);
/* 等待卡完成内部写入，主要供FatFs的CTRL_SYNC使用。 */
uint8_t SD_Sync(void);
/* 根据当前SPI4分频返回实际SCK频率，单位Hz。 */
uint32_t SD_GetSpiClock(void);
/* 创建并回读G474_DEMO.TXT；成功返回1，并更新SD_FileResult。 */
uint8_t SD_FileTest(void);

#endif
