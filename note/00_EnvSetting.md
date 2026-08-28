# zephyr环境安装

写在开始前：

（1）建议在linux系统下配置

（2）windows系统可以使用WSL环境

（3）zephyr官方环境配置网站：[Getting Started Guide — Zephyr Project Documentation](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)

## 环境配置

1. 更新linux包

> ```
> sudo apt update
> sudo apt upgrade
> ```

2. 安装所需依赖环境
> ```
> sudo apt install --no-install-recommends git cmake ninja-build gperf \
>   ccache dfu-util device-tree-compiler wget python3-dev python3-venv python3-tk \
>   xz-utils file make gcc gcc-multilib g++-multilib libsdl2-dev libmagic1
> ```

安装完毕后检查版本

> ```
> cmake --version
> python3 --version
> dtc --version
> ```

建议使用python3.12，有些系统下默认为3.14，可以使用以下方式安装3.12。安装完毕后使用python3.12创建虚拟环境

> ```
> sudo add-apt-repository ppa:deadsnakes/ppa
> sudo apt update
> sudo apt install -y python3.12 python3.12-venv python3.12-dev
> python3.12 --version
> ```

3. 创建虚拟环境(可以选择自己的路径创建)

> ```
> python3 -m venv ~/zephyrproject/.venv
> ```

4. 激活虚拟环境

> ```
> source ~/zephyrproject/.venv/bin/activate
> ```

5. 安装west

> ```
> pip install west
> ```

6. 获取zephyr源码 （这一步较慢，可能会需要流畅的网络）

> ```
> west init -m https://github.com/zephyrproject-rtos/zephyr ~/zephyrproject
> cd ~/zephyrproject
> west update
> ```

7. 安装 zephyr python 依赖

> ```
> west packages pip --install
> ```

8. 配置环境变量（为了编译能找到zephyr源码和SDK）
> ```
>cat >> ~/.bashrc << 'EOF'
>
>export ZEPHYR_BASE=~/01_project/zephyrproject/zephyr
>export ZEPHYR_SDK_INSTALL_DIR=~/01_project/zephyr-sdk-1.0.1
>export PATH=$PATH:$ZEPHYR_SDK_INSTALL_DIR/gnu/arm-zephyr-eabi/bin:$ZEPHYR_SDK_INSTALL_DIR/hosttools
>EOF
>
>source ~/.bashrc
> ```

9. 安装 sdk 并验证环境

> ```
> cd ~/zephyrproject/zephyr
> west sdk install -t xxx
> # xxx 为工具链名字，见 附录1 工具链
> 
> echo $ZEPHYR_BASE
> # 输出类似 /home/01_project/zephyrproject/zephyr
>
> which arm-zephyr-eabi-gcc
> # 应当输出 SDK 路径下的 gcc
> ```

10. 编译验证

> ```
> cd ~/zephyrproject/zephyr
> west build -p always -b esp32s3_devkitc/esp32s3/procpu samples/hello_world
> ```

应该有输出

![image-20260821205741354](..\assets\BuildResult.png)

11. 可以使用west flash刷写

12. 常规编译命令
> west build -p always -b esp32_s3_board/esp32s3/procpu

### 附录1 工具链
- x86_64-zephyr-elf
- arm-zephyr-eabi
- aarch64-zephyr-elf
- arc64-zephyr-elf
- arc-zephyr-elf
- microblazeel-zephyr-elf
- mips-zephyr-elf
- or1k-zephyr-elf
- riscv64-zephyr-elf
- rx-zephyr-elf
- sparc-zephyr-elf
- xtensa-amd_acp_6_0_adsp_zephyr-elf
- xtensa-amd_acp_7_0_adsp_zephyr-elf
- xtensa-amd_acp_7_3_adsp_zephyr-elf
- xtensa-dc233c_zephyr-elf
- xtensa-espressif_esp32_zephyr-elf
- xtensa-espressif_esp32s2_zephyr-elf
- xtensa-espressif_esp32s3_zephyr-elf
- xtensa-intel_ace15_mtpm_zephyr-elf
- xtensa-intel_ace30_ptl_zephyr-elf
- xtensa-intel_ace40_zephyr-elf
- xtensa-intel_tgl_adsp_zephyr-elf
- xtensa-mtk_mt8195_adsp_zephyr-elf
- xtensa-mtk_mt818x_adsp_zephyr-elf
- xtensa-mtk_mt8196_adsp_zephyr-elf
- xtensa-mtk_mt8365_adsp_zephyr-elf
- xtensa-nxp_imx_adsp_zephyr-elf
- xtensa-nxp_imx8m_adsp_zephyr-elf
- xtensa-nxp_imx8ulp_adsp_zephyr-elf
- xtensa-nxp_rt500_adsp_zephyr-elf
- xtensa-nxp_rt600_adsp_zephyr-elf
- xtensa-nxp_rt700_hifi1_zephyr-elf
- xtensa-nxp_rt700_hifi4_zephyr-elf
- xtensa-sample_controller_zephyr-elf
- xtensa-sample_controller32_zephyr-elf
