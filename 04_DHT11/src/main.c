/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>

const struct device *dht = DEVICE_DT_GET(DT_ALIAS(dht0));

int main(void)
{
	struct sensor_value temp, hum;

	if (!device_is_ready(dht)) 
	{
    	printk("DHT device not ready\n");
    	return -1;
	}

	while (1) 
	{
		k_msleep(2000);
		
		int test = sensor_sample_fetch(dht);
		if (test < 0) 
		{
    		printk("test failed: %d\n", test);
		}

		sensor_channel_get(dht, SENSOR_CHAN_AMBIENT_TEMP, &temp);
		sensor_channel_get(dht, SENSOR_CHAN_HUMIDITY, &hum);

		printk("Temp: %d.%06d °C, Humidity: %d.%06d %%RH\n",
       			temp.val1, temp.val2,
       			hum.val1, hum.val2);
	}

	return 0;
}