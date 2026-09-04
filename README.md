# STM32G474 Starter：从 CubeMX 配置到外设驱动

这是一份面向 STM32 初学者的可运行样板工程。目标不是把所有功能藏进框架，而是让你从
[`main.c`](Core/Src/main.c) 开始，能够顺着函数调用看到 GPIO、I2C、SPI、UART、TIM 和
FDCAN 是怎样初始化、怎样调用 HAL、又怎样把结果显示到 OLED 上。

读者只需要掌握 C 语言中的变量、函数、数组和基本指针。工程不使用 RTOS、DMA、动态内存、
CAN FD 或中文点阵字库。

> 重要：SD 菜单测试会以“创建并覆盖”方式写入根目录的 `G474_DEMO.TXT`。上电不会自动执行
> FRAM 或 SD 写测试，只有进入相应页面并短按 PC2 才会写入。

## 1. 工程运行后能看到什么

上电后，SSD1306 OLED 显示 `G474 DEMO MENU`。旋转编码器选择菜单，PC2 按键操作：

- 旋转编码器：上、下选择菜单项。
- PC2 短按：进入所选页面，或执行当前页面的测试。
- PC2 长按 800 ms：返回主菜单。

| 页面 | 功能 |
|---|---|
| System Status | 查看时钟、OLED、FRAM、SPI、SD 和 CAN 状态 |
| LED Test | PB11 每 500 ms 闪烁一次 |
| Key Test | 统计 PC2 有效短按次数 |
| Encoder Test | 显示 TIM1 原始计数、机械格数和方向 |
| DIP Switch | 实时查看 SW_1、SW_2 状态 |
| FRAM Test | 备份、写入、校验并恢复 16 字节 |
| SD Card Test | 初始化、挂载、写文件并重新读回校验 |
| UART Test | UART2 接收回显，UART3 每秒发送一行 |
| CAN Test | 接收并显示最新标准帧，短按发送 ID `0x123` |
| OLED Test | 切换边框、棋盘格和对角线测试图案 |

## 2. 准备软件、编译、下载和首次运行

### 2.1 所需工具

- STM32CubeMX：查看或修改 [`G474_Starter.ioc`](G474_Starter.ioc)。
- Arm GNU Toolchain：提供 `arm-none-eabi-gcc`。
- CMake 和 Ninja：构建工程。
- ST-LINK 与 STM32CubeProgrammer，或已经配置好的 VS Code 调试环境。
- 串口工具：115200 bit/s、8 数据位、无校验、1 停止位（115200 8N1）。
- PCAN-View：测试经典 CAN。

### 2.2 编译

在工程根目录打开 PowerShell：

```powershell
cmake --preset Debug
cmake --build --preset Debug --clean-first --parallel
```

成功后程序位于 `build/Debug/G474_Starter.elf`。`Debug` 适合断点调试；发布时可以换成
`Release`。

### 2.3 连接、下载和首测

ST-LINK 至少连接 `3V3/Vref`、`GND`、`SWDIO`、`SWCLK`，推荐同时连接 `NRST`。下载器与
开发板必须共地，ST-LINK 必须能检测到板上 3.3 V。

下载后复位。如果 OLED 显示菜单，旋转和短按均有反应，说明 CPU 时钟、SysTick、I2C、
编码器和 PC2 基础链路已经工作。

### 2.4 供电注意事项

- MCU、OLED、FRAM、SD 和 CAN 收发器的逻辑电平都是 3.3 V，不要把 5 V 信号直接接入 MCU。
- I2C 的 SCL/SDA 必须有上拉。本板使用外部上拉，CubeMX 中因此选择 `No pull-up`。
- CAN_H/CAN_L 不是普通 TTL 信号，必须经过 TCAN3413 收发器。
- 修改接线、插拔模块或测量终端电阻前，建议先断电。

## 3. 引脚表与时钟树

### 3.1 引脚表

| 功能 | MCU 引脚 | 方向/复用 | 说明 |
|---|---|---|---|
| HSE | PF0/PF1 | OSC_IN/OSC_OUT | 24 MHz 外部晶振 |
| OLED/FRAM SCL | PC6 | I2C4_SCL | 外部上拉 |
| OLED/FRAM SDA | PC7 | I2C4_SDA | 外部上拉 |
| Encoder A/B | PC0/PC1 | TIM1_CH1/CH2 | 硬件正交译码 |
| 独立按键 | PC2 | EXTI2 输入 | 内部上拉，低有效 |
| SW_1/SW_2 | PC14/PC15 | GPIO 输入 | 板外上拉，ON 为低 |
| 状态 LED | PB11 | GPIO 输出 | 低电平点亮 |
| SD SCK/CS | PE2/PE4 | SPI4/GPIO | CS 软件控制，低有效 |
| SD MISO/MOSI | PE5/PE6 | SPI4 | Mode 0 |
| UART2 TX/RX | PD5/PD6 | USART2 | RX 使用中断 |
| UART3 TX/RX | PB10/PE15 | USART3 | 示例主要使用 TX |
| CAN RX/TX | PD0/PD1 | FDCAN1 | 连接 TCAN3413 数字侧 |
| CAN STB | PD4 | GPIO 输出 | 低电平进入 Normal |
| SWD | PA13/PA14 | SWDIO/SWCLK | 下载与调试 |

PC13 编码器自带按键和 SD 卡插入检测脚在当前版本中不使用。PC14/PC15 被占用后不能同时使用
32.768 kHz LSE。

### 3.2 系统时钟

配置来源是 [`G474_Starter.ioc`](G474_Starter.ioc)，生成结果在
[`main.c`](Core/Src/main.c) 的 `SystemClock_Config()`：

```text
HSE = 24 MHz
PLL 输入 = 24 / PLLM(6) = 4 MHz
VCO = 4 * PLLN(85) = 340 MHz
SYSCLK = VCO / PLLR(2) = 170 MHz
HCLK  = 170 MHz
PCLK1 = 170 MHz
PCLK2 =  85 MHz
TIM1 时钟 = 170 MHz（APB2 分频不为 1 时，定时器时钟自动乘 2）
FDCAN kernel clock = 24 MHz（直接选择 HSE）
```

SPI4 使用 PCLK2，因此初始化速度为 `85 MHz / 256 = 332.031 kHz`，卡初始化后为
`85 MHz / 16 = 5.3125 MHz`。

## 4. 工程目录：哪些能改，哪些由 CubeMX 生成

```text
G474_Starter/
├─ G474_Starter.ioc          CubeMX 配置源
├─ Core/Inc, Core/Src        CubeMX 生成的启动与外设初始化代码
│  └─ Src/main.c             菜单、主循环和 HAL 回调位于 USER CODE 区域
├─ User/Inc                  用户驱动公开接口
├─ User/Src                  用户驱动实现
├─ ThirdParty/FatFs          FatFs 文件系统
├─ Drivers                   STM32 HAL/CMSIS，不要直接修改
├─ CMakeLists.txt            用户源码、包含目录和警告选项
└─ README.md                 本教程
```

推荐阅读顺序：

1. [`main.c`](Core/Src/main.c)：初始化顺序和 `while (1)`。
2. [`oled.h`](User/Inc/oled.h) 与 [`oled.c`](User/Src/oled.c)：一个简单驱动。
3. [`fram.c`](User/Src/fram.c)：I2C 存储器。
4. [`encoder.c`](User/Src/encoder.c)：TIM Encoder Interface。
5. [`sdcard.c`](User/Src/sdcard.c) 与 [`sd_diskio.c`](User/Src/sd_diskio.c)：协议层和文件系统层。
6. [`can_demo.c`](User/Src/can_demo.c)：经典 CAN 收发。

`gpio.c`、`i2c.c`、`spi.c`、`usart.c`、`tim.c`、`fdcan.c` 是 CubeMX 根据 `.ioc` 生成的代码。
可以阅读，但配置应回到 CubeMX 修改。

## 5. 启动顺序和单主循环

```text
复位
  -> HAL_Init()                     初始化 HAL 和 1 ms SysTick
  -> SystemClock_Config()           设置 170 MHz 系统时钟
  -> MX_GPIO/I2C/SPI/..._Init()     CubeMX 生成的寄存器配置
  -> OLED_Init()/FRAM_Init()        无损探测 I2C 设备
  -> Encoder_Init()/CAN_Init()      启动 TIM1 和 FDCAN1
  -> HAL_UART_Receive_IT()          启动 UART2 第一个字节接收
  -> while (1)                      永久运行菜单
```

主循环是“协作式状态机”：每次更新输入，然后只执行当前页面一次。页面函数不能再写永久
`while`，否则其他页面和长按返回都会失效。

```c
while (1)
{
    Encoder_Update();
    Key_Update();
    switch (current_mode)
    {
        case MODE_MENU: Menu_Process(); break;
        case MODE_LED:  LED_TestProcess(); break;
        /* 其他页面 */
    }
}
```

周期动作使用 `HAL_GetTick()`，不使用长时间 `HAL_Delay()`：

```c
uint32_t now = HAL_GetTick();
if ((now - last_tick) >= 500U)
{
    last_tick = now;
    HAL_GPIO_TogglePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin);
}
```

## 6. OLED 菜单和 `DemoMode`

**它是什么：** `DemoMode` 枚举表示当前只处于一个页面，比十个独立布尔标志更安全，不会让
两个页面同时启用。`menu_text[]` 保存文字，`menu_mode[]` 保存对应枚举。

**源码位置：** 枚举、菜单数组、主循环和页面函数都在 [`main.c`](Core/Src/main.c)。

**执行流程：** `Menu_Process()` 比较编码器本次与上次位置，更新 `menu_index`；短按调用
`Mode_Enter()`。`mode_first_entry` 控制页面的一次性初始化，`screen_dirty` 请求立即刷新，
否则 OLED 最多每 100 ms 刷新一次。

**常见错误：** 新增文字却没同步增加 `menu_mode[]` 会造成页面错位；页面中写死循环会令返回
失效；忘记消费事件会导致一次按键执行多次。

## 7. GPIO 输出：PB11 低电平 LED

**它是什么：** GPIO 输出可主动输出高低电平。本板 PB11 输出低时 LED 亮，输出高时灭。

**CubeMX：** PB11 为 Push-Pull Output，初始 High，避免上电误亮。初始化见
[`gpio.c`](Core/Src/gpio.c)，示例见 [`main.c`](Core/Src/main.c) 的 `LED_TestProcess()`。

**最小代码：**

```c
HAL_GPIO_WritePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin, GPIO_PIN_RESET); /* 亮 */
HAL_Delay(200U);
HAL_GPIO_WritePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin, GPIO_PIN_SET);   /* 灭 */
```

当前页面用 `HAL_GetTick()` 每 500 ms 调用 `HAL_GPIO_TogglePin()`，退出时关闭 LED。常亮时先检查
是否误判极性、程序是否卡在异常函数；不亮时检查 PB11、限流电阻和焊接。练习：让短按切换
100/500/1000 ms 三种周期。

## 8. PC2 外部中断、20 ms 消抖和长短按

**它是什么：** EXTI 在引脚边沿时中断 CPU。PC2 内部上拉，按下接地，所以使用下降沿。但机械
触点会抖动，下降沿只能表示“可能按下”，不能直接算一次有效按键。

**CubeMX：** PC2=`GPIO_EXTI2`、Falling edge、Pull-up；EXTI2 优先级 3。中断入口在
[`stm32g4xx_it.c`](Core/Src/stm32g4xx_it.c)，回调和状态机在 [`main.c`](Core/Src/main.c)。

**最小代码：**

```c
void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
    if (pin == KEY_Pin)
        key_irq_flag = 1U; /* 中断中只通知主循环 */
}
```

`Key_Update()` 在主循环确认低电平持续 20 ms，松开也确认 20 ms；不足 800 ms 产生短按，达到
800 ms 只产生一次长按。中断中不刷新 OLED、不打印、不延时，因为 ISR 应尽快返回。

正常时短按只计一次，长按返回且不附带短按。重复触发通常是缺少释放消抖；无反应时量 PC2
空闲是否高、按下是否低，并检查 EXTI2 IRQ。练习：把阈值改为 1200 ms 并显示按住时间。

## 9. PC14/PC15 拨码开关

**它是什么：** GPIO 输入读取外部电平。SW_1=PC14、SW_2=PC15，板外上拉，ON 时接地，因此
`GPIO_PIN_RESET` 表示 ON。

**CubeMX 和源码：** 两脚为 Input、No pull。读取在 `Switch_TestProcess()`：

```c
uint8_t sw1 =
    (HAL_GPIO_ReadPin(SW_1_GPIO_Port, SW_1_Pin) == GPIO_PIN_RESET);
uint8_t sw2 =
    (HAL_GPIO_ReadPin(SW_2_GPIO_Port, SW_2_Pin) == GPIO_PIN_RESET);
uint8_t value = (uint8_t)((sw2 << 1U) | sw1);
```

正常时 OLED 实时显示 ON/OFF 与二进制组合。状态飘动通常是外部上拉缺失或开关公共端未接地。
练习：用组合值选择四种 LED 周期。

## 10. TIM1 Encoder Interface

**它是什么：** 编码器 A/B 两相相差约 90°，哪一相先变化表示方向。TIM Encoder Interface
由硬件完成正交译码，比两路 GPIO 中断或普通输入捕获更适合测位置和方向。

**连接和 CubeMX：** PC0=`TIM1_CH1`，PC1=`TIM1_CH2`；Mode=`TI12`，Period=`65535`，两输入
上升沿，数字滤波 0。板上已有 10 kΩ + 100 nF 硬件滤波。

**源码：** 参数在 [`tim.c`](Core/Src/tim.c)，驱动在 [`encoder.c`](User/Src/encoder.c)，接口在
[`encoder.h`](User/Inc/encoder.h)。

```c
Encoder_Init();
while (1)
{
    Encoder_Update();
    int32_t position = Encoder_GetCount();
}
```

TIM1 是 16 位。两次读数之差转为 `int16_t` 后，`65535 -> 0` 得到 `+1`，反向得到 `-1`，
再累计成 32 位。当前每个机械格按 2 个原始计数换算；编码器不同可改
`ENCODER_COUNTS_PER_DETENT`。方向颠倒可交换 A/B。练习：测量转满一圈的机械格数。

## 11. I2C 基础、外部上拉和地址左移

**它是什么：** I2C 用 SCL/SDA 连接多个设备，靠地址区分。OLED 为 `0x3C`，FRAM 为 `0x50`。
开漏输出只能拉低，因此总线必须有上拉。

**CubeMX：** I2C4 PC6/PC7、7 位地址、约 100 kHz、外部上拉、Analog Filter 开、Digital
Filter=0，kernel clock=PCLK1=170 MHz。生成代码在 [`i2c.c`](Core/Src/i2c.c)。

HAL 的设备地址参数预留最低位给读写位，因此 7 位地址调用时左移：

```c
HAL_StatusTypeDef status =
    HAL_I2C_IsDeviceReady(&hi2c4, 0x3CU << 1U, 3U, 100U);
```

头文件仍定义 `0x3C`，不要直接改写成 `0x78`。没有 ACK 时检查地址、供电、共地和 SDA/SCL；
总线一直低时检查短路或某设备拉住总线；边沿太慢时检查上拉和总线电容。练习：用
`HAL_I2C_IsDeviceReady()` 扫描 `0x01~0x7E`。

## 12. SSD1306 OLED

**它是什么：** SSD1306 兼容 128x64 OLED 与 FRAM 共用 I2C4，7 位地址 `0x3C`。控制字节
`0x00` 代表命令，`0x40` 代表显示数据。

**源码：** [`oled.h`](User/Inc/oled.h)、[`oled.c`](User/Src/oled.c)、
[`font.h`](User/Inc/font.h)、[`font.c`](User/Src/font.c)。

```c
OLED_Init();
OLED_Clear();
OLED_ShowString(0U, 0U, "HELLO G474");
OLED_DrawPixel(10U, 20U, 1U);
(void)OLED_Refresh();
```

绘图只改 RAM 帧缓冲，调用 `OLED_Refresh()` 后屏幕才变化。128x64 分成 8 个 Page，每页
8x128 像素，总缓冲 1024 字节。刷新函数比较帧缓冲和影子缓冲，只发送变化的 Page，减少闪烁。
6x8 ASCII 按列存储，8x16 数字按行存储。

正常时 OLED Test 边框连续、文字清晰、对角线到达四角。花屏检查芯片兼容性、扫描方向和地址；
不亮时观察 `OLED_Ready` 并检查 I2C。练习：基于 `OLED_DrawPixel()` 写画水平线函数。

## 13. MB85RC64T FRAM

**它是什么：** FRAM 是断电保持的存储器，接口类似 EEPROM，但无需等待毫秒级写周期。
MB85RC64T 容量 8 KiB，地址 `0x0000~0x1FFF`；A0/A1/A2 接地后 7 位地址 `0x50`。

**源码：** [`fram.h`](User/Inc/fram.h)、[`fram.c`](User/Src/fram.c)。

```c
uint8_t tx[2] = {0x12U, 0x34U};
uint8_t rx[2];
if (FRAM_Init() == HAL_OK)
{
    (void)FRAM_Write(0x0100U, tx, sizeof(tx));
    (void)FRAM_Read(0x0100U, rx, sizeof(rx));
}
```

底层使用 `HAL_I2C_Mem_Read/Write()` 和 `I2C_MEMADD_SIZE_16BIT`，并先做越界检查。
`FRAM_SelfTest()` 备份最后 16 字节，写入测试图案、读回比较、恢复并再次校验。正常显示
`DEVICE:READY` 和 `RESULT:PASS`。练习：在其他地址保存启动次数，不要占用最后 16 字节。

## 14. SPI 基础、Mode 0 和软件片选

**它是什么：** SPI 使用 SCK、MOSI、MISO 和 CS。没有设备地址，每个从机需要独立 CS。本工程
SD 使用 SPI4，PE4 是 GPIO 软件片选。

**CubeMX：** Master、Full Duplex、8 bit、MSB first；Mode 0（CPOL Low、CPHA 1 Edge）；
Software NSS、NSS Pulse 关闭；初始 Prescaler=256。配置见 [`spi.c`](Core/Src/spi.c)。

```c
uint8_t tx = 0xFFU, rx;
HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_RESET);
(void)HAL_SPI_TransmitReceive(&hspi4, &tx, &rx, 1U, 100U);
HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);
```

SPI 是全双工：读取时也要发送占位字节来提供时钟。常见错误是 CPOL/CPHA 不匹配、MOSI/MISO
接反、CS 未拉低或电压错误。练习：用逻辑分析仪观察 CMD0 的 6 字节包。

## 15. SD SPI 协议和 FatFs

[`sdcard.c`](User/Src/sdcard.c) 负责 SD 命令和 512 字节块；
[`sd_diskio.c`](User/Src/sd_diskio.c) 把块读写适配为 FatFs 磁盘接口；
[`ff.c`](ThirdParty/FatFs/src/ff.c) 负责目录、文件名和文件内容。

```text
f_write() -> FatFs -> disk_write(pdrv=0) -> SD_WriteBlock() -> SPI4
```

SD 进入 SPI 模式时要求低速。驱动以约 332 kHz、CS 高发送至少 80 个空闲时钟，然后：

1. CMD0 进入 Idle。
2. CMD8 检查新卡和电压范围。
3. CMD55+ACMD41 等待初始化。
4. CMD58 读 OCR 并判断 SDHC/SDXC。
5. 旧 SDSC 用 CMD16 固定 512 字节块长。
6. 提速至约 5.31 MHz，用 CMD9 读取 CSD 容量。

SDHC/SDXC 参数是块号，旧 SDSC 参数是字节地址，驱动会自动换算。

```c
if (SD_FileTest())
{
    /* 初始化、挂载、写入、重新打开、校验均成功 */
}
```

`SD_FileTest()` 会覆盖 `G474_DEMO.TXT`，关闭后重新打开，并校验内容
`STM32G474 SD CARD TEST\r\n`。文件名主体超过 8 个字符，因此
[`ffconf.h`](ThirdParty/FatFs/src/ffconf.h) 已启用 LFN；支持 FAT，不支持 exFAT。

- Init ERR：检查 3.3 V、CS/SCK/MISO/MOSI、Mode 0 和低速波形。
- Mount ERR：检查是否 FAT16/FAT32、分区和块读取。
- Write ERR：检查供电跌落、MOSI、写保护和忙超时。
- Verify ERR：检查 MISO、信号完整性和卡可靠性。
- 调试变量：`SD_State`、`SD_LastR1`、`SD_HighCapacity`、`SD_BlockCount`。

练习：在文件中写入递增计数，并始终检查 `FRESULT` 和实际读写字节数。

## 16. UART：阻塞发送与单字节中断接收

**它是什么：** UART 是异步串口，两端要共地并使用相同参数。USART2/3 均为 115200 8N1、
无流控、16 倍过采样。配置见 [`usart.c`](Core/Src/usart.c)。USART2 是 PD5/PD6 并启用 RX 中断；
USART3 是 PB10/PE15，页面主要演示 TX。

```c
static const uint8_t text[] = "USART3 TEST\r\n";
(void)HAL_UART_Transmit(&huart3, (uint8_t *)text,
                        (uint16_t)(sizeof(text) - 1U), 100U);
```

中断接收必须先启动，并在每次完成回调后重新启动：

```c
HAL_UART_Receive_IT(&huart2, &uart2_rx_byte, 1U);

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        uart2_last_byte = uart2_rx_byte;
        uart2_rx_pending = 1U;
        HAL_UART_Receive_IT(&huart2, &uart2_rx_byte, 1U);
    }
}
```

回调只保存数据，主循环再回显，避免 ISR 中阻塞。共享变量用 `volatile`。正常时 UART2 输入字符
会原样返回并更新 OLED；UART3 每秒输出 `USART3 TEST: n`。乱码检查 115200 8N1、TTL 电平、
TX/RX 交叉和共地。当前只保存最新字节，高速连续数据应升级为环形缓冲或 DMA。练习：把小写
字母转换成大写回显。

## 17. FDCAN：500 kbit/s 经典 CAN

**它是什么：** FDCAN1 输出数字 TX/RX，TCAN3413 转换为 CAN_H/CAN_L。PD4 STB 低电平为
Normal。CAN 至少需要另一个节点提供 ACK；节点需要可靠参考地。总线两端各 120 Ω，断电测
CAN_H 与 CAN_L 通常约 60 Ω。

**CubeMX：** kernel clock=HSE 24 MHz；Classic、Normal、Auto Retransmission；Prescaler=3、
Seg1=13、Seg2=2、SJW=2；一个标准过滤器、无扩展过滤器、FIFO0 中断。

```text
每 bit = 1 + Seg1 + Seg2 = 16 tq
波特率 = 24 MHz / 3 / 16 = 500 kbit/s
采样点 = (1 + 13) / 16 = 87.5%
```

初始化参数见 [`fdcan.c`](Core/Src/fdcan.c)，驱动见 [`can_demo.c`](User/Src/can_demo.c)，接口见
[`can_demo.h`](User/Inc/can_demo.h)。

```c
uint8_t data[8] = {'G', '4', '7', '4', 0, 0, 0, 0};
CAN_Frame frame;
if (CAN_Init() == HAL_OK)
    (void)CAN_SendStd(0x123U, data, sizeof(data));

CAN_Process();
if (CAN_GetLastFrame(&frame))
{
    /* 使用 frame.id、frame.length、frame.data[] */
}
```

`CAN_Init()` 拉低 STB，接收全部 `0x000~0x7FF` 标准数据帧，拒绝扩展帧和远程帧，再启动
FIFO0/Bus-Off 通知。发送明确使用 `FDCAN_CLASSIC_CAN`、`FDCAN_BRS_OFF`。中断只保存最新帧。

PCAN-View 设置：经典 CAN（SJA1000）、500 kbit/s、Standard 11-bit、过滤范围 `000~7FF`。
PCAN 与板共地并正确接终端。短按发送 ID `0x123`、DLC 8，数据为 ASCII `G474` 加 32 位
小端计数器。无 ACK 时可能 Bus-Off，但菜单不应阻塞。无报文时检查波特率、STB、H/L、终端、
地线和 ACK 节点。练习：让 PCAN 发 `0x321`，观察 OLED 最新帧。

## 18. 添加一个新的菜单测试页面

以蜂鸣器页面为例。先在 CubeMX 为真实引脚配置 `GPIO_Output` 并标记为 `BUZZER`；本工程没有
预设蜂鸣器脚，不要随意占用未知引脚。

1. 在 [`main.c`](Core/Src/main.c) 的 `DemoMode` 增加 `MODE_BUZZER`。
2. `MENU_ITEM_COUNT` 加 1，`menu_text[]` 增加 `"Buzzer Test"`。
3. `menu_mode[]` 同位置增加 `MODE_BUZZER`。
4. USER CODE 函数声明区增加 `static void Buzzer_TestProcess(void);`。
5. USER CODE 0 实现处理函数。
6. 主循环 `switch` 增加对应 `case`。

```c
static void Buzzer_TestProcess(void)
{
    if (mode_first_entry)
    {
        HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
        mode_first_entry = 0U;
        screen_dirty = 1U;
    }
    if (key_short_event)
    {
        HAL_GPIO_TogglePin(BUZZER_GPIO_Port, BUZZER_Pin);
        screen_dirty = 1U;
    }
    if (Display_IsDue())
    {
        OLED_Clear();
        OLED_ShowString(0U, 0U, "BUZZER TEST");
        OLED_ShowString(0U, 16U, "Press=Toggle");
        Display_Footer();
        (void)OLED_Refresh();
    }
}
```

若蜂鸣器需要 PWM，应在 CubeMX 中配置定时器 PWM，而不是永久循环快速翻转 GPIO。

## 19. CubeMX 重新生成代码的正确方法

1. 先备份当前能编译的版本。
2. 打开 [`G474_Starter.ioc`](G474_Starter.ioc)，在 Pinout、Clock、NVIC 或外设页面修改。
3. Project Manager 保持 Toolchain=CMake、`Keep User Code` 开启。
4. Generate Code，随后执行干净构建并检查差异。

CubeMX 只保证保留 `USER CODE BEGIN/END` 之间的内容。本工程 `main.c` 的自定义代码和注释都在
这些区域，完整驱动放在 `User/`。不要修改 `Drivers/`。生成后确认 `CMakeLists.txt` 中
`User/Src`、`User/Inc` 和 `ThirdParty/FatFs` 仍存在。

若进入 `Default_Handler`，通常是启用了某 IRQ 却没有对应处理函数，或启动文件与芯片不匹配。
检查调用栈里的 IRQ、[`stm32g4xx_it.c`](Core/Src/stm32g4xx_it.c)、`.ioc` 和芯片型号。

## 20. 常见故障排查

| 现象 | 优先检查 |
|---|---|
| ST-LINK `No target` | 3.3 V、共地、SWDIO/SWCLK、NRST、焊接、Connect under reset |
| 停在 `Default_Handler` | 调用栈 IRQ、IRQHandler、启动文件和 MCU 型号 |
| 进入 `Error_Handler` | 给各 `MX_*_Init()` 和用户初始化返回处下断点，找首个失败调用 |
| OLED 不亮 | 0x3C ACK、SCL/SDA 上拉、3.3 V、SSD1306 兼容性 |
| FRAM ERROR | A0/A1/A2 接地、0x50、I2C 波形、16 位内部地址 |
| SD Init/Mount ERR | SPI Mode 0、低速、接线、供电、FAT16/FAT32（不支持 exFAT） |
| 编码器不动 | PC0/PC1 复用、`Encoder_Init()`、A/B 接线 |
| 按键一次多次 | 20 ms 按下与释放消抖、低有效电平 |
| UART 乱码 | 115200 8N1、TTL 电平、TX/RX 交叉、共地 |
| CAN 无报文 | 500 kbit/s、经典 CAN、STB=Low、终端、共地、ACK 节点 |
| CAN Bus-Off | 波特率、接线或无 ACK；排除后复位或重新初始化 |

调试应从底层向上：先量供电和静态电平，再核对引脚/时钟，再看 HAL 返回值，最后检查 OLED
显示逻辑。I2C、SPI、CAN 问题用逻辑分析仪或示波器通常比反复改代码更快。

## 21. 术语表

| 术语 | 含义 |
|---|---|
| HAL | ST 提供的硬件抽象库 API |
| Handle | 如 `hi2c4`，保存外设实例、配置和运行状态的结构体 |
| GPIO AF | Alternate Function，引脚交给 UART/SPI/TIM 等外设控制 |
| EXTI | 外部中断/事件控制器 |
| ISR/IRQ | 中断服务程序/中断请求 |
| Debounce | 机械触点消抖 |
| FIFO | 先进先出缓冲区 |
| ACK | 接收方确认；CAN 与 I2C 的机制不同 |
| DLC | CAN 数据长度编码 |
| Bus-Off | CAN 错误过多后节点离开总线的保护状态 |
| Sector/Block | SD/FatFs 的 512 字节逻辑单位 |
| Frame buffer | OLED 在 RAM 中准备的一整幅画面 |
| `static` | 限制文件内可见性，或让局部变量跨调用保存值 |
| `volatile` | 变量可能被中断或硬件异步改变，必须实际读取内存 |

## 22. 建议练习顺序

1. 修改 LED 周期，理解 `HAL_GetTick()`。
2. 将拨码组合显示为十进制，理解 GPIO 与位运算。
3. 给 OLED 增加画线函数，理解帧缓冲。
4. 用 FRAM 保存启动计数，理解非易失存储。
5. 给 UART2 增加大写回显，再学习环形缓冲。
6. 改 CAN ID 和数据格式，用 PCAN 验证字节序。
7. 在 SD 文件中保存计数，理解打开、写入、关闭、重新读取。
8. 按第 18 章增加新页面，完成一次 CubeMX 配置与用户代码扩展。

每次只改一个功能，先编译，再下载，再记录正常现象。遇到问题时从对应章节的源码位置和常见
错误开始检查，尽量不要同时修改时钟、引脚和驱动三层。
