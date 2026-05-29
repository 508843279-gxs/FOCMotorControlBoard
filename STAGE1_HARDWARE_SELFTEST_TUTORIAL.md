# 第一阶段硬件自检学习教程

这份教程面向刚开始接触 STM32 电机控制板的新手。第一阶段不做电机闭环、不做 FOC、不让电机转动，只验证最基础的硬件链路是否可靠。

当前阶段的目标：

1. 上电后串口能打印启动信息。
2. LED 能单独点亮和熄灭。
3. 按键按下和松开时能通过串口打印事件。
4. PWM 引脚能用示波器看到固定波形。
5. ADC 注入转换完成回调能稳定计数。

完成这一阶段后，说明 MCU、串口、GPIO、定时器 PWM、ADC 中断这些基础模块已经可以工作。后面再加电机控制代码时，排查范围会小很多。

## 安全说明

第一阶段只建议给 MCU、调试器、串口模块供电。

不要一开始就接电机和母线高压。PWM 自检阶段只是确认 MCU 输出脚是否有波形，不代表驱动桥、电机、电流采样已经可以安全运行。

如果必须接功率板，先断开电机，或者关闭母线高压，只测 MCU 侧或驱动输入侧信号。

## 工具准备

建议准备：

1. STM32CubeMX 或已经生成好的工程。
2. Keil MDK 或其他能编译 STM32 工程的 IDE。
3. ST-Link 下载器。
4. USB 转串口模块。
5. 串口助手，波特率设置为 `115200 8N1`。
6. 示波器或逻辑分析仪。
7. 万用表，用来检查按键和 LED 电平。

当前工程的串口是：

```text
USART1_TX  PB6
USART1_RX  PB7
BaudRate   115200
Format     8N1
```

接 USB 转串口时注意：

```text
板子 PB6/TX  -> 串口模块 RX
板子 PB7/RX  -> 串口模块 TX
GND          -> GND
```

## 当前工程文件分工

第一阶段主要看这几个文件：

```text
Core/Src/main.c
Int/Src/Int_bsp.c
Core/Src/usart.c
Core/Src/gpio.c
Core/Src/tim.c
Core/Src/adc.c
Core/Inc/main.h
```

它们的作用如下：

| 文件 | 作用 |
| --- | --- |
| `Core/Src/main.c` | 第一阶段自检主逻辑，负责启动串口接收、LED 轮询、按键扫描、PWM 启动、ADC 启动 |
| `Int/Src/Int_bsp.c` | 中断回调函数，负责 ADC 回调计数、UART 接收事件打印 |
| `Core/Src/usart.c` | USART1 初始化，并把 `printf` 重定向到串口 |
| `Core/Src/gpio.c` | LED 和按键 GPIO 初始化 |
| `Core/Src/tim.c` | TIM1 PWM 和 TIM5 初始化 |
| `Core/Src/adc.c` | ADC1 规则通道和注入通道初始化 |
| `Core/Inc/main.h` | CubeMX 生成的引脚宏定义 |

学习时建议先看 `main.c` 和 `Int_bsp.c`，其他文件先当作 CubeMX 的外设配置文件。

## 程序启动流程

`main.c` 中的启动顺序大致是：

```c
HAL_Init();
SystemClock_Config();

MX_GPIO_Init();
MX_DMA_Init();
MX_USART1_UART_Init();
MX_ADC1_Init();
MX_TIM1_Init();
MX_CRC_Init();
MX_TIM5_Init();

SelfTest_Start();

while (1)
{
    SelfTest_Task();
}
```

理解这个顺序很重要：

1. `HAL_Init()` 初始化 HAL 库和 SysTick。
2. `SystemClock_Config()` 配置系统时钟。
3. `MX_xxx_Init()` 初始化各个外设。
4. `SelfTest_Start()` 启动第一阶段硬件自检。
5. `SelfTest_Task()` 在主循环里周期运行 LED、按键、ADC 状态打印。

这个阶段没有使用电机状态机，也没有速度环、电流环和 FOC 算法。

## 串口打印如何工作

`Core/Src/usart.c` 里有这一段：

```c
int fputc(int ch, FILE * file)
{
  HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
  return ch;
}
```

这段代码的作用是把标准库的 `printf()` 输出重定向到 USART1。

所以在 `main.c` 里写：

```c
printf("[SELFTEST] UART OK\r\n");
```

电脑串口助手就能看到这行文字。

上电后应看到类似信息：

```text
[SELFTEST] FOCMotorControlBoard hardware self-test
[SELFTEST] UART OK, starting LED/PWM/ADC checks...
[SELFTEST] PWM: PA8/PA9/PA10 and PA7/PB0/PB1, ADC trigger: PA11/TIM1_CH4
[SELFTEST] Keys: PA1 RUN, PA2 DIR, PB13 UP, PB14 DOWN
[SELFTEST] TIM1 PWM started, ARR=4199
[SELFTEST] ADC injected interrupt started
```

如果完全没有串口打印，优先检查：

1. 串口助手是否选对 COM 口。
2. 波特率是否是 `115200`。
3. TX/RX 是否交叉连接。
4. GND 是否共地。
5. 程序是否真的下载到板子。
6. 代码是否卡在 `Error_Handler()`。

## LED 自检

当前 LED 引脚：

| 名称 | 引脚 |
| --- | --- |
| `LED_UV` | PH0 |
| `LED_OV` | PC15 |
| `LED_OC` | PC14 |
| `LED_OT` | PC13 |

自检代码中 LED 每 `250 ms` 轮流点亮一个：

```c
#define SELFTEST_LED_INTERVAL_MS 250U
```

当前代码按低电平点亮处理：

```c
#define SELFTEST_LED_ON  GPIO_PIN_RESET
#define SELFTEST_LED_OFF GPIO_PIN_SET
```

如果看到 LED 亮灭逻辑反了，比如本来应该灭却亮、本来应该亮却灭，只需要交换这两个宏：

```c
#define SELFTEST_LED_ON  GPIO_PIN_SET
#define SELFTEST_LED_OFF GPIO_PIN_RESET
```

排查 LED 时按这个顺序：

1. 看 LED 是否轮流变化。
2. 如果完全不亮，用万用表量 LED 引脚是否有高低变化。
3. 如果引脚有变化但灯不亮，检查 LED 焊接方向和限流电阻。
4. 如果某一个灯不亮，重点查对应引脚和硬件连接。

## 按键自检

当前按键引脚：

| 名称 | 引脚 | 逻辑 |
| --- | --- | --- |
| `KEY_RUN` | PA1 | 上拉输入，低电平有效 |
| `KEY_DIR` | PA2 | 上拉输入，低电平有效 |
| `KEY_SPEED_UP` | PB13 | 上拉输入，低电平有效 |
| `KEY_SPEED_DOWN` | PB14 | 上拉输入，低电平有效 |

`gpio.c` 中按键配置为上拉输入：

```c
GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
GPIO_InitStruct.Pull = GPIO_PULLUP;
```

所以正常情况下：

```text
没按下：高电平
按下：低电平
```

自检代码每 `20 ms` 扫描一次按键：

```c
#define SELFTEST_KEY_SCAN_MS 20U
```

按下时串口打印：

```text
[SELFTEST] KEY_RUN PRESSED
```

松开时串口打印：

```text
[SELFTEST] KEY_RUN RELEASED
```

如果按键没反应，按这个顺序检查：

1. 用万用表量引脚，没按下时是否为高电平。
2. 按下时是否变成低电平。
3. 确认按键引脚和 `main.h` 中宏定义一致。
4. 确认主循环里一直在运行 `SelfTest_Task()`。
5. 确认串口没有被大量信息刷屏导致看漏。

## PWM 自检

当前使用 TIM1 输出 PWM：

| 信号 | 引脚 | 说明 |
| --- | --- | --- |
| `TIM1_CH1` | PA8 | 主 PWM |
| `TIM1_CH2` | PA9 | 主 PWM |
| `TIM1_CH3` | PA10 | 主 PWM |
| `TIM1_CH1N` | PA7 | 互补 PWM |
| `TIM1_CH2N` | PB0 | 互补 PWM |
| `TIM1_CH3N` | PB1 | 互补 PWM |
| `TIM1_CH4` | PA11 | ADC 注入触发相关通道 |

`tim.c` 中 TIM1 的关键配置：

```text
Prescaler         0
CounterMode       CenterAligned1
Period            4200 - 1
RepetitionCounter 1
DeadTime          84
```

在 84 MHz 定时器时钟下，中心对齐 PWM 频率大约是：

```text
PWM 频率 = 84 MHz / 2 / 4200 = 10 kHz
```

自检代码给三个主通道设置固定占空比：

```c
CH1 = 25%
CH2 = 50%
CH3 = 75%
```

示波器检查建议：

1. 探头地线接板子 GND。
2. 先测 PA8，看是否有约 `10 kHz` PWM。
3. 再测 PA9、PA10，看占空比是否不同。
4. 再测 PA7、PB0、PB1，看互补输出是否存在。
5. 最后测 PA11，看 CH4 是否有触发相关波形。

如果没有 PWM，优先检查：

1. `SelfTest_StartPwm()` 是否被调用。
2. 串口是否打印 `TIM1 PWM started`。
3. 引脚是否真的复用为 `GPIO_AF1_TIM1`。
4. 示波器量的是 MCU 侧引脚还是驱动之后的信号。
5. TIM1 是否被 Break 保护关断。

## ADC 注入回调自检

当前 ADC1 使用注入转换，注入通道包括：

| Rank | ADC 通道 | 引脚 |
| --- | --- | --- |
| 1 | ADC1_IN3 | PA3 |
| 2 | ADC1_IN4 | PA4 |
| 3 | ADC1_IN5 | PA5 |
| 4 | ADC1_IN6 | PA6 |

`adc.c` 中注入转换触发源是：

```text
ADC_EXTERNALTRIGINJECCONV_T1_CC4
ADC_EXTERNALTRIGINJECCONVEDGE_RISING
```

也就是说，ADC 注入转换由 TIM1 的 CH4 事件触发。

`Int/Src/Int_bsp.c` 中的 ADC 回调很简单：

```c
void HAL_ADCEx_InjectedConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1)
    {
        adc_injected_count++;
    }
}
```

主循环每秒打印一次计数：

```text
[SELFTEST] adc_count=xxxxx delta=xxxxx
```

这里重点看 `delta`：

```text
delta 不为 0：ADC 回调正在触发
delta 基本稳定：触发频率稳定
delta 一直为 0：ADC 回调没有进来
```

如果 `delta=0`，按这个顺序检查：

1. 是否打印了 `ADC injected interrupt started`。
2. `ADC_IRQn` 是否在 `adc.c` 中开启。
3. `stm32f4xx_it.c` 中 `ADC_IRQHandler()` 是否调用了 `HAL_ADC_IRQHandler(&hadc1)`。
4. TIM1 CH4 是否已经启动。
5. ADC 注入触发源是否仍然是 `T1_CC4`。
6. 回调函数名字是否写成了 `HAL_ADCEx_InjectedConvCpltCallback`。

## UART 接收事件自检

当前串口接收使用：

```c
HAL_UARTEx_ReceiveToIdle_IT(&huart1, rxBuff, 1000U);
```

它的意思是：串口收到数据后，如果检测到空闲，会进入：

```c
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
```

回调里打印收到的数据长度：

```text
[SELFTEST] UART RX size=5
```

然后重新开启下一次接收：

```c
HAL_UARTEx_ReceiveToIdle_IT(&huart1, rxBuff, sizeof(rxBuff));
```

新手容易漏掉这一步。UART 接收完成一次后，如果不重新开启接收，后面的数据就收不到了。

## 为什么中断里只做计数和简单打印

第一阶段的原则是：中断里尽量少做事。

ADC 回调只做：

```c
adc_injected_count++;
```

这样做的好处：

1. 不容易因为中断执行太久影响其他外设。
2. 排查问题简单，先确认回调有没有进来。
3. 后面要加采样值读取、滤波、电流计算时，可以一步一步扩展。

等第一阶段完全稳定后，再考虑在 ADC 回调里读取注入通道值。

## 推荐调试顺序

新手调试时不要同时看所有功能，建议按这个顺序来：

1. 只确认板子能下载程序。
2. 打开串口助手，看上电打印。
3. 看 LED 是否轮流点亮。
4. 按每个按键，看串口是否打印按下和松开。
5. 用示波器测 PA8、PA9、PA10。
6. 用示波器测 PA7、PB0、PB1。
7. 看串口每秒打印的 `adc_count` 和 `delta`。
8. 串口助手发送任意字符，看是否打印 `UART RX size`。

每通过一步，就记录一次现象。不要跳着排查。

## 常见问题速查表

| 现象 | 优先检查 |
| --- | --- |
| 没有任何串口打印 | COM 口、波特率、TX/RX、GND、程序是否下载 |
| 串口乱码 | 波特率是否 `115200`，串口工具编码不用管，打印内容主要是英文 |
| LED 全不亮 | LED 极性、GPIO 是否初始化、PH0/PC13/PC14/PC15 是否接对 |
| LED 亮灭反了 | 交换 `SELFTEST_LED_ON` 和 `SELFTEST_LED_OFF` |
| 某个按键没反应 | 用万用表量对应引脚，检查是否低电平有效 |
| PWM 没波形 | TIM1 是否启动、GPIO 复用是否正确、示波器测点是否正确 |
| 互补 PWM 没波形 | 是否调用 `HAL_TIMEx_PWMN_Start()` |
| ADC `delta=0` | ADC 中断、TIM1 CH4、注入触发源、回调函数名 |
| 串口只能接收一次 | 回调里是否重新调用 `HAL_UARTEx_ReceiveToIdle_IT()` |

## 第一阶段通过标准

满足下面条件，就可以认为第一阶段通过：

1. 上电能看到 `[SELFTEST]` 启动信息。
2. 四个 LED 能轮流单独点亮。
3. 四个按键按下和松开都能打印。
4. PA8、PA9、PA10 有 PWM 波形。
5. PA7、PB0、PB1 有互补 PWM 波形。
6. ADC 每秒打印的 `delta` 稳定且不为 0。
7. 串口发送数据后能打印 `UART RX size`。

## 建议给新手的小练习

完成第一阶段后，可以做几个小练习：

1. 把 LED 轮流间隔从 `250 ms` 改成 `500 ms`。
2. 把按键扫描周期从 `20 ms` 改成 `10 ms`，观察串口打印是否更灵敏。
3. 改变 TIM1 CH1/CH2/CH3 占空比，用示波器验证波形变化。
4. 注释掉 `HAL_UARTEx_ReceiveToIdle_IT()` 的重新开启接收语句，观察为什么串口只能收一次。
5. 暂时停止 TIM1 CH4，观察 ADC `delta` 是否变成 0。

这些练习的目的不是改出最终代码，而是理解每条硬件链路是怎么工作的。

## 后续阶段建议

第一阶段通过后，不要立刻上复杂 FOC。建议后续按阶段推进：

1. 第二阶段：读取 ADC 注入采样值，并打印原始值。
2. 第三阶段：加入电流采样零偏校准。
3. 第四阶段：加入安全保护，比如过流、过压、欠压、过温。
4. 第五阶段：加入开环 PWM 或开环电角度测试。
5. 第六阶段：再考虑闭环电流控制和 FOC。

这样做的好处是，每一步都有明确验证目标，出问题时不会被一大堆模块同时干扰。

