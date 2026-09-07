## 一、查看当前芯片的开发板

linux环境下，激活虚拟环境后使用west命令：

>  west boards | grep -i  ``` esp32s3 ```

输出如下：

> ((.venv) ) mason@DESKTOP-GGQQQHR:~/01_project/$ west boards | grep -i esp32s3
> esp32s3_matrix
> esp32s3_geek
> esp32s3_touch_lcd_1_28
> esp32s3_rlcd_4_2
> weact_esp32s3_mini
> weact_esp32s3_b
> adafruit_qt_py_esp32s3
> adafruit_feather_esp32s3
> adafruit_feather_esp32s3_tft_reverse
> adafruit_feather_esp32s3_tft
> esp32s3_luatos_core
> dnesp32s3b
> esp32s3_eye
> esp32s3_box3
> esp32s3_devkitc
> xiao_esp32s3

## 二、zephyr工程的正确设计方式

1. 官方推荐不要动zephyrproject/zephyr文件夹下目录结构

- ``` zephyr/ ``` : 只当“SDK + OS内核”，不改动
- ``` myapps ``` : 你自己的应用仓库
- ``` boards/ ```、```modules```: 放在应用工程里或者独立仓库 

* **不要直接在 ``` zephyr/boards``` 下加板子**

2. 建议结构

> ###### your_app/
>
> ######     |__boards/
>
> ######               |_arm/
>
> ######                     |__your_boards/

这样做可以直接进行编译： 

> west build -b ``` your_board```

## 三、创建自定义板子

> ###### 01_project/
>
> ######           |__myapps
>
> ######           |__zephyrproject/zephyr

在zephyrproject同级目录下创建myapps