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

---

# 第二阶段 BSP 分层调试说明

第二阶段已经不再使用 `[SELFTEST]` 作为主要调试前缀，新的 BSP 分层调试信息统一使用：

```text
[BSP2]
```

当前第二阶段目标：

1. `main.c` 只负责调用 `BspInit()` 和 `BspTask()`。
2. ADC 采样进入 `bsp_adc.c`，并写入 `mc_info`。
3. 按键和串口命令进入 `bsp_command.c`，并统一写入 `comm[]`。
4. PWM 启停进入 `bsp_pwm.c`。
5. 欠压、过压、过流、过温保护进入 `bsp_protection.c`。
6. 状态机进入 `bsp_motor.c`。

## 烧录前

第二阶段仍然建议先不要接电机和母线高压。

推荐连接：

```text
USART1_TX  PB6 -> 串口模块 RX
USART1_RX  PB7 -> 串口模块 TX
GND            -> GND
波特率          115200 8N1
```

示波器仍然优先测 MCU 侧或驱动输入侧 PWM，不要直接测危险功率侧。

## 上电串口

上电后应看到：

```text
[BSP2] BSP layer bring-up
[BSP2] Modules: adc command pwm protection motor
[BSP2] UART CMD: RUN STOP DIR UP DOWN STATUS HELP
```

之后每秒应看到类似：

```text
[BSP2] adc=12345 delta=10000 vbus=12 ia=0 ib=0 temp=25 state=3 err=0
```

字段含义：

| 字段 | 含义 |
| --- | --- |
| `adc` | ADC 注入回调累计次数 |
| `delta` | 最近 1 秒 ADC 回调增加量 |
| `vbus` | 母线电压换算值 |
| `ia` | A 相电流换算值 |
| `ib` | B 相电流换算值 |
| `temp` | 温度估算值 |
| `state` | 状态机状态 |
| `err` | 当前故障类型 |

状态机枚举：

```text
0 MC_RDY
1 MC_START
2 MC_RUN
3 MC_STOP
4 MC_ERR
```

故障枚举：

```text
0 NONE_ERR
1 LV_ERR 欠压
2 OV_ERR 过压
3 OC_ERR 过流
4 OT_ERR 过温
```

## 串口命令调试

串口助手可以发送：

```text
HELP
STATUS
RUN
STOP
DIR
UP
DOWN
```

发送 `HELP` 应看到：

```text
[BSP2] CMD: RUN STOP DIR UP DOWN STATUS HELP
```

发送 `STATUS` 应看到类似：

```text
[BSP2] UART_STATUS run=0 level=0 dir=1 rpm=0 state=3 err=0
```

字段含义：

| 字段 | 含义 |
| --- | --- |
| `run` | 命令层运行使能 |
| `level` | 速度档位，0~3 |
| `dir` | 方向，1 正向，-1 反向 |
| `rpm` | 当前斜坡后的目标转速 |
| `state` | 状态机状态 |
| `err` | 当前故障 |

如果发送命令没有任何反应，按这个顺序检查：

1. 串口 TX/RX 是否交叉接好。
2. `USART1_IRQn` 是否开启。
3. `HAL_UARTEx_RxEventCallback()` 是否进入。
4. `HAL_UARTEx_ReceiveToIdle_IT()` 是否在回调末尾重新开启。
5. 串口工具是否发送了换行或发送了有效字符。

## 按键调试

按键仍然是上拉输入、低电平有效：

```text
KEY_RUN        PA1
KEY_DIR        PA2
KEY_SPEED_UP   PB13
KEY_SPEED_DOWN PB14
```

按键触发后会打印类似：

```text
[BSP2] KEY_RUN run=1 level=1 dir=1 rpm=0 state=3 err=0
[BSP2] KEY_UP run=1 level=2 dir=1 rpm=0 state=3 err=0
[BSP2] KEY_DIR run=1 level=2 dir=-1 rpm=0 state=3 err=0
[BSP2] KEY_DOWN run=1 level=1 dir=-1 rpm=0 state=3 err=0
```

如果按键无打印：

1. 用万用表量按键引脚，没按下应为高电平。
2. 按下时应变成低电平。
3. 确认 `BspTask()` 一直在主循环中运行。
4. 确认当前没有卡进 `Error_Handler()`。

## PWM 调试

第二阶段 PWM 分成两部分：

1. TIM1 基准和 CH4 在 `BspInit()` 中启动，用来继续触发 ADC。
2. 三相 PWM 主输出和互补输出由状态机调用 `StartPWM()` / `StopPWM()` 控制。

先发送：

```text
RUN
```

正常情况下状态会变化：

```text
[BSP2] UART_RUN run=1 level=1 dir=1 rpm=0 state=3 err=0
[BSP2] STATE cmd=1 state=3 err=0 rpm=50 vbus=12 temp=25
[BSP2] STATE cmd=1 state=1 err=0 rpm=100 vbus=12 temp=25
[BSP2] STATE cmd=1 state=2 err=0 rpm=150 vbus=12 temp=25
```

进入 `MC_RUN` 后，用示波器检查：

```text
TIM1_CH1   PA8   约 25%
TIM1_CH2   PA9   约 50%
TIM1_CH3   PA10  约 75%
TIM1_CH1N  PA7
TIM1_CH2N  PB0
TIM1_CH3N  PB1
TIM1_CH4   PA11  ADC 触发相关波形
```

发送：

```text
STOP
```

应停止三相 PWM，状态回到 `MC_STOP`。

如果 `RUN` 后没有 PWM：

1. 看 `err` 是否不为 0。
2. 看 `state` 是否进入 `MC_RUN`。
3. 如果 `state=4`，说明进入了故障。
4. 如果 `err=1`，多半是母线欠压。
5. 确认示波器测的是 MCU PWM 引脚，不是功率输出侧。

## ADC 调试

第二阶段 ADC 回调不仅计数，还会调用：

```c
BspAdcSample_Update();
MotorProtection_CheckVbus();
```

串口每秒的 `delta` 应稳定且不为 0。

如果 `delta=0`：

1. 检查 TIM1 CH4 是否启动。
2. 检查 ADC 注入触发源是否仍是 `T1_CC4`。
3. 检查 `ADC_IRQn` 是否开启。
4. 检查 `ADC_IRQHandler()` 是否调用 `HAL_ADC_IRQHandler(&hadc1)`。

如果 `vbus` 明显不对：

1. 先打印或观察 `adc_vbus_raw`。
2. 检查 `ADC_TO_VBUS_V` 系数。
3. 确认 Rank 1 是否真的是母线电压通道。
4. 确认硬件分压电阻是否和代码系数匹配。

## 保护调试

保护阈值当前在 `bsp_protection.c`：

```c
#define VBUS_UNDERVOLTAGE_V 4.0f
#define VBUS_OVERVOLTAGE_V  20.0f
#define PHASE_OVERCURRENT_A 20.0f
#define MOTOR_OVERTEMP_C    80.0f
```

无母线或母线低于 4V 时，可能会看到：

```text
err=1
state=4
```

这表示欠压故障，状态机进入 `MC_ERR`。

故障灯对应关系：

```text
LV_ERR -> LED_UV
OV_ERR -> LED_OV
OC_ERR -> LED_OC
OT_ERR -> LED_OT
```

清故障方式：

```text
按 KEY_RUN
或串口发送 RUN
```

注意：如果故障条件仍然存在，例如母线一直欠压，清故障后会马上再次进入故障。

## 第二阶段通过标准

1. 上电能看到 `[BSP2] BSP layer bring-up`。
2. 每秒 `[BSP2] adc=... delta=...` 稳定打印。
3. 串口 `HELP` / `STATUS` 有响应。
4. 按键能打印 `KEY_RUN` / `KEY_UP` / `KEY_DOWN` / `KEY_DIR`。
5. `RUN` 后状态能进入 `MC_RUN`。
6. `RUN` 后 PA8/PA9/PA10 和互补 PWM 引脚能看到波形。
7. `STOP` 后 PWM 能停止。
8. 欠压、过压、过流、过温能进入 `MC_ERR` 并点亮对应 LED。
