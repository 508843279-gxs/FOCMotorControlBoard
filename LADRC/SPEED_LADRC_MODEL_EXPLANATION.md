# Speed_LADRC_2018 模型详解

本文档对应工程中的 Simulink 模型：

```text
LADRC/Speed_LADRC_2018.slx
```

该模型是一个永磁同步电机 PMSM 的矢量控制仿真模型。它的核心学习点是：用 LADRC，也就是线性自抗扰控制，替代传统速度环 PI，输出 q 轴电流给定 `iq_ref`，再交给电流环和 SVPWM 驱动电机。

## 1. 模型整体功能

模型整体控制链路如下：

```text
速度给定 nref
    -> 速度 LADRC
    -> q 轴电流给定 iq_ref
    -> q 轴电流 PI
    -> uq*
    -> 反 Park 变换
    -> SVPWM
    -> 三相逆变器
    -> PMSM
    -> 电流/速度/角度反馈
```

同时 d 轴电流给定为 0：

```text
id_ref = 0
```

也就是常见的表贴式 PMSM 控制方式：

```text
id = 0
iq 控制转矩
```

## 2. 模型文件说明

`LADRC` 文件夹中和模型相关的主要文件：

| 文件 | 作用 |
| --- | --- |
| `Speed_LADRC_2018.slx` | 当前主要 Simulink 模型 |
| `Speed_LADRC_2018.slx.r2022b` | R2022b 兼容版本 |
| `Speed_LADRC_2018.slxc` | Simulink 缓存文件 |
| `Speed_LADRC_2018.zip` | 模型压缩备份 |
| `LADRC相关文献/` | 相关论文资料 |
| `slprj/` | Simulink 自动生成的缓存和临时工程 |

学习时优先打开：

```text
Speed_LADRC_2018.slx
```

`slprj` 和 `.slxc` 不需要手动修改。

## 3. 模型初始化参数

模型在 `InitFcn` 中定义了主要仿真和电机参数：

```matlab
Ts = 5e-7;        % 仿真步长，2 MHz
Tpwm = 1e-4;      % PWM 周期，100 us，对应 10 kHz
Tsample = Tpwm;   % 电流采样/控制周期
Tspeed = 5*Tpwm;  % 速度环周期，500 us，对应 2 kHz

Pn = 4;           % 电机极对数
Ls = 8.5e-3;      % 定子电感，Ld=Lq=Ls
Rs = 3;           % 定子电阻
flux = 0.1688;    % 永磁体磁链
Vdc = 311;        % 直流母线电压
J = 0.01;         % 转动惯量
B = 0;            % 阻尼系数

n_init = 0;       % 初始转速
fc_lpf = 200;     % 转速低通滤波截止频率

b = 1.5*Pn*flux/J;
wc = 100;
wo = 200;
iqmax = 25;

Ld1 = Ls;
Lq1 = Ls;
flux1 = flux;
Rs1 = Rs;
fc = 300;         % 电流环带宽

Enable = 1;       % 1 表示开启 1.5 拍延时补偿
td = 0.00005;
```

重点参数：

| 参数 | 含义 | 当前值 |
| --- | --- | --- |
| `Tpwm` | PWM 周期 | `100 us` |
| `Tsample` | 电流环周期 | `100 us` |
| `Tspeed` | 速度环周期 | `500 us` |
| `Pn` | 极对数 | `4` |
| `flux` | 磁链 | `0.1688` |
| `J` | 转动惯量 | `0.01` |
| `wc` | LADRC 控制带宽 | `100` |
| `wo` | LESO 观测器带宽 | `200` |
| `iqmax` | q 轴电流限幅 | `25 A` |
| `fc` | 电流环带宽 | `300 Hz` |

其中：

```text
b = 1.5 * Pn * flux / J
  = 1.5 * 4 * 0.1688 / 0.01
  = 101.28
```

`b` 可以理解成 q 轴电流对机械速度变化的控制增益。它来自 PMSM 电磁转矩公式和机械运动方程。

## 4. 顶层模型结构

顶层模型中可以看到这些关键模块：

| 模块 | 作用 |
| --- | --- |
| `Speed_set_switch` | 生成分段速度给定 `nref` |
| `iq的adrc1` | 速度 LADRC，输出 `iq_ref` |
| `Clark` | 三相电流 `abc` 到 `alpha/beta` |
| `Park` | `alpha/beta` 到 `d/q` |
| `PI电流环1` | d 轴电流环，输出 `ud*` |
| `PI电流环2` | q 轴电流环，输出 `uq*` |
| `Anti-park1` | 反 Park，同时带 1.5 拍延时补偿 |
| `SVPWM` | 根据 `alpha/beta` 电压生成三相 PWM |
| `Universal Bridge` | 三相逆变器 |
| `Permanent Magnet Synchronous Machine` | PMSM 电机模型 |
| `Subsystem1` | 根据电角度计算速度并低通滤波 |

顶层中还有一个 MathWorks 自带的：

```text
Active Disturbance Rejection Control
```

但是该模块在模型里是 `Commented on`，即被注释，不是当前实际参与控制的主速度环。

当前真正使用的是：

```text
iq的adrc1
```

## 5. 速度给定 Speed_set_switch

`Speed_set_switch` 用于生成分段速度给定。内部 MATLAB Function 逻辑如下：

```matlab
function y = fcn(t, t1,t2,t3,u1,u2,u3,v0)

if t<t1
    y=v0;
elseif t>t1 && t<t2
    y = u1;
elseif t>t2 && t<t3
    y = u2;
else
    y = u3;
end
```

含义是：

```text
t < t1           -> 输出 v0
t1 < t < t2      -> 输出 u1
t2 < t < t3      -> 输出 u2
t > t3           -> 输出 u3
```

它用于仿真速度阶跃，观察 LADRC 在不同目标转速下的响应。

在真实 STM32 工程中，这部分对应：

```text
按键/串口命令 -> refRPM
```

也就是你现在工程里的 `comm[1]` 和 `mc_info.refRPM`。

## 6. 速度测量 Subsystem1

`Subsystem1` 根据电机角度计算速度，并做低通滤波。

内部 MATLAB Function：

```matlab
wm = (thetam-thetamk1)/Tsample;

low_pass_factor1 = 2*pi*fc_lpf*Tsample;
low_pass_factor2 = low_pass_factor1/(1+low_pass_factor1);

wm_now = low_pass_factor2*wm+(1-low_pass_factor2)*wm_1;

if t==0
    wm_now = n_init/60*2*pi;
end
```

功能分两步：

1. 用角度差分计算瞬时机械角速度。
2. 用一阶低通滤波器降低速度抖动。

滤波系数：

```text
a = 2*pi*fc_lpf*Tsample
alpha = a / (1 + a)
wm_now = alpha * wm + (1-alpha) * wm_last
```

当前：

```text
fc_lpf = 200 Hz
Tsample = 100 us
```

## 7. Clark 和 Park 变换

模型从 PMSM 模块拿到三相电流：

```text
ia, ib, ic
```

先进入 `Clark`：

```text
ia/ib/ic -> i_alpha/i_beta
```

再进入 `Park`：

```text
i_alpha/i_beta + 电角度 theta_e -> id/iq
```

`id` 和 `iq` 是 FOC 的核心反馈量：

```text
id 控制磁链方向
iq 控制转矩方向
```

表贴式 PMSM 通常让：

```text
id_ref = 0
```

然后通过 `iq_ref` 控制转矩和速度。

## 8. 速度 LADRC 子系统 iq的adrc1

`iq的adrc1` 是这个模型的核心。

它有两个输入：

```text
Nr*  -> 目标转速
Nr   -> 实际转速
```

它有一个输出：

```text
iq   -> q 轴电流给定 iq_ref
```

从信号连接看，`iq的adrc1` 输出后被送到：

```text
iq_ref
```

然后进入 q 轴电流环 `PI电流环2`。

## 9. LADRC 控制律

模型里的速度 LADRC 大致控制律是：

```text
e = Nr* - Nr
e_rad = e * pi / 30
u0 = wc * e_rad
iq_ref_unsat = (u0 - z2) / b
iq_ref = sat(iq_ref_unsat, -iqmax, iqmax)
```

对应模型里的模块：

| 模块 | 作用 |
| --- | --- |
| `Sum1` | 计算速度误差 `Nr* - Nr` |
| `kp1` | 乘 `pi/30`，把 rpm 换成 rad/s |
| `kp` | 乘 `wc`，得到基础控制量 |
| `Sum` | 减去扰动估计 `z2` |
| `Gain` | 乘 `1/b`，得到电流给定 |
| `Saturation1` | 限幅到 `±iqmax` |

为什么要乘 `pi/30`：

```text
rpm -> rad/s
1 rpm = 2*pi/60 rad/s = pi/30 rad/s
```

所以 `Nr*` 和 `Nr` 如果是 rpm，进入控制律前要先换成 rad/s。

## 10. z1 和 z2 是什么

LADRC 的核心是 LESO，也就是线性扩张状态观测器。

它估计两个状态：

```text
z1 -> 速度估计值
z2 -> 总扰动估计值
```

这里的“总扰动”包括：

```text
负载变化
风扇阻力
摩擦
参数误差
母线电压变化带来的影响
未建模动态
```

传统 PI 只能看到速度误差，而 LADRC 会额外估计 `z2`，再把它从控制量里补偿掉。

所以 LADRC 的直观理解是：

```text
先根据速度误差算一个基础控制量
再估计外部扰动
最后把扰动抵消掉
```

## 11. LESO 子系统

模型里有两个 LESO：

```text
LESO
LESO1
```

外层有一个 `Manual Switch1`，可以在两个 LESO 实现之间切换。

### 11.1 LESO 离散递推版本

`LESO` 子系统使用 Delay 和增益手工搭出来。

它的输入：

```text
u   -> 控制量，也就是 iq_ref
Nr  -> 实际转速
```

关键增益：

```text
beta1 = 2*wo
beta2 = wo^2
```

当前：

```text
wo = 200
beta1 = 400
beta2 = 40000
```

它的结构可以理解为：

```text
e1 = Nr_rad - z1

z1(k+1) = z1(k) + Tspeed * (z2(k) + b*u(k) + beta1*e1)
z2(k+1) = z2(k) + Tspeed * (beta2*e1)
```

其中：

```text
Nr_rad = Nr * pi / 30
```

`z1` 跟踪实际速度，`z2` 跟踪外部扰动。

### 11.2 LESO1 Tustin 离散版本

`LESO1` 使用了两个离散传递函数：

```text
Discrete Transfer Fcn3:
Numerator   = [Tspeed Tspeed]
Denominator = [2 -2]

Discrete Transfer Fcn4:
Numerator   = [Tspeed Tspeed]
Denominator = [2 -2]
```

模型注释中写了：

```text
Digitization: Tustin's method
s = (2/T)(z-1)/(z+1)
```

也就是说 `LESO1` 是用双线性变换/Tustin 方法离散化出来的版本。

两个 LESO 的目标一样：

```text
估计 z1 和 z2
```

区别是离散实现方式不同。

## 12. 为什么 LADRC 输出的是 iq_ref

PMSM 电磁转矩近似为：

```text
Te = 1.5 * Pn * flux * iq
```

机械方程：

```text
J * dω/dt = Te - TL - Bω
```

忽略阻尼和负载时：

```text
dω/dt = (1.5 * Pn * flux / J) * iq
```

所以：

```text
b = 1.5 * Pn * flux / J
```

这就是初始化脚本里 `b` 的来源。

LADRC 速度环最终要控制的是速度变化，而速度变化主要由 `iq` 产生，所以速度环输出自然就是：

```text
iq_ref
```

## 13. 电流环 PI

模型里有两个电流环：

```text
PI电流环1 -> d 轴
PI电流环2 -> q 轴
```

电流环参数：

```matlab
fc = 300;
Ld1 = Ls;
Lq1 = Ls;
Rs1 = Rs;
```

d 轴 PI 关键增益：

```text
Kp_d = 2*pi*fc*Ld1
Ki_d = 2*pi*fc*Rs1
```

q 轴 PI 关键增益：

```text
Kp_q = 2*pi*fc*Lq1
Ki_q = 2*pi*fc*Rs1
```

当前数值大致为：

```text
Kp = 2*pi*300*0.0085 ≈ 16.02
Ki = 2*pi*300*3 ≈ 5654.87
```

电压输出限幅：

```text
± Vdc/sqrt(3)
```

当前：

```text
Vdc = 311 V
Vdc/sqrt(3) ≈ 179.56 V
```

## 14. 反 Park 和 1.5 拍延时补偿

`Anti-park1` 不只是普通反 Park，它内部还做了 1.5 拍延时补偿。

MATLAB Function 逻辑：

```matlab
m = 2/(we*Ts)*(sin(0.5*we*Ts));
theta_delay = theta + 1.5*we*Ts;

if (we == 0)||(Enable == 0)
   m = 1;
   theta_delay = theta;
end

ua_ref = m*(cos(theta_delay)*ud - sin(theta_delay)*uq);
ub_ref = m*(sin(theta_delay)*ud + cos(theta_delay)*uq);
```

含义：

1. 电机高速时，采样、计算、PWM 更新存在延迟。
2. 延迟会导致实际输出电压角度滞后。
3. 该模块通过提前角度 `1.5*we*Ts` 来补偿延迟。

其中：

```text
Enable = 1
```

表示当前开启该补偿。

## 15. SVPWM 模块

`SVPWM` 子系统输入：

```text
alpha
beta
```

输出：

```text
Pulse
```

内部包括：

| 子模块 | 作用 |
| --- | --- |
| `XYZ Caculate` | 计算 X/Y/Z 中间变量 |
| `N Caculate` | 判断空间矢量扇区 |
| `T1 T2 Caculate` | 计算相邻有效矢量作用时间 |
| `Tc Caculate` | 计算三相比较时间 |
| `Sabc Pulse` | 与三角载波比较生成 PWM |

`N Caculate` 中有一个 MATLAB Function：

```matlab
function y = fcn(u)
if u==0
    y=1;
else
    y = u;
end
```

作用是避免扇区 `N=0` 时后续多路选择出错，把 0 修正为 1。

## 16. 模型和当前 STM32 工程的对应关系

当前 STM32 工程里 TIM1 PWM 频率是：

```text
10 kHz
```

该 Simulink 模型中：

```text
Tpwm = 1e-4 = 100 us = 10 kHz
```

所以 PWM 频率是匹配的。

模型和工程变量可以这样对应：

| Simulink 模型 | STM32 工程 |
| --- | --- |
| `nref` / `Nr*` | `mc_info.refRPM` / `ObserverParam.RefRPM` |
| `Nr` | `ObserverParam.ObserverRPM` 或实际测速 |
| `iq_ref` | 后续应接入 `FOC_CURRENT_U.Ref_Iq` 或模型内部 `ObserverParam.RefIq` |
| `id_ref=0` | `FOC_CURRENT_U.Ref_Id = 0` |
| `ia/ib/ic` | `mc_info.isens_a/b/c` |
| `Vdc` | `mc_info.vbus` |
| `Tspeed` | 速度环运行周期，建议 500 us 或 1 ms |

## 17. 如果要移植到当前工程

建议不要一上来就把整个 Simulink 模型搬进 STM32，而是分步骤：

### 第一步：只保留当前 FOC 电流环

当前工程已经有：

```text
ADC 电流采样
母线电压采样
FOC_CURRENT_step()
TIM1 CCR 输出
```

先确认低压空载能稳定试转。

### 第二步：加入速度反馈

LADRC 必须有实际速度：

```text
Nr
```

如果没有编码器，就要确认无感观测器 `ObserverParam.ObserverRPM` 在低速和带风扇负载时是否可信。

### 第三步：实现速度 LADRC

每隔 `Tspeed` 执行一次：

```text
speed_error = refRPM - actualRPM
z1/z2 更新
iq_ref = (wc * speed_error_rad - z2) / b
iq_ref 限幅
```

### 第四步：把 `iq_ref` 接进 FOC

当前你的 `FOC_CURRENT.c` 中速度环已经会生成 `ObserverParam.RefIq`。

如果要外部 LADRC 接管速度环，有两种做法：

1. 修改 `FOC_CURRENT.c`，让外部 LADRC 输出的 `iq_ref` 进入电流环。
2. 在 Simulink 里重新生成一个支持外部 `Ref_Iq` 输入的 FOC 模型。

更推荐第二种，因为自动生成代码内部变量较多，手改容易破坏模型一致性。

## 18. 参数移植注意事项

模型参数不能直接照搬到真实电机。

尤其要重新确认：

| 参数 | 原因 |
| --- | --- |
| `J` | 风扇会明显增加转动惯量 |
| `b` | 由 `J` 决定，J 不准则 b 不准 |
| `iqmax` | 必须按真实板子和电机安全电流设置 |
| `wc` | 太大容易抖动或过流，太小响应慢 |
| `wo` | 太大噪声放大，太小扰动估计慢 |
| `fc_lpf` | 速度反馈噪声大时需要调低 |
| `Tspeed` | 必须和 MCU 实际调度周期一致 |

带风扇时尤其要注意：

```text
启动转矩需求更高
负载随转速上升变大
惯量比裸电机更大
方向错误会产生危险推力
```

## 19. 初学者建议观察的波形

在 Simulink 里建议先观察：

```text
nref
n
iq_ref
iq
z1
z2
Te
ia/ib/ic
PWM Pulse
```

观察重点：

1. `n` 是否能跟上 `nref`。
2. `iq_ref` 是否频繁打到 `±iqmax`。
3. `z2` 是否在负载变化时明显变化。
4. `iq` 是否能跟随 `iq_ref`。
5. 三相电流是否正弦、是否过大。
6. PWM 是否出现异常占空比。

## 20. 一句话总结

这个模型的核心思想是：

```text
速度误差 -> LADRC -> iq_ref -> 电流环 -> SVPWM -> 电机
```

LADRC 和普通 PI 最大的区别是：

```text
它不仅根据速度误差控制，还通过 LESO 估计总扰动 z2，并在控制量中补偿掉。
```

对你当前带风扇的电机来说，LADRC 的意义在于：风扇负载、摩擦、惯量误差都可以被看成扰动，理论上可以比单纯 PI 更抗负载变化。但真正上板前，必须先完成低压限流试转和速度反馈验证，再逐步接入 LADRC。
