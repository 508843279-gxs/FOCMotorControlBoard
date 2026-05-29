# 第三阶段 FOC 接入学习教程

第三阶段开始把 MATLAB/Simulink 生成的 FOC 模型接回工程，但仍然建议先断开电机或关闭母线高压，只验证数据链路和 PWM 输出是否合理。

本阶段目标：

1. 恢复 `MATLAB/FOC_CURRENT.c` 及其头文件。
2. ADC 采样电流写入 `FOC_CURRENT_U.ISensA/B/C`。
3. ADC 母线电压写入 `FOC_CURRENT_U.VBus`。
4. 目标转速写入 `ObserverParam.RefRPM`。
5. `FOC_CURRENT_step()` 输出 `FOC_CURRENT_Y.tAout/tBout/tCout`。
6. 输出值写入 TIM1 的 CH1/CH2/CH3 比较寄存器。

## 先看结论

当前第三阶段不是最开始的完整终版代码，也不能直接等同于“已经可以安全驱动电机旋转”。

当前已经完成的是：

1. FOC 模型文件已经恢复。
2. ADC 采样值已经接到 FOC 输入。
3. 目标转速已经接到 `ObserverParam.RefRPM`。
4. FOC 输出已经接到 TIM1 三相 PWM 比较值。

当前还没有完整恢复的是：

| 内容 | 当前状态 | 学习时要注意什么 |
| --- | --- | --- |
| `bsp_delay.c` | 未恢复 | 原始启动延时逻辑还没回到终版状态 |
| `bsp_shared.c` | 未恢复 | 全局变量组织方式和原始终版不同 |
| `bsp_status_led.c` | 未恢复 | 状态灯逻辑目前是简化版本 |
| `bsp_temperature.c` | 未恢复 | 温度滤波和 PT100 换算还不是完整版本 |
| Gate/EN 使能 | 未完整恢复 | 驱动芯片是否真正打开要单独确认 |
| 保护逻辑 | 已简化 | 欠压、过压、过流、过温阈值要实测校准 |
| 启停状态机 | 已简化 | 还不是最终可试转状态机 |

所以这一阶段的学习重点不是“让电机转起来”，而是理解 FOC 控制链路是怎么接进工程里的。

## 小白需要先理解的 5 件事

### 1. FOC 不是单独一个函数就能让电机转

`FOC_CURRENT_step()` 只是控制算法的一步。它需要正确的输入，也需要正确的输出通道。

输入包括：

```text
三相电流
母线电压
目标转速
目标 d 轴电流
```

输出包括：

```text
A 相 PWM 比较值
B 相 PWM 比较值
C 相 PWM 比较值
```

如果电流采样不准、母线电压不准、PWM 没开、驱动芯片没使能，即使 FOC 代码能运行，电机也不一定能安全旋转。

### 2. ADC 回调是控制周期的核心入口

本工程里 FOC 不是放在 `while(1)` 里随便跑，而是放在 ADC 注入转换完成之后跑。

原因是电机控制最关心“刚刚采到的电流”。典型顺序是：

```text
PWM 定时器触发 ADC
ADC 采到相电流和母线电压
ADC 回调读取采样值
FOC 根据新采样计算下一周期 PWM
TIM1 输出新的三相 PWM
```

这样才能保证采样、计算、输出有稳定的节拍。

### 3. TIM1 是三相 PWM 的输出核心

本工程使用 TIM1 输出三相 PWM：

| 信号 | 引脚 |
| --- | --- |
| TIM1_CH1 | PA8 |
| TIM1_CH2 | PA9 |
| TIM1_CH3 | PA10 |
| TIM1_CH1N | PA7 |
| TIM1_CH2N | PB0 |
| TIM1_CH3N | PB1 |

第三阶段的重点就是把：

```text
FOC_CURRENT_Y.tAout
FOC_CURRENT_Y.tBout
FOC_CURRENT_Y.tCout
```

写入：

```text
TIM1 CCR1
TIM1 CCR2
TIM1 CCR3
```

### 4. `ObserverParam.RefRPM` 是速度给定入口

串口或按键改变速度档位后，状态机会更新 `mc_info.refRPM`。

第三阶段再把它写入：

```c
ObserverParam.RefRPM = mc_info.refRPM;
```

FOC 模型内部会根据这个目标转速做速度环或观测器相关计算。

### 5. 有 PWM 不等于可以接电机

示波器看到 PA8/PA9/PA10 有 PWM，只能说明 MCU 正在输出控制信号。

真正让电机安全旋转，还需要确认：

1. 驱动芯片已经正确使能。
2. 三相桥没有短路风险。
3. 死区时间正确。
4. 母线电压采样正确。
5. 相电流零点正确。
6. 过流保护能及时关断 PWM。
7. `STOP` 命令能稳定关闭 PWM。

## 文件分工

新增或重点修改的文件：

| 文件 | 作用 |
| --- | --- |
| `MATLAB/FOC_CURRENT.c` | Simulink 生成的 FOC 控制模型 |
| `MATLAB/FOC_CURRENT.h` | FOC 输入、输出、参数结构体声明 |
| `Int/Src/bsp_motor.c` | 状态机和 FOC 调度 |
| `Int/Src/Int_bsp.c` | ADC 回调中触发 FOC 控制周期 |
| `MDK-ARM/FOCMotorControlBoard.uvprojx` | 加入 MATLAB include path 和 `FOC_CURRENT.c` |

建议阅读顺序：

1. 先看 `Int/Src/Int_bsp.c`，理解系统初始化和 ADC 回调入口。
2. 再看 `Int/Src/bsp_adc.c`，理解 ADC 原始值如何换算成电流和电压。
3. 再看 `Int/Src/bsp_command.c`，理解按键和串口如何改变运行命令。
4. 再看 `Int/Src/bsp_motor.c`，理解状态机如何调用 FOC。
5. 最后看 `MATLAB/FOC_CURRENT.c`，只需要先知道它的输入和输出，不用一开始就啃完整算法。

## FOC 数据流

当前第三阶段的数据流是：

```text
ADC 注入回调
    -> BspAdcSample_Update()
    -> mc_info.isens_a / isens_b / isens_c / vbus
    -> MotorControl_FocControlStep()
    -> FOC_CURRENT_U
    -> ObserverParam.RefRPM
    -> FOC_CURRENT_step()
    -> FOC_CURRENT_Y
    -> TIM1 CCR1/CCR2/CCR3
```

对应代码在 `bsp_motor.c`：

```c
FOC_CURRENT_U.ISensA = mc_info.isens_a;
FOC_CURRENT_U.ISensB = mc_info.isens_b;
FOC_CURRENT_U.ISensC = mc_info.isens_c;
FOC_CURRENT_U.VBus = mc_info.vbus;
FOC_CURRENT_U.Ref_Id = mc_info.refId;
ObserverParam.RefRPM = mc_info.refRPM;

FOC_CURRENT_step();

__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, ...FOC_CURRENT_Y.tAout...);
__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, ...FOC_CURRENT_Y.tBout...);
__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, ...FOC_CURRENT_Y.tCout...);
```

## 从代码入口一步一步看

### 第一步：主循环只负责调度慢任务

`main.c` 中仍然保持比较干净的结构：

```c
BspInit();

while (1)
{
    BspTask();
}
```

`BspTask()` 适合处理按键扫描、串口命令、状态打印这类慢任务。

FOC 不放在这里，因为主循环执行时间不稳定。

### 第二步：ADC 回调触发快控制周期

`Int/Src/Int_bsp.c` 里的 ADC 注入回调是快周期入口：

```c
void HAL_ADCEx_InjectedConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1)
    {
        adc_injected_count++;
        BspAdcSample_Update();
        MotorProtection_CheckVbus();
        MotorControl_FocControlStep();
    }
}
```

这段代码的含义是：

1. 先计数，方便串口观察 ADC 是否稳定触发。
2. 更新 ADC 采样换算结果。
3. 检查母线电压保护。
4. 如果状态允许，就运行一次 FOC。

### 第三步：状态机决定能不能跑 FOC

`MotorControl_FocControlStep()` 不是无条件运行。它会先判断：

```c
if (mc_state != MC_RUN)
{
    return;
}
```

这表示只有进入 `MC_RUN` 后，FOC 才会真正更新三相 PWM。

### 第四步：把 BSP 数据写入 FOC 输入

FOC 模型不直接认识 `mc_info`，所以需要做一次数据搬运：

```c
FOC_CURRENT_U.ISensA = mc_info.isens_a;
FOC_CURRENT_U.ISensB = mc_info.isens_b;
FOC_CURRENT_U.ISensC = mc_info.isens_c;
FOC_CURRENT_U.VBus = mc_info.vbus;
FOC_CURRENT_U.Ref_Id = mc_info.refId;
ObserverParam.RefRPM = mc_info.refRPM;
```

这一步非常关键。以后如果电机运行不正常，优先检查这些输入是不是合理。

### 第五步：执行 FOC 并更新 PWM

执行：

```c
FOC_CURRENT_step();
```

然后把输出写入 TIM1：

```c
__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, ...);
__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, ...);
__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, ...);
```

这就是第三阶段的核心闭环。

## 为什么 FOC 放在 ADC 回调后

FOC 控制需要使用“刚采到”的电流和母线电压。

所以第三阶段不是在主循环里随便跑 FOC，而是在 ADC 注入转换完成后运行：

```c
HAL_ADCEx_InjectedConvCpltCallback()
```

这样每次控制周期的顺序都是：

1. TIM1 CH4 触发 ADC。
2. ADC 注入转换完成。
3. 读取电流和母线电压。
4. 运行 FOC。
5. 更新下一周期 PWM 比较值。

这个顺序更接近真正电机控制的结构。

## 状态机变化

第二阶段 `MC_START` 会给固定占空比。

第三阶段改成：

1. `MC_START` 中调用 `FOC_CURRENT_initialize()`。
2. 三相 PWM 先置为 50% 中点。
3. 启动 PWM。
4. 状态进入 `MC_RUN`。
5. 下一次 ADC 回调开始运行 FOC 并更新 TIM1 比较值。

这样做是为了避免刚启动 PWM 时输出上一次模型残留值。

## 和第二阶段的区别

第二阶段主要验证 BSP 分层，所以 PWM 输出比较简单。

第二阶段的重点是：

```text
命令能进来
状态机会切换
PWM 能启动停止
保护能触发
```

第三阶段开始关注：

```text
ADC 采样是否能喂给 FOC
FOC 是否能根据输入计算输出
输出是否能写到 TIM1
串口是否能观察 ref/obs/state/err
```

简单说，第二阶段是“外壳和通路”，第三阶段是“把算法接进通路里”。

## 串口调试信息

第三阶段串口前缀变成：

```text
[FOC3]
```

上电应看到：

```text
[FOC3] FOC_CURRENT bring-up
[FOC3] Modules: adc command pwm protection motor foc_current
[FOC3] UART CMD: RUN STOP DIR UP DOWN STATUS HELP
```

每秒应看到类似：

```text
[FOC3] adc=12345 delta=10000 ref=2000 obs=1800 vbus=12 ia=0 ib=0 temp=25 state=2 err=0
```

重点看：

| 字段 | 含义 |
| --- | --- |
| `ref` | 写入 `ObserverParam.RefRPM` 的目标转速 |
| `obs` | FOC 模型估算的 `ObserverParam.ObserverRPM` |
| `state=2` | 当前处于 `MC_RUN` |
| `err=0` | 没有故障 |

学习时建议重点观察这 4 个字段：

| 字段 | 正常现象 |
| --- | --- |
| `delta` | 每秒稳定增加，说明 ADC 回调在跑 |
| `ref` | `UP/DOWN/DIR` 后会变化 |
| `obs` | 不接电机时可能为 0，这是正常现象 |
| `err` | 调试早期尽量保持为 0，出现故障要先排查 |

## 调试顺序

建议按这个顺序验证：

1. 不接电机，只给 MCU 和串口供电。
2. 烧录后确认 `[FOC3]` 启动信息出现。
3. 确认 `adc` 和 `delta` 每秒稳定增加。
4. 发送 `STATUS`，确认串口命令仍正常。
5. 接示波器到 PA8/PA9/PA10。
6. 发送 `RUN`，确认状态进入 `MC_RUN`。
7. 观察三相 PWM 是否从 50% 中点开始变化。
8. 发送 `UP` / `DOWN`，观察 `ref` 是否变化。
9. 发送 `STOP`，确认 PWM 停止。

## 学习任务

为了让后面的人真正学会，而不是只会烧录代码，建议按下面任务练习。

### 任务 1：确认 ADC 控制周期

目标：理解 ADC 回调为什么重要。

操作：

1. 烧录程序。
2. 打开串口助手。
3. 观察每秒打印的 `adc` 和 `delta`。

通过标准：

```text
delta 持续非 0，并且每秒变化比较稳定
```

如果 `delta=0`，说明 FOC 快周期没有入口，后面的电机控制都不用继续查。

### 任务 2：确认命令能改变目标转速

目标：理解命令层和 FOC 给定之间的关系。

操作：

1. 发送 `STATUS`。
2. 发送 `RUN`。
3. 发送 `UP`。
4. 发送 `DOWN`。
5. 发送 `DIR`。

观察：

```text
ref 是否跟着命令变化
state 是否能进入 MC_RUN
err 是否为 0
```

### 任务 3：确认 FOC 输出到 TIM1

目标：理解 FOC 输出不是打印出来，而是写到 PWM 比较寄存器。

操作：

1. 示波器接 PA8、PA9、PA10。
2. 发送 `RUN`。
3. 观察三相 PWM。
4. 发送 `STOP`。
5. 确认 PWM 停止。

注意：这一步仍然不要接电机，不要上高压。

### 任务 4：确认保护能关 PWM

目标：理解保护逻辑比让电机转更重要。

操作：

1. 观察串口中的 `err`。
2. 人为制造欠压条件，或保持未接母线状态。
3. 确认出现故障后状态进入 `MC_ERR`。
4. 确认三相 PWM 被关闭。

如果故障出现后 PWM 还在输出，不允许进入低压试转。

## 常见问题

| 现象 | 排查方向 |
| --- | --- |
| 编译找不到 `FOC_CURRENT.h` | Keil 和 VS Code 是否包含 `../MATLAB` |
| 链接找不到 `FOC_CURRENT_step` | Keil 是否加入 `MATLAB/FOC_CURRENT.c` |
| `ref` 不变化 | 按键/串口命令是否更新 `comm[1]` |
| `obs` 一直为 0 | 无感观测器还没得到有效电压/电流条件，或电机未实际运行 |
| PWM 比较值异常 | 检查 `VBus` 是否为 0 或明显错误 |
| RUN 后马上故障 | 先看 `err`，欠压通常是没有母线或采样比例不对 |

## 为什么现在不能直接说“可以转”

因为第三阶段只证明软件链路已经接通，还没有证明功率链路安全。

想让电机真正转起来，还需要继续恢复或确认：

1. Gate/EN 使能逻辑。
2. 原始启动延时。
3. 完整状态机。
4. 完整温度采样和滤波。
5. 电流零点校准。
6. 母线电压校准。
7. 过流保护实测。
8. 低压限流试转流程。

换句话说：

```text
第三阶段通过 = FOC 接入链路通过
第三阶段通过 != 可以直接高压带电机运行
```

## 安全提醒

第三阶段已经接入 FOC 输出，虽然还在调试阶段，也可能让驱动桥输出真实三相 PWM。

第一次测试建议：

1. 不接电机。
2. 不上母线高压。
3. 示波器先测 MCU 侧或驱动输入侧。
4. 确认 PWM、故障保护、STOP 都正常后，再考虑低压限流上电。

## 下一阶段建议

第四阶段建议目标是“恢复可安全试转版本”。

建议恢复或补齐：

1. `bsp_delay.c`。
2. `bsp_shared.c`。
3. `bsp_status_led.c`。
4. `bsp_temperature.c`。
5. Gate/EN 使能和启动延时。
6. 更完整的电机状态机。
7. 更完整的故障锁定和清除逻辑。
8. 低压试转调试说明。

等这些内容补齐，并且示波器和限流电源验证通过后，再进入真正的电机旋转测试。
