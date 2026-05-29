# 第二阶段 BSP 分层学习教程

第二阶段的目标不是马上让电机闭环运行，而是把第一阶段散在 `main.c` 里的硬件自检逻辑整理成 BSP 分层。

这一阶段完成后，主程序只负责：

```c
BspInit();

while (1)
{
    BspTask();
}
```

具体的 ADC、命令、PWM、保护、状态机逻辑都放回 `Int` 目录，后续再恢复 FOC、电流环、速度环时，代码会更容易读和调试。

## 本阶段恢复的文件

当前第二阶段恢复了这些文件：

```text
Int/Inc/bsp.h
Int/Src/bsp_adc.c
Int/Src/bsp_command.c
Int/Src/bsp_pwm.c
Int/Src/bsp_protection.c
Int/Src/bsp_motor.c
```

文件分工如下：

| 文件 | 作用 |
| --- | --- |
| `bsp.h` | BSP 公共类型、全局变量声明、函数声明 |
| `bsp_adc.c` | ADC 注入采样读取，把原始码值换算成电压、电流、温度 |
| `bsp_command.c` | 按键和串口命令解析，统一写入 `comm[]` |
| `bsp_pwm.c` | TIM1 PWM 启停，保留 CH4 触发 ADC |
| `bsp_protection.c` | 欠压、过压、过流、过温保护和故障灯控制 |
| `bsp_motor.c` | 电机状态机，当前只恢复 PWM 启停框架，不接 FOC |
| `Int_bsp.c` | BSP 总入口和 HAL 中断回调 |

## 为什么要做 BSP 分层

第一阶段为了快速确认硬件，很多逻辑直接写在 `main.c` 里。这适合调试硬件，但不适合继续扩展。

如果后面把电流采样、串口命令、PWM 启停、保护、状态机、FOC 都放在 `main.c`，代码会很快变乱。

BSP 分层的目的就是：

1. `main.c` 保持干净。
2. 每个硬件模块有自己的文件。
3. 按键和串口不直接操作 PWM，只改命令。
4. 状态机统一决定什么时候启动和停止 PWM。
5. 保护逻辑统一决定什么时候进入故障状态。

## 当前程序启动流程

`main.c` 中外设初始化后调用：

```c
BspInit();
```

`BspInit()` 在 `Int/Src/Int_bsp.c` 中，主要做这些事：

1. 串口打印第二阶段启动信息。
2. 调用 `ParaInit()` 初始化状态机变量。
3. 熄灭故障灯。
4. 启动 TIM1 基准和 CH4，让 ADC 注入触发继续工作。
5. 启动 ADC 注入转换中断。
6. 启动 UART 空闲接收中断。

主循环中调用：

```c
BspTask();
```

`BspTask()` 负责：

1. 扫描按键。
2. 把 `comm[]` 转换成状态机输入。
3. 执行一次状态机。
4. 每秒打印一次采样和状态。

## ADC 分层

`bsp_adc.c` 负责 ADC 采样换算。

当前 ADC 注入通道按下面的顺序读取：

| ADC 注入 Rank | 当前用途 |
| --- | --- |
| Rank 1 | 母线电压 |
| Rank 2 | 温度 |
| Rank 3 | A 相电流 |
| Rank 4 | B 相电流 |

ADC 回调在 `Int_bsp.c`：

```c
void HAL_ADCEx_InjectedConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1)
    {
        adc_injected_count++;
        BspAdcSample_Update();
        MotorProtection_CheckVbus();
    }
}
```

这里的原则是：中断里只做轻量工作。

当前做了三件事：

1. `adc_injected_count++`：证明 ADC 回调稳定触发。
2. `BspAdcSample_Update()`：读取采样值并换算。
3. `MotorProtection_CheckVbus()`：快速检查母线欠压/过压。

每秒串口会打印：

```text
[BSP2] adc=xxxxx delta=xxxxx vbus=xx ia=xx ib=xx temp=xx state=x err=x
```

重点看：

| 字段 | 含义 |
| --- | --- |
| `adc` | ADC 回调累计次数 |
| `delta` | 最近 1 秒 ADC 回调增量 |
| `vbus` | 母线电压 |
| `ia` / `ib` | A/B 相电流 |
| `temp` | 温度估算值 |
| `state` | 状态机状态 |
| `err` | 当前故障 |

如果 `delta=0`，先回到第一阶段方法检查 TIM1 CH4、ADC 注入触发和 ADC 中断。

## 命令分层

`bsp_command.c` 负责把按键和串口命令统一转换成 `comm[]`。

当前命令缓冲区含义：

```text
comm[0]  预留，参考 q 轴电流
comm[1]  参考转速 RPM
comm[2]  启停命令，1 表示启动，0 表示停止
```

按键功能：

| 按键 | 功能 |
| --- | --- |
| `KEY_RUN` | 启动/停止；故障时用于清故障 |
| `KEY_DIR` | 切换方向 |
| `KEY_SPEED_UP` | 加档，最高 3 档 |
| `KEY_SPEED_DOWN` | 减档，最低 0 档 |

当前速度档：

```c
RPM1 = 2000;
RPM2 = 4000;
RPM3 = 6000;
```

串口命令：

```text
RUN
START
STOP
DIR
UP
DOWN
STATUS
HELP
```

例如发送：

```text
STATUS
```

会打印当前命令层状态：

```text
[BSP2] UART_STATUS run=0 level=1 dir=1 rpm=0 state=3 err=0
```

## PWM 分层

`bsp_pwm.c` 负责 TIM1 PWM。

这里要注意一个设计点：

1. TIM1 基准计数和 CH4 会在 `BspInit()` 中启动。
2. CH4 用来触发 ADC 注入转换。
3. 三相 PWM 的 CH1/CH2/CH3 和互补输出由 `StartPWM()` / `StopPWM()` 控制。

也就是说，即使电机 PWM 没启动，ADC 仍然可以继续被 TIM1 CH4 触发。

当前状态机进入 `MC_START` 时会调用：

```c
BspPwm_SetComparePercent(25U, 50U, 75U);
StartPWM();
```

这一步只是第二阶段用于示波器验证的固定占空比，还不是 FOC 输出。

如果你接了真实电机，第二阶段仍然建议断开电机或关闭母线高压，只看 MCU/驱动输入侧 PWM。

## 保护分层

`bsp_protection.c` 当前恢复了四类保护：

| 故障 | 条件 |
| --- | --- |
| 欠压 | `vbus < 4V` |
| 过压 | `vbus > 20V` 持续多次 |
| 过流 | 任一相电流超过 `+/-20A` 持续多次 |
| 过温 | 温度超过 `80C` 持续多次 |

故障发生后：

1. `mc_info.mc_err` 写入故障类型。
2. `mc_info.mc_state` 进入 `MC_ERR`。
3. 对应故障 LED 点亮。
4. 状态机执行 `StopPWM()`。

故障状态下，按 `KEY_RUN` 或串口发送 `RUN` 会尝试清故障。

注意：如果硬件条件仍然不满足，比如母线电压一直低于 4V，刚清掉故障后又会马上再次进入欠压故障。

## 状态机分层

`bsp_motor.c` 负责状态机。

当前状态：

| 状态 | 含义 |
| --- | --- |
| `MC_RDY` | 就绪 |
| `MC_START` | 准备启动 PWM |
| `MC_RUN` | PWM 已运行 |
| `MC_STOP` | 停止 |
| `MC_ERR` | 故障 |

当前状态流转：

```text
MC_STOP + START_CMD -> MC_START
MC_START            -> StartPWM() -> MC_RUN
MC_RUN + STOP_CMD   -> StopPWM()  -> MC_STOP
任意故障             -> MC_ERR
MC_ERR              -> StopPWM()
```

这一阶段还没有接 `FOC_CURRENT_step()`。

这样做是故意的：先确认“命令 -> 状态机 -> PWM 启停 -> 保护关断”这条链路正确，再恢复 FOC 算法会稳很多。

## 建议调试顺序

第二阶段建议按这个顺序调：

1. 上电看串口是否打印 `[BSP2] BSP layer bring-up`。
2. 观察每秒 `[BSP2] adc=... delta=...` 是否稳定。
3. 发送 `HELP`，确认 UART RX 回调和命令解析正常。
4. 发送 `STATUS`，确认状态机初始状态。
5. 按 `KEY_UP` / `KEY_DOWN`，看档位是否变化。
6. 发送 `RUN` 或按 `KEY_RUN`，看状态是否从 `MC_STOP` 到 `MC_START` 再到 `MC_RUN`。
7. 用示波器看 PA8/PA9/PA10 和 PA7/PB0/PB1 是否输出。
8. 发送 `STOP` 或按 `KEY_RUN`，确认 PWM 停止。
9. 人为制造欠压或保持无母线状态，确认 `LV_ERR` 是否触发。

## 常见问题

| 现象 | 排查方向 |
| --- | --- |
| 编译找不到 `bsp.h` | Keil IncludePath 是否包含 `../Int/Inc` |
| 串口没有 `[BSP2]` | `BspInit()` 是否在 `main.c` 中调用 |
| `delta=0` | ADC 注入触发、TIM1 CH4、ADC 中断 |
| 一上电就是 `MC_ERR` | 多半是母线欠压，检查 `vbus` |
| RUN 后没有 PWM | 是否仍处于故障、状态是否进入 `MC_RUN` |
| 串口命令没反应 | USART1 RX、DMA、`HAL_UARTEx_RxEventCallback()` |
| LED 指示反了 | 检查 `STATUS_LED_ON` / `STATUS_LED_OFF` |

## 第二阶段通过标准

满足下面条件，就可以认为第二阶段通过：

1. `main.c` 只通过 `BspInit()` / `BspTask()` 调 BSP。
2. ADC 数据能进入 `mc_info`。
3. 按键和串口命令都能改 `comm[]`。
4. 状态机能响应 RUN/STOP。
5. `StartPWM()` / `StopPWM()` 能控制三相 PWM 输出。
6. 欠压、过压、过流、过温能进入故障状态。
7. 故障状态会停止 PWM，并点亮对应 LED。

## 下一阶段建议

第三阶段建议专门做 ADC 标定：

1. 打印 ADC 原始值。
2. 标定母线电压比例。
3. 标定 A/B 相电流零点。
4. 标定电流比例系数。
5. 标定温度换算。

ADC 标定完成后，再恢复 FOC_CURRENT 或重新接电流环会更稳。

