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

volatile bool failed_in_isr;
static const struct device *uart_dev;
uint8_t *read_ptr;

void init_uart(void)
{
	uart_dev = device_get_binding(UART_DEVICE_NAME);
}

uint8_t double_buffer[2][12];
uint8_t *next_buf = double_buffer[1];

void double_buffer_callback(const struct device *uart_dev,
				 struct uart_event *evt, void *user_data)
{
	switch (evt->type) {
	case UART_TX_DONE:
		k_sem_give(&tx_done);
		break;
	case UART_RX_RDY:
		read_ptr = evt->data.rx.buf + evt->data.rx.offset;
		k_sem_give(&rx_rdy);
		break;
	case UART_RX_BUF_REQUEST:
		uart_rx_buf_rsp(uart_dev, next_buf, sizeof(double_buffer[0]));
		break;
	case UART_RX_BUF_RELEASED:
		next_buf = evt->data.rx_buf.buf;
		k_sem_give(&rx_buf_released);
		break;
	case UART_RX_DISABLED:
		k_sem_give(&rx_disabled);
		break;
	default:
		break;
	}

}

void double_buffer_setup(void)
{
	uart_callback_set(uart_dev, double_buffer_callback, NULL);
}

void double_buffer_read(void)
{
	uint8_t tx_buf[6];

	uart_rx_enable(uart_dev,
				     double_buffer[0],
				     sizeof(double_buffer[0]),
				     50 * USEC_PER_MSEC);
	char* send_data = "simon";
	for (int i = 0; i < 10; i++) {
		snprintf(tx_buf, sizeof(tx_buf), "%s\n", send_data);
		uart_tx(uart_dev, tx_buf, sizeof(tx_buf), 100 * USEC_PER_MSEC);
		k_sem_take(&tx_done, K_MSEC(100));
		k_sem_take(&rx_rdy, K_MSEC(100));
		printk("Recv: %s\n", read_ptr);
	}
	uart_rx_disable(uart_dev);
	k_sem_take(&rx_disabled, K_MSEC(100));
}
