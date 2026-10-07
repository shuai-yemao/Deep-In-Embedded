# QPR 控制与 STM32 三相逆变器 SPWM 闭环实践

> 这是一份面向 STM32、三相两电平逆变器和静止坐标系正弦量控制的工程学习笔记。
主线是：**为什么用 QPR → QPR 到底是什么 → 如何把 QPR 接到 STM32 的 SPWM 输出链路**。

## 0. 先看结论

- PI 的积分器在 `0 Hz` 处具有很高增益，因此天然适合把**直流量或同步旋转坐标系中的恒定量**的稳态误差压到很小。
- 如果直接在静止坐标系中用 PI 控制 `50 Hz/60 Hz` 正弦参考，误差不是直流，而是周期信号；积分项不会像处理直流误差那样持续累加到“无限大”，因此会留下稳态跟踪误差。
- QPR 在 PI 的基础上增加一个以目标角频率 `ω0` 为中心的谐振通道，把高增益从 `0 Hz` 搬到目标交流频率附近，所以适合直接控制静止坐标系中的正弦电压或电流。
- 工程上的 QPR 通常是“准比例谐振”而不是理想无限尖锐的 PR：用 `ωc` 给谐振峰增加阻尼和有限带宽，使其能承受频率偏差、采样误差和数字实现误差。
- 在 STM32 中，QPR 只负责根据误差计算电压指令；后面的 `Vdc 归一化 → 限幅 → 占空比 → CCR → PWM` 仍然必须配合 ADC 同步、定时器预装载、死区、硬件关断和过流保护一起设计。

## 证据与边界

|标签|本笔记中的含义|
|--|--|
|`confirmed`|控制理论公式、QPR 的标准结构，以及 STM32 定时器中 ARR/CCR 表示周期和比较值的通用关系。|
|`inferred`|面向通用 STM32 + 三相两电平逆变器的参考闭环架构；并不代表某个具体工程已经采用这些引脚、定时器或采样点。|
|`unverified`|具体芯片的 PWM 模式、中心对齐计数细节、ADC 通道、传感器极性、死区时间、功率器件安全裕量，以及上板后的稳定性。|

本笔记没有绑定某块开发板、某个 STM32 型号或某个 HAL 工程。真正接入项目时，必须重新核对芯片参考手册、原理图、CubeMX 配置、ADC 触发点和栅极驱动器时序。

---

# 第一部分：为什么使用 QPR 控制？

## 1. QPR 解决的核心问题

假设我们希望逆变器输出一个正弦量：

$$
r(t)=R\sin(\omega_0 t)
$$

反馈量为 `y(t)`，误差为：

$$
e(t)=r(t)-y(t)
$$

闭环控制器要做的事情，是让 `e(t)` 尽可能小。这里的“小”不仅包括平均值小，还包括正弦基波幅值误差、相位误差和谐波误差小。

如果参考量是常数，例如 `r(t)=10 V`，PI 很擅长处理；但如果参考量是 `r(t)=10 sin(2π50t) V`，那么误差的主要成分也在 `50 Hz` 附近。此时，控制器需要在 `50 Hz` 附近有足够大的增益，而不是只在 `0 Hz` 有增益。

## 2. QPR 的典型应用场景

|应用场景|被控制量|为什么适合 QPR|
|--|--|--|
|单相并网逆变器|并网电流或输出电压|参考量和反馈量在静止坐标系中是正弦量，QPR 可以直接对基波频率提供高增益。|
|UPS/交流电源|输出电压|需要跟踪 `50/60 Hz` 正弦电压，并抑制负载变化造成的幅值和相位误差。|
|三相逆变器的 `αβ` 电压环|`vα、vβ`|两个静止坐标轴上的量均为正弦量，可分别配置两个 QPR。|
|三相逆变器的 `αβ` 电流环|`iα、iβ`|适用于不想做 Park 变换，或者需要直接处理不平衡交流量的场合。|
|有源电力滤波器|补偿电流|除基波 QPR 外，还可以增加 `3、5、7` 次等谐波谐振支路。|
|电网电压/频率变化较小的交流系统|指定频率交流量|只要 `ω0` 已知或可由 PLL/频率估计器更新，就能对目标频率集中补偿。|

QPR 并不是“所有交流系统的万能控制器”。如果频率大范围变化，固定 `ω0` 的谐振峰会偏离信号；此时需要自适应 QPR、频率估计，或者改用同步旋转坐标系控制。

## 3. PI 与 QPR 的公式对比

### 3.1 PI 控制器

时域公式：

$$
u(t)=K_p e(t)+K_i\int_0^t e(\tau)d\tau
$$

传递函数：

$$
G_{PI}(s)=K_p+\frac{K_i}{s}
$$

它的关键特性是：

$$
\lim_{s\rightarrow 0}\left|\frac{K_i}{s}\right|=\infty
$$

也就是说，PI 对 `0 Hz` 的恒定误差具有无限增益的理想趋势。只要系统没有饱和、模型没有严重错误，积分器就会继续调整输出，直到直流稳态误差足够小。

### 3.2 为什么静止坐标系中的 PI 不擅长正弦量

设误差为正弦信号：

$$
e(t)=E\sin(\omega_0 t)
$$

积分项为：

$$
\int e(t)dt=-\frac{E}{\omega_0}\cos(\omega_0t)+C
$$

注意：它仍然是一个有界的正弦量，不会像直流误差那样随时间无限增长。因此，PI 的积分器并没有自动获得“把 `ω0` 处误差压到零”的无限增益。

在频域中，PI 在 `ω0` 处的积分增益为：

$$
\left|\frac{K_i}{j\omega_0}\right|=\frac{K_i}{\omega_0}
$$

这个值是有限的，所以通常会留下非零的正弦稳态误差。

> 重要修正：不能简单说“PI 只能用于直流系统”。更准确的说法是：**PI 直接工作在静止坐标系时，最适合控制恒定量；它也能控制交流系统，但通常要先通过 Clarke/Park 变换，把正弦量变成同步旋转坐标系中的近似直流量。**

### 3.3 QPR 控制器

理想比例谐振控制器：

$$
G_{PR}(s)=K_p+\frac{2K_r s}{s^2+\omega_0^2}
$$

其中，`ω0` 是要跟踪的目标角频率，例如：

$$
\omega_0=2\pi f_0
$$

在 `s=jω0` 附近，谐振项的增益很高，因此 QPR 能直接在静止坐标系中对基波正弦量提供强跟踪能力。

工程中更常用带阻尼的准比例谐振形式：

$$
G_{QPR}(s)=K_p+\frac{2K_r\omega_c s}{s^2+2\omega_c s+\omega_0^2}
$$

其中 `ωc` 决定谐振峰的有效宽度。`ωc` 越小，峰越尖、选择性越强；`ωc` 越大，频率偏差容忍度越好，但会增加更多邻近频率的增益。

### 3.4 对比表

|项目|静止坐标系 PI|同步旋转坐标系 PI|静止坐标系 QPR|
|--|--|--|--|
|主要高增益频率|`0 Hz`|经过同步变换后，交流基波变成 `0 Hz`|直接位于 `ω0` 附近|
|是否需要坐标变换|不需要|通常需要 Clarke/Park 和反变换|通常不需要 Park 变换|
|适合的参考量|直流量|同步坐标系中的 `d/q` 恒定量|静止坐标系正弦量|
|对不平衡交流量的处理|需要额外设计|需要解耦、负序处理等|可分别在 `αβ` 轴处理，结构直接|
|主要调参|`Kp、Ki`|`Kp、Ki`、解耦和角度|`Kp、Kr、ωc、ω0`|
|主要风险|积分饱和、响应慢|角度误差、变换符号、解耦错误|频率失配、谐振状态饱和、数值稳定性|

## 4. 用一个费曼比喻理解“为什么是 QPR”

把控制误差想成客户欠款：

- PI 的积分器像一个只会“累计总欠款”的会计。客户每天固定欠 `1 元`，账本会持续增长，直到系统采取行动；这对应直流误差。
- 如果客户欠款是“今天欠、明天还、周期性反复”，普通会计看到的是一个平均接近零的账面变化，账本不再无限增长；这对应正弦误差。
- QPR 额外增加了一个“只对某个节拍敏感的共振记账器”。它对 `50 Hz` 的周期性欠款特别敏感，因此即使平均欠款为零，也能持续修正这一频率上的幅值和相位误差。

这个比喻的工程含义是：**PI 关注零频误差，QPR 关注目标交流频率误差。**

---

# 第二部分：什么是 QPR 控制？

## 5. QPR 的结构：比例通道 + 谐振通道

QPR 的输出可以拆成两部分：

$$
u(t)=u_p(t)+u_r(t)
$$

比例通道：

$$
u_p(t)=K_p e(t)
$$

谐振通道：

$$
G_r(s)=\frac{2K_r\omega_c s}{s^2+2\omega_c s+\omega_0^2}
$$

因此：

$$
U(s)=K_pE(s)+G_r(s)E(s)
$$

### 5.1 每个参数到底做什么

|参数|直观作用|调大后的典型影响|需要观察的风险|
|--|--|--|--|
|`Kp`|看到误差后立即修正的力度|带宽和响应速度提高|噪声放大、相位裕量下降、振荡|
|`Kr`|对 `ω0` 附近正弦误差的专门修正力度|基波稳态误差减小|过大时容易振铃、饱和、对频率失配敏感|
|`ω0`|谐振中心频率|把高增益放到指定基波处|设错会导致基波补偿效果很差|
|`ωc`|谐振峰宽度/阻尼|允许更大的频率偏差|过大时选择性变差，可能放大邻近谐波|
|`Ts`|控制器采样周期|决定离散系数与可实现带宽|实际 ISR 抖动或采样延迟会改变相位裕量|

## 6. 理想 PR 与工程 QPR 的区别

理想 PR 的分母是：

$$
s^2+\omega_0^2
$$

它在目标频率处可以产生理论上的无限增益，但数字系统中存在：

- 采样周期有限；
- 目标频率可能漂移；
- 浮点/定点数值有限；
- ADC 噪声和 PWM 延迟存在；
- 输出电压受 `Vdc` 和调制比限制。

所以直接实现理想 PR 往往不实用。QPR 通过 `2ωc s` 增加阻尼，使峰值变为有限值，并让控制器能容忍一定的频率偏差。

## 7. QPR 的离散实现

采用 Tustin（双线性）变换，将谐振项离散化。定义：

$$
G_r(z)=\frac{Y(z)}{E(z)}=
\frac{a_1(1-z^{-2})}{b_0+b_1z^{-1}+b_2z^{-2}}
$$

其中：

$$
a_1=4K_rT_s\omega_c
$$

$$
b_0=T_s^2\omega_0^2+4T_s\omega_c+4
$$

$$
b_1=2T_s^2\omega_0^2-8
$$

$$
b_2=T_s^2\omega_0^2-4T_s\omega_c+4
$$

对应的运行时差分方程是：

$$
y[k]=\frac{a_1e[k]-a_1e[k-2]-b_1y[k-1]-b_2y[k-2]}{b_0}
$$

最终控制器输出：

$$
u[k]=K_pe[k]+y[k]
$$

这里的 `y[k]` 是谐振通道内部状态输出，不是被控对象反馈量。

> 不同资料可能把 `Kr` 叫作 `Ki`，也可能把整个谐振增益写成别的归一化形式。实现时必须从传递函数重新推导离散系数，不能只复制变量名。

## 8. 参考 C 代码骨架

下面代码是**通用算法示例**，不是某个具体 STM32 工程的现成驱动。它只实现 QPR 运算，不包含 ADC、PWM、故障保护和 RTOS 绑定。

```c_cpp
typedef struct
{
    float kp;
    float kr;
    float w0;
    float wc;
    float ts;

    float a1;
    float b0;
    float b1;
    float b2;

    float e_z1;
    float e_z2;
    float y_z1;
    float y_z2;
} QprController_t;

static float Qpr_Clamp(float value, float min_value, float max_value)
{
    if (value < min_value)
    {
        return min_value;
    }

    if (value > max_value)
    {
        return max_value;
    }

    return value;
}

void Qpr_Init(QprController_t *controller,
              float kp,
              float kr,
              float w0,
              float wc,
              float ts)
{
    controller->kp = kp;
    controller->kr = kr;
    controller->w0 = w0;
    controller->wc = wc;
    controller->ts = ts;

    controller->a1 = 4.0f * kr * ts * wc;
    controller->b0 = ts * ts * w0 * w0 + 4.0f * ts * wc + 4.0f;
    controller->b1 = 2.0f * ts * ts * w0 * w0 - 8.0f;
    controller->b2 = ts * ts * w0 * w0 - 4.0f * ts * wc + 4.0f;

    controller->e_z1 = 0.0f;
    controller->e_z2 = 0.0f;
    controller->y_z1 = 0.0f;
    controller->y_z2 = 0.0f;
}

float Qpr_Update(QprController_t *controller, float error)
{
    float resonant_output;
    float output;

    resonant_output =
        (controller->a1 * error
         - controller->a1 * controller->e_z2
         - controller->b1 * controller->y_z1
         - controller->b2 * controller->y_z2)
        / controller->b0;

    output = controller->kp * error + resonant_output;

    controller->e_z2 = controller->e_z1;
    controller->e_z1 = error;
    controller->y_z2 = controller->y_z1;
    controller->y_z1 = resonant_output;

    return output;
}
```

### 8.1 代码边界和状态所有权

- `QprController_t` 由调用者静态分配，避免 ISR 中动态分配。
- `Qpr_Init()` 只在启动或参数切换时调用；不要在每个 PWM 周期重复初始化，否则谐振状态会被清零。
- `Qpr_Update()` 应在固定周期执行；`Ts` 必须与实际调用周期一致。
- `e_z1、e_z2、y_z1、y_z2` 是控制器私有状态，不能被其他模块直接修改。
- 如果 `ω0` 改变，必须重新计算离散系数；参数切换时要设计状态平滑策略，避免输出突跳。

## 9. QPR 的饱和与抗“谐振累积”

QPR 也会遇到类似积分器的“状态积累”问题：当输出电压已经达到可用上限，但误差还在推动输出继续增大时，谐振状态可能不断变大，解除饱和后出现过冲或长时间振铃。

基础策略：

1. 对 `u_raw` 做输出限幅，得到 `u_sat`。
2. 记录 `u_raw - u_sat`，作为诊断量。
3. 在饱和且误差继续把输出推向饱和方向时，冻结谐振状态更新或采用状态空间抗饱和回算。
4. 当误差已经把输出拉回限幅范围时，恢复正常更新。
5. 把限幅状态、谐振输出和 `Vdc` 一起记录，不能只看最终 PWM。

简单的“直接修改 `y_z1`”并不总是正确，因为 IIR 结构的状态含义与实现形式有关。更稳妥的方式是：先确定状态空间实现，再设计与 `u_sat-u_raw` 对应的 back-calculation；如果暂时没有完整抗饱和设计，至少采用条件更新并降低实验电压。

---

# 第三部分：如何使用 QPR 配合 STM32 和三相逆变器 SPWM？

## 10. 参考闭环系统

这里选择一个便于理解的参考架构：在静止 `αβ` 坐标系中，使用两个 QPR 控制器调节三相逆变器的交流电压。对于电流控制，把反馈量从 `vαβ` 换成 `iαβ`，后续的调制链路基本不变。

```echarts
flowchart LR
    VREF[正弦参考 vαβ*] --> ERR[误差 eαβ = vαβ* - vαβ]
    MEAS[ADC采样 vabc] --> CLARKE[Clarke变换]
    CLARKE --> FB[反馈 vαβ]
    FB --> ERR
    ERR --> QPR[两路 QPR：QPRα / QPRβ]
    QPR --> VCMD[电压指令 vαβ_cmd]
    VDC[ADC采样 Vdc] --> NORM[Vdc归一化]
    VCMD --> NORM
    NORM --> INV[反Clarke：得到 ma、mb、mc]
    INV --> LIMIT[调制比限幅]
    LIMIT --> DUTY[占空比 d = 0.5 + 0.5m]
    DUTY --> CCR[CCR = round(d × 计数周期)]
    CCR --> PWM[STM32定时器 PWM/互补PWM]
    PWM --> POWER[三相逆变桥 + 滤波器 + 负载]
    POWER --> MEAS
    FAULT[过流/过压/欠压/驱动故障] --> SAFE[硬件Break或安全关断]
    SAFE --> PWM
```

### 10.1 这张图中哪些是已确认、哪些是目标设计

- `confirmed`：QPR 的误差输入、控制器输出、PWM 调制和反馈闭环之间的信号关系。
- `inferred`：使用 `αβ` 两路 QPR、反 Clarke 得到三相调制波，是三相静止坐标系控制的通用参考实现。
- `unverified`：实际工程是否采样相电压、线电压、相电流，是否使用中性点，是否采用中心对齐 PWM，ADC 采样位于载波中心还是边沿。

## 11. 实际接入步骤

### 步骤 1：先确定功率级、采样和时序

至少要明确：

|项目|必须确定的内容|
|--|--|
|逆变器|两电平还是三电平；半桥上下管逻辑；驱动器输入极性；死区需求。|
|反馈量|输出电压、输出电流、相电流或线电流；传感器增益和零点。|
|DC 母线|`Vdc` 的 ADC 分压比例、采样范围、欠压/过压阈值。|
|PWM|载波频率、边沿对齐/中心对齐、ARR、预装载、更新事件。|
|采样|ADC 触发点、采样保持时间、转换完成到控制计算的延迟。|
|安全|硬件过流比较器、Break 输入、软件限流和故障恢复状态机。|

不要先调 `Kr` 再补这些基础条件。相序、传感器极性、ADC 偏置或 PWM 互补关系错误时，调参只会把问题隐藏得更深。

### 步骤 2：ADC 采样、单位换算和 Clarke 变换

以三相电压反馈为例：

1. 读取 ADC 原始值。
2. 减去零点偏置。
3. 按传感器增益换算成伏特或安培。
4. 根据相序、传感器方向和硬件接线确认 `a-b-c` 顺序。
5. 进行 Clarke 变换。

常用的幅值不变 Clarke 变换为：

$$
v_\alpha=v_a
$$

$$
v_\beta=\frac{v_a+2v_b}{\sqrt{3}}
$$

如果工程使用的是功率不变 Clarke 变换，系数会不同。两种约定都可以，但参考量、反馈量和后续反变换必须使用同一种约定。

### 步骤 3：计算两路 QPR 输出

$$
e_\alpha=v_\alpha^*-v_\alpha
$$

$$
e_\beta=v_\beta^*-v_\beta
$$

$$
v_{\alpha,cmd}=QPR_\alpha(e_\alpha)
$$

$$
v_{\beta,cmd}=QPR_\beta(e_\beta)
$$

如果系统有输入电压前馈、负载电流前馈或反电动势补偿，可以在这里合成：

$$
v_{\alpha,raw}=v_{\alpha,ff}+v_{\alpha,cmd}
$$

$$
v_{\beta,raw}=v_{\beta,ff}+v_{\beta,cmd}
$$

前馈不是 QPR 的替代品；它用于减轻反馈控制器的负担，必须经过限幅和验证。

### 步骤 4：用 `Vdc` 做归一化

对于两电平逆变器、以桥臂中点等效电压为参考的基础 SPWM，常用归一化关系为：

$$
m_\alpha=\frac{2v_{\alpha,cmd}}{V_{dc}}
$$

$$
m_\beta=\frac{2v_{\beta,cmd}}{V_{dc}}
$$

这里的 `2` 来自桥臂平均极点电压幅值约为 `m·Vdc/2`。如果你的 `v_cmd` 定义的是线电压、滤波器两端电压或其他坐标量，比例关系必须重新推导，不能机械套用。

工程注意事项：

- `Vdc` 不能为零或低于安全阈值；否则必须关闭 PWM 或进入降额状态。
- 对 `Vdc` 做合理滤波，但不要让滤波延迟破坏快速保护。
- `Vdc` 采样必须有上限检查，ADC 异常值不能直接进入除法。
- 当母线电压下降时，归一化会使调制比增加；这正是限幅和欠压策略必须存在的原因。

### 步骤 5：反 Clarke 转为三相调制波

采用零序为零的基础 SPWM：

$$
m_a=m_\alpha
$$

$$
m_b=-\frac{1}{2}m_\alpha+\frac{\sqrt{3}}{2}m_\beta
$$

$$
m_c=-\frac{1}{2}m_\alpha-\frac{\sqrt{3}}{2}m_\beta
$$

这是一种“正弦 PWM 基线”。如果改用零序注入或 SVPWM，可以提高母线利用率，但那已经改变了调制器的实现边界，不能仍按纯 SPWM 的幅值上限解释。

### 步骤 6：限幅输出

先对每相调制比做限幅：

$$
m_x=\operatorname{sat}(m_x,-m_{max},m_{max}),\quad x\in\{a,b,c\}
$$

基础调试阶段可以取 `mmax < 1`，例如 `0.8~0.95`，给死区、采样误差和母线波动留出余量。最终值必须结合功率级、栅极驱动、热设计和实测波形确定。

限幅时应同时记录：

```text
u_raw       QPR未限幅输出
u_sat       限幅后的输出
sat_flag    是否发生限幅
Vdc         当前母线电压
m_a/b/c     三相调制比
```

如果 `sat_flag` 长时间为 1，不要继续增大 `Kr`；先查母线能力、负载需求、参考幅值和抗饱和策略。

### 步骤 7：调制比转换为占空比

将 `m ∈ [-1,1]` 平移到 `d ∈ [0,1]`：

$$
d_a=\frac{1+m_a}{2}
$$

$$
d_b=\frac{1+m_b}{2}
$$

$$
d_c=\frac{1+m_c}{2}
$$

之后再次做软件保护限幅，例如：

$$
d=\operatorname{sat}(d,d_{min},d_{max})
$$

`dmin/dmax` 是否需要设置，以及具体取值，取决于定时器、驱动器、死区和允许的最小脉宽。不要为了“公式完整”而默认把占空比推到 0% 或 100%。

### 步骤 8：占空比转换为 CCR 计数值

对于常见的边沿对齐、PWM1、向上计数模型，ST 的通用培训资料给出的关系可写成：

$$
D=\frac{CCR}{ARR+1}
$$

因此：

$$
CCR=\operatorname{round}\left(D(ARR+1)\right)
$$

伪代码：

```c_cpp
static uint32_t Pwm_DutyToCcr(float duty, uint32_t arr)
{
    float clamped_duty;

    clamped_duty = Qpr_Clamp(duty, 0.0f, 1.0f);

    return (uint32_t)(clamped_duty * ((float)arr + 1.0f) + 0.5f);
}
```

但必须注意：

- 中心对齐模式的计数路径不同；
- PWM1/PWM2 和输出极性会改变“高电平脉宽”的解释；
- 某些 HAL 示例使用 `CCR/ARR` 的近似写法；
- 高级定时器还涉及重复计数器、互补输出、死区和 Break 输入。

所以 `CCR` 公式必须以具体 STM32 系列参考手册和当前定时器配置为准。更新 CCR 时建议打开比较寄存器预装载，让新值在安全的更新事件生效，避免在一个 PWM 周期中途改变脉宽。

### 步骤 9：PWM 输出与功率级响应

STM32 定时器负责产生 PWM，但完整的功率输出链路是：

```text
CCR1/CCR2/CCR3
    ↓
定时器比较输出
    ↓
互补输出 + 死区 + Break关断
    ↓
栅极驱动器
    ↓
三相桥臂
    ↓
LC/L滤波器与负载
    ↓
电压/电流传感器
    ↓
ADC同步采样
```

QPR 计算不能替代硬件保护。过流关断应尽量有独立的比较器或定时器 Break 路径，不能只依赖软件在下一个 ISR 中把占空比写成零。

## 12. 从 QPR 到 SPWM 的参考伪代码

下面示例把步骤串在一起，具体 ADC、Clarke、定时器和故障 API 需要由目标工程实现。

```c_cpp
void Control_Isr(void)
{
    float va;
    float vb;
    float vc;
    float v_alpha;
    float v_beta;
    float e_alpha;
    float e_beta;
    float v_alpha_cmd;
    float v_beta_cmd;
    float ma;
    float mb;
    float mc;
    float duty_a;
    float duty_b;
    float duty_c;

    if (Protection_IsFaulted())
    {
        Pwm_DisableOutputs();
        return;
    }

    Adc_ReadPhaseVoltage(&va, &vb, &vc);
    Vdc_ReadFiltered();

    if (Vdc_IsBelowSafeThreshold())
    {
        Pwm_DisableOutputs();
        return;
    }

    Clarke_AmplitudeInvariant(va, vb, vc, &v_alpha, &v_beta);

    e_alpha = v_alpha_ref - v_alpha;
    e_beta = v_beta_ref - v_beta;

    v_alpha_cmd = Qpr_Update(&qpr_alpha, e_alpha);
    v_beta_cmd = Qpr_Update(&qpr_beta, e_beta);

    ma = 2.0f * v_alpha_cmd / vdc;
    mb = 2.0f * (-0.5f * v_alpha_cmd
                 + 0.8660254f * v_beta_cmd) / vdc;
    mc = 2.0f * (-0.5f * v_alpha_cmd
                 - 0.8660254f * v_beta_cmd) / vdc;

    ma = Qpr_Clamp(ma, -MODULATION_LIMIT, MODULATION_LIMIT);
    mb = Qpr_Clamp(mb, -MODULATION_LIMIT, MODULATION_LIMIT);
    mc = Qpr_Clamp(mc, -MODULATION_LIMIT, MODULATION_LIMIT);

    duty_a = 0.5f + 0.5f * ma;
    duty_b = 0.5f + 0.5f * mb;
    duty_c = 0.5f + 0.5f * mc;

    Timer_SetCompareA(Pwm_DutyToCcr(duty_a, PWM_ARR));
    Timer_SetCompareB(Pwm_DutyToCcr(duty_b, PWM_ARR));
    Timer_SetCompareC(Pwm_DutyToCcr(duty_c, PWM_ARR));
}
```

### 12.1 这个伪代码没有替你决定的事情

- `v_alpha_ref/v_beta_ref` 如何生成：固定频率正弦表、相位累加器、PLL，还是上层给定。
- `Adc_ReadPhaseVoltage()` 读的是相电压、线电压还是电流。
- `Clarke_AmplitudeInvariant()` 使用哪一种变换归一化。
- PWM 是边沿对齐还是中心对齐。
- `Timer_SetCompareX()` 是否写入预装载寄存器，以及何时真正生效。
- `MODULATION_LIMIT` 如何结合母线电压、死区、负载和热约束确定。
- 故障发生后是立即关断、锁存等待人工清除，还是允许有条件自动恢复。

## 13. 数值例子：从 QPR 电压指令到 CCR

假设：

```text
Vdc       = 48 V
v_alpha   = 15 V
v_beta    = 0 V
PWM_ARR   = 2399
采用边沿对齐、向上计数的近似关系
```

归一化：

$$
m_\alpha=\frac{2\times15}{48}=0.625
$$

当 `vβ=0` 时：

$$
m_a=0.625
$$

$$
m_b=m_c=-0.3125
$$

占空比：

$$
d_a=0.5+0.5\times0.625=0.8125
$$

$$
d_b=d_c=0.5+0.5\times(-0.3125)=0.34375
$$

以 `CCR≈round(d(ARR+1))` 计算：

```text
CCR_A ≈ round(0.8125 × 2400) = 1950
CCR_B ≈ round(0.34375 × 2400) = 825
CCR_C ≈ round(0.34375 × 2400) = 825
```

这只是数值链路验证，不等于功率级已经安全，也不等于实际定时器一定使用这个精确公式。

## 14. QPR 的调试手段

### 14.1 软件变量观测

建议在低频调试接口、RTT、SWO 或环形缓冲区中记录以下变量：

|变量|用途|
|--|--|
|`reference`、`feedback`|判断参考量和反馈量单位、相位、幅值是否一致。|
|`error`|判断误差是否包含直流偏置、基波或明显谐波。|
|`qpr_resonant_output`|判断 QPR 是否真的在目标频率工作。|
|`u_raw/u_sat`|判断是否被电压能力或调制比限幅。|
|`Vdc`|判断归一化是否受母线波动影响。|
|`m_a/m_b/m_c`|判断三相调制是否对称、是否越界。|
|`duty_a/b/c`、`CCR1/2/3`|连接算法结果和定时器实际寄存器值。|
|`sat_flag`、`fault_code`|区分控制器饱和和功率级保护触发。|
|`ISR_time`、`jitter`|判断控制周期和计算时间是否满足实时性。|

不要只输出最终 PWM。最终 PWM 正常，并不能证明 QPR 参数、ADC 极性和闭环反馈都是正确的。

### 14.2 示波器观察顺序

建议按风险从低到高观察：

1. **无功率级/低压逻辑检查**：观察三路 PWM 频率、占空比对称性、互补关系和死区。
2. **固定占空比开环**：确认桥臂极性、驱动器输入输出逻辑和 Break 关断有效。
3. **低母线电压开环 SPWM**：确认三相基波相序和幅值。
4. **只开 `Kp`**：`Kr=0`，确认基本闭环方向正确。
5. **加入较小 `Kr`**：观察基波误差和相位误差是否下降。
6. **逐步增大 `Kr` 或调整 `ωc`**：同时监视饱和、振铃和温升。
7. **在目标负载与扰动下复测**：确认负载变化不会触发持续饱和或保护抖动。

推荐示波器通道：

```text
CH1：PWM/栅极信号
CH2：ADC触发或控制ISR翻转脚
CH3：相电压或滤波后输出电压
CH4：相电流/负载电流
```

### 14.3 频域调试

对参考、反馈和误差做 FFT，重点看：

- `f0` 处误差是否下降；
- `3f0、5f0、7f0` 等谐波是否过大；
- 开关频率附近是否出现采样噪声或控制带宽过高；
- `ω0` 偏移后，谐振补偿是否明显变差；
- 限幅前后误差频谱是否发生变化。

如果目标是电流控制，还应检查电感电流纹波、采样时刻和 PWM 载波的相位关系。

### 14.4 注入测试与小信号验证

在安全的低压、限流条件下，可以给参考量或控制通道注入小幅扫频信号，观察闭环幅频和相频响应。注入测试可以帮助判断：

- `Kp` 是否导致带宽过高；
- `Kr` 和 `ωc` 是否形成过尖的谐振峰；
- ADC、计算、PWM 更新总延迟是否侵蚀相位裕量；
- LC/LCL 滤波器共振是否落在控制器高增益区域。

这类测试属于工程验证手段，不应替代功率级过压、过流和热保护。

## 15. 常见现象 → 可能原因 → 排查方向

|现象|可能原因|首先检查|
|--|--|--|
|QPR 输出一加就振荡|`Kr` 太大、`ωc` 太小、延迟太大或反馈极性反了|`error` 符号、`ω0`、ISR 周期、PWM 更新时刻|
|基波跟踪很差|`ω0` 设错、QPR 没有真正更新、单位不一致|参考频率、离散系数、`qpr_resonant_output`|
|三相幅值不对称|ADC 增益/偏置不同、相序错误、CCR 映射错|三相原始 ADC、校准值、`CCR1/2/3`|
|过零点畸变严重|死区、最小脉宽、采样错位或调制限幅|栅极波形、死区、ADC 触发点|
|母线电压下降时突然失控|`Vdc` 归一化过大、欠压保护缺失|`Vdc` 原始值、除法保护、调制比|
|负载变化后长时间恢复|输出饱和但谐振状态继续积累|`u_raw/u_sat`、`sat_flag`、抗饱和策略|
|PWM 频率对但波形错|PWM1/PWM2、输出极性或中心对齐公式理解错误|参考手册、定时器寄存器、实际引脚波形|
|CPU 占用高或控制抖动|QPR 在错误上下文运行、浮点开销、ISR 被阻塞|ISR 时间、最高优先级、编译优化和采样同步|

## 16. 推荐调参顺序

1. 先把 `Kr=0`，只用较小 `Kp` 验证闭环方向。
2. 在低压、限流、低幅值参考下逐步提高 `Kp`，直到响应足够快但没有明显振荡。
3. 将 `ω0` 设置为实际目标频率的角频率：`ω0=2πf0`。
4. 先选择一个较宽但不过分的 `ωc`，再逐步增大 `Kr`。
5. 观察基波误差、相位误差、输出饱和和谐振振铃。
6. 如果频率会漂移，确认是增大 `ωc` 还是增加频率估计/自适应参数，而不是盲目增大 `Kr`。
7. 最后才在更高母线电压、更大负载和更宽参考幅值下验证。

调参过程中每次只改变一个主要参数，并保留参数、母线电压、负载、采样频率和波形记录。

## 17. 验证分层

|验证层级|可以证明什么|不能证明什么|
|--|--|--|
|公式推导/桌面计算|PI/QPR 频率特性和归一化链路是否自洽|实时执行、ADC 噪声、功率级安全|
|主机仿真或 Python/Matlab|差分方程、限幅和参考响应的逻辑|具体 MCU 定时器、采样延迟和硬件极性|
|C 编译/静态检查|类型、边界、接口和基础实现问题|真实波形、死区和保护动作|
|低压上板|PWM、ADC、反馈、QPR 闭环的基本方向|满压满载、热稳定性、极限故障|
|示波器/逻辑分析仪|PWM 频率、死区、采样触发、相序和延迟|长时间可靠性和复杂负载全部工况|
|额定工况测试|波形质量、THD、动态响应、温升|未测试工况和器件寿命|

本笔记只提供算法和参考接入链路；没有执行主机仿真、交叉编译、烧录或目标板测试，因此 STM32 实机状态仍为 `unverified`。

## 18. 三问自检

### 问题 1：为什么 QPR 能减少正弦稳态误差？

因为它在目标频率 `ω0` 附近提供很高的控制增益，使该频率上的误差得到强力修正；PI 的高增益主要在 `0 Hz`，直接处理静止坐标系正弦量时在 `ω0` 处只有有限增益。

### 问题 2：哪个参数最关键？

没有一个参数永远最关键：`ω0` 决定“共振对准哪里”，`Kr` 决定“对准后的力度”，`ωc` 决定“峰有多尖”，`Kp` 决定基础带宽和相位裕量。工程上四者必须结合采样频率、延迟、滤波器和调制能力一起看。

### 问题 3：如果需求从 50 Hz 改为 60 Hz，应改哪里？

至少要更新 `ω0=2πf0` 和由它计算的离散系数；同时检查参考信号生成、PLL/频率估计、采样频率、滤波器和调参结果。不能只改一个宏后假设整个系统仍然稳定。

---

# 总结

QPR 的本质不是“比 PI 更高级的积分器”，而是把控制器的高增益从直流频率搬到指定的交流频率附近。它特别适合在静止坐标系中控制正弦电压或电流，减少了 Park 变换和部分解耦工作；但它要求目标频率、采样时序、离散系数、输出限幅和抗饱和策略都被认真处理。

在 STM32 三相逆变器中，可以把完整链路记成：

```text
ADC采样
→ 单位换算/偏置校准
→ Clarke变换
→ 正弦参考减反馈
→ QPRα/QPRβ
→ Vdc归一化
→ 反Clarke
→ 调制比限幅
→ 占空比
→ CCR计数值
→ 定时器PWM/互补PWM
→ 逆变桥与滤波器
→ 再次采样反馈
```

真正调试时，优先保证“方向正确、相序正确、采样正确、保护有效”，再追求 `Kr`、`ωc` 和 THD 的优化。任何主机公式通过都不能替代低压上板、示波器观察和硬件保护验证。

## 参考资料

1. [Imperix — Proportional resonant controller](https://imperix.com/doc/implementation/proportional-resonant-controller)：QPR/PR 的连续域公式、Tustin 离散系数和调参含义。
2. [Texas Instruments — Grid Connected Inverter Reference Design](https://www.ti.com/lit/ug/tidub21d/tidub21d.pdf)：并网逆变器中 PI 对直流误差和 PR 对交流频率误差的应用说明，并包含 PWM、采样、保护和频域验证背景。
3. [STMicroelectronics — General-purpose timer modes, AN4013](https://www.st.com/resource/en/application_note/dm00042534-gpio-operation-in-stm32-microcontroller-family.pdf)：STM32 定时器 PWM、ARR/CCR、预装载和边沿/中心对齐配置边界。具体型号仍应以对应 Reference Manual 为准。
