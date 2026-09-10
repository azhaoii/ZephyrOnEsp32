# 05_PWM_LED 学习笔记：PWM 驱动 LED 呼吸灯

## 0. 项目总览

- 硬件：ESP32-S3 N16R8 定制板，自定义板 `boards/xtensa/esp32_s3_board/`
- 外设：IO1 外接红色 LED（低电平有效，外接 330Ω 限流电阻至 3.3V）
- 软件：Zephyr v4.4.99，使用 LEDC 外设（`espressif,esp32-ledc` 驱动）
- 目标：PWM 输出 5kHz 方波，软件以"三角波包络"调制占空比，实现呼吸灯
- 效果：占空比 0%→100%→0% 循环，周期 2.5s，LED 从灭渐渐变亮、再渐渐变暗

```
硬件连线（低电平有效接法）：

 3.3V ──LED(阳极→阴极)──[330Ω]── IO1 ──┐
                                        └── GPIO 输出 PWM，拉低时 LED 亮
```

> 注意：DHT11 实验（04 笔记）用的也是 GPIO1（`dio-gpios = <&gpio0 1 ...>`）。
> 同一个物理引脚不能接两个外设，DHT11 与 LED 分时实验，别同时接。

---

## 1. 什么是 PWM（基础概念）

PWM（Pulse Width Modulation，脉宽调制）：快速切换电源开关来控制模拟量输出。

一个 PWM 信号有三个关键量（都是"时间"，单位纳秒 ns）：

```
                 ┌────── 周期 period = 200000ns (200µs)
                 │                      频率 = 1/period = 5000Hz = 5kHz
   3.3V ▁▄▄▅▅▇▆▄▃▂▁▂▃▄▅▆█▆▅▄▃▂▁▂▃▄▅▆▇▅▄▃▂▁▂▃▄▅
    0V         └── 脉宽 pulse：这个周期里高电平持续的时间

   占空比 duty = pulse / period × 100%
```

| 名词 | 含义 | 本实验取值 |
|---|---|---|
| period（周期） | 一整个方波（高+低）的时间 | 200000ns（5kHz），**固定不变** |
| pulse（脉宽） | 一个周期内高电平的持续时间 | 0~200000ns 之间变化，**呼吸时动态改变** |
| 占空比 duty | pulse / period 的百分比 | 0%~100% |
| amplitude（幅值） | 电压高低（3.3V/0V） | **始终不变**（所以 pulse 不是"幅值"！） |

### 为什么 LED 亮度能"平滑"？

人眼感知亮度存在"视觉暂留"：大约 25~50ms 内会积分平均亮度。
- PWM 开关频率 5kHz 远高于人眼的积分频率 → 人眼看不到闪烁
- 平均电流 ∝ 占空比 → 占空比 50% 就是"半亮"，80% 就是"较亮"

所以：**改变占空比 = 改变亮度**。0% 灭、100% 全亮、中间值就是中间亮度。

---

## 2. 呼吸灯原理：三角波包络

### 2.1 两个时间尺度（最容易混淆的地方）

PWM 本身是方波，数字世界里"方波"一词容易撞车，必须分清两层：

```
第 1 层：载波 carrier   ▁▇▁▇▁▇▁▇▁▇  5kHz 方波，某一瞬间占空比是定值
                                  ↑ 负责"把亮度切成某一档"
第 2 层：占空比包络     ▄▅▆▇▆▅▄▃▂▁  占空比随时间变化的曲线（慢，~0.4Hz）
                                  ↑ 负责"这一档怎么来回走"
```

- **PWM 信号 = 5kHz 方波**：任何时刻都在 5kHz 闪，占空比这是定值 → 正确
- **呼吸 = 占空比被"逐渐"调节**：动态改占空比（50%→70%→90%…）→ 也是正确的
- 关键在"**逐渐**"两个字：占空比只取 0%/100% 两个值来回切 → 那是"闪烁"（blinky），不是呼吸

| 占空比随时间变化的方式 | 名称 | 效果 |
|---|---|---|
| 0% ↔ 100% 瞬间跳变（只有两档） | 方波包络 | 闪烁（blinky） |
| 0%→100%→0% **逐渐**走过每档 | 三角波包络 | 呼吸（fade_led） |
| 0%→100%→0% 非线性渐变（正弦） | 正弦包络 | 更接近真实生物呼吸 |

### 2.2 为什么官方用三角波而不是"方波"？

- 呼吸的本质是"亮度连续变化"，三角波能遍历所有中间亮度等级
- 方波只有两个等级，亮度在灭/全亮之间瞬间跳变，那就是 01_blinky 干的事
- 三角波用加减法就能实现，不需要 `math.h`，代码小、易移植
- 人眼对亮度感知是对数关系（韦伯-费希纳定律），线性三角波看起来"先快后慢"，但效果已足够像

### 2.3 "占空比渐变"的官方实现（三角波逻辑）

50 个台阶 + 方向标志位，就是这个"逐渐"：

```c
// 状态：pulse 当前脉宽, step 一步的增量, dir 方向旗(1=上升/0=下降)
pulse = 0;  step = period / 50;  dir = 1;

while (1) {
    pwm_set_pulse_dt(&spec, pulse);       // 先把当前亮度写给硬件
    if (dir == 1) {                        // 上升段
        if (pulse + step >= period) {      // 到顶
            pulse = period;  dir = 0;      // 钳位 100% + 翻方向
        } else {
            pulse += step;                 // 前进一格
        }
    } else {                               // 下降段
        if (pulse <= step) {               // 到底
            pulse = 0;     dir = 1;        // 钳位 0% + 翻方向
        } else {
            pulse -= step;                 // 后退一格
        }
    }
    k_msleep(25);                          // 时间心跳：每步等 25ms
}
```

时间核算：`50 步 × 25ms × 2 方向 = 2.5s` 一个完整呼吸循环，接近人类呼吸频率。

---

## 3. 呼吸灯波形图（可打印）

### 3.1 宏观：占空比(亮度)包络

```
呼吸灯原理（三角波包络调制 PWM 占空比）

占空比
 100% ┆▆▇█▇▆▅▃▂▃▄▅▆▇▇▇▇▆▅▃▂▃▄▅▆▇▇▆▅▃▂▃▄▅▆▇▆▅▃▂
  50% ┆▄▅▆▇▆▄▃▂▁▂▃▄▅▅▅▅▃▂▁▂▃▄▅▅▃▂▁▂▃▄▅▅▃▂▁▂▃
   0% ┆▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁
      └──────────────┘
      渐亮 1.25s    渐暗 1.25s
      └─────── 周期 2.5s ───────┘ → 循环

100% 时 LED 最亮；0% 时 LED 灭
```

### 3.2 微观：载波方波（pulse 被逐档加宽）

```
   5kHz 载波：period 固定 200µs，pulse 慢慢变宽再变窄

   占空比 20%   ▁▁▁▁▄▁▁▁▁▄▁▁▁▁▄▁▁▁▁▄
   占空比 50%   ▁▁▄▄▁▁▄▄▁▁▄▄▁▁▄▄▁▁▄▄
   占空比 80%   ▁▄▄▄▄▁▄▄▄▄▁▄▄▄▄▁▄▄▄▄
              |←——— 周期不变 ——→|
              ↑ pulse 变宽 = LED 更亮
```

一句话总结：**呼吸 = pulse 在 0~period 之间往返，载波周期固定不变。**

---

## 4. 硬件补充：LED 极性 + 限流电阻计算

### 4.1 什么是"低电平有效"？

LED 是二极管，电流从阳极流向阴极才亮：

- 高电平有效：`IO → 电阻 → LED阳极` → 阴极 GND，IO 输出高时亮
- 低电平有效：`3.3V → LED阳极 → 电阻 → IO`，IO 拉低时形成电流 → 亮

本实验采用低电平有效（灯负极接 IO），IO 悬空/输出高时两端都是 3.3V 左右，无电流通路、无待机功耗；IO 拉低时亮。

### 4.2 限流电阻为什么是 330Ω？（欧姆定律）

```
R = (3.3V - LED压降Vf - IO低电平≈0.15V) / 目标电流
```

| 阻值 | 红色 LED（Vf≈2.0V）| 蓝色/白色 LED（Vf≈3.0V）|
|---|---|---|
| 100Ω | ≈11mA | ≈1.9mA |
| 220Ω | ≈5mA | ≈0.8mA |
| 330Ω | ≈3.5mA | ≈0.5mA |
| 470Ω | ≈2.5mA | ≈0.3mA |

结论：

- 红色 LED → **330Ω**（3.5mA，亮度/安全平衡点），范围 220~470Ω
- 蓝色/白色 LED → 低阻值（100~150Ω）
- 不要低于 100Ω——ESP32-S3 单个 IO 安全电流约 20mA（建议 ≤10mA）
- 高阻值（1kΩ+）会让蓝白 LED 过暗

> 这个电阻同时完成"上拉"：LED 作为上拉通路的一部分。想强制暗态也可并联小电路，但本实验不需要另一个单独的上拉电阻。

---

## 5. ESP32-S3 的 PWM 硬件：LEDC 外设

### 5.1 通道与定时器（问答：ESP32-S3 有 PWM 通道吗？）

| 外设 | 数量 | 用途 |
|---|---|---|
| LEDC（LED Control） | 8 通道（ch0~7）+ 4 定时器（timer0~3） | 调光、发声、通用 PWM，**本实验用这个** |
| MCPWM | 2 组 × 3 路 | 电机控制，也可当 PWM 用 |

特性：

- 源时钟 APB 80MHz / REF_TICK，PWM 频率最高约 1MHz
- 分辨率 14bit（占空比计数值 0~16383）
- **通道与物理引脚无固定对应**：LEDC 信号经 GPIO 矩阵可路由到任意 IO（见第 6 节）
- ESP32-S3 只有 8 个通道，**没有**原版 ESP32 的高速/低速（16 路）之分

### 5.2 LEDC 工作模型

```
LEDC 通道0（计数器+比较器，纯寄存器功能）
    │ 输出信号 ESP_LEDC_LS_SIG_OUT0
    ▼
 GPIO 矩阵（pinctrl 驱动的职责：把信号接到哪根引脚）
    │
    ▼
 IO1 物理引脚 → LED
```

关键理解：设备树里的 `channel0@0 { reg = 0; }` 只是"用哪个比较器"，而"这个比较器的输出接到哪个 GPIO"由 pinctrl 宏决定——两者解耦，这正是 ESP32 与众不同的地方。

---

## 6. 为什么需要配置 pinctrl（问答讲解）

### 6.1 为什么要有 `esp32_s3_board-pinctrl.dtsi` 这个文件？

对比不同芯片的设计：

- nRF/NXP：通道能输出到哪些脚，芯片手册固定死
- STM32：定时器通道有固定的可复用引脚表，DTS 里选一组
- **ESP32：任意外设信号可路由到任意 IO（GPIO 矩阵）,出厂无默认接线**

所以 ESP32 的设备树必须"声明"LEDC 通道 0 的信号接到了哪根引脚。如果不配置，LEDC 驱动初始化时没有可应用的引脚状态，输出信号悬空，IO1 就是普通 IO，不会有 PWM。

### 6.2 pinctrl 文件的本质

`<board>-pinctrl.dtsi` 里集中存放**这个板子上所有"引脚状态包"**（uart 的、i2c 的、ledc 的……），主 `.dts` 用一行 `#include` 引入，保持文件整洁——这是 Zephyr 板级文件约定（官方 esp32s3_devkitc 也是同样结构）。

每个状态包形如：

```dtsi
&pinctrl {
    ledc0_default: ledc0_default {
        group1 {
            pinmux = <LEDC_CH0_GPIO11>;   /* 宏：LEDC 通道0信号 → GPIO11 */
            output-enable;
        };
    };
};
```

`LEDC_CH0_GPIO11` 宏展开后是 `ESP32_PINMUX(11, ESP_NOSIG, ESP_LEDC_LS_SIG_OUT0)`——"把 LEDC 的 0 号输出信号接到 11 号引脚"。

### 6.3 改成 IO1 怎么办？

只需把 pinmux 换成 IO1 对应的宏：

```dtsi
pinmux = <LEDC_CH0_GPIO1>;   /* 通道0信号 → GPIO1 */
```

通道号、定时器不用动：**换引脚 = 只改 pinctrl 一行**，这也再次印证第 5.2 节的"通道/引脚解耦"。

---

## 7. 设备树（DTS）完整配置

### 7.1 本实验 dts 全貌

`boards/xtensa/esp32_s3_board/esp32_s3_board_procpu.dts`：

```dts
/dts-v1/;

#include <espressif/esp32s3/esp32s3_wroom_n16r8.dtsi>   /* N16 = 16MB flash, R8 = 8MB PSRAM */
#include <espressif/partitions_0x0_amp.dtsi>
#include <zephyr/dt-bindings/pwm/pwm.h>                  /* PWM_POLARITY_INVERTED 宏 */
#include "esp32_s3_board-pinctrl.dtsi"                   /* 引脚状态包 */

/ {
    pwmleds {
        compatible = "pwm-leds";

        pwm_led0: led_0 {
            label = "extend_pwm_led0";
            /* <&ledc0 通道0 周期200000ns=5kHz 极性取反(低电平有效)> */
            pwms = <&ledc0 0 200000 PWM_POLARITY_INVERTED>;
        };
    };

    aliases {
        pwm-led0 = &pwm_led0;
    };

    chosen {
        zephyr,sram = &sram1;
        zephyr,flash = &flash0;
        zephyr,console = &uart0;
    };
};

&ledc0 {
    status = "okay";
    pinctrl-0 = <&ledc0_default>;
    pinctrl-names = "default";
    #address-cells = <1>;
    #size-cells = <0>;
    channel0@0 {
        reg = <0x0>;     /* 通道号 0，必须与 pinctrl 里的 CH0 一致 */
        timer = <0>;     /* 使用定时器0 */
    };
};
```

### 7.2 逐块讲解

| 部分 | 作用 |
|---|---|
| `pwms = <&ledc0 0 200000 INVERTED>` | 3 个 cell：**通道号、周期(ns)、极性**。200000ns = 1/200000 = 5kHz |
| `pwm-led0 = &pwm_led0` | 别名：让 C 代码可以用 `DT_ALIAS(pwm_led0)` 找到这个 LED |
| `chosen` 节点 | 告诉 Zephyr 用哪块内存/哪个 flash/哪个串口，**绝不能删**（见 9.3 踩坑） |
| `&ledc0 { status="okay" }` | LEDC 在 SoC dtsi 里默认 disabled，必须使能 |
| `pinctrl-0 = <&ledc0_default>` | 绑定第 6 节那个引脚状态包 |
| `channel0@0 { reg; timer; }` | 激活 LEDC 通道 0 并绑定定时器 0 |

### 7.3 Kconfig 配置 `prj.conf`

```conf
CONFIG_GPIO=y
CONFIG_PWM=y
CONFIG_PRINTK=y
CONFIG_LOG=y
CONFIG_PWM_LOG_LEVEL_DBG=y
```

要点：

- `CONFIG_PWM=y` 开启 PWM API（menuconfig，默认不开启）
- `PWM_LED_ESP32` 驱动由 devicetree 自动触发（`depends on DT_HAS_ESPRESSIF_ESP32_LEDC_ENABLED` + `default y`），无需手动写
- 注意符号名：**是 `PWM_LOG_LEVEL_DBG` 不是 `PWM_LOG_LEVEL_DEBUG`**！`_DEBUG` 不存在，写成它会在 Kconfig 阶段直接报错
- ESP32 上不需要 `CONFIG_STDOUT_CONSOLE=y`（它是 Zephyr console 驱动体系，ESP32 走自己的 UART 控制台，`printk` 直接用）

---

## 8. 软件代码：PWM API 与呼吸主循环

### 8.1 核心 API 速查（都在 `<zephyr/drivers/pwm.h>`）

```c
/* 从 devicetree 编译期生成的一个"LED 打包信息"结构体 */
struct pwm_dt_spec {
    const struct device *dev;     /* ledc0 设备 */
    uint8_t channel;              /* 通道=0 */
    uint32_t period;              /* 周期=200000ns */
    pwm_flags_t flags;            /* PWM_POLARITY_INVERTED */
};

/* 三种常用操作：第一个参数一律传 &你的spec（取地址） */
pwm_is_ready_dt(&spec)                     // 驱动就绪?
pwm_set_dt(&spec, period, pulse, flags)    // 周期+脉宽+极性全给
pwm_set_pulse_dt(&spec, pulse)             // 只给脉宽；周期/极性从 spec 自动取 ★呼吸用这个最顺手
```

### 8.2 `&spec` 是什么？（问答）

`&` 是取地址运算符。API 参数类型是 `struct pwm_dt_spec *`（指针），所以必须传 `&pwm_led0`——传 4 字节地址即可，函数不必复制整个结构体。

### 8.3 最终 `main.c`（英文注释，简单词）

```c
/*
 * ESP32-S3 + Zephyr PWM breathing LED
 * Idea: LEDC makes a 5kHz square wave (carrier).
 *       Software moves pulse (width in ns) from 0 to period,
 *       then back. So the envelope is a triangle wave = "breath".
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>

/* Get the LED info from dts alias "pwm-led0".
 * The info has: dev=ledc0, channel=0, period=200000ns(5kHz),
 *               flags=PWM_POLARITY_INVERTED */
static const struct pwm_dt_spec pwm_led0 = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led0));

#define NUM_STEPS  50U    /* steps in one way (up or down): 50 = 2% each */
#define SLEEP_MSEC 25U    /* wait 25ms after each step:
                           * 50*25ms*2 ways = 2.5s one full breath */

int main(void)
{
	uint32_t pulse = 0U;                          /* current pulse width (ns) */
	uint32_t step = pwm_led0.period / NUM_STEPS;  /* 200000/50 = 4000ns */
	bool up = true;                               /* true = going up, false = down */
	int ret;

	printk("PWM breathing LED start\r\n");

	if (!pwm_is_ready_dt(&pwm_led0)) {
		printk("Error: PWM device %s is not ready\n", pwm_led0.dev->name);
		return 0;
	}

	while (1) {
		/* 1. Write the current level to hardware.
		 * Period and polarity come from spec, we only give pulse. */
		ret = pwm_set_pulse_dt(&pwm_led0, pulse);
		if (ret != 0) {
			printk("Error %d: failed to set pulse width\n", ret);
		}

		/* 2. Move one step: going up */
		if (up) {
			if (pulse + step >= pwm_led0.period) {
				pulse = pwm_led0.period;   /* hit top: clamp 100% */
				up = false;                /* change direction */
			} else {
				pulse += step;             /* else step forward */
			}
		} else {
		/* 3. Move one step: going down */
			if (pulse <= step) {
				pulse = 0U;                /* hit bottom: clamp 0% */
				up = true;                 /* change direction */
			} else {
				pulse -= step;             /* else step back */
			}
		}

		/* 4. Heartbeat: sleep 25ms, wake up back to step 1 */
		k_msleep(SLEEP_MSEC);
	}
	return 0;
}
```

### 8.4 代码里每个变量的含义（谨记，答错率最高）

```c
uint32_t pulse;    // 当前脉宽(ns)：0=灭, 200000=最亮 → "0~200000 之间的 50 档"
uint32_t step;     // 每步增量 = 4000ns = 200000/50 = 亮度每格 2%
bool     up;       // 方向记忆：true 往亮走, false 往暗走
int      ret;      // pwm_set 返回值，!=0 即出错
```

### 8.5 官方 fade_led 样例逐块对照（学习源码用）

官方路径：`samples/basic/fade_led/src/main.c`

| 块 | 讲解 |
|---|---|
| `PWM_LED_ALIAS(i)` 三行宏 | `DT_ALIAS(_CONCAT(pwm_led, i))` 拼接出 pwm-led0…pwm-led9，`IF_ENABLED` 只收集存在的 LED——编译期"数 LED"魔法 |
| `pwm_leds[]` 数组 + LISTIFY | 每个元素是一个 `struct pwm_dt_spec`，支持 1~10 颗 LED 通用 |
| `steps[i] = period/50` | 50 步 × 各 2% 亮度 |
| 状态数组 | `pulse_widths/dirs`：三角波是"位置+方向"，程序无记忆必须自存 |
| `pwm_set_pulse_dt` | 只传脉宽；周期和极性从 spec 自动取——占空比变化时载波周期不变 |
| 边界判断 | `>=` / `<=` 保证恰在 100%/0% 翻转，无毛刺 |
| `k_sleep(K_MSEC(25))` | 每步 25ms 时间心跳，睡眠让出 CPU（节能，不空转） |

我们只做单 LED，宏魔法（LISTIFY/IF_ENABLED）省略，核心逻辑一字未变。

### 8.6 调参：逻辑分析仪观察（调试技巧）

观察时建议放慢节奏（每档在 LA 上多停一会儿）：

```c
#define NUM_STEPS  10U     /* 每格 10% 亮度，10 个台阶更清晰 */
#define SLEEP_MSEC 200U    /* 每档 200ms → 一整轮 4s */
```

或保留 50 档但拉长停留：`SLEEP_MSEC = 250`（一轮 25s）。
想看得更微观：把 dts 的周期改 `1000000`（1kHz），采样率要求更低。

LA 观察要点：**单通道接 IO1、下降沿触发**。“占空比一档一档变宽再变窄、period 固定不变”——这就是 pulse/period 各司其职的直观证据。

---

## 9. 编译与烧录全流程 + 今日踩坑合集

### 9.1 编译命令

```
west build -p always -b esp32_s3_board/esp32s3/procpu
west flash
```

`-p always` 每次全量重新构建 DTS/Kconfig（改设备树、配置后必用）。

### 9.2 一次成功构建的各阶段（顺序很关键）

```
1. 解析 DTS（device tree）→ 生成 zephyr.dts / devicetree_generated.h
2. Kconfig 合并 board defconfig + prj.conf → .config/autoconf.h
3. CMake 配置（量 flash 大小、找工具链/SDK）
4. 编译源文件（CC）→ 链接（link）→ zephyr.elf
5. esptool 打包 ESP32 镜像（.bin）
```

每一阶段报错都有各自的典型原因，见下表。

### 9.3 今日报错全档案（按出现顺序）

| # | 报错 | 根因 | 修复 |
|---|---|---|---|
| 1 | `devicetree error: undefined node label 'pwm1'` | 写成了 `&pwm1`——ESP32 的 PWM 控制器节点标签是 **`ledc0`**（`esp32s3_common.dtsi` 里 `ledc0: ledc@60019000`）；`pwm0/pwm1` 是 nRF 等芯片的叫法 | `pwms = <&ledc0 0 200000 PWM_POLARITY_INVERTED>`；同时补上 ledc0 的 `status/pinctrl/channel` |
| 2 | `STDOUT_CONSOLE assigned 'y' but got 'n'` + `PWM_LOG_LEVEL_DEBUG is undefined` + `Aborting due to Kconfig warnings` | ① ESP32 控制台不依赖 Zephyr console 驱动，`CONFIG_STDOUT_CONSOLE=y` 依赖不满足；② 正确符号是 `PWM_LOG_LEVEL_DBG`，`_DEBUG` 不存在 | 删 `CONFIG_STDOUT_CONSOLE`；`_DEBUG`→`_DBG` |
| 3 | `dt_reg_size(flash_size_bytes ...) missing required argument: PATH` | CMake 要读 `chosen` 里的 `zephyr,flash` 节点量 flash 大小，但重写 dts 时**把 chosen 节点删了** | 补回 `chosen { zephyr,sram / zephyr,flash / zephyr,console }`——chosen 是 Zephyr 的"入口坐标"，不能丢 |
| 4 | `warning: %lu expects long unsigned int but arg2 is uint32_t` | `%lu` 配 `uint32_t` 类型不符（uint32_t 用 `%u`） | 改为 `%u` 或打印用 `PRIu32` |
| 5 | `warning: unused variable 'ret'` | 重写后 `ret` 没用上 | 删除声明（呼吸版里 ret 有用途了） |

### 9.4 好习惯 CheckList

- 改 DTS / prj.conf 后：`west build -p always`（不清缓存会吃到旧设备树，见过太多"我明明改了怎么还错"）
- 阶段化定位：先确认 DTS 解析过 → 再 Kconfig → 再 CMake → 再看编译 warning
- 报错信息永远读**第一行**（`devicetree error:` / `warning: ... undefined symbol` 等），下面的 Call Stack 只是栈回溯
- LED 不亮排查顺序（软件→硬件）：`status="okay"` ✓ → pinctrl 宏与物理引脚一致 ✓ → channel reg 与宏的 CHn 一致 ✓ → 极性 ✓（低有效 LED 必须 `PWM_POLARITY_INVERTED`）→ 万用表查电路
- 编译警告即使能过也要修：类型不匹配属于未定义行为，换库/换平台可能炸

---

## 10. 学习自查（今日问题清单）

你能不看资料回答以下问题，说明今天内容全部掌握：

1. ESP32-S3 的 PWM 有几个通道几个定时器？和原版 ESP32 区别？
2. 为什么 ESP32 需要 pinctrl 配置，而 nRF 不需要？
3. `pwms = <&ledc0 0 200000 PWM_POLARITY_INVERTED>` 三个数字分别是什么？为什么极性要 INVERTED？
4. `pwm1` 为什么不存在？正确的标签是什么？
5. `chosen` 节点有什么用？删掉会怎样？
6. `pulse` 和 amplitude 的区别？`period` 和 `pulse` 谁在变？
7. "载波"和"包络"分别指什么？为什么方波包络是闪烁、三角波包络是呼吸？
8. 呼吸周期 2.5s 是怎么算出来的？（50 步 × 25ms × 2 方向）
9. `pwm_set_dt` 和 `pwm_set_pulse_dt` 的差别？
10. 为什么红色 LED 用 330Ω？（欧姆定律过程）
11. `&pwm_led0` 为什么必须加 `&`？
12. 逻辑分析仪上应该看到什么波形？

---

## 11. 参考资料

- Zephyr 官方样例：`samples/basic/fade_led/`（呼吸灯标准答案）
- 官方文档 PWM API：`https://docs.zephyrproject.org/latest/hardware/peripherals/pwm.html`
- LEDC 驱动绑定说明：`dts/bindings/pwm/espressif,esp32-ledc.yaml`
- 引脚宏定义：`include/zephyr/dt-bindings/pinctrl/esp32s3-pinctrl.h`
- 环境搭建回忆：`note/00_EnvSetting.md`；DHT 调试（同类思维）：`note/04_DHT11_Debug.md`
