# 04_DHT11 调试笔记

## 项目背景

- 硬件：ESP32-S3 N16R8 定制板（淘宝款），采用自定义板 `boards/xtensa/esp32_s3_board/`
- 软件：Zephyr v4.4.0，使用内置 DHT 驱动 `drivers/sensor/aosong/dht/dht.c`（`CONFIG_DHT=y`）
- 设备树：DHT11 挂在 `esp32_s3_board_procpu.dts`，`dio-gpios = <&gpio0 1 ...>`（GPIO1），别名 `dht0`
- 应用：`src/main.c` 用 sensor API 循环读取温湿度

## 故障①：Failed to fetch sample，逻辑分析仪看总线恒高

### 现象

`printk("Failed to fetch sample\n")`，LA 抓不到任何明显波形。

### 根因

DTS 极性配置写反：

```dts
/* 错误：active high */
dio-gpios = <&gpio0 1 (GPIO_ACTIVE_HIGH)>;
```

Zephyr 官方 binding `dts/bindings/sensor/aosong,dht.yaml` 明确说明：

> Control and data are encoded by the duration of **active low** signals.

驱动源码 dht.c:79-91 全部按**逻辑电平**操作：

```c
/* assert to send start signal */
gpio_pin_set_dt(&cfg->dio_gpio, true);   /* active = 物理低，维持18ms 才是起始信号 */
k_busy_wait(18000);
gpio_pin_set_dt(&cfg->dio_gpio, false);
gpio_pin_configure_dt(&cfg->dio_gpio, GPIO_INPUT);
dht_measure_signal_duration(dev, false); /* 轮询等传感器应答，100µs 超时 */
```

配成 `GPIO_ACTIVE_HIGH` 时：起始 18ms 被拉到物理高（与总线原本高电平重叠，LA 看不出），应答轮询逻辑全反，100µs 超时返回 `-EIO`。

### 修复

```dts
dio-gpios = <&gpio0 1 GPIO_ACTIVE_LOW>;
```

注：逻辑分析仪看不到活动，不一定只是代码问题——若 LA 恒高，也要排除：探头 GND 未共地、探针接错物理引脚、烧录的旧固件（增量构建/忘重烧）。改 DTS 后必须 `west build -p always` 再烧。

## 故障②：极性修好后仍报 -5

### 现象

fetch 仍失败（`-5 == -EIO`）。用 LA（**下降沿触发，≥1MSa/s，约30ms 窗口**）抓波得到结果：

| 波形段 | 含义 |
|---|---|
| ~250ns 低脉冲 | 复位瞬间引脚浮空，忽略 |
| 76.875µs 高 | init 后空闲（输出非有效 = 物理高） |
| 18ms 低 | 起始信号 ✅ 极性修复已生效 |
| 释放后一直接高 | **传感器未应答** ✗ |

此时确认：MCU 时序、引脚映射、极性全部正常，问题在传感器侧硬件。

### 万用表排查步骤

1. **通断档（断电）**：核对 DHT 的 DATA/VCC/GND 与 GPIO1 / 3V3 / GND 一一对应。注意 4 针裸件脚序 `VCC/DATA/NC/GND`，不同模块排布不同，接 NC 或接反 VCC/DATA 都会表现为总线恒高无应答
2. **断电量上拉**：DATA ↔ VCC 电阻应为 ~4.7k~10k；若无（OL）必须外接，**拉到 3.3V**（ESP32-S3 引脚不耐 5V）
3. **上电量供电**：在**传感器引脚上**量 VCC-GND ≈ 3.3V（量板端没用）

### 根因

上拉电阻错选为 **47Ω**：对 3.3V 拉得太强，DHT11 的 open-drain 引脚无法将其拉低（沉电流约 70mA），传感器应答被钳死 → 恒 EIO。换成 **4.7kΩ** 后通信恢复正常。

## 故障③：能显示温湿度，但首次 fetch 仍报 -5

### 现象

```
test failed: -5
Temp: 0.000000 °C, Humidity: 0.000000 %RH
Temp: 23.000000 °C, Humidity: 44.000000 %RH   ← 之后连续成功
```

### 原因

1. DHT11 上电后需 **>1s 预热**才响应，而 main 启动后几十微秒内就发起首次读取 → 应答超时
2. 驱动校验失败时不更新 sample 缓冲区（dht.c:161-167），只保留旧数据——所以"数值显示正常 + 报 -5"同时出现也不矛盾
3. `-5` 即 `-EIO`：两种触发源——100µs 状态超时（`DHT_SIGNAL_MAX_WAIT_DURATION=100µs`）或 40 位校验和错误

### 修复

```c
k_msleep(2000);          /* DHT11 上电预热，循环前 */
while (1) {
    int rc = sensor_sample_fetch(dht);
    if (rc < 0) {
        printk("fetch failed: %d\n", rc);  /* 打印 errno 便于定位 */
        k_msleep(2000);
        continue;
    }
    ...
}
```

## 经验 CheckList

- DHT 单总线协议：必须外部 4.7k~10k 上拉到 3.3V，驱动不使能内部上拉
- Zephyr dht 驱动要求 `dio-gpios` 配 **GPIO_ACTIVE_LOW**
- 时序敏感（26µs/70µs 区分0/1）：如运行中零星 -5，prj.conf 加 `CONFIG_DHT_LOCK_IRQS=y`（读取期间关中断）
- LA 抓单总线：下降沿触发、≥1MSa/s、先给传感器留预热时间；探头 GND 必须共地
- 首次读数失败：预热延时 + 失败重试是标配
- `sensor_sample_fetch` 返回码必须检查；校验失败时 `sensor_channel_get` 输出的是**旧值/0**，勿误判

## 最终运行日志（稳定）

```
I (spi_flash): flash io: dio
W (spi_flash): Detected size(16384k) larger than the size in the binary image header(8192k). Using the size in the binary hea.
*** Booting Zephyr OS build v4.4.0-12491-g3f6e4b89aa6f ***
Temp: 23.000000 °C, Humidity: 43.000000 %RH
Temp: 23.000000 °C, Humidity: 43.000000 %RH
Temp: 24.000000 °C, Humidity: 43.000000 %RH
Temp: 24.000000 °C, Humidity: 42.000000 %RH
```

注：`Detected size(16384k) larger than binary header(8192k)` 为无害警告（分区表按 8MB 划分，芯片 16MB），需大分区时再改 `partitions_0x0_amp.dtsi`。
