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

struct k_work upload_work_item;
struct k_sem upload_sem;
K_SEM_DEFINE(upload_sem, 1, 1);

/* Buffer for response */
struct smp_buffer {
	struct bt_dfu_smp_header header;
	uint8_t payload[300];
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
	uint8_t *p_outdata = (uint8_t *)(&smp_rsp_buff);
	const struct bt_dfu_smp_rsp_state *rsp_state;

	rsp_state = bt_dfu_smp_rsp_state(dfu_smp);
	//printk("Upload response part received, size: %zu.\n",rsp_state->chunk_size);

	if (rsp_state->offset + rsp_state->chunk_size > sizeof(smp_rsp_buff)) {
		printk("Response size buffer overflow\n");
	} else {
		p_outdata += rsp_state->offset;
		memcpy(p_outdata,
		       rsp_state->data,
		       rsp_state->chunk_size);
	}

	if (bt_dfu_smp_rsp_total_check(dfu_smp)) {
		if (smp_rsp_buff.header.op != 3 /* WRITE RSP*/) {
			printk("Unexpected operation code (%u)!\n",
			       smp_rsp_buff.header.op);
			return;
		}
		uint16_t group = ((uint16_t)smp_rsp_buff.header.group_h8) << 8 |
				      smp_rsp_buff.header.group_l8;
		if (group != 1 /* Application/software image management group */) {
			printk("Unexpected command group (%u)!\n", group);
			return;
		}
		if (smp_rsp_buff.header.id != 1 /* UPLOAD */) {
			printk("Unexpected command (%u)",
			       smp_rsp_buff.header.id);
			return;
		}
		size_t payload_len = ((uint16_t)smp_rsp_buff.header.len_h8) << 8 |
				      smp_rsp_buff.header.len_l8;

		zcbor_state_t zsd[CBOR_DECODER_STATE_NUM];
		struct zcbor_string value = {0};
		bool ok;
		zcbor_new_decode_state(zsd, ARRAY_SIZE(zsd), smp_rsp_buff.payload, payload_len, 1);

		/* Stop decoding on the error. */
		zsd->constant_state->stop_on_error = true;

		ok = zcbor_map_start_decode(zsd);
		if (!ok) {
			printk("Decoding error, start_decode (err: %d)\n", zcbor_pop_error(zsd));
			return;
		} 
		
		/**
		 * 
		 * Decoding rc (status error code)
		 * 
		*/

		//Decoding rc key
		char rc_key[5];
		ok = zcbor_tstr_decode(zsd, &value);
		if (!ok) {
			printk("Decoding error, rc key (err: %d)\n", zcbor_pop_error(zsd));
			return;
		}  else if (value.len != 2) {
			printk("Invalid data received (rc key). Length %d is not equal 2\n", value.len);
			return;
		} else if(!strncmp(value.value, 'rc', 2)){
			printk("Invalid data received (rc key). String '%.2s' is not equal to 'rc'\n", value.value);
			return;
		}
		memcpy(rc_key, value.value, value.len);
		rc_key[value.len] = '\0';

		//Decoding rc value
		int32_t rc_value;
		ok = zcbor_int32_decode(zsd, &rc_value);
		if (!ok) {
			printk("Decoding error, rc value (err: %d)\n", zcbor_pop_error(zsd));
			return;
		};
		if(rc_value){
			printk("Error in image upload response: %d\n", rc_value);
			return;
		}

		//decoding offset key
		char off_key[5];
		ok = zcbor_tstr_decode(zsd, &value);
		if (!ok) {
			printk("Decoding error, offset key (err: %d)\n", zcbor_pop_error(zsd));
			return;
		} else if ((value.len != 3)) {
			printk("Invalid data received (rc key). Length %d is not equal 3\n", value.len);
			return;
		}
		//TODO: The below throws a BUS FAULT. Look into it.
		/* else if(!strncmp(value.value, 'off', 3)){
			printk("Invalid data received (offset key). String '%.3s' is not equal to 'off'\n", value.value);
			return;
		}*/

		memcpy(off_key, value.value, value.len);
		off_key[value.len] = '\0';

		//Decoding offset value
		int32_t off_val;
		ok = zcbor_int32_decode(zsd, &off_val);
		if (!ok) {
			printk("Decoding error, offset value (err: %d)\n", zcbor_pop_error(zsd));
			return;
		}
		zcbor_map_end_decode(zsd);
		if (zcbor_check_error(zsd)) {
			/* Print textual representation of the received CBOR map. */
			//printk("%s: %d\n", rc_key, rc_value);
			printk("%s: %d\n", off_key, off_val);
		} else {
			printk("Cannot print received image upload CBOR stream (err: %d)\n",
					zcbor_pop_error(zsd));
		}
		k_sem_give(&upload_sem);
	}
}

static void smp_list_rsp_proc(struct bt_dfu_smp *dfu_smp)
{
	printk("List response callback running\n");
	uint8_t *p_outdata = (uint8_t *)(&smp_rsp_buff);
	const struct bt_dfu_smp_rsp_state *rsp_state;

	rsp_state = bt_dfu_smp_rsp_state(dfu_smp);

	if (rsp_state->offset + rsp_state->chunk_size > sizeof(smp_rsp_buff)) {
		printk("Response size buffer overflow\n");
	} else {
		p_outdata += rsp_state->offset;
		memcpy(p_outdata,
		       rsp_state->data,
		       rsp_state->chunk_size);
	}
	if (bt_dfu_smp_rsp_total_check(dfu_smp)) {
		printk("All parts received\n");
		if (smp_rsp_buff.header.op != 1 /* READ RSP*/) {
			printk("Unexpected operation code (%u)!\n",
			       smp_rsp_buff.header.op);
			return;
		}else{
			printk("Operation codr correct (read rsp)\n");
		}
		uint16_t group = ((uint16_t)smp_rsp_buff.header.group_h8) << 8 |
				      smp_rsp_buff.header.group_l8;
		if (group != 1 /* Application/software image management group */) {
			printk("Unexpected command group (%u)!\n", group);
			return;
		}else{
			printk("group correct (appl/image mngmt group)\n");
		}
		if (smp_rsp_buff.header.id != 0 /* STATE */) {
			printk("Unexpected command (%u)",
			       smp_rsp_buff.header.id);
			return;
		}else{
			printk("command ID correct (state)\n");
		}
		size_t payload_len = ((uint16_t)smp_rsp_buff.header.len_h8) << 8 |
				      smp_rsp_buff.header.len_l8;
		zcbor_state_t zsd[10];
		struct zcbor_string value = {0};
		bool ok;
		zcbor_new_decode_state(zsd, ARRAY_SIZE(zsd), smp_rsp_buff.payload, payload_len, 5);
		/* Stop decoding on the error. */
		zsd->constant_state->stop_on_error = true;
		ok = zcbor_map_start_decode(zsd);
		if (!ok) {
			printk("Decoding error 1, start_decode (err: %d)\n", zcbor_pop_error(zsd));
			return;
		} 
		
		//Decoding images key
		char images_key[10];
		ok = zcbor_tstr_decode(zsd, &value);
		if (!ok) {
			printk("Decoding error 2, images key (err: %d)\n", zcbor_pop_error(zsd));
			return;
		}  /*else if (value.len != 6) {
			printk("Invalid data received (images key). Length %d is not equal 2\n", value.len);
			return;
		} else if(!strncmp(value.value, 'images', 6)){
			printk("Invalid data received (images key). String '%.2s' is not equal to 'images'\n", value.value);
			return;
		}*/
		memcpy(images_key, value.value, value.len);
		images_key[value.len] = '\0';
		printk("Images key: %s\n",images_key);
		ok = zcbor_list_start_decode(zsd);
		if (!ok) {
			printk("Decoding error, start_decode images->list  (err: %d)\n", zcbor_pop_error(zsd));
			return;
		} 
		ok = zcbor_map_start_decode(zsd);
		if (!ok) {
			printk("Decoding error, start_decode images->list->map (err: %d)\n", zcbor_pop_error(zsd));
			return;
		} 


		//Decoding slot key 
		char slot_key[5];
		ok = zcbor_tstr_decode(zsd, &value);
		if (!ok) {
			printk("Decoding error, slot key (err: %d)\n", zcbor_pop_error(zsd));
			return;
		}  /*else if (value.len != 6) {
			printk("Invalid data received (images key). Length %d is not equal 2\n", value.len);
			return;
		} else if(!strncmp(value.value, 'images', 6)){
			printk("Invalid data received (images key). String '%.2s' is not equal to 'images'\n", value.value);
			return;
		}*/
		memcpy(slot_key, value.value, value.len);
		slot_key[value.len] = '\0';
		printk("Slot key: %s\n",slot_key);

		//Decoding slot value
		int32_t slot_value;
		ok = zcbor_int32_decode(zsd, &slot_value);
		if (!ok) {
			printk("Decoding error, slot value (err: %d)\n", zcbor_pop_error(zsd));
			return;
		}
		printk("Slot value: %d\n",slot_value);


		zcbor_map_end_decode(zsd);
		zcbor_list_end_decode(zsd);
		zcbor_map_end_decode(zsd);


	}else{
		printk("All parts not received\n");
	}
}

static void smp_echo_rsp_proc(struct bt_dfu_smp *dfu_smp)
{
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

		zcbor_map_start_decode(zsd);
		
		ok = zcbor_tstr_decode(zsd, &value);

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

#define UPLOAD_CHUNK		50 //This has to be at least 32 bytes, since first it has to send the whole header (which is 32 bytes)

void send_upload2(struct k_work *item)
{
	//printk("Send upload 2 is running\n");
   	zcbor_state_t zse[2];
	size_t payload_len;
	

	

	const struct device *flash_dev;
	uint8_t data[UPLOAD_CHUNK+1]; // One more byte, to store '/0'

	flash_dev = device_get_binding("NRF_FLASH_DRV_NAME");
	//TODO: Find some smarter ways to get these
	int last_addr = 0xFBB68;
	int start_addr = 0xf6000;
	int curr_addr = 0xf6000;
	//int curr_addr = 0xf6000;
	static uint64_t image_length = 0x5B68; //last_addr - curr_addr;
	int upload_chunk = UPLOAD_CHUNK;
	int err;
	bool update_complete = false;
	while(!update_complete){
		struct smp_buffer smp_cmd;
		zcbor_new_encode_state(zse, ARRAY_SIZE(zse), smp_cmd.payload,
			       sizeof(smp_cmd.payload), 0);
		//Wait until response is received until sending the next chunk
		//printk("Before upload semaphore\n");
		k_sem_take(&upload_sem, K_FOREVER);
		//printk("After upload semaphore\n");
		//If the whole binary is sent, then break this loop

		/**
		 * UPLOAD_CHUNK = 3
		 * curr_addr = 0
		 * last_addr = 7 (should be the last byte to read (which is 'H' in our case))
		 * upload_chunk = UPLOAD_CHUNK
		 * Memory: ABC DEF GH
		 * 
		 * while(curr_addr =< last_addr)
		 *     -Check if upload_chunk is larger than rest of mem to read
		 *     -If it is, decrease upload_chunk
		 *     -else, let upload_chunk = UPLOAD_CHUNK (propably not necessary)
		 *     flash_read(curr_addr, upload_chunk)
		 *     -Increase curr_addr by upload_chunk (the area just read)
		 * 
		 * 
		**/ 

		if(curr_addr+UPLOAD_CHUNK > last_addr){
			upload_chunk = last_addr - curr_addr;
			update_complete = true;
		}
		printk("Uploading offset %d/%d and size %d\n", curr_addr-start_addr,last_addr-start_addr, upload_chunk);
		err = flash_read(flash_dev, curr_addr, data, upload_chunk);
		if (err != 0) {
			printk("flash_read failed with error: %d\n", err);
			return;
		}

		data[upload_chunk] = '\0';
		/*printk("Sending bytes: 0x");
		for(int x = 0;x < upload_chunk+1; x++){
			printk("(%d)%x ",x,data[x]);
		}
		printk("\n");*/
		/* Stop encoding on the error. */
		zse->constant_state->stop_on_error = true;
		/*printk("Image length: %d | 0x%x\n",image_length,image_length);
		printk("Image offset: 0x%x\n", curr_addr - start_addr);
		printk("Length of data: %d\n", strlen(data));
		printk("Size of data: %d\n", sizeof(data)-1);
		printk("Change\n");
		printk("Decoding 1\n");*/
		zcbor_map_start_encode(zse, 20);
		zcbor_tstr_put_lit(zse, "image");
		zcbor_int64_put(zse, 0);
		zcbor_tstr_put_lit(zse, "data");
		zcbor_bstr_put_lit(zse, data);
		zcbor_tstr_put_lit(zse, "len");
		zcbor_uint64_put(zse, (uint64_t)0x5B68);
		zcbor_tstr_put_lit(zse, "off");
		zcbor_uint64_put(zse, curr_addr - start_addr);
		zcbor_tstr_put_lit(zse, "sha");
		zcbor_bstr_put_lit(zse, "12345");
		zcbor_tstr_put_lit(zse, "upgrade");
		zcbor_bool_put(zse, false);
		zcbor_map_end_encode(zse, 20);

		if (!zcbor_check_error(zse)) {
			printk("Failed to encode SMP test packet, err: %d\n", zcbor_pop_error(zse));
			return;
		}
		curr_addr+=upload_chunk;

		/* Stop encoding on the error. */
		/*zse->constant_state->stop_on_error = true;
		zcbor_map_start_encode(zse, 20);
		zcbor_tstr_put_lit(zse, "image");
		zcbor_int64_put(zse, 0);
		zcbor_tstr_put_lit(zse, "data");
		zcbor_bstr_put_lit(zse, data);
		zcbor_tstr_put_lit(zse, "len");
		zcbor_uint64_put(zse, 100);//image_length); //This is only needed in first packet, where "off" is 0
		zcbor_tstr_put_lit(zse, "off");
		zcbor_uint64_put(zse, 0);//curr_addr);
		zcbor_tstr_put_lit(zse, "sha");
		zcbor_bstr_put_lit(zse, "12345");
		zcbor_tstr_put_lit(zse, "upgrade");
		zcbor_bool_put(zse, false);
		zcbor_map_end_encode(zse, 20);

		if (!zcbor_check_error(zse)) {
			printk("1 Failed to encode SMP test packet, err: %d\n", zcbor_pop_error(zse));
			return 50;
		}*/
		//printk("zse->payload: %d\n", zse->payload);
		//printk("smp_cmd.payload: %d\n", smp_cmd.payload);

		payload_len = (size_t)(zse->payload - smp_cmd.payload);

		smp_cmd.header.op = 2; /* write request */
		smp_cmd.header.flags = 0;
		smp_cmd.header.len_h8 = (uint8_t)((payload_len >> 8) & 0xFF);
		smp_cmd.header.len_l8 = (uint8_t)((payload_len >> 0) & 0xFF);
		smp_cmd.header.group_h8 = 0;
		smp_cmd.header.group_l8 = 1; /* IMAGE */
		smp_cmd.header.seq = 0;
		smp_cmd.header.id  = 1; /* UPLOAD */
		/*printk("Total size of packet to send: %d\n", sizeof(smp_cmd.header) + payload_len);
		printk("Header size: %d\n", sizeof(smp_cmd.header));
		printk("Payload size: %d\n", payload_len);*/
		err = bt_dfu_smp_command(&dfu_smp, smp_upload_rsp_proc,
					sizeof(smp_cmd.header) + payload_len,
					&smp_cmd);
		if(err){
			printk("bt_dfu_smp_command failed with %d\n", err);
			return;
		}
	}
}

static int send_smp_list(struct bt_dfu_smp *dfu_smp)
{
	static struct smp_buffer smp_cmd;
	zcbor_state_t zse[CBOR_ENCODER_STATE_NUM];
	size_t payload_len;
	printk("send_smp_list\n");
	/*zcbor_new_encode_state(zse, ARRAY_SIZE(zse), smp_cmd.payload,
			       sizeof(smp_cmd.payload), 0);

	// Stop encoding on the error. 
	zse->constant_state->stop_on_error = true;

	zcbor_map_start_encode(zse, CBOR_MAP_MAX_ELEMENT_CNT);
	zcbor_tstr_put_lit(zse, "d");
	zcbor_tstr_put_term(zse, "sdf");
	zcbor_map_end_encode(zse, CBOR_MAP_MAX_ELEMENT_CNT);

	if (!zcbor_check_error(zse)) {
		printk("Failed to encode SMP list packet, err: %d\n", zcbor_pop_error(zse));
		return -EFAULT;
	}*/

	payload_len = (size_t)(zse->payload);
	printk("Before setting header in spm list\n");
	smp_cmd.header.op = 0; /* read request */
	smp_cmd.header.flags = 0;
	smp_cmd.header.len_h8 = 0;//(uint8_t)((payload_len >> 8) & 0xFF);
	smp_cmd.header.len_l8 = 0;//(uint8_t)((payload_len >> 0) & 0xFF);
	smp_cmd.header.group_h8 = 0;
	smp_cmd.header.group_l8 = 1; /* IMAGE */
	smp_cmd.header.seq = 0;
	smp_cmd.header.id  = 0; /* LIST */
	printk("Before running bt_dfu_smp in send_smp_list\n");
	return bt_dfu_smp_command(dfu_smp, smp_list_rsp_proc,
				  sizeof(smp_cmd.header),
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

static void button_upload(bool state)
{
	
	if (state) {
		printk("Upload command\n");
		int ret;

		k_work_submit(&upload_work_item);

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

static void button_image_list(bool state)
{
	if (state) {
		int ret;

		ret = send_smp_list(&dfu_smp);
		if (ret) {
			printk("Echo command send error (err: %d)\n", ret);
		}
	}
}


static void button_handler(uint32_t button_state, uint32_t has_changed)
{
	if (has_changed & KEY_ECHO_MASK) {
		button_image_list(button_state & KEY_ECHO_MASK);
	}
	if(has_changed & KEY_RESET_MASK){
		button_upload(button_state & KEY_RESET_MASK);
	}
}



void main(void)
{
	int err;


	printk("Starting Bluetooth Central SMP Client example\n");

	k_work_init(&upload_work_item, send_upload2);

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
