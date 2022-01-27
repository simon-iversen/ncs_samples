/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <sys/printk.h>
#include "os_mgmt/os_mgmt.h"
#include "img_mgmt/img_mgmt.h"


void main(void)
{
	printk("Hello World! %s\n", CONFIG_BOARD);
	printk("build time: " __DATE__ " " __TIME__ "\n");
	os_mgmt_register_group();
	img_mgmt_register_group();
}
