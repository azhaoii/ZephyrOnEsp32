# DeviceTree

## 1、zephyr使用DeviceTree描述硬件

使用DeviceTree描述硬件而不是使用硬代码写外设地址、引脚分配、中断号，这样能适配不同的板子和芯片。

当编译时，可以在编译日志中看到

>\-- Found BOARD.dts: .../boards/st/nucleo_h563zi/nucleo_h563zi.dts
>
>-- Generated zephyr.dts: .../build/zephyr/zephyr.dts

在编译结束后，在

> build/zephyr/zephyr.dts

可以看到类似

```c
uart0: serial@40004400 {
	compatible = "st,stm32-uart";
	reg = <0x40004400 0x400>;
	interrupts = <53 0>;
	status = "okay";
};

aliases {
    led0 = &green_led;
    sw0 = &user_button;
};
```

note：aliases是起别名，以便APP可以直接使用硬件节点

如果没有设备树，我们会这样定义硬件

```C
#define UART_BASE 0x40021000
#define UART_IRQ  37
```

如果更改芯片则需要更改宏

但是在设备树中，你可以给每个芯片写他自己的dts文件

```C
// VendorA
uart0: uart@40021000 {
	compatible = "vendora,uart";
	reg = <0x40021000 0x400>;
	status = "okay";
};

// VendorB
uart0: uart@50003000 {
	compatible = "vendorb,uart";
	reg = <0x50003000 0x400>;
	status = "okay";
};
```

一般一个zephyr板子不在同一个dts文件描述所有硬件

> boards/<vendor>/<board>/<board>.dts

一般引用

芯片、引脚定义、共享外设等。例如：

> #include <st/h5/stm32h563xx.dtsi>

所有的dts会merge成一个文件，这是最终的设备树文件

> build/zephyr/zephyr.dts

## 2、创建设备树overlays

板级设备树定义默认的硬件配置，但是APP有时候也需要使能外设，修改属性和硬件描述。但不想直接修改原始的板级文件。这时候可以创建 .overlay 文件

> boards/<board>.overlay

或者可以指定位置

> EXTRA_DTC_OVERLAY_FILE=path/to/myfile.overlay

overlay文件不会替代board 设备树文件，只会拓展和覆盖。overlay可以

- Change property values 

- Modify node status 

- Add new nodes

处理过程：

1. Devicetree sources are merged into a single tree. 
2. The merged result becomes zephyr.dts. 
3. The build system generates devicetree_generated.h.

![image-20260909212613525](D:\04_Code\07_zephyr\assets\image-20260909212613525.png)

最终C代码：

build/zephyr/include/generated/devicetree_generated.h