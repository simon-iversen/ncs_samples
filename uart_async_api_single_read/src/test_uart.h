/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief UART cases header file
 *
 * Header file for UART cases
 */

#ifndef __TEST_UART_H__
#define __TEST_UART_H__

#include <drivers/uart.h>
#include <ztest.h>

/* RX and TX pins have to be connected together*/

#if defined(CONFIG_BOARD_NRF52840DK_NRF52840)
#define UART_DEVICE_NAME DT_LABEL(DT_NODELABEL(uart0))
#elif defined(CONFIG_BOARD_NRF9160DK_NRF9160)
#define UART_DEVICE_NAME DT_LABEL(DT_NODELABEL(uart1))
#endif

void init_uart(void);

void single_read(void);
void single_read_setup(void);


#endif /* __TEST_UART_H__ */
