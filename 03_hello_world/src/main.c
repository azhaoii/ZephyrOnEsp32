/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>

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
