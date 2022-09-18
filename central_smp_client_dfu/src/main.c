/* main.c - Application main entry point */

/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <zcbor_encode.h>
#include <zcbor_decode.h>
#include <zcbor_common.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <bluetooth/gatt_dm.h>
#include <zephyr/sys/byteorder.h>
#include <bluetooth/scan.h>
#include <bluetooth/services/dfu_smp.h>
#include <dk_buttons_and_leds.h>

#include <zephyr/device.h>
#include <drivers/flash.h>

/* Mimimal number of ZCBOR encoder states to provide full encoder functionality. */
#define CBOR_ENCODER_STATE_NUM 2

/* Number of ZCBOR decoder states required to provide full decoder functionality plus one
 * more level for decoding nested map received in response to SMP echo command.
 */
#define CBOR_DECODER_STATE_NUM 3

#define CBOR_MAP_MAX_ELEMENT_CNT 2
#define CBOR_BUFFER_SIZE 128

#define SMP_ECHO_MAP_KEY_MAX_LEN 2
#define SMP_ECHO_MAP_VALUE_MAX_LEN 30

#define KEY_ECHO_MASK  DK_BTN1_MSK
#define KEY_RESET_MASK  DK_BTN2_MSK

static struct bt_conn *default_conn;
static struct bt_dfu_smp dfu_smp;
static struct bt_gatt_exchange_params exchange_params;

/* Buffer for response */
struct smp_buffer {
	struct bt_dfu_smp_header header;
	uint8_t payload[CBOR_BUFFER_SIZE];
};
static struct smp_buffer smp_rsp_buff;


static void scan_filter_match(struct bt_scan_device_info *device_info,
			      struct bt_scan_filter_match *filter_match,
			      bool connectable)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(device_info->recv_info->addr, addr, sizeof(addr));

	printk("Filters matched. Address: %s connectable: %s\n",
		addr, connectable ? "yes" : "no");
}

static void scan_connecting_error(struct bt_scan_device_info *device_info)
{
	printk("Connecting failed\n");
}

static void scan_connecting(struct bt_scan_device_info *device_info,
			    struct bt_conn *conn)
{
	default_conn = bt_conn_ref(conn);
}

BT_SCAN_CB_INIT(scan_cb, scan_filter_match, NULL,
		scan_connecting_error, scan_connecting);

static void discovery_completed_cb(struct bt_gatt_dm *dm,
				   void *context)
{
	int err;

	printk("The discovery procedure succeeded\n");

	bt_gatt_dm_data_print(dm);

	err = bt_dfu_smp_handles_assign(dm, &dfu_smp);
	if (err) {
		printk("Could not init DFU SMP client object, error: %d\n",
		       err);
	}

	err = bt_gatt_dm_data_release(dm);
	if (err) {
		printk("Could not release the discovery data, error "
		       "code: %d\n", err);
	}
}

static void discovery_service_not_found_cb(struct bt_conn *conn,
					   void *context)
{
	printk("The service could not be found during the discovery\n");
}

static void discovery_error_found_cb(struct bt_conn *conn,
				     int err,
				     void *context)
{
	printk("The discovery procedure failed with %d\n", err);
}

static const struct bt_gatt_dm_cb discovery_cb = {
	.completed = discovery_completed_cb,
	.service_not_found = discovery_service_not_found_cb,
	.error_found = discovery_error_found_cb,
};

static void exchange_func(struct bt_conn *conn, uint8_t err,
			  struct bt_gatt_exchange_params *params)
{
	printk("MTU exchange %s\n", err == 0 ? "successful" : "failed");
	printk("Current MTU: %u\n", bt_gatt_get_mtu(conn));
}

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
	char addr[BT_ADDR_LE_STR_LEN];
	int err;

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (conn_err) {
		printk("Failed to connect to %s (%u)\n", addr, conn_err);
		if (conn == default_conn) {
			bt_conn_unref(default_conn);
			default_conn = NULL;

			/* This demo doesn't require active scan */
			err = bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE);
			if (err) {
				printk("Scanning failed to start (err %d)\n",
				       err);
			}
		}

		return;
	}

	printk("Connected: %s\n", addr);

	if (bt_conn_set_security(conn, BT_SECURITY_L2)) {
		printk("Failed to set security\n");
	}

	exchange_params.func = exchange_func;
	err = bt_gatt_exchange_mtu(conn, &exchange_params);
	if (err) {
		printk("MTU exchange failed (err %d)\n", err);
	} else {
		printk("MTU exchange pending\n");
	}

	if (conn == default_conn) {
		err = bt_gatt_dm_start(conn, BT_UUID_DFU_SMP_SERVICE,
				       &discovery_cb, NULL);
		if (err) {
			printk("Could not start the discovery procedure "
			       "(err %d)\n", err);
		}
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];
	int err;

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Disconnected: %s (reason %u)\n", addr, reason);

	if (default_conn != conn) {
		return;
	}

	bt_conn_unref(default_conn);
	default_conn = NULL;

	/* This demo doesn't require active scan */
	err = bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE);
	if (err) {
		printk("Scanning failed to start (err %d)\n", err);
	}
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err) {
		printk("Security changed: %s level %u\n", addr, level);
	} else {
		printk("Security failed: %s level %u err %d\n", addr, level,
			err);
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed
};

static void scan_init(void)
{
	int err;

	struct bt_scan_init_param scan_init = {
		.connect_if_match = 1,
		.scan_param = NULL,
		.conn_param = BT_LE_CONN_PARAM_DEFAULT
	};

	bt_scan_init(&scan_init);
	bt_scan_cb_register(&scan_cb);

	err = bt_scan_filter_add(BT_SCAN_FILTER_TYPE_UUID,
				 BT_UUID_DFU_SMP_SERVICE);
	if (err) {
		printk("Scanning filters cannot be set (err %d)\n", err);

		return;
	}

	err = bt_scan_filter_enable(BT_SCAN_UUID_FILTER, false);
	if (err) {
		printk("Filters cannot be turned on (err %d)\n", err);
	}
}


static void dfu_smp_on_error(struct bt_dfu_smp *dfu_smp, int err)
{
	printk("DFU SMP generic error: %d\n", err);
}


static const struct bt_dfu_smp_init_params init_params = {
	.error_cb = dfu_smp_on_error
};

static void smp_reset_rsp_proc(struct bt_dfu_smp *dfu_smp)
{
	printk("RESET RESPONSE CB. Doing nothing\n");
}

static void smp_upload_rsp_proc(struct bt_dfu_smp *dfu_smp)
{
	printk("UPLOAD RESPONSE CB. Doing nothing\n");
	uint8_t *p_outdata = (uint8_t *)(&smp_rsp_buff);
	const struct bt_dfu_smp_rsp_state *rsp_state;

	rsp_state = bt_dfu_smp_rsp_state(dfu_smp);
	printk("Echo response part received, size: %zu.\n",
	       rsp_state->chunk_size);

	if (rsp_state->offset + rsp_state->chunk_size > sizeof(smp_rsp_buff)) {
		printk("Response size buffer overflow\n");
	} else {
		p_outdata += rsp_state->offset;
		memcpy(p_outdata,
		       rsp_state->data,
		       rsp_state->chunk_size);
	}


	size_t payload_len = ((uint16_t)smp_rsp_buff.header.len_h8) << 8 |smp_rsp_buff.header.len_l8;
	zcbor_state_t zsd[CBOR_DECODER_STATE_NUM];
	struct zcbor_string value = {0};
	char map_key[SMP_ECHO_MAP_KEY_MAX_LEN];
	char map_value[SMP_ECHO_MAP_VALUE_MAX_LEN];
	bool ok;
	printk("Lenght of payload: %d\n", payload_len);
	printk(" Bytes: ");
	for(int x = 0; x < payload_len;x++){
		printk("%x ",  smp_rsp_buff.payload[x]);
	}
	printk("\n");
	zcbor_new_decode_state(zsd, ARRAY_SIZE(zsd), smp_rsp_buff.payload, payload_len, 1);

	/* Stop decoding on the error. */
	zsd->constant_state->stop_on_error = true;

	ok = zcbor_map_start_decode(zsd);
	if (!ok) {
		printk("zcbor_map_start_decode decoding error (err: %d)\n", zcbor_pop_error(zsd));
		return;
	} else {
		printk("zcbor_map_start_decode() successfully run\n");
	}
	
	ok = zcbor_tstr_decode(zsd, &value);
	uint8_t length_payload = zsd->payload_end - zsd->payload;
	if (!ok) {
		printk("sdf Decoding error (err: %d)\n", zcbor_pop_error(zsd));
		return;
	}  else {
		printk("Successfully decoded\n");
	}
	char map_key2[20];
	memcpy(map_key2, value.value, value.len);
	map_key2[value.len] = '\0';
	printk("Key value: %s\n", map_key2);
	
	/*map_key[0] = value.value[0];

	
	map_key[1] = '\0';
	ok = zcbor_tstr_decode(zsd, &value);
	if (!ok) {
		//fails here
		printk("234fd Decoding error (err: %d)\n", zcbor_pop_error(zsd));
		return;
	} else {
		printk("Successfully decoded\n");
	}
	memcpy(map_value, value.value, value.len);
	
	map_value[value.len] = '\0';

	zcbor_map_end_decode(zsd);
	if (zcbor_check_error(zsd)) {
		
		printk("{_\"%s\": \"%s\"}\n", map_key, map_value);
	} else {
		printk("Cannot print received CBOR stream (err: %d)\n",
				zcbor_pop_error(zsd));
	}*/



}

static void smp_list_rsp_proc(struct bt_dfu_smp *dfu_smp)
{
	printk("LIST RESPONSE CB. Doing nothing\n");
	/*uint8_t *p_outdata = (uint8_t *)(&smp_rsp_buff);
	const struct bt_dfu_smp_rsp_state *rsp_state;

	rsp_state = bt_dfu_smp_rsp_state(dfu_smp);
	printk("Echo response part received, size: %zu.\n",
	       rsp_state->chunk_size);

	if (bt_dfu_smp_rsp_total_check(dfu_smp)) {
		zcbor_state_t zsd[CBOR_DECODER_STATE_NUM];
		size_t payload_len = ((uint16_t)smp_rsp_buff.header.len_h8) << 8 |
				      smp_rsp_buff.header.len_l8;
		zcbor_new_decode_state(zsd, ARRAY_SIZE(zsd), smp_rsp_buff.payload, payload_len, 1);
		uint8_t length_payload = zsd->payload_end - zsd->payload;
		uint8_t payl_offs = 0;
		printk("Start addr of payload: 0x%x\n", zsd->payload);
		printk("end addr of payload: 0x%x\n", zsd->payload_end);
		printk("Received bytes: 0x");
		while(payl_offs <length_payload){
			printk("%x", zsd->payload[payl_offs]);
			payl_offs++;
		}
		printk("\n");
	}*/
}

static void smp_echo_rsp_proc(struct bt_dfu_smp *dfu_smp)
{
	uint8_t *p_outdata = (uint8_t *)(&smp_rsp_buff);
	const struct bt_dfu_smp_rsp_state *rsp_state;

	size_t payload_len2 = ((uint16_t)smp_rsp_buff.header.len_h8) << 8 |
				      smp_rsp_buff.header.len_l8;

	printk("ECHO Lenght of payload: %d\n", payload_len2);
	printk("ECHO Bytes: ");
	for(int x = 0; x < 10;x++){
		printk("%x ",  smp_rsp_buff.payload[x]);
	}
	printk("\n");

	rsp_state = bt_dfu_smp_rsp_state(dfu_smp);
	printk("Echo response part received, size: %zu.\n",
	       rsp_state->chunk_size);

	if (rsp_state->offset + rsp_state->chunk_size > sizeof(smp_rsp_buff)) {
		printk("Response size buffer overflow\n");
	} else {
		p_outdata += rsp_state->offset;
		memcpy(p_outdata,
		       rsp_state->data,
		       rsp_state->chunk_size);
	}

	if (bt_dfu_smp_rsp_total_check(dfu_smp)) {
		printk("Total response received - decoding\n");
		if (smp_rsp_buff.header.op != 3 /* WRITE RSP*/) {
			printk("Unexpected operation code (%u)!\n",
			       smp_rsp_buff.header.op);
			return;
		}
		uint16_t group = ((uint16_t)smp_rsp_buff.header.group_h8) << 8 |
				      smp_rsp_buff.header.group_l8;
		if (group != 0 /* OS */) {
			printk("Unexpected command group (%u)!\n", group);
			return;
		}
		if (smp_rsp_buff.header.id != 0 /* ECHO */) {
			printk("Unexpected command (%u)",
			       smp_rsp_buff.header.id);
			return;
		}
		size_t payload_len = ((uint16_t)smp_rsp_buff.header.len_h8) << 8 |
				      smp_rsp_buff.header.len_l8;

		zcbor_state_t zsd[CBOR_DECODER_STATE_NUM];
		struct zcbor_string value = {0};
		char map_key[SMP_ECHO_MAP_KEY_MAX_LEN];
		char map_value[SMP_ECHO_MAP_VALUE_MAX_LEN];
		bool ok;

		zcbor_new_decode_state(zsd, ARRAY_SIZE(zsd), smp_rsp_buff.payload, payload_len, 1);

		/* Stop decoding on the error. */
		zsd->constant_state->stop_on_error = true;

		printk("ECHO 2 Lenght of payload: %d\n", payload_len2);
		printk("ECHO 2 Bytes: ");
		for(int x = 0; x < 10;x++){
			printk("%x ",  smp_rsp_buff.payload[x]);
		}
		printk("\n");

		zcbor_map_start_decode(zsd);
		
		ok = zcbor_tstr_decode(zsd, &value);
		printk("Start addr of payload: 0x%x\n", zsd->payload);
		printk("end addr of payload: 0x%x\n", zsd->payload_end);
		uint8_t length_payload = zsd->payload_end - zsd->payload;
		uint8_t payl_offs = 0;
		printk("Received bytes: 0x");
		while(payl_offs <length_payload){
			printk("%x", zsd->payload[payl_offs]);
			payl_offs++;
		}
		printk("\n");

		if (!ok) {
			printk("Decoding error (err: %d)\n", zcbor_pop_error(zsd));
			return;
		} else if ((value.len != 1) || (*value.value != 'r')) {
			printk("Invalid data received.\n");
			return;
		} else {
			/* Do nothing */
		}

		map_key[0] = value.value[0];

		/* Add string NULL terminator */
		map_key[1] = '\0';

		ok = zcbor_tstr_decode(zsd, &value);

		if (!ok) {
			printk("Decoding error (err: %d)\n", zcbor_pop_error(zsd));
			return;
		} else if (value.len > (sizeof(map_value) - 1)) {
			printk("To small buffer for received data.\n");
			return;
		} else {
			/* Do nothing */
		}

		memcpy(map_value, value.value, value.len);

		/* Add string NULL terminator */
		map_value[value.len] = '\0';

		zcbor_map_end_decode(zsd);

		if (zcbor_check_error(zsd)) {
			/* Print textual representation of the received CBOR map. */
			printk("{_\"%s\": \"%s\"}\n", map_key, map_value);
		} else {
			printk("Cannot print received CBOR stream (err: %d)\n",
			       zcbor_pop_error(zsd));
		}
	}

}

#define IMAGE_HEADER_SIZE		32

static int send_upload(struct bt_dfu_smp *dfu_smp)
{
	zcbor_state_t zse[2];
	size_t payload_len;
	static struct smp_buffer smp_cmd;

	zcbor_new_encode_state(zse, ARRAY_SIZE(zse), smp_cmd.payload,
			       sizeof(smp_cmd.payload), 0);

		const struct device *flash_dev;
	uint8_t data[33];

	flash_dev = device_get_binding("NRF_FLASH_DRV_NAME");
	int err = flash_read(flash_dev, 0xf6000, data, IMAGE_HEADER_SIZE);
	/*if (err != 0) {
		printk("Could not read bytes\n");
	}else{
		printk("Data: ");
		for(int x=0; x<10; x++){
			printk("%x ", data[x]);
		}
		printk("\n");
	}*/
	if(err){
		printk("flash_read error: %d\n", err);
	}
	data[IMAGE_HEADER_SIZE] = '\0';


	/* Stop encoding on the error. */
	zse->constant_state->stop_on_error = true;
	printk("Change\n");
	zcbor_map_start_encode(zse, 20);
	zcbor_tstr_put_lit(zse, "image");
	zcbor_int64_put(zse, 0);
	zcbor_tstr_put_lit(zse, "data");
	zcbor_bstr_put_lit(zse, data);
	zcbor_tstr_put_lit(zse, "len");
	zcbor_uint64_put(zse, 100);
	zcbor_tstr_put_lit(zse, "off");
	zcbor_uint64_put(zse, 0);
	zcbor_tstr_put_lit(zse, "sha");
	zcbor_bstr_put_lit(zse, "12345");
	zcbor_tstr_put_lit(zse, "upgrade");
	zcbor_bool_put(zse, false);
	zcbor_map_end_encode(zse, 20);

	if (!zcbor_check_error(zse)) {
		printk("Failed to encode SMP test packet, err: %d\n", zcbor_pop_error(zse));
	}else{
		printk("Successfully encoded SMP test packet");
	}

	payload_len = (size_t)(zse->payload - smp_cmd.payload);

	smp_cmd.header.op = 2; /* write request */
	smp_cmd.header.flags = 0;
	smp_cmd.header.len_h8 = (uint8_t)((payload_len >> 8) & 0xFF);
	smp_cmd.header.len_l8 = (uint8_t)((payload_len >> 0) & 0xFF);
	smp_cmd.header.group_h8 = 0;
	smp_cmd.header.group_l8 = 1; /* IMAGE */
	smp_cmd.header.seq = 0;
	smp_cmd.header.id  = 1; /* UPLOAD */
	
	return bt_dfu_smp_command(dfu_smp, smp_upload_rsp_proc,
				  sizeof(smp_cmd.header) + payload_len,
				  &smp_cmd);
}

static int send_smp_list(struct bt_dfu_smp *dfu_smp,
			 const char *string)
{
	static struct smp_buffer smp_cmd;
	zcbor_state_t zse[CBOR_ENCODER_STATE_NUM];
	size_t payload_len;

	zcbor_new_encode_state(zse, ARRAY_SIZE(zse), smp_cmd.payload,
			       sizeof(smp_cmd.payload), 0);

	/* Stop encoding on the error. */
	zse->constant_state->stop_on_error = true;

	zcbor_map_start_encode(zse, CBOR_MAP_MAX_ELEMENT_CNT);
	zcbor_tstr_put_lit(zse, "d");
	zcbor_tstr_put_term(zse, string);
	zcbor_map_end_encode(zse, CBOR_MAP_MAX_ELEMENT_CNT);

	if (!zcbor_check_error(zse)) {
		printk("Failed to encode SMP echo packet, err: %d\n", zcbor_pop_error(zse));
		return -EFAULT;
	}

	payload_len = (size_t)(zse->payload - smp_cmd.payload);

	smp_cmd.header.op = 0; /* read request */
	smp_cmd.header.flags = 0;
	smp_cmd.header.len_h8 = 0;//(uint8_t)((payload_len >> 8) & 0xFF);
	smp_cmd.header.len_l8 = 0;//(uint8_t)((payload_len >> 0) & 0xFF);
	smp_cmd.header.group_h8 = 0;
	smp_cmd.header.group_l8 = 1; /* IMAGE */
	smp_cmd.header.seq = 0;
	smp_cmd.header.id  = 0; /* LIST */
	
	return bt_dfu_smp_command(dfu_smp, smp_list_rsp_proc,
				  sizeof(smp_cmd.header) + payload_len,
				  &smp_cmd);
}


static int send_smp_reset(struct bt_dfu_smp *dfu_smp,
			 const char *string)
{
	static struct smp_buffer smp_cmd;
	zcbor_state_t zse[CBOR_ENCODER_STATE_NUM];
	size_t payload_len;

	zcbor_new_encode_state(zse, ARRAY_SIZE(zse), smp_cmd.payload,
			       sizeof(smp_cmd.payload), 0);

	/* Stop encoding on the error. */
	zse->constant_state->stop_on_error = true;

	zcbor_map_start_encode(zse, CBOR_MAP_MAX_ELEMENT_CNT);
	zcbor_tstr_put_lit(zse, "d");
	zcbor_tstr_put_term(zse, string);
	zcbor_map_end_encode(zse, CBOR_MAP_MAX_ELEMENT_CNT);

	if (!zcbor_check_error(zse)) {
		printk("Failed to encode SMP echo packet, err: %d\n", zcbor_pop_error(zse));
		return -EFAULT;
	}

	payload_len = (size_t)(zse->payload - smp_cmd.payload);

	smp_cmd.header.op = 2; /* Write */
	smp_cmd.header.flags = 0;
	smp_cmd.header.len_h8 = 0;//(uint8_t)((payload_len >> 8) & 0xFF);
	smp_cmd.header.len_l8 = 0;//(uint8_t)((payload_len >> 0) & 0xFF);
	smp_cmd.header.group_h8 = 0;
	smp_cmd.header.group_l8 = 0; /* OS */
	smp_cmd.header.seq = 0;
	smp_cmd.header.id  = 5; /* RESET */

	return bt_dfu_smp_command(dfu_smp, smp_reset_rsp_proc,
				  sizeof(smp_cmd.header) + payload_len,
				  &smp_cmd);
}


static int send_smp_echo(struct bt_dfu_smp *dfu_smp,
			 const char *string)
{
	static struct smp_buffer smp_cmd;
	zcbor_state_t zse[CBOR_ENCODER_STATE_NUM];
	size_t payload_len;

	zcbor_new_encode_state(zse, ARRAY_SIZE(zse), smp_cmd.payload,
			       sizeof(smp_cmd.payload), 0);

	/* Stop encoding on the error. */
	zse->constant_state->stop_on_error = true;

	zcbor_map_start_encode(zse, CBOR_MAP_MAX_ELEMENT_CNT);
	zcbor_tstr_put_lit(zse, "d");
	zcbor_tstr_put_term(zse, string);
	zcbor_map_end_encode(zse, CBOR_MAP_MAX_ELEMENT_CNT);

	if (!zcbor_check_error(zse)) {
		printk("Failed to encode SMP echo packet, err: %d\n", zcbor_pop_error(zse));
		return -EFAULT;
	}

	payload_len = (size_t)(zse->payload - smp_cmd.payload);

	smp_cmd.header.op = 2; /* Write */
	smp_cmd.header.flags = 0;
	smp_cmd.header.len_h8 = (uint8_t)((payload_len >> 8) & 0xFF);
	smp_cmd.header.len_l8 = (uint8_t)((payload_len >> 0) & 0xFF);
	smp_cmd.header.group_h8 = 0;
	smp_cmd.header.group_l8 = 0; /* OS */
	smp_cmd.header.seq = 0;
	smp_cmd.header.id  = 0; /* ECHO */

	return bt_dfu_smp_command(dfu_smp, smp_echo_rsp_proc,
				  sizeof(smp_cmd.header) + payload_len,
				  &smp_cmd);
}

static void button_reset(bool state)
{
	
	if (state) {
		printk("Reset command\n");
		static unsigned int reset_cnt;
		char buffer[32];
		int ret;

		++reset_cnt;
		printk("Reset test: %d\n", reset_cnt);
		snprintk(buffer, sizeof(buffer), "Reset message: %u", reset_cnt);
		//ret = send_smp_reset(&dfu_smp, buffer);
		//ret = send_smp_list(&dfu_smp, buffer);
		ret = send_upload(&dfu_smp);
		if (ret) {
			printk("Reset command send error (err: %d)\n", ret);
		}
	}
}


static void button_echo(bool state)
{
	if (state) {
		static unsigned int echo_cnt;
		char buffer[32];
		int ret;

		++echo_cnt;
		printk("Echo test: %d\n", echo_cnt);
		snprintk(buffer, sizeof(buffer), "Echo message: %u", echo_cnt);
		ret = send_smp_echo(&dfu_smp, buffer);
		if (ret) {
			printk("Echo command send error (err: %d)\n", ret);
		}
	}
}


static void button_handler(uint32_t button_state, uint32_t has_changed)
{
	if (has_changed & KEY_ECHO_MASK) {
		button_echo(button_state & KEY_ECHO_MASK);
	}
	if(has_changed & KEY_RESET_MASK){
		button_reset(button_state & KEY_RESET_MASK);
	}
}



void main(void)
{
	int err;


	printk("Starting Bluetooth Central SMP Client example\n");

	bt_dfu_smp_init(&dfu_smp, &init_params);

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	printk("Bluetooth initialized\n");

	scan_init();

	err = dk_buttons_init(button_handler);
	if (err) {
		printk("Failed to initialize buttons (err %d)\n", err);
		return;
	}

	err = bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE);
	if (err) {
		printk("Scanning failed to start (err %d)\n", err);
		return;
	}

	printk("Scanning successfully started\n");
}
