## Configuring Zephyr Applications with Kconfig

zephyr通过Kconfig控制功能使能以及子系统行为

例如：Logging、网络、debugging

### 1. Controlling Zephyr features using Kconfig

使用 `prj.conf`文件去使能zephyr相关功能

以helloworld为例，

先在helloworld工程进行编译

`west build -p always -b esp32s3_devkitc/esp32s3/procpu`

然后打开 `prj.conf` 添加 

> `CONFIG_LOG=y `
>
> `CONFIG_LOG_DEFAULT_LEVEL=3`

Enable the logging framework 

Set the default logging level

并在main.c 写

```c
int main(void)
{
	printk("Hello World from Zephyr!\n");

#if defined(CONFIG_LOG)
    printk("CONFIG_LOG is enabled (level=%d)\n", CONFIG_LOG_DEFAULT_LEVEL);
#else
    printk("CONFIG_LOG is disabled\n");
#endif

	return 0;
}
```

可以看到打印

> Hello World from Zephyr!                                                              
> CONFIG_LOG is enabled (level=3)

如何工作：

当你编译zephyr时

- The board default configuration is loaded 

- Your prj.conf is merged 

- Dependencies are resolved 

- A final .config file is generated

The resulting configuration is stored in:

> build-dir>/zephyr/.config

开启对应宏的实际文件在

> <build-dir>/zephyr/include/generated/autoconf.h

ps:使用IS_ENABLED确定宏是否开启

``` IS_ENABLED(CONFIG_SYMBOL)
IS_ENABLED(CONFIG_SYMBOL)
```

### 2. Generating the merged Kconfig configuration file