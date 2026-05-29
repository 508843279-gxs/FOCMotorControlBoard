# 第一阶段硬件自检调试说明

当前固件只验证硬件基础链路，不运行电机状态机和 FOC。

## 烧录前

1. 先断开电机，或至少断开母线高压，只给 MCU 和调试串口供电。
2. 串口工具设置为 `115200 8N1`，连接 `USART1`：`PB6=TX`，`PB7=RX`，并共地。
3. 示波器先量 MCU 或驱动输入侧 PWM，不要直接量危险功率侧。

## 上电串口

上电后应看到：

```text
[SELFTEST] FOCMotorControlBoard hardware self-test
[SELFTEST] UART OK, starting LED/PWM/ADC checks...
[SELFTEST] PWM: PA8/PA9/PA10 and PA7/PB0/PB1, ADC trigger: PA11/TIM1_CH4
[SELFTEST] Keys: PA1 RUN, PA2 DIR, PB13 UP, PB14 DOWN
[SELFTEST] TIM1 PWM started, ARR=4199
[SELFTEST] ADC injected interrupt started
```

之后每秒会打印：

```text
[SELFTEST] adc_count=xxxxx delta=xxxxx
```

`delta` 稳定且不为 0，说明 ADC 注入回调在持续触发。

## LED

四个 LED 每 250 ms 轮流只点亮一个。当前代码按低电平点亮处理：

```c
#define SELFTEST_LED_ON  GPIO_PIN_RESET
#define SELFTEST_LED_OFF GPIO_PIN_SET
```

如果亮灭逻辑反了，就把这两个宏对调。

## 按键

按键是上拉输入、低电平有效。按下或松开会打印：

```text
[SELFTEST] KEY_RUN PRESSED
[SELFTEST] KEY_RUN RELEASED
```

对应关系：

```text
KEY_RUN        PA1
KEY_DIR        PA2
KEY_SPEED_UP   PB13
KEY_SPEED_DOWN PB14
```

如果没打印，先用万用表或示波器看按下时引脚是否从高电平变低电平。

## PWM

示波器检查以下引脚：

```text
TIM1_CH1   PA8   约 25%
TIM1_CH2   PA9   约 50%
TIM1_CH3   PA10  约 75%
TIM1_CH1N  PA7
TIM1_CH2N  PB0
TIM1_CH3N  PB1
TIM1_CH4   PA11  ADC 触发相关波形
```

当前 `ARR=4199`，系统时钟 84 MHz，中心对齐 PWM 基频约 10 kHz。

## ADC

ADC 回调现在只做一件事：

```c
adc_injected_count++;
```

不要在 ADC 中断里加 `printf`。判断是否正常只看串口每秒打印的 `delta` 是否持续非 0。

## 通过标准

1. 上电串口有自检 banner。
2. 四个 LED 能轮流单独点亮/熄灭。
3. 四个按键都有 `PRESSED/RELEASED` 打印。
4. TIM1 PWM 引脚能看到固定占空比波形。
5. ADC `delta` 每秒稳定非 0。
