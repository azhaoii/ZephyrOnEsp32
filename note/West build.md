## West build

![image-20260905124624548](C:\Users\ZMZ\AppData\Roaming\Typora\typora-user-images\image-20260905124624548.png)

- Kconfig:识别哪个子系统被使能，驱动被选择
- Devicetree:描述硬件，定义外设，中断等配置。

编译器编译

- application源文件
- zephyr kernel
- 使能的硬件
- 使能的子系统

输出`zephyr.elf`

所有配置决策都在编译开始之前解决。编译器和链接器 只处理早期阶段的结果。

> 不要使用Kconfig描述硬件
>
> 不要用Devicetree使能软件
>
> 不要把west当场构建系统

