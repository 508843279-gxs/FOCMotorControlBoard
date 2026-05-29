# 第三阶段 FOC 接入学习教程

第三阶段开始把 MATLAB/Simulink 生成的 FOC 模型接回工程，但仍然建议先断开电机或关闭母线高压，只验证数据链路和 PWM 输出是否合理。

本阶段目标：

1. 恢复 `MATLAB/FOC_CURRENT.c` 及其头文件。
2. ADC 采样电流写入 `FOC_CURRENT_U.ISensA/B/C`。
3. ADC 母线电压写入 `FOC_CURRENT_U.VBus`。
4. 目标转速写入 `ObserverParam.RefRPM`。
5. `FOC_CURRENT_step()` 输出 `FOC_CURRENT_Y.tAout/tBout/tCout`。
6. 输出值写入 TIM1 的 CH1/CH2/CH3 比较寄存器。

## 文件分工

新增或重点修改的文件：

| 文件 | 作用 |
| --- | --- |
| `MATLAB/FOC_CURRENT.c` | Simulink 生成的 FOC 控制模型 |
| `MATLAB/FOC_CURRENT.h` | FOC 输入、输出、参数结构体声明 |
| `Int/Src/bsp_motor.c` | 状态机和 FOC 调度 |
| `Int/Src/Int_bsp.c` | ADC 回调中触发 FOC 控制周期 |
| `MDK-ARM/FOCMotorControlBoard.uvprojx` | 加入 MATLAB include path 和 `FOC_CURRENT.c` |

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

## 常见问题

| 现象 | 排查方向 |
| --- | --- |
| 编译找不到 `FOC_CURRENT.h` | Keil 和 VS Code 是否包含 `../MATLAB` |
| 链接找不到 `FOC_CURRENT_step` | Keil 是否加入 `MATLAB/FOC_CURRENT.c` |
| `ref` 不变化 | 按键/串口命令是否更新 `comm[1]` |
| `obs` 一直为 0 | 无感观测器还没得到有效电压/电流条件，或电机未实际运行 |
| PWM 比较值异常 | 检查 `VBus` 是否为 0 或明显错误 |
| RUN 后马上故障 | 先看 `err`，欠压通常是没有母线或采样比例不对 |

## 安全提醒

第三阶段已经接入 FOC 输出，虽然还在调试阶段，也可能让驱动桥输出真实三相 PWM。

第一次测试建议：

1. 不接电机。
2. 不上母线高压。
3. 示波器先测 MCU 侧或驱动输入侧。
4. 确认 PWM、故障保护、STOP 都正常后，再考虑低压限流上电。

