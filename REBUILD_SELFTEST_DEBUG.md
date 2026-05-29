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

---

# 第三阶段 FOC 接入调试说明

第三阶段串口调试前缀为：

```text
[FOC3]
```

本阶段已经接入 `MATLAB/FOC_CURRENT.c`，运行链路为：

```text
ADC 采样 -> mc_info -> FOC_CURRENT_U / ObserverParam.RefRPM
         -> FOC_CURRENT_step()
         -> FOC_CURRENT_Y.tAout/tBout/tCout
         -> TIM1 CH1/CH2/CH3
```

## 当前版本定位

当前第三阶段不是最开始的完整终版电机控制代码。

它已经恢复并接入了 FOC 模型，具备把 ADC 采样、电流输入、母线电压输入、目标转速和 TIM1 三相 PWM 输出串起来的能力。但是部分原始辅助模块和完整启动保护逻辑还没有恢复，不能直接当成最终可试转版本使用。

当前仍缺少或简化的内容：

| 内容 | 当前状态 | 影响 |
| --- | --- | --- |
| `bsp_delay.c` | 未恢复 | 原始启动延时和部分时序逻辑还不完整 |
| `bsp_shared.c` | 未恢复 | 全局共享变量组织方式和原始版本不同 |
| `bsp_status_led.c` | 未恢复 | 状态灯逻辑已简化 |
| `bsp_temperature.c` | 未恢复 | PT100/温度滤波逻辑还不是原始完整版本 |
| Gate/EN 使能 | 未按原始版本完整恢复 | 驱动芯片使能路径需要单独确认 |
| 状态机 | 已简化 | 启动、停止、故障恢复逻辑和终版不同 |
| 保护逻辑 | 已简化 | 欠压、过压、过流、过温阈值需要实测校准 |
| ADC 换算 | 可运行但未重新校准 | 电流零点、母线电压比例可能不准 |

所以本阶段的目标是验证 FOC 数据链路和 PWM 输出，不建议直接接电机上高压试转。

## 上电串口

上电应看到：

```text
[FOC3] FOC_CURRENT bring-up
[FOC3] Modules: adc command pwm protection motor foc_current
[FOC3] UART CMD: RUN STOP DIR UP DOWN STATUS HELP
```

之后每秒应看到：

```text
[FOC3] adc=xxxxx delta=xxxxx ref=xxxx obs=xxxx vbus=xx ia=xx ib=xx temp=xx state=x err=x
```

新增字段：

| 字段 | 含义 |
| --- | --- |
| `ref` | `ObserverParam.RefRPM`，目标转速 |
| `obs` | `ObserverParam.ObserverRPM`，FOC 模型估算转速 |

## FOC 输入检查

进入 `MC_RUN` 后，`bsp_motor.c` 会写入：

```c
FOC_CURRENT_U.ISensA = mc_info.isens_a;
FOC_CURRENT_U.ISensB = mc_info.isens_b;
FOC_CURRENT_U.ISensC = mc_info.isens_c;
FOC_CURRENT_U.VBus = mc_info.vbus;
FOC_CURRENT_U.Ref_Id = mc_info.refId;
ObserverParam.RefRPM = mc_info.refRPM;
```

如果 FOC 输出异常，优先看：

1. `vbus` 是否明显错误或为 0。
2. `ia` / `ib` 静态时是否接近 0。
3. `ref` 是否随 `RUN/UP/DOWN/DIR` 变化。
4. `err` 是否已经进入故障。

## PWM 输出检查

发送：

```text
RUN
```

状态应进入：

```text
state=2
```

此时示波器检查：

```text
PA8   TIM1_CH1
PA9   TIM1_CH2
PA10  TIM1_CH3
PA7   TIM1_CH1N
PB0   TIM1_CH2N
PB1   TIM1_CH3N
```

第三阶段不再是固定 25%/50%/75%，而是由：

```text
FOC_CURRENT_Y.tAout
FOC_CURRENT_Y.tBout
FOC_CURRENT_Y.tCout
```

实时写入 TIM1 比较值。

## 不接电机调试流程

第一次验证第三阶段时，建议只给 MCU、调试器和串口供电，不接电机，不上母线高压。

推荐顺序：

1. 烧录后确认串口出现 `[FOC3] FOC_CURRENT bring-up`。
2. 观察每秒打印的 `delta`，确认 ADC 回调持续触发且不为 0。
3. 发送 `STATUS`，确认串口命令仍能响应。
4. 发送 `RUN`，确认 `state` 能进入 `2`，也就是 `MC_RUN`。
5. 示波器测 PA8、PA9、PA10，确认三相 PWM 有输出。
6. 发送 `UP` / `DOWN`，确认 `ref` 会变化。
7. 发送 `DIR`，确认 `ref` 方向或目标给定逻辑符合预期。
8. 发送 `STOP`，确认 PWM 停止。
9. 重复 `RUN` / `STOP`，确认不会卡死、不会进入不可恢复故障。

如果 `RUN` 后马上进入故障，先看 `err`：

| `err` | 含义 | 常见原因 |
| --- | --- | --- |
| 1 | 欠压 | 没有母线电压，或母线采样比例不对 |
| 2 | 过压 | 母线采样比例过大，或阈值设置太低 |
| 3 | 过流 | 电流零点偏移过大，或电流比例不对 |
| 4 | 过温 | 温度换算不准，或温度默认值异常 |

## 低压试转前检查

只有下面项目全部确认后，才建议进入低压限流试转。

| 检查项 | 通过标准 |
| --- | --- |
| 串口 | `[FOC3]` 稳定打印，无异常复位 |
| ADC 回调 | `delta` 每秒稳定非 0 |
| PWM 输出 | PA8/PA9/PA10 有合理 PWM，`STOP` 后能关闭 |
| 比较值范围 | TIM1 CCR1/CCR2/CCR3 不越界，不出现异常跳变 |
| 电流零点 | 不接电机静态时 `ia` / `ib` 接近 0 |
| 母线电压 | `vbus` 和万用表实测值接近 |
| 保护 | 欠压、过流等故障能让 PWM 停止 |
| Gate/EN | 驱动芯片使能脚逻辑确认正确 |
| 电源 | 使用低压、限流电源，不直接上高压大电流 |

低压试转建议：

1. 先不上高压，只确认 PWM 和 STOP。
2. 使用限流电源，电流限值先设小。
3. 电机空载，固定牢靠。
4. 发送 `RUN` 后先看是否有异常电流。
5. 如果电流瞬间冲高、驱动发热、声音异常，立即 `STOP` 或断电。
6. 确认低速稳定后，再逐步 `UP`。

## 当前阶段不通过的判断

出现下面任意一种情况，都不要接电机试转：

1. `delta=0`，说明 ADC 控制周期没有跑。
2. `vbus` 明显错误，例如没接母线却显示很高。
3. 静态 `ia` / `ib` 偏移很大。
4. `RUN` 后 `state` 不能进入 `MC_RUN`。
5. `STOP` 后 PWM 不能关闭。
6. 示波器看到三相 PWM 异常毛刺或占空比长时间顶到 0/ARR。
7. `err` 反复出现，且原因没有查清。

## 第三阶段通过标准

1. 编译能找到 `FOC_CURRENT.h` 并编译 `FOC_CURRENT.c`。
2. 上电能看到 `[FOC3]` 启动信息。
3. 每秒 `delta` 稳定非 0。
4. `RUN` 后 `state=2`。
5. `UP/DOWN/DIR` 后 `ref` 有变化。
6. PA8/PA9/PA10 的比较值由 FOC 输出更新。
7. `STOP` 后 PWM 停止。
8. 故障发生时进入 `MC_ERR` 并停止 PWM。

注意：第三阶段通过，只代表 FOC 接入链路通过；它不等于最开始的完整终版，也不等于已经可以直接高压带电机运行。

---

# 第四阶段 安全试转准备调试说明

第四阶段串口调试前缀为：

```text
[SAFE4]
```

本阶段在第三阶段 FOC 接入基础上，恢复安全试转相关模块：

```text
bsp_delay.c
bsp_shared.c
bsp_status_led.c
bsp_temperature.c
```

并把温度保护、故障灯、Gate/EN 调用点、启动/停止短延时重新接回状态机。

## 上电串口

上电后应看到：

```text
[SAFE4] safe trial bring-up
[SAFE4] Modules: adc command pwm protection motor temperature foc_current
[SAFE4] UART CMD: RUN STOP DIR UP DOWN STATUS HELP
```

之后每秒应看到类似：

```text
[SAFE4] adc=12345 delta=10000 ref=2000 obs=0 vbus=12 ia=0 ib=0 temp=25 state=2 err=0
```

重点看：

| 字段 | 说明 |
| --- | --- |
| `delta` | ADC 注入回调是否稳定触发 |
| `ref` | 目标转速，来自按键或串口命令 |
| `obs` | FOC 观测转速，不接电机时可能为 0 |
| `vbus` | 母线电压换算值 |
| `ia` / `ib` | A/B 相电流 |
| `temp` | PT100 平均滤波后的温度 |
| `state` | 电机状态机 |
| `err` | 当前故障 |

## 第四阶段启动流程检查

发送：

```text
RUN
```

期望流程：

```text
MC_STOP -> MC_START -> MC_RUN
```

`MC_START` 中会执行：

```text
FOC_CURRENT_initialize()
三相 PWM 写 50% 中点
EN_GATE_SET
delay_nop(500)
StartPWM()
```

注意：当前 `EN_GATE_SET` / `EN_GATE_RESET` 仍是保留宏。如果你的驱动芯片必须由 MCU 使能，需要先在 `bsp.h` 里把它们接到真实 GPIO。

## 停止流程检查

发送：

```text
STOP
```

应执行：

```text
StopPWM()
EN_GATE_RESET
delay_nop(500)
FOC_CURRENT_initialize()
MC_STOP
```

示波器上 PA8/PA9/PA10 以及互补 PWM 应停止输出。

## 温度保护检查

第四阶段温度链路已经恢复为 PT100 查表和平均滤波：

```text
ADC Rank2 -> BspTemperature_Accumulate() -> SafeTemp -> 过温保护
```

刚上电时 `temp` 可能为 0，因为平均滤波还没完成一轮。这是正常现象。

如果 `temp` 一直不合理：

1. 检查 ADC Rank2 是否真的是温度通道。
2. 检查 PT100 分压电阻是否为代码中的 150 欧。
3. 检查 `BspTemperature_FilterTick()` 是否在 ADC 回调中持续调用。
4. 检查 ADC `delta` 是否稳定。

## 故障灯检查

故障灯现在由 `bsp_status_led.c` 单独控制。

对应关系：

```text
LV_ERR -> LED_UV
OV_ERR -> LED_OV
OC_ERR -> LED_OC
OT_ERR -> LED_OT
```

如果故障出现但灯不亮：

1. 确认 LED 是否低电平点亮。
2. 检查 `StatusLed_Set()` 是否被调用。
3. 检查对应 GPIO 是否被 CubeMX 配成输出。

## 低压试转前禁止项

出现下面任何一种情况，都不要接电机试转：

1. `delta=0`。
2. `STOP` 后 PWM 不能关闭。
3. `vbus` 与万用表差距很大。
4. 静态 `ia` / `ib` 偏移很大。
5. `RUN` 后状态反复进入 `MC_ERR`。
6. 过流、过温、欠压、过压触发后 PWM 没有关断。
7. Gate/EN 硬件需要使能，但代码宏还没有接真实 GPIO。

## 第四阶段通过标准

1. 编译通过，Keil 工程能找到全部 `bsp_*.c` 文件。
2. 上电出现 `[SAFE4]`。
3. `delta` 稳定非 0。
4. 串口命令和按键能改变 `ref`。
5. `RUN` 后能进入 `MC_RUN`。
6. PA8/PA9/PA10 能看到由 FOC 更新的 PWM。
7. `STOP` 后 PWM 能关闭。
8. 故障能进入 `MC_ERR`，对应 LED 点亮，PWM 关闭。
9. `vbus`、`ia`、`ib`、`temp` 的数值经过实测确认。

第四阶段通过后，只代表可以进入低压限流空载试转，不代表可以直接高压、大电流、带载运行。
