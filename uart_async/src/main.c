/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @addtogroup t_driver_uart
 * @{
 * @defgroup t_uart_async test_uart_async
 * @}
 */

#include "test_uart.h"
#include <stdio.h>

void test_main(void)
{
	init_uart();

#ifdef CONFIG_USERSPACE
	set_permissions();
#endif
	printk("uart async api sample started\n");
	double_buffer_setup_and_read();
}
