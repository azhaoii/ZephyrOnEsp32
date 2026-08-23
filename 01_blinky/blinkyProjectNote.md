# Zephyr 4.4 自定义 ESP32-S3 板卡 blinky 点灯完整记录

> 本文档记录在 ESP32-S3 自研板（核心板 + 底板）上，从零搭建 Zephyr HWMv2 板级支持、编译、烧录、排查 LED 不亮问题直到点灯成功的全过程。
>
> 日期：2026-08-18

---

## 目录

1. [项目概览](#1-项目概览)
2. [硬件资料（来自原理图）](#2-硬件资料来自原理图)
3. [板级定义（HWMv2 规则）](#3-板级定义hwmv2-规则)
4. [构建、烧录、串口](#4-构建烧录串口)
5. [排障实录](#5-排障实录)
6. [结论与最终成果](#6-结论与最终成果)
7. [经验教训清单](#7-经验教训清单)

---

## 1. 项目概览

| 项目 | 内容 |
|---|---|
| 目标 | 在自研 ESP32-S3 板上跑通 Zephyr blinky 点灯程序 |
| 板卡 | ESP32-S3 核心板（ESP32-S3-WROOM N8R8 类模组，8MB Flash + 8MB PSRAM）+ 底板（电源 / LCD / 排针） |
| 芯片识别 | ESP32-S3 (QFN56) rev v0.2，240MHz，Wi-Fi / BT 5 (LE)，Dual Core + LP Core，Embedded PSRAM 8MB (AP_3v3)，晶振 40MHz |
| Zephyr | v4.4.99（main 分支，HWMv2 架构）位于 `~/01_project/zephyrproject/zephyr` |
| SDK | `~/01_project/zephyr-sdk-1.0.1`（工具链 `xtensa-espressif_esp32s3_zephyr-elf`） |
| 主机 | Linux 虚拟机（mason@masonVirtual），Python venv 位于 `~/01_project/zephyrproject/.venv` |
| 应用 | `~/01_project/zephyrproject/HelloLED/blinky`（blinky 样例，改过 led0 引脚） |
| 板级目录 | `~/01_project/zephyrproject/HelloLED/blinky/boards/xtensa/esp32_s3_board/`（应用内 BOARD_ROOT） |
| 构建目标 | `esp32_s3_board/esp32s3/procpu`（双核芯片需限定处理器） |

---

## 2. 硬件资料（来自原理图）

原理图文件（教程资料包 `1 原理图` 目录）：

- `ESP32-S3底板原理图.pdf`：电源（DC005 → CJ7805 5V → AMS1117-3.3）、LCD 接口（LCD_SCL/LCD_DSA/LCD_RST/LCD_DC/LCD_CS/LCD_BL）、排针引出（IO4-18、IO3、IO46、RESET 等）
- `ESP32-S3核心板原理图.pdf`：模组、CH340C、双 Type-C、LED、SD 卡座、按键

### 2.1 核心板关键器件

| 器件 | 说明 |
|---|---|
| U1 | ESP32-S3 模组（41 脚，引脚定义见下文） |
| U3 | CH340C USB 转串口芯片，接 **U0_TXD/U0_RXD（IO43/IO44）** |
| LED1 | 用户灯，接 **U0_TXD（IO43）**，`VCC3.3 → LED1 → R4(1.5K) → U0_TXD`，**低电平点亮** |
| PWR1 | 电源指示灯（与 GPIO 无关，勿混淆） |
| R4/R5 | 1.5K 限流电阻（分别对应 LED1 / PWR1） |
| TF_CARD | 板载 MicroSD 卡座（SPI 接口） |
| BOOT / RST | 按键 |

### 2.2 双 USB 口（本项目最关键的硬件知识点！）

核心板有两个 Type-C 口，功能完全不同：

| 接口 | 芯片通路 | 设备节点 | 用途 |
|---|---|---|---|
| **USB 口** | ESP32-S3 原生 USB-Serial/JTAG（IO19/IO20） | `/dev/ttyACM0` | **烧录下载**（esptool 用） |
| **USART 口** | CH340C → U0_TXD/U0_RXD（IO43/IO44） | `/dev/ttyUSB0` | **串口监视**（看 app 打印） |

> ⚠️ 这就是"烧录成功却看不到串口输出"的根因：`zephyr,console = &uart0` 的打印在 **USART 口**（IO43/44），而插的是 **USB 口**（IO19/20）——两个口互不相通。
> 烧录必须用 USB 口（esptool 需要下载模式），看日志换到 USART 口。

### 2.3 模组 U1 引脚定义（41 脚）

```
1:GND  2:3V3  3:EN  4:IO44  5:IO5  6:IO6  7:IO7  8:IO15  9:IO16  10:IO17
11:IO18  12:IO8  13:IO19  14:IO20  15:IO3  16:IO46  17:IO9  18:IO10  19:IO11  20:IO12
21:IO13  22:IO14  23:IO21  24:IO47  25:IO48  26:IO45  27:IO0  28:IO35  29:IO36  30:IO37
31:IO38  32:IO39  33:IO40  34:IO41  35:IO42  36:RXD0(=IO44)  37:TXD0(=IO43)  38:IO2  39:IO1  40:GND  41:GND
```

### 2.4 板载 LED 定位过程（最终结论）

- 官方教程例程（`4_Program/1_LED`）用的是 `GPIO_NUM_38` —— **与实测不符，是教程笔误**
- 原理图文本提取无法直接得到 LED 网络（连线为图形）
- **万用表蜂鸣档实测**：灯的一端只与丝印带 **Tx** 的引脚（TXD0 = IO43）导通
- 结论：**LED1 = IO43，低电平点亮**

> "按 reset 灯亮一下然后熄灭"的机理：复位瞬间 ROM bootloader 通过 U0_TXD 输出启动日志（驱动 IO43），灯亮一下；固件接管后 uart0 未启用、IO43 回到高阻（被 LED+电阻上拉到 3.3V），灯灭。

---

## 3. 板级定义（HWMv2 规则）

### 3.1 目录结构（应用内 BOARD_ROOT）

```
blinky/
├── boards/xtensa/esp32_s3_board/
│   ├── board.cmake                  # 引用 common esp32.board.cmake + openocd.board.cmake
│   ├── board.yml                    # 板卡元数据（缺失会导致 board not found！）
│   ├── esp32_s3_board_procpu.dts    # 板级 dts（必须带 _procpu 限定符后缀）
│   ├── esp32_s3_board_procpu_defconfig  # 配置片段（.conf 格式，只允许 CONFIG_*=y）
│   ├── esp32_s3_board-pinctrl.dtsi  # 引脚复用定义
│   ├── Kconfig                      # 板级 Kconfig（HEAP_MEM_POOL_ADD_SIZE_BOARD）
│   ├── Kconfig.defconfig            # 板级默认值（Kconfig 语言，可写 if/endif）
│   └── Kconfig.esp32_s3_board       # 板卡 Kconfig（SoC select）——注意是 Kconfig.<板名> 而非 Kconfig.board
├── CMakePresets.json                # BOARD="esp32_s3_board/esp32s3/procpu"
├── prj.conf                         # CONFIG_GPIO=y（blinky 自带）
└── src/main.c
```

### 3.2 HWMv2 命名与语法规则（踩坑总结）

| 规则 | 说明 |
|---|---|
| Kconfig 文件名 | 必须是 `Kconfig.<板名>`（如 `Kconfig.esp32_s3_board`），不是 `Kconfig.board` |
| BOARD_* 符号 | 由构建系统自动生成，**不要**手写 `bool`/`help`/`config BOARD_XXX` |
| defconfig 文件名 | 必须带限定符后缀：`esp32_s3_board_procpu_defconfig`；不带后缀会被**静默忽略**（extensions.cmake 按限定符查找） |
| defconfig 内容 | 只允许 `CONFIG_XXX=y` 行；**禁止 if/endif**（那是 `Kconfig.defconfig` 的语法），写了会报 `ignoring malformed line` 并 abort |
| 完整 target | 双核芯片必须用 `esp32_s3_board/esp32s3/procpu`（或 `/appcpu`），否则报 "Board qualifiers not found" |
| SoC select | `Kconfig.esp32_s3_board` 中：`select SOC_ESP32S3_PROCPU if BOARD_ESP32_S3_BOARD_ESP32S3_PROCPU` |

### 3.3 dts 关键点

```dts
/dts-v1/;

#include <espressif/esp32s3_wroom_n8r8.dtsi>   /* 必须用模组级 dtsi！通用 esp32s3.dtsi 缺 flash0 的 reg，会报 dtc unit_address_vs_reg */
#include "esp32_s3_board-pinctrl.dtsi"
#include <espressif/partitions_0x0_amp.dtsi>    /* 提供 boot_partition@0 / slot0_partition@20000 */

/ {
    aliases { led0 = &led0; };

    chosen {
        zephyr,sram = &sram1;                   /* 双核：procpu 用 sram1 */
        zephyr,flash = &flash0;
        zephyr,console = &uart0;                /* IO43/44，需 USART 口观察 */
        zephyr,code-partition = &slot0_partition;  /* 缺它报 dt_reg_addr(image_off) PATH 错误 */
    };

    leds {
        compatible = "gpio-leds";
        led0: led_0 {
            gpios = <&gpio1 11 GPIO_ACTIVE_LOW>;  /* IO43 = TXD0 = 板载 LED1，低电平点亮 */
        };
    };
};

&uart0 {                                        /* 需要串口打印时启用 */
    status = "okay";
    current-speed = <115200>;
    pinctrl-0 = <&uart0_default>;
    pinctrl-names = "default";
};
```

### 3.4 GPIO 映射与保留引脚（ESP32-S3）

| 映射 | 说明 |
|---|---|
| gpio0 | IO0~IO31，Zephyr 引脚号 = IO 号 |
| gpio1 | IO32~IO53，Zephyr 引脚号 = IO 号 − 32（例：IO38 → `<&gpio1 6>`，IO43 → `<&gpio1 11>`） |
| IO22~IO25 | **不存在**，别用 |
| IO0 / IO3 / IO45 / IO46 | strapping 引脚（上电状态影响启动模式） |
| IO19 / IO20 | USB-Serial/JTAG（D-/D+） |
| IO26~IO32 | 八线模式 Flash 占用（模组内部） |
| IO33~IO37 | 八线模式 PSRAM 占用（模组内部） |
| IO43 / IO44 | UART0 默认 TX/RX（本板 TXD0 上还挂了 LED1） |

> `GPIO_ACTIVE_HIGH` / `GPIO_ACTIVE_LOW` 是**标志位编码**（GPIO_DT_SPEC 解码用），不是电平本身；程序按 `led_state` 置位，驱动内部换算。

### 3.5 堆配置（解决 k_malloc 链接错误）

`intc_esp32.c` 报 `undefined reference to 'k_malloc'` 是因为 `CONFIG_HEAP_MEM_POOL_SIZE` 默认 0。

板级 `Kconfig`（裸 Kconfig 文件，不是 Kconfig.esp32_s3_board）：

```kconfig
config HEAP_MEM_POOL_ADD_SIZE_BOARD
    int
    default 4096 if BOARD_ESP32_S3_BOARD_ESP32S3_PROCPU
    default 256 if BOARD_ESP32_S3_BOARD_ESP32S3_APPCU
```

（参照官方 `boards/espressif/esp32s3_devkitc/Kconfig` 的官方模式；`kernel/Kconfig` 中 `HEAP_MEM_POOL_ADD_SIZE_*` 会被累加进 `HEAP_MEM_POOL_SIZE`。）

> 兜底方案：`prj.conf` 直接 `CONFIG_HEAP_MEM_POOL_SIZE=4096`。

---

## 4. 构建、烧录、串口

### 4.1 环境准备

```bash
# esptool（hal_espressif 要求 esptool>=5.3.0，缺失时构建报 "esptool not found"）
west packages pip --install

# 串口权限（/dev/ttyACM0 Permission denied）
sudo usermod -a -G dialout $USER     # 然后注销重登或 newgrp dialout
groups                                # 确认包含 dialout
```

### 4.2 构建命令

```bash
cmake --preset zephyr-debug            # 配置（-B cmake-build-debug-zephyr）
cmake --build --preset zephyr-debug-build
```

**什么时候需要手动重新 configure？**

| 改动对象 | 是否需要手动 `cmake --preset zephyr-debug` |
|---|---|
| dts / pinctrl dtsi（含传递 include） | ❌ 自动重配（dts.cmake 注册了 CMAKE_CONFIGURE_DEPENDS） |
| prj.conf / *_defconfig | ❌ 自动重配 |
| **Kconfig 定义类文件（新建/修改板级 Kconfig）** | ✅ **必须手动**（Kconfig 树在 configure 阶段生成，ninja 检测不到新增） |

### 4.3 烧录

```bash
west flash -d cmake-build-debug-zephyr --esp-device /dev/ttyACM0
```

- 板子识别为 `/dev/ttyACM0`（原生 USB-Serial/JTAG）
- runner 是 esp32（esptool），`--esp-idf-path` 由 `boards/common/esp32.board.cmake` 自动提供
- 支持 `--esp-device=COMx`/`ESPTOOL_PORT` 环境变量；默认参数（921600bps、DIO、80MHz、8MB）即可

### 4.4 串口监视

```bash
source .venv/bin/activate
python -m serial.tools.miniterm /dev/ttyUSB0 115200    # USART 口（CH340C → IO43/44）！
```

要点：

- **esp32 runner 不支持 `monitor` 命令**（`RunnerCaps(commands={'flash'})`，`--esp-monitor-baud` 是死参数），`west build -t monitor` 不可用
- 这个 Zephyr 版本**没有 USB-Serial/JTAG 驱动**（SoC dtsi 有 `usb_serial` 节点但 drivers 树无绑定），原生 USB 口只能看 ROM bootloader 启动日志
- app 的 `LED state: ON/OFF` 打印走 uart0（IO43/44）→ **必须插 USART 口**

---

## 5. 排障实录

### 5.1 构建链错误（按出现顺序）

| # | 错误 | 原因 | 解决 |
|---|---|---|---|
| 1 | `Board qualifiers esp32s3 not found` | target 没带处理器限定符 | 用 `esp32_s3_board/esp32s3/procpu`，文件加 `_procpu` 后缀 |
| 2 | `esptool not found` | hal_espressif 的 Python 依赖没装 | `west packages pip --install` |
| 3 | `stub.dts ... flash_size_bytes ... missing PATH` | 板 dts 文件名不对（没被识别） | 建 `esp32_s3_board_procpu.dts` |
| 4 | `board ... has been moved or deleted`（BOARD_DIR 显示在应用内） | `board.yml` 缺失/丢失 | 恢复 board.yml |
| 5 | `dtc: unit_address_vs_reg`（flash@0） | include 了通用 `esp32s3.dtsi`（flash0 无 reg） | 换模组级 `esp32s3_wroom_n8r8.dtsi` |
| 6 | `dt_reg_addr(image_off) missing PATH` | 分区表没进来 | 加 `#include <espressif/partitions_0x0_amp.dtsi>` + `zephyr,code-partition` |
| 7 | `undefined reference to 'k_malloc'`（intc_esp32.c） | 堆未开（默认 0） | 板级 Kconfig 定义 `HEAP_MEM_POOL_ADD_SIZE_BOARD`（4096/256）→ **手动重新 configure** |
| 8 | `ignoring malformed line 'if BOARD_ESP32_S3_BOARD' / 'endif'`（defconfig） | defconfig 里写了 if/endif（.conf 语法不支持） | 删掉，只留 `CONFIG_*=y` |

编译通过后的内存占用：FLASH 134948B / 8388352B（1.61%），成功生成 ESP32-S3 镜像（esptool v5.3.1）。

### 5.2 LED 不亮终极排查（本次最曲折的部分）

```
怀疑 IO1（模组丝印）→ 万用表证实 IO1 每秒翻转 → 灯不亮
→ 官方例程暗示 IO38 → 改 &gpio1 6 → IO38 每秒翻转 → 灯还是不亮
→ 按 reset 绿灯亮一下（关键线索：ROM 启动日志驱动）
→ 原理图确认有两个灯（PWR1 电源灯 / LED1 用户灯）
→ 万用表蜂鸣档实测：LED1 只与丝印带 Tx 的引脚导通
→ 结论：LED1 = TXD0 = IO43，最终 dts：gpios = <&gpio1 11 GPIO_ACTIVE_LOW>
```

关键经验：

1. **程序是否在跑，用万用表量引脚电压翻转即可判定**，与串口输出无关
2. 官方例程的引脚号不一定对（教程笔误），**以万用表实测为准**
3. "复位瞬间灯亮一下"是 ROM bootloader 行为的特征信号（U0_TXD 输出启动日志）
4. 板上有多个灯时先分清：电源指示灯（常亮，不受 GPIO 控制）vs 用户灯
5. 原理图 PDF 的文本提取无法还原图形连线（网络标号可能丢失），追线用万用表蜂鸣档最可靠

### 5.3 串口乱码排查

| 现象 | 结论 |
|---|---|
| miniterm 打开 `/dev/ttyACM0` 显示 `␀���␀␀␀...` | 是 ROM bootloader 启动日志残片，不是 app 输出 |
| 为什么看不到 `LED state` | console=uart0（IO43/44）在 USB 口（IO19/20）上永远不可见；需换 USART 口（CH340C） |

---

## 6. 结论与最终成果

### 6.1 最终配置

```dts
led0: led_0 {
    gpios = <&gpio1 11 GPIO_ACTIVE_LOW>;   /* IO43 = TXD0 = 板载 LED1，低电平点亮 */
};
```

### 6.2 验证结果

- 编译：`[236/236] Linking C executable zephyr/zephyr.elf`，esptool 镜像生成成功
- 烧录：`Wrote 134948 bytes at 0x00000000`，Hash verified，`Hard resetting via RTS pin`
- 运行：IO43 每秒 0↔3.3V 翻转（万用表确认），板载 LED 每秒闪烁

### 6.3 后续建议

- **串口打印**：启用 `&uart0`（IO43/44）+ `CONFIG_SERIAL/CONSOLE/UART_CONSOLE`，USART 口 miniterm 查看；注意 IO43 同时接了 LED，串口空闲时 IO43 保持高电平、灯灭，打印时 LED 会随波特率乱闪（该板设计权衡，功能互不影响）
- **双核**：appcpu 核的 defconfig/Kconfig 已预留（`_APPCU` 256 堆），后续可按 `esp32_s3_board/esp32s3/appcpu` 构建
- **深入学习**：按教程资料包 `4_Program` 顺序从 `1_LED`（ESP-IDF）对照学习 GPIO/PWM/ADC/IIC/SPI/WIFI 等外设

---

## 7. 经验教训清单

1. **HWMv2 一切以文件名和后缀为准**：`Kconfig.<板名>`、defconfig 带限定符、target 带处理器——少一个都会以迷惑的方式失败
2. **defconfig 是 .conf 格式**：只写 `CONFIG_*=y`，if/endif 属于 Kconfig.defconfig
3. **改 dts/prj.conf 自动重配，改 Kconfig 定义文件必须手动重配**——否则改了等于没改
4. **链接报 k_malloc/内存类错误，先查堆**：`HEAP_MEM_POOL_ADD_SIZE_BOARD` 或 `CONFIG_HEAP_MEM_POOL_SIZE`
5. **ESP32 的 GPIO 分两个端口**：IO≥32 时用 `&gpio1 (N-32)`，写错引脚号编译不报错、运行时无效果
6. **"程序在跑吗"用万用表判定**：量目标引脚是否按预期周期翻转，不依赖串口
7. **板子上的灯先分清角色**：电源灯（常亮）≠ 用户灯（GPIO 控制）
8. **复位瞬间灯亮一下 = ROM bootloader 动作的特征信号**（对 ESP32-S3 即 U0_TXD 启动日志）
9. **教程例程的引脚号要核实**：以原理图 + 万用表实测为准
10. **双 USB 口的板子**：烧录用原生 USB（ACM），日志走 CH340C/CP2102（ttyUSB）——两者通道独立
11. **原理图 PDF 自动化提取有极限**：图形连线信息提取不到，追网络用万用表蜂鸣档最稳
12. **遇到 Permission denied (/dev/ttyACM0)**：`sudo usermod -a -G dialout $USER` + 重登