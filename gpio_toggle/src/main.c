/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <sys/printk.h>

#define PIN_GPIO  (31UL)

void main(void)
{
	NRF_GPIO->PIN_CNF[PIN_GPIO] = (GPIO_PIN_CNF_DIR_Output << GPIO_PIN_CNF_DIR_Pos) |
								(GPIO_PIN_CNF_DRIVE_S0S1 << GPIO_PIN_CNF_DRIVE_Pos) |
								(GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) |
								(GPIO_PIN_CNF_PULL_Disabled << GPIO_PIN_CNF_PULL_Pos) |
								(GPIO_PIN_CNF_SENSE_Disabled << GPIO_PIN_CNF_SENSE_Pos);

	k_sleep(K_MSEC(2000));
	NRF_GPIO->OUTSET = (1UL << PIN_GPIO); 
	k_sleep(K_MSEC(2000));
	NRF_GPIO->OUTCLR = (1UL << PIN_GPIO); 

	printk("Hello World! %s\n", CONFIG_BOARD);
}
