/*
* Copyright (c) 2012-2014 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*/

#include <zephyr.h>
#include <sys/printk.h>
#include <drivers/spi.h>

#define DT_DRV_COMPAT nordic_nrf_spis

static const struct spi_config spi_cfg = {
	.operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_SLAVE |
		     SPI_MODE_CPOL | SPI_MODE_CPHA,
	.frequency = 4000000,
	.slave = 1,
};

const struct device *spi_dev;

#define SPIS_STACK_SIZE 500
#define SPIS_PRIORITY 5

extern void spis_thread(void *, void *, void *);

#define MY_SPI_SLAVE			DT_LABEL(DT_NODELABEL(my_spi_slave))

K_THREAD_DEFINE(spis_tid, SPIS_STACK_SIZE, spis_thread, NULL, NULL, NULL,
		SPIS_PRIORITY, 0, 0);

static void spi_init(void)
{
	spi_dev = device_get_binding(MY_SPI_SLAVE);//DT_LABEL(DT_DRV_INST(0)));

	//printk("Device name: %s\n", DT_LABEL(DT_DRV_INST(0)));

	if (spi_dev == NULL) {
		printk("Could not get %s device\n", DT_LABEL(DT_DRV_INST(0)));
		return;
	}

	/*printk("SPI CSN %d, MISO %d, MOSI %d, CLK %d\n",
	       DT_PROP(DT_DRV_INST(0), csn_pin),
	       DT_PROP(DT_DRV_INST(0), miso_pin),
	       DT_PROP(DT_DRV_INST(0), mosi_pin),
	       DT_PROP(DT_DRV_INST(0), sck_pin));*/
}

void spi_test_transceive(void)
{
	int err;
	static uint8_t tx_buffer[32] = { 's', 'p', 'i', 's', 'l',
					 'a', 'v', 'e', '\n' };
	static uint8_t rx_buffer[32];

	const struct spi_buf tx_buf = { .buf = tx_buffer,
					.len = sizeof(tx_buffer) };
	const struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };

	struct spi_buf rx_buf = {
		.buf = rx_buffer,
		.len = sizeof(rx_buffer),
	};
	const struct spi_buf_set rx = { .buffers = &rx_buf, .count = 1 };

	err = spi_transceive(spi_dev, &spi_cfg, &tx, &rx);
	if (err < 0) {
		printk("SPI error: %d\n", err);
	} else {		
		printk("byte received: %d\n", err);
		printk("TX buffer [0]: %x\n", tx_buffer[0]);
		printk("RX buffer [0]: %x\n", rx_buffer[0]);
		tx_buffer[0]++;
	}
}

extern void spis_thread(void *unused1, void *unused2, void *unused3)
{
	spi_init();

	while (1) {
		spi_test_transceive();
	}
}

void main(void)
{
	printk("SPIS Example\n");
	printk("Add any handling you'd like in the main thread\n");
	printk("SPIS handling will be performed in an own thread\n");
}