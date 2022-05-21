/*
 *  Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_uart.h"

K_SEM_DEFINE(tx_done, 0, 1);
K_SEM_DEFINE(tx_aborted, 0, 1);
K_SEM_DEFINE(rx_rdy, 0, 1);
K_SEM_DEFINE(rx_buf_released, 0, 1);
K_SEM_DEFINE(rx_disabled, 0, 1);

ZTEST_BMEM static const struct device *uart_dev;

void init_uart(void)
{
	uart_dev = device_get_binding(UART_DEVICE_NAME);
}


void single_read_callback(const struct device *dev,
			       struct uart_event *evt, void *user_data)
{
	ARG_UNUSED(dev);

	switch (evt->type) {
	case UART_TX_DONE:
		k_sem_give(&tx_done);
		break;
	case UART_TX_ABORTED:
		(*(uint32_t *)user_data)++;
		break;
	case UART_RX_RDY:
		k_sem_give(&rx_rdy);
		break;
	case UART_RX_BUF_RELEASED:
		k_sem_give(&rx_buf_released);
		break;
	case UART_RX_DISABLED:
		printk("UART RX DIS");
		break;
	default:
		break;
	}

}

ZTEST_BMEM volatile uint32_t tx_aborted_count;

void single_read_setup(void)
{
	uart_callback_set(uart_dev,
			  single_read_callback,
			  (void *) &tx_aborted_count);
}

void single_read(void)
{
	uint8_t rx_buf[10] = {0};

	/* Check also if sending from read only memory (e.g. flash) works. */
	static const uint8_t tx_buf[5] = "test";

	uart_rx_enable(uart_dev, rx_buf, 10, 50 * USEC_PER_MSEC);

	uart_tx(uart_dev, tx_buf, sizeof(tx_buf), 100 * USEC_PER_MSEC);

	printk("Recv: %s\n", rx_buf);
}
