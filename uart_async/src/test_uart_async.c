/*
 *  Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr.h>
#include <logging/log.h>
#include <drivers/uart.h>

#include "test_uart.h"

K_SEM_DEFINE(tx_done, 0, 1);
K_SEM_DEFINE(tx_aborted, 0, 1);
K_SEM_DEFINE(rx_rdy, 0, 1);
K_SEM_DEFINE(rx_buf_released, 0, 1);
K_SEM_DEFINE(rx_disabled, 0, 1);

#define BUF_SIZE 64

static K_MEM_SLAB_DEFINE(uart_slab, BUF_SIZE, 3, 4);

volatile bool failed_in_isr;
static const struct device *uart_dev;
uint8_t *read_ptr;
int recv_length;

void init_uart(void)
{
	uart_dev = device_get_binding(UART_DEVICE_NAME);
}

uint8_t double_buffer[2][12];
uint8_t *next_buf = double_buffer[1];

void double_buffer_callback(const struct device *uart_dev,
				 struct uart_event *evt, void *user_data)
{
	int err;
	uint8_t *buf;

	switch (evt->type) {
	case UART_TX_DONE:
		k_sem_give(&tx_done);
		break;
	case UART_RX_RDY:
		read_ptr = evt->data.rx.buf + evt->data.rx.offset;
		recv_length = evt->data.rx.len;
		//Log("Recv: %s\n", read_ptr);
		k_sem_give(&rx_rdy);
		break;
	case UART_RX_BUF_REQUEST:
		

		err = k_mem_slab_alloc(&uart_slab, (void **)&buf, K_NO_WAIT);
		__ASSERT(err == 0, "Failed to allocate slab");

		err = uart_rx_buf_rsp(uart_dev, buf, BUF_SIZE);
		__ASSERT(err == 0, "Failed to provide new buffer");
		break;
	case UART_RX_BUF_RELEASED:
		k_mem_slab_free(&uart_slab, (void **)&evt->data.rx_buf.buf);
		k_sem_give(&rx_buf_released);
		break;
	case UART_RX_DISABLED:
		k_sem_give(&rx_disabled);
		break;
	default:
		break;
	}

}

void double_buffer_setup_and_read(void)
{
	uint8_t err;
	uint8_t *buf;

	err = uart_callback_set(uart_dev, double_buffer_callback, NULL);
	__ASSERT(err == 0, "Failed to set callback");

	err = k_mem_slab_alloc(&uart_slab, (void **)&buf, K_NO_WAIT);
	__ASSERT(err == 0, "Failed to alloc slab");

	err = uart_rx_enable(uart_dev, buf, BUF_SIZE, 10000);
	__ASSERT(err == 0, "Failed to enable RX");

	while (true) {
		k_sem_take(&rx_rdy, K_FOREVER);
		printk("%.*s\n", recv_length, read_ptr);
		//printk("Recv: %s\n", read_ptr);
	}

}
