# G474 Starter 详细故障排查

遇到问题时按“供电与接线 → CubeMX 配置 → HAL 返回值 → 应用显示”的顺序检查。一次只改变一个条件。

[返回入门 README](../README.md) · [外设教学](PERIPHERALS.md)

## 1. ST-LINK 提示 `No target found`

这句话表示电脑已经启动了调试服务，但 ST-LINK 没有在 SWD 线上识别到 MCU。它通常不是 ELF 或业务代码问题。

依次检查：

1. 万用表测 MCU 供电是否约 3.3 V。
2. ST-LINK 的 VTref 是否连接到板卡 3.3 V 参考。
3. ST-LINK 与板卡是否共地。
4. SWDIO→PA13、SWCLK→PA14，是否接反或虚焊。
5. 推荐连接 NRST，并尝试 Connect under reset。
6. BOOT 配置是否正常，芯片是否一直被外部电路拉复位。
7. 降低 SWD Frequency。
8. 用 STM32CubeProgrammer 单独连接；仍失败时再怀疑芯片供电、焊接、保护位或下载器硬件。

若 Windows 设备管理器根本没有 ST-LINK，先重装 ST-LINK USB 驱动并更换可传数据的 USB 线。

## 2. 下载后停在 `main()`

这是正常调试行为。[`launch.json`](../.vscode/launch.json) 中配置了：

```json
"runEntry": "main"
```

程序下载后会主动在 `main()` 暂停。再按一次 `F5` 继续运行；不想调试时可使用 CubeProgrammer 下载后 Reset。

## 3. 进入 `Default_Handler`

`Default_Handler` 表示发生了一个没有正确处理的中断。重点看调试器调用栈中的 IRQ 名称，而不是修改启动文件里的死循环。

检查：

1. 调用栈显示的是哪个 `xxx_IRQHandler`。
2. [`stm32g4xx_it.c`](../Core/Src/stm32g4xx_it.c) 是否存在对应函数。
3. `.ioc` 中是否启用了该 NVIC 中断。
4. 初始化代码是否启动了中断，但生成文件缺失或芯片型号不匹配。
5. [`startup_stm32g474xx.s`](../startup_stm32g474xx.s) 是否与 STM32G474 对应。

不要直接删除 IRQ 或把 `Default_Handler` 改成返回；这样会掩盖真正原因。

## 4. 进入 `Error_Handler` 或时钟不启动

给 `SystemClock_Config()` 和各个 `MX_*_Init()` 后的错误分支下断点，找到第一个返回失败的 HAL 调用。

本工程期望：

```text
HSE = 24 MHz
PLL input = 24 / 6 = 4 MHz
VCO = 4 * 85 = 340 MHz
SYSCLK = 340 / 2 = 170 MHz
HCLK = 170 MHz
PCLK1 = 170 MHz
PCLK2 = 85 MHz
FDCAN kernel = HSE 24 MHz
```

PLL 无法锁定时检查：晶振是否确为 24 MHz、HSE 模式是否与晶体/有源时钟匹配、负载电容与焊接、供电旁路、芯片电压范围以及 PLL 输入/VCO 是否满足手册限制。直连 HSE 正常但 PLL 失败时，优先核对 PLLM/N/R 与电源电压等级。

## 5. VS Code 没发现工程或没有 ELF

- 必须打开同时含 `.ioc`、`CMakeLists.txt`、`CMakePresets.json` 的根目录。
- Bundle Manager 中的项目工具应全部安装完成。
- 选择 `Debug` preset 后先 Configure，再 Build。
- 没有 ELF 时查看第一条错误，而不是最后一行汇总。
- 可在根目录执行：

```powershell
cmake --preset Debug
cmake --build --preset Debug --clean-first --parallel
```

成功产物是 `build/Debug/G474_Starter.elf`。

### 5.1 点击 `.ioc` 时报“打开外部程序时出错（0x2）”

这个提示通常不是 `.ioc` 文件缺失，而是 Windows 保存的 CubeMX 打开命令有误。最常见情况是 CubeMX 安装路径含空格，但注册表中的程序路径没有用双引号包住。

先用不依赖文件关联的方法继续工作：

1. 在 VS Code 左侧打开 **STM32Cube** 面板。
2. 点击 **启动 STM32CubeMX**。
3. 在 CubeMX 中选择 **File → Open Project**。
4. 选择工程根目录中的 `G474_Starter.ioc`。

如果希望恢复双击打开功能，可以重新安装/修复 STM32CubeMX，或在 Windows 的“打开方式”中重新选择 `STM32CubeMX.exe`。典型安装位置是：

```text
C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeMX\STM32CubeMX.exe
```

正确的打开命令必须为可执行文件路径加双引号，例如：

```text
"C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeMX\STM32CubeMX.exe" "%1"
```

## 6. OLED 不亮或花屏

1. 确认模块使用 3.3 V 并共地。
2. PC6=SCL、PC7=SDA，检查是否接反。
3. SCL/SDA 必须有外部上拉；空闲时两线都应为高。
4. 用 `HAL_I2C_IsDeviceReady(&hi2c4, 0x3CU << 1U, ...)` 检查 0x3C 应答。
5. 调试观察 `OLED_Ready`。
6. 有 ACK 但花屏时检查是否为 SSD1306 兼容 128×64、扫描方向和初始化命令。

注意：`OLED_ShowString()` 只改内存，必须调用 `OLED_Refresh()` 才会显示。

## 7. FRAM 显示 ERROR

- 型号为 MB85RC64T，A0/A1/A2 接地时 7 位地址是 0x50。
- HAL 调用地址应使用 `0x50U << 1U`。
- 内部地址是 16 位，需使用 `I2C_MEMADD_SIZE_16BIT`。
- 有效范围为 `0x0000~0x1FFF`，检查 `address + length` 越界。
- 与 OLED 共用 I2C4，若两者都失败，先排查总线；只有 FRAM 失败再查地址和芯片焊接。

`FRAM_SelfTest()` 会恢复测试区。若写入或校验中途断电，最后 16 字节可能保留测试图案，因此重要数据不要放在该区域。

## 8. SD 页面显示 ERR

先看页面处于哪个阶段，并观察调试变量 `SD_State`、`SD_LastR1`、`SD_HighCapacity` 和 `SD_BlockCount`。

### Init ERR

- 卡必须使用 3.3 V 电平，检查供电是否在初始化时跌落。
- PE2=SCK、PE5=MISO、PE6=MOSI、PE4=CS。
- SPI 必须为 Mode 0、8 bit、MSB first。
- 初始化约 332 kHz，CS 高时先发至少 80 个空闲时钟。
- MISO 若一直高，常见原因是卡未供电、CS 不工作或接线错误。

### Mount ERR

- 先确认块读取已经成功。
- 当前支持 FAT16/FAT32，不支持 exFAT。
- 重新格式化会删除数据，先在电脑备份 SD 卡；此操作由用户自行完成。

### Write ERR / Verify ERR

- `SD_FileTest()` 会覆盖 `G474_DEMO.TXT`。
- Write ERR 重点查供电、MOSI、CS 和卡忙超时。
- Verify ERR 重点查 MISO、信号完整性、卡座接触和卡本身。
- 用逻辑分析仪确认命令、R1、数据 Token 和 CRC 字节的位置。

## 9. 编码器不动、方向反或跳数

- PC0/PC1 必须复用为 TIM1_CH1/CH2。
- 确认 `Encoder_Init()` 已调用，并且 `HAL_TIM_Encoder_Start()` 返回成功。
- A/B 交换会改变方向。
- 板上已有 10 kΩ + 100 nF 滤波；滤波过重会限制高速旋转。
- 每机械格计数不符时测量实际原始计数，调整 `ENCODER_COUNTS_PER_DETENT`。

## 10. PC2 按键无反应或一次多次

- PC2 空闲应为高，按下应为低。
- `.ioc` 应配置下降沿 EXTI 和内部上拉。
- `HAL_GPIO_EXTI_Callback()` 中只置 `key_irq_flag`。
- 主循环必须持续调用 `Key_Update()`，不能被页面死循环或长延时阻塞。
- 按下与释放都需要 20 ms 消抖。

PC13 编码器自带按键在当前版本中不使用。

## 11. UART 无数据或乱码

- 两端均设置 115200、8 数据位、无校验、1 停止位。
- USB-TTL 必须是 3.3 V 逻辑，并与板卡共地。
- MCU TX 接转换器 RX，MCU RX 接转换器 TX。
- USART2：PD5 TX、PD6 RX；USART3：PB10 TX、PE15 RX。
- 中断接收每完成一个字节后必须重新调用 `HAL_UART_Receive_IT()`。
- 如果进入错误回调，记录 `HAL_UART_GetError()`，不要在 IRQ 中长时间阻塞。

当前例程只保存最新字节，适合人工输入；高速连续数据需要环形缓冲或 DMA。

## 12. CAN 无报文或 Bus-Off

先确认 PCAN：Classic CAN、500 kbit/s、Standard 11-bit、过滤器 `000~7FF`。

硬件检查：

- PD0=FDCAN1_RX，PD1=FDCAN1_TX。
- PD4=TCAN3413 STB，低电平才是 Normal。
- CAN_H 对 CAN_H，CAN_L 对 CAN_L，PCAN 与开发板共参考地。
- 总线两端各 120 Ω；断电测 H-L 通常约 60 Ω。
- 收发器 VCC/VIO 电压正确。

软件检查：

```text
FDCAN kernel = 24 MHz
Prescaler = 3
Seg1 = 13
Seg2 = 2
Bit rate = 24 MHz / 3 / (1 + 13 + 2) = 500 kbit/s
```

- `hfdcan1.Init.Mode` 应为 `FDCAN_MODE_NORMAL`。
- 发送头 `FDFormat` 应为 `FDCAN_CLASSIC_CAN`，`BitRateSwitch` 为 `FDCAN_BRS_OFF`。
- 只有一个发送节点、没有其他节点 ACK 时，发送会不断重试并可能 Bus-Off。
- 排除接线/波特率/ACK 后，复位或重新初始化 CAN 才能恢复。

## 13. 调试方法速查

1. **测电压：** 3.3 V、复位脚、关键输入空闲电平。
2. **核引脚：** 原理图、`.ioc`、[`main.h`](../Core/Inc/main.h) 三者一致。
3. **核时钟：** `SystemClock_Config()` 和外设 kernel clock。
4. **查返回值：** 不要丢弃第一个失败的 `HAL_StatusTypeDef`。
5. **看状态变量：** 设备 Ready、错误码、计数器和协议状态。
6. **看波形：** I2C、SPI、UART、CAN 用示波器或逻辑分析仪比盲改代码更快。
7. **缩小范围：** 每次只验证一个外设，不要同时改时钟、接线和驱动。
