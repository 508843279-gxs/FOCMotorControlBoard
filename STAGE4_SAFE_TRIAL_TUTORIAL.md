# 第四阶段 安全试转准备学习教程

第四阶段的目标不是直接高压带电机运行，而是在第三阶段 FOC 已接入的基础上，把最开始版本里和安全试转相关的 BSP 模块逐步恢复回来。

本阶段关键词：

```text
先恢复安全链路，再低压限流试转
```

## 本阶段完成了什么

第四阶段恢复或补齐了这些内容：

| 模块 | 作用 |
| --- | --- |
| `bsp_delay.c` | 恢复短延时接口，用于门极使能后的等待 |
| `bsp_shared.c` | 恢复 BSP 全局共享变量集中定义 |
| `bsp_status_led.c` | 恢复独立故障灯控制模块 |
| `bsp_temperature.c` | 恢复 PT100 查表和平均滤波 |
| `bsp_adc.c` | 温度采样重新接入 PT100 滤波模块 |
| `bsp_motor.c` | 启动/停止流程加入 Gate/EN 调用点和短延时 |
| `bsp_protection.c` | 过温保护改用滤波后的 `SafeTemp` |
| Keil 工程 | 重新加入恢复的 BSP 源文件 |

串口前缀从第三阶段的：

```text
[FOC3]
```

切换为第四阶段的：

```text
[SAFE4]
```

看到 `[SAFE4]`，说明当前烧录的是第四阶段安全试转准备版本。

## 为什么要恢复这些模块

第三阶段已经能做到：

```text
ADC -> FOC_CURRENT -> TIM1 三相 PWM
```

但是“能输出 PWM”和“可以安全驱动电机”不是一回事。

电机真正试转前，还需要恢复这些基础能力：

1. 故障灯要能清楚指示欠压、过压、过流、过温。
2. 温度不能只看单次 ADC 毛刺，要有滤波。
3. 启动 PWM 前要预留 Gate/EN 使能流程。
4. 停止或故障时要同时关闭 PWM 和 Gate/EN。
5. 全局状态变量要集中管理，避免多个文件重复定义。

第四阶段就是把这些“试转前的地基”补回来。

## 当前控制链路

第四阶段的快控制周期仍然从 ADC 注入回调开始：

```text
TIM1 CH4 触发 ADC
    -> HAL_ADCEx_InjectedConvCpltCallback()
    -> adc_injected_count++
    -> BspAdcSample_Update()
    -> BspTemperature_FilterTick()
    -> MotorProtection_CheckVbus()
    -> MotorControl_FocControlStep()
```

主循环负责慢任务：

```text
while (1)
    -> BspTask()
        -> KeyControl_Update()
        -> MotorControl_UpdateCommand()
        -> MotorControl_StateMachineStep()
        -> 每秒打印状态
```

这样分工的好处是：

1. ADC、保护、FOC 输出保持稳定节拍。
2. 按键和串口不占用 ADC 中断太多时间。
3. 调试信息按 1 秒打印一次，不会刷爆串口。

## 启动流程

发送 `RUN` 或按下 RUN 键后，状态机大致这样走：

```text
MC_STOP
    -> 收到 START_CMD
MC_START
    -> FOC_CURRENT_initialize()
    -> 三相 PWM 先写 50% 中点
    -> EN_GATE_SET
    -> delay_nop(500)
    -> StartPWM()
MC_RUN
    -> ADC 回调中持续运行 FOC
```

这里有两个关键点：

1. PWM 启动前先写 50% 中点，避免输出上一次残留值。
2. `EN_GATE_SET` 目前仍是保留宏，如果你的硬件需要真实驱动芯片使能，后续要在 `bsp.h` 里把它改成对应 GPIO 操作。

## 停止和故障流程

发送 `STOP` 后：

```text
StopPWM()
EN_GATE_RESET
delay_nop(500)
FOC_CURRENT_initialize()
MC_STOP
```

发生故障后：

```text
StopPWM()
EN_GATE_RESET
MC_ERR
点亮对应故障灯
```

故障灯对应关系：

| 故障 | 含义 | 指示灯 |
| --- | --- | --- |
| `LV_ERR` | 欠压 | `LED_UV` |
| `OV_ERR` | 过压 | `LED_OV` |
| `OC_ERR` | 过流 | `LED_OC` |
| `OT_ERR` | 过温 | `LED_OT` |

## 温度链路

第三阶段温度是简单线性估算，第四阶段恢复为：

```text
ADC 温度原始值
    -> BspTemperature_Accumulate()
    -> PT100 电阻换算
    -> PT100 表格插值
    -> 10000 次平均
    -> SafeTemp
    -> 过温保护
```

注意：`SafeTemp` 需要积累一段时间才会更新，刚上电时显示 0 是正常现象。

## 低压试转前必须检查

先不要接电机，不要上高压。按下面顺序检查：

1. 烧录后串口出现 `[SAFE4] safe trial bring-up`。
2. 每秒 `delta` 稳定非 0。
3. `STATUS`、`RUN`、`STOP`、`UP`、`DOWN`、`DIR` 命令有响应。
4. 不接电机时，PA8/PA9/PA10 能看到三相 PWM。
5. 发送 `STOP` 后，三相 PWM 能关闭。
6. `vbus` 和万用表测到的母线电压接近。
7. 静态 `ia`、`ib` 接近 0，不能偏得离谱。
8. 人为触发故障时，对应 LED 能点亮，PWM 能关闭。
9. 确认 Gate/EN 如果硬件需要，已经真实接到对应 GPIO。

任意一项不通过，都不要接电机试转。

## 低压限流试转建议

通过前面的检查后，才进入低压限流试转。

建议顺序：

1. 使用可调限流电源。
2. 母线电压先用低压，不要直接上高压。
3. 电流限值先设小。
4. 电机空载并固定好。
5. 发送 `RUN` 后先观察电流和电机声音。
6. 如果电流瞬间冲高、驱动发热、声音异常，立即 `STOP` 或断电。
7. 低速稳定后，再逐步 `UP`。
8. 每次改速度都观察 `err`、`vbus`、`ia`、`ib`、`temp`。

## 当前还不是最终版的地方

第四阶段比第三阶段更接近可试转版本，但仍然不是最终高压运行版本。

还需要继续确认：

1. `EN_GATE_SET` / `EN_GATE_RESET` 是否要绑定真实 GPIO。
2. 电流零点是否要做上电自动校准。
3. 过流阈值是否符合你的硬件和电机。
4. 母线电压比例是否和实际分压电阻一致。
5. 温度滤波窗口是否适合实际控制频率。
6. 运行中换向是否需要恢复更完整的缓停再反转逻辑。

第四阶段通过后，才建议进入“低压空载试转调参”阶段。
