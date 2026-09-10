## Writing a West manifest and organizing projects

The West manifest is a YAML file named `west.yml`

```
manifest:
	remotes:
		- name: zephyrproject
		url-base: https://github.com/zephyrproject-rtos
		
	projects:
		- name: zephyr
		remote: zephyrproject
		repo-path: zephyr
		revision: v4.3.0
```

它主要回答四个问题：

1. 工作区需要哪些仓库？
2. 从哪里获取这些仓库？
3. 使用哪个 Git 分支、标签或提交？
4. 每个仓库放在本地什么路径？

## West build

![image-20260905124624548](\\wsl.localhost\Ubuntu\home\mason\01_project\ZephyrOnEsp32\note\01_zephyr_cookbook\assets\image-20260905124624548.png)

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

