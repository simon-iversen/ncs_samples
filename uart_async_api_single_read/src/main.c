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

void test_main(void)
{
	init_uart();
	single_read_setup();
	single_read();
}
