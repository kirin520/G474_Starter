# 外设原理与代码教学

本分册用于完成第一次烧录之后的学习。它不要求你一次读完，建议按“简单 GPIO → I2C → SPI/SD → UART → CAN”的顺序练习。

[返回入门 README](https://github.com/kirin520/G474_Starter/blob/main/README.md) · [故障排查](https://github.com/kirin520/G474_Starter/blob/main/docs/TROUBLESHOOTING.md)

## 1. 阅读路线与程序主线

| 难度 | 建议章节 | 先学到什么 |
|---|---|---|
| 入门 | 主循环、GPIO、按键、拨码 | HAL 基本调用、输入输出、非阻塞定时 |
| 基础通信 | I2C、OLED、FRAM | 总线、地址、读写和驱动分层 |
| 定时器 | Encoder | 外设硬件计数与溢出处理 |
| 进阶通信 | SPI、SD、FatFs | 协议层、块设备、文件系统 |
| 中断通信 | UART | 阻塞发送、单字节中断接收、回调 |
| 总线通信 | FDCAN | 波特率、过滤器、ACK、Bus-Off |

程序从 [`main.c`](../Core/Src/main.c) 开始：

```text
复位
  -> HAL_Init()                     HAL 与 1 ms SysTick
  -> SystemClock_Config()           170 MHz 系统时钟
  -> MX_GPIO/I2C/SPI/..._Init()     CubeMX 生成的外设初始化
  -> OLED/FRAM/Encoder/CAN_Init()   用户驱动初始化
  -> HAL_UART_Receive_IT()          开始接收 UART2 第一个字节
  -> while (1)                      更新输入并运行当前菜单页
```

菜单使用一个 `DemoMode` 枚举表示当前页面。主循环每次只调用当前页面一次，而不是进入第二个永久 `while`：

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

这样 PC2 长按、OLED 刷新和其他外设都不会因为某个页面阻塞而失效。

## 2. GPIO 输出：PB11 低电平 LED

### 它是什么

GPIO 输出能主动输出高电平或低电平。本板 LED 接法决定了 PB11 输出低电平时点亮，高电平时熄灭。

### 本板连接与 CubeMX 配置

- PB11：`GPIO_Output`、Push-Pull、初始 High。
- 引脚名称：`STATUS_LED`。
- 初始化代码：[`gpio.c`](../Core/Src/gpio.c)。
- 页面示例：[`main.c`](../Core/Src/main.c) 中 `LED_TestProcess()`。

### 最小代码

```c
HAL_GPIO_WritePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin, GPIO_PIN_RESET); /* 点亮 */
HAL_GPIO_WritePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin, GPIO_PIN_SET);   /* 熄灭 */
HAL_GPIO_TogglePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin);               /* 翻转 */
```

菜单页面使用 `HAL_GetTick()` 每 500 ms 翻转一次：

```c
uint32_t now = HAL_GetTick();
if ((now - last_tick) >= 500U)
{
    last_tick = now;
    HAL_GPIO_TogglePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin);
}
```

这比 `HAL_Delay(500)` 更适合主循环，因为等待期间仍能处理按键和 OLED。

**正常现象：** LED 每 500 ms 改变一次状态，退出页面后熄灭。  
**常见错误：** 忘记低有效会把亮灭判断写反；程序进入异常也可能表现为常亮。  
**练习：** 用 PC2 短按切换 100、500、1000 ms 三种周期。

## 3. PC2 外部中断、消抖和长短按

### 它是什么

EXTI 可以在引脚出现边沿时打断主程序。PC2 空闲时被上拉，按下接地，因此按下产生下降沿。机械按键会抖动，所以一次边沿只能表示“可能按下”，仍需软件确认。

### 配置与源码

- PC2：`GPIO_EXTI2`、Falling edge、Pull-up、低有效。
- IRQ 优先级：3。
- 中断入口：[`stm32g4xx_it.c`](../Core/Src/stm32g4xx_it.c)。
- 回调和 20 ms 消抖：[`main.c`](../Core/Src/main.c) 的 `HAL_GPIO_EXTI_Callback()` 与 `Key_Update()`。

### 最小代码

```c
void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
    if (pin == KEY_Pin)
    {
        key_irq_flag = 1U; /* 中断只通知主循环 */
    }
}
```

主循环确认按下和释放均稳定 20 ms；按住不足 800 ms 产生短按，达到 800 ms 产生一次长按。中断中不要刷新 OLED、发送长串口数据或调用 `HAL_Delay()`。

**正常现象：** 短按只计一次，长按返回菜单且不附带短按。  
**常见错误：** 没有释放消抖会一次触发多次；引脚空闲不是高电平时先查硬件。  
**练习：** 将长按时间改为 1200 ms，并在 OLED 显示当前按住时间。

## 4. PC14/PC15 拨码开关

### 它是什么

GPIO 输入用于读取外部高低电平。SW_1、SW_2 有板外上拉，拨到 ON 时接地，所以低电平代表 ON。

### 配置、源码和最小代码

- PC14=`SW_1`、PC15=`SW_2`：Input、No pull。
- 示例：[`main.c`](../Core/Src/main.c) 中 `Switch_TestProcess()`。

```c
uint8_t sw1 =
    (HAL_GPIO_ReadPin(SW_1_GPIO_Port, SW_1_Pin) == GPIO_PIN_RESET);
uint8_t sw2 =
    (HAL_GPIO_ReadPin(SW_2_GPIO_Port, SW_2_Pin) == GPIO_PIN_RESET);
uint8_t value = (uint8_t)((sw2 << 1U) | sw1);
```

**正常现象：** OLED 实时显示两个开关的 ON/OFF 和 0～3 组合值。  
**常见错误：** 状态随机变化通常是上拉缺失、公共端未接地或焊接问题。  
**练习：** 用组合值选择四种 LED 闪烁周期。

## 5. TIM1 Encoder Interface

### 它是什么

旋转编码器 A/B 两相相差约 90°，哪一相先变化表示方向。TIM Encoder Interface 能在硬件中完成正交译码，比用两路 GPIO 中断或普通输入捕获更适合测位置。

### 本板配置与源码

- PC0=`TIM1_CH1`，PC1=`TIM1_CH2`。
- Mode=`TI12`，Period=`65535`，两路上升沿，数字滤波 0。
- 板上已有 10 kΩ + 100 nF 硬件滤波。
- CubeMX 代码：[`tim.c`](../Core/Src/tim.c)。
- 驱动接口：[`encoder.h`](../User/Inc/encoder.h)。
- 驱动实现：[`encoder.c`](../User/Src/encoder.c)。

### 最小代码

```c
Encoder_Init();

while (1)
{
    Encoder_Update();
    int32_t position = Encoder_GetCount();
    /* 使用 position */
}
```

TIM1 计数器是 16 位。驱动把两次读数的差转成 `int16_t`，因此 `65535 -> 0` 会正确得到 `+1`，反方向得到 `-1`，再累计成 32 位位置。当前默认每个机械格为 2 个原始计数。

**正常现象：** 正反转位置分别增减，越过 0/65535 时不跳变。  
**常见错误：** 方向相反可交换 A/B；数值不变时检查 PC0/PC1 复用和 `Encoder_Init()`。  
**练习：** 测量一圈的机械格数并调整 `ENCODER_COUNTS_PER_DETENT`。

## 6. I2C4：一条总线连接 OLED 和 FRAM

### 它是什么

I2C 使用 SCL 和 SDA 两根线连接多个设备，设备靠地址区分。本板 OLED 地址为 `0x3C`，FRAM 地址为 `0x50`。I2C 引脚为开漏输出，只能主动拉低，因此总线必须有上拉电阻。

### 本板配置与源码

- PC6=`I2C4_SCL`，PC7=`I2C4_SDA`。
- 7 位寻址，约 100 kHz，外部上拉，Analog Filter 开，Digital Filter=0。
- Kernel clock=PCLK1=170 MHz。
- 生成代码：[`i2c.c`](../Core/Src/i2c.c)。

HAL 把地址参数最低位留给读写位，所以传入 7 位地址时要左移：

```c
HAL_StatusTypeDef status =
    HAL_I2C_IsDeviceReady(&hi2c4, 0x3CU << 1U, 3U, 100U);
```

头文件中仍应写人类常用的 7 位地址 `0x3C`，不要改成 `0x78`。

**正常现象：** `0x3C` 和 `0x50` 都能应答。  
**常见错误：** 没 ACK 时检查地址、3.3 V、共地和上拉；SDA/SCL 一直低时检查短路或从机锁住总线。  
**练习：** 扫描 `0x01~0x7E` 并通过 UART 打印应答地址。

## 7. SSD1306 OLED

### 它是什么

SSD1306 兼容 128×64 OLED 使用 I2C4，7 位地址 `0x3C`。发送前的控制字节 `0x00` 表示命令，`0x40` 表示显示数据。

### 源码位置与接口

- [`oled.h`](../User/Inc/oled.h)：公开函数。
- [`oled.c`](../User/Src/oled.c)：初始化、帧缓冲、像素和刷新。
- [`font.h`](../User/Inc/font.h)、[`font.c`](../User/Src/font.c)：ASCII 与数字字模。

### 最小代码

```c
OLED_Init();
OLED_Clear();
OLED_ShowString(0U, 0U, "HELLO G474");
OLED_DrawPixel(10U, 20U, 1U);
(void)OLED_Refresh();
```

绘图函数只修改 RAM 中的 1024 字节帧缓冲；`OLED_Refresh()` 才把它发送到屏幕。128×64 被分为 8 个 Page，每页包含 8×128 像素。刷新时比较影子缓冲，只发送变化的 Page，以减少闪烁和 I2C 流量。

**正常现象：** OLED Test 的边框完整、文字清晰、图案切换正常。  
**常见错误：** 不亮先查 `OLED_Ready` 和 0x3C；花屏再查控制器兼容性和扫描方向。  
**练习：** 基于 `OLED_DrawPixel()` 实现水平线和矩形函数。

## 8. MB85RC64T FRAM

### 它是什么

FRAM 是掉电保持存储器，读写接口类似 EEPROM，但不需要等待毫秒级内部写周期。MB85RC64T 容量 8 KiB，内部地址 `0x0000~0x1FFF`；A0/A1/A2 接地后 7 位地址为 `0x50`。

### 源码和最小代码

- [`fram.h`](../User/Inc/fram.h)：公开接口。
- [`fram.c`](../User/Src/fram.c)：地址检查和安全自检。

```c
uint8_t tx[2] = {0x12U, 0x34U};
uint8_t rx[2];

if (FRAM_Init() == HAL_OK)
{
    (void)FRAM_Write(0x0100U, tx, sizeof(tx));
    (void)FRAM_Read(0x0100U, rx, sizeof(rx));
}
```

底层使用 `HAL_I2C_Mem_Read/Write()` 和 `I2C_MEMADD_SIZE_16BIT`。`FRAM_SelfTest()` 会备份最后 16 字节，写测试图案、读回比较、恢复原数据并再次校验。

**正常现象：** 页面显示 `DEVICE:READY` 和 `RESULT:PASS`。  
**常见错误：** 检查 A0/A1/A2、0x50、16 位内部地址和范围边界。  
**练习：** 在其他地址保存启动次数，不要占用最后 16 字节测试区。

## 9. SPI4：Mode 0 和软件片选

### 它是什么

SPI 使用 SCK、MOSI、MISO 和 CS。它没有 I2C 那样的设备地址，每个从设备通常需要独立片选。本工程 SD 卡使用 SPI4，PE4 是 GPIO 软件片选。

### 本板配置

- PE2=`SPI4_SCK`，PE5=`SPI4_MISO`，PE6=`SPI4_MOSI`。
- PE4=`SD_CS`，低有效。
- Master、Full Duplex、8 bit、MSB first。
- Mode 0：CPOL Low、CPHA 1 Edge。
- Software NSS，NSS Pulse 关闭。
- 初始化分频 `/256`，约 332 kHz。
- 配置代码：[`spi.c`](../Core/Src/spi.c)。

### 最小代码

```c
uint8_t tx = 0xFFU;
uint8_t rx;

HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_RESET);
(void)HAL_SPI_TransmitReceive(&hspi4, &tx, &rx, 1U, 100U);
HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);
```

SPI 是全双工的：读取时仍要发送占位字节才能产生时钟。

**常见错误：** CPOL/CPHA 错误、MOSI/MISO 接反、CS 未拉低、模块使用 5 V 电平。  
**练习：** 用逻辑分析仪观察 SD 的 CMD0 六字节命令包。

## 10. SD SPI 协议和 FatFs

### 三层代码分别做什么

- [`sdcard.c`](../User/Src/sdcard.c)：发送 SD 命令并读写 512 字节块。
- [`sd_diskio.c`](../User/Src/sd_diskio.c)：把块读写适配成 FatFs 的磁盘接口。
- [`ff.c`](../ThirdParty/FatFs/src/ff.c)：处理目录、文件名和文件内容。

```text
f_write() -> FatFs -> disk_write(pdrv=0) -> SD_WriteBlock() -> SPI4
```

### 初始化为什么先慢后快

SD 进入 SPI 模式需要低速。驱动先以约 332 kHz、CS 高发送至少 80 个空闲时钟，再依次发送：

1. CMD0：进入 Idle。
2. CMD8：检查新卡和电压范围。
3. CMD55 + ACMD41：等待初始化完成。
4. CMD58：读取 OCR 并判断是否 SDHC/SDXC。
5. 旧 SDSC 使用 CMD16 固定 512 字节块长。
6. 提速到约 5.31 MHz，再用 CMD9 读取容量。

SDHC/SDXC 的命令参数是块号，旧 SDSC 是字节地址，驱动会自动换算。

### 文件测试

```c
if (SD_FileTest())
{
    /* 初始化、挂载、写入、重新打开和校验均成功 */
}
```

`SD_FileTest()` 会覆盖根目录中的 `G474_DEMO.TXT`，写入后关闭文件，重新打开并校验 `STM32G474 SD CARD TEST\r\n`。当前支持 FAT16/FAT32，不支持 exFAT；长文件名已开启。

**正常现象：** Init、Mount、Write、Verify 全部 PASS。  
**常见错误：** Init ERR 查接线/低速；Mount ERR 查格式；Write ERR 查供电/MOSI；Verify ERR 查 MISO 和信号完整性。  
**练习：** 写入一个递增计数，并检查每次 `FRESULT` 与实际字节数。

## 11. UART：阻塞发送和单字节中断接收

### 它是什么

UART 是异步串口，两端必须共地并设置相同参数。本工程 USART2 和 USART3 都是 115200 8N1、无流控、16 倍过采样。

### 本板连接和源码

- USART2：PD5 TX、PD6 RX，使用单字节 RX 中断。
- USART3：PB10 TX、PE15 RX，示例页面主要使用 TX。
- CubeMX 配置：[`usart.c`](../Core/Src/usart.c)。
- 页面与回调：[`main.c`](../Core/Src/main.c)。

### 阻塞发送

```c
static const uint8_t text[] = "USART3 TEST\r\n";
(void)HAL_UART_Transmit(&huart3, (uint8_t *)text,
                        (uint16_t)(sizeof(text) - 1U), 100U);
```

### 中断接收

```c
HAL_UART_Receive_IT(&huart2, &uart2_rx_byte, 1U);

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        uart2_last_byte = uart2_rx_byte;
        uart2_rx_pending = 1U;
        (void)HAL_UART_Receive_IT(&huart2, &uart2_rx_byte, 1U);
    }
}
```

每接收一个字节后必须再次调用 `HAL_UART_Receive_IT()`。回调只保存数据，主循环再回显，避免中断中阻塞。被中断和主循环共同访问的标志使用 `volatile`。

**正常现象：** UART2 输入字符被原样回显；UART3 每秒输出 `USART3 TEST: n`。  
**常见错误：** 乱码查 115200 8N1、TTL 电平、TX/RX 交叉和共地。当前只保存最新字节，高速流数据应学习环形缓冲或 DMA。  
**练习：** 把 UART2 收到的小写字母转换为大写再回显。

## 12. FDCAN1：500 kbit/s 经典 CAN

### 它是什么

MCU 的 FDCAN1 输出数字 TX/RX，TCAN3413 收发器把它转换为差分 CAN_H/CAN_L。PD4 的 STB 拉低后收发器才进入 Normal。CAN 帧要由另一个节点 ACK，只有一块板时发送可能持续报错。

### 时钟和 CubeMX 配置

- FDCAN kernel clock：HSE 24 MHz。
- Classic CAN、Normal mode、Auto Retransmission。
- Prescaler=3，Seg1=13，Seg2=2，SJW=2。
- 标准过滤器 1 个，扩展过滤器 0 个，RX FIFO0 中断。

```text
每 bit = 1 + Seg1 + Seg2 = 16 tq
波特率 = 24 MHz / 3 / 16 = 500 kbit/s
采样点 = (1 + 13) / 16 = 87.5%
```

CubeMX 参数在 [`fdcan.c`](../Core/Src/fdcan.c)，驱动接口在 [`can_demo.h`](../User/Inc/can_demo.h)，实现见 [`can_demo.c`](../User/Src/can_demo.c)。

### 最小代码

```c
uint8_t data[8] = {'G', '4', '7', '4', 0, 0, 0, 0};
CAN_Frame frame;

if (CAN_Init() == HAL_OK)
{
    (void)CAN_SendStd(0x123U, data, sizeof(data));
}

CAN_Process();
if (CAN_GetLastFrame(&frame))
{
    /* 使用 frame.id、frame.length、frame.data[] */
}
```

`CAN_Init()` 把 PD4 拉低，接收所有 `0x000~0x7FF` 标准数据帧，拒绝扩展帧和远程帧。发送头明确使用 `FDCAN_CLASSIC_CAN` 与 `FDCAN_BRS_OFF`。

### PCAN 设置与接线

- PCAN-View：Classic CAN（SJA1000）、500 kbit/s、Standard 11-bit。
- 接收过滤范围：`000~7FF`。
- PCAN 与开发板必须共地。
- 总线两端各 120 Ω；断电测 CAN_H 到 CAN_L 通常约 60 Ω。

短按发送标准 ID `0x123`、DLC 8，内容是 ASCII `G474` 加 32 位小端计数器。

**正常现象：** PCAN 收到 0x123，OLED 能显示 PCAN 发来的标准帧。  
**常见错误：** 无报文查波特率、STB、CAN_H/L、终端、地线和 ACK；错误太多会进入 Bus-Off。  
**练习：** 让 PCAN 发送 `0x321`，观察 OLED 的最新 ID、DLC 和计数。

## 13. OLED 菜单怎么工作

[`main.c`](../Core/Src/main.c) 中：

- `DemoMode`：当前页面的唯一状态。
- `menu_text[]`：OLED 显示的菜单文字。
- `menu_mode[]`：每项文字对应的页面枚举。
- `Menu_Process()`：读取编码器并改变 `menu_index`。
- `Mode_Enter()`：进入页面并设置首次进入标志。
- `mode_first_entry`：只在页面首次进入时执行初始化。
- `screen_dirty`：请求尽快刷新 OLED，否则最多每 100 ms 刷一次。

一个互斥枚举比十个布尔标志更可靠：不会同时进入两个页面，也更容易从 `switch` 看出完整流程。

## 14. 添加一个新菜单页面

以蜂鸣器为例。先在 CubeMX 给真实引脚配置 `GPIO_Output` 并命名 `BUZZER`；本工程没有预留蜂鸣器脚，不要随意占用未知引脚。

1. 在 `DemoMode` 中增加 `MODE_BUZZER`。
2. `MENU_ITEM_COUNT` 加 1。
3. `menu_text[]` 增加 `"Buzzer Test"`。
4. `menu_mode[]` 的同一位置增加 `MODE_BUZZER`。
5. 声明并实现 `Buzzer_TestProcess()`。
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

如果蜂鸣器需要 PWM，应在 CubeMX 配置定时器 PWM，而不是在永久循环中高速翻转 GPIO。

## 15. CubeMX 重新生成代码

1. 提交或备份当前可编译版本。
2. 从 VS Code 的 STM32Cube 面板启动 CubeMX，再打开工程根目录的 `G474_Starter.ioc`，修改 Pinout、Clock、NVIC 或外设参数。
3. Project Manager 中保持 Toolchain=CMake，并启用 Keep User Code。
4. Generate Code。
5. 检查 Git 差异并执行干净构建。

CubeMX 只保证保留 `USER CODE BEGIN/END` 之间的内容。完整驱动放在 `User/`，不要修改 `Drivers/`。生成后确认 [`CMakeLists.txt`](../CMakeLists.txt) 仍包含 `User/Src` 和 FatFs 源码。

## 16. 建议练习顺序

1. 修改 LED 周期，理解 `HAL_GetTick()`。
2. 将拨码组合显示成十进制，理解 GPIO 与位运算。
3. 给 OLED 增加画线函数，理解帧缓冲。
4. 用 FRAM 保存启动计数，理解掉电存储。
5. 给 UART2 增加大写回显，再学习环形缓冲。
6. 修改 CAN ID 和数据格式，用 PCAN 验证字节序。
7. 在 SD 文件中保存计数，理解打开、写入、关闭和重新读取。
8. 增加一个新菜单页面，再用 CubeMX 配置真实硬件引脚。

每次只改一个功能，先编译，再下载，再记录正常现象。不要同时修改时钟、引脚和驱动三层。

## 17. 常用术语

| 术语 | 含义 |
|---|---|
| HAL | ST 提供的硬件抽象库 API |
| Handle | 如 `hi2c4`，保存外设实例、配置和运行状态的结构体 |
| GPIO AF | Alternate Function，引脚交给 UART、SPI、TIM 等外设控制 |
| EXTI / IRQ / ISR | 外部中断 / 中断请求 / 中断服务程序 |
| Debounce | 机械触点消抖 |
| FIFO | 先进先出缓冲区 |
| ACK | 接收确认；I2C 与 CAN 的具体机制不同 |
| DLC | CAN 数据长度编码 |
| Bus-Off | CAN 错误过多后节点暂时离开总线 |
| Sector / Block | SD 和 FatFs 常用的 512 字节逻辑单位 |
| Frame buffer | OLED 在 RAM 中准备的一整幅画面 |
| `static` | 限制文件可见性，或让局部变量跨调用保存值 |
| `volatile` | 变量可能被中断或硬件异步改变，编译器必须实际读写内存 |
