/*
 * ESP32-S3 + Zephyr PWM breathing LED
 * Idea: LEDC makes a 5kHz square wave (carrier).
 *       Software moves pulse (width in ns) from 0 to period,
 *       then back. So the envelope is a triangle wave = "breath".
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>

/* Get the LED info from dts alias "pwm-led0".
 * The info has: dev=ledc0, channel=0, period=200000ns(5kHz),
 *               flags=PWM_POLARITY_INVERTED */
static const struct pwm_dt_spec pwm_led0 = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led0));

#define NUM_STEPS  50U    /* steps in one way (up or down): 50 = 2% each */
#define SLEEP_MSEC 25U    /* wait 25ms after each step:
                           * 50*25ms*2 ways = 2.5s one full breath */

int main(void)
{
	uint32_t pulse = 0U;                       /* current pulse width (ns) */
	uint32_t step = pwm_led0.period / NUM_STEPS;   /* 200000/50 = 4000ns */
	bool up = true;                            /* true = going up, false = going down */
	int ret;

	printk("PWM breathing LED start\r\n");

	if (!pwm_is_ready_dt(&pwm_led0)) {
		printk("Error: PWM device %s is not ready\n", pwm_led0.dev->name);
		return 0;
	}

	while (1) {
		/* 1. Write the "current level" to hardware.
		 * Period and polarity come from spec, we only give pulse. */
		ret = pwm_set_pulse_dt(&pwm_led0, pulse);
		if (ret != 0) {
			printk("Error %d: failed to set pulse width\n", ret);
		}

		/* 2. Move one step: going up */
		if (up) {
			if (pulse + step >= pwm_led0.period) {
				pulse = pwm_led0.period;   /* hit the top: clamp to 100% */
				up = false;                /* change direction */
			} else {
				pulse += step;             /* else step forward */
			}
		} else {
		/* 3. Move one step: going down */
			if (pulse <= step) {
				pulse = 0U;                /* hit the bottom: clamp to 0% */
				up = true;                 /* change direction */
			} else {
				pulse -= step;             /* else step back */
			}
		}

		/* 4. Heartbeat: sleep 25ms, wake up and go back to step 1 */
		k_msleep(SLEEP_MSEC);
	}
	return 0;
}