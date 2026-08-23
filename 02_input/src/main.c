/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>

/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS   1000

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)

/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static void input_event_callback(struct input_event *event, void *user_data)
{
	if (event->type != INPUT_EV_KEY)
	{
		return;
	}

	if ((event->code == INPUT_KEY_A) && (event->value == 0))
	{
		gpio_pin_toggle_dt(&led0);
	}

	if ((event->code == INPUT_KEY_B) && (event->value == 0))
	{
		gpio_pin_toggle_dt(&led0);
	}
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_NODELABEL(gpio_keys)), input_event_callback, NULL);

int main(void)
{
	gpio_pin_configure_dt(&led0, GPIO_OUTPUT_ACTIVE);
	while (true)
	{
		k_msleep(SLEEP_TIME_MS);
	}

	return 0;
}
