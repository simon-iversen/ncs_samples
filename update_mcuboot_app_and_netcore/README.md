# Update application core (main application and MCUboot) and the network core of the nRF5340

## Requirements
This sample is tested with an nRF5340DK v0.11.0 and NCS v1.9.0

## Preparations

Disable the Mass Storage feature on the device, so that it does not interfere:
```
JLinkExe -device NRF52840_XXAA -if SWD -speed 4000 -autoconnect 1 -SelectEmuBySN SEGGER_ID
J-Link>MSDDisable
Probe configured successfully.
J-Link>exit
```

This patch should be applied to _< ncs location >/bootloader/mcuboot/_ in order to be able to update the network core while NSIB is enabled (this patch is merged with NCS v2.0.0: https://github.com/nrfconnect/sdk-mcuboot/commit/6097de22bd1f1b499e599bfef618a235de7073b2)
```diff
diff --git a/boot/bootutil/src/loader.c b/boot/bootutil/src/loader.c
index d5c370c4..7b2d823b 100644
--- a/boot/bootutil/src/loader.c
+++ b/boot/bootutil/src/loader.c
@@ -926,6 +926,7 @@ boot_validated_swap_type(struct boot_loader_state *state,
         vtable = (uint32_t *)(vtable_addr);
         reset_addr = vtable[1];
 #ifdef PM_S1_ADDRESS
+        if(reset_addr < PM_CPUNET_B0N_ADDRESS){ 
         const struct flash_area *primary_fa;
         int rc = flash_area_open(flash_area_id_from_multi_image_slot(
                     BOOT_CURR_IMG(state),
@@ -942,6 +943,7 @@ boot_validated_swap_type(struct boot_loader_state *state,
             */
             return BOOT_SWAP_TYPE_NONE;
         }
+        }
 #endif /* PM_S1_ADDRESS */
     }
 #endif /* PM_S1_ADDRESS || CONFIG_SOC_NRF5340_CPUAPP */
 ```


## Update the main application
1. Build and flash the program
2. To make the update visible, make a change to the application. For example, make it print ["Test 3"](https://github.com/simon-iversen/ncs_samples/blob/master/update_mcuboot_app_and_netcore/src/main.c#L14).
3. Build again, and upload the new image using mcumgr:
```
mcumgr conn add acm0 type="serial" connstring="dev=/dev/ttyACM0,baud=115200,mtu=512"
mcumgr -c acm0 image list
mcumgr -c acm0 image upload build/zephyr/app_update.bin
mcumgr -c acm0 image list
```
:exclamation: NOTE: The dev argument may be different for you. For example in Windows it may be COM20 

5. Copy the hash of the newly uploaded image, and use it to confirm it, making the new image run at next reboot, such as:
```
mcumgr -c acm0 image confirm 2348de4f84cb19c1c2721662ad1275da5c21eca749da9b32db20d2c9dffb47c0
```

6. Then reboot the Developement Kit. MCUboot will now see the new image in the secondary slot, swap it and boot into it.

## Update MCUboot

1. Build and flash the program using west.
2. Now increment CONFIG_FW_INFO_FIRMWARE_VERSION in child_image/mcuboot/prj.conf.
3. To make the change visible, make a change to MCUBoot, for example add a log in $NRF_CONNECT_SDK/bootloader/mcuboot/boot/zephyr/main.c. 
4. Build again, and upload the new image using mcumgr:
```
mcumgr conn add acm0 type="serial" connstring="dev=/dev/ttyACM0,baud=115200,mtu=512"
mcumgr -c acm0 image list
mcumgr -c acm0 image upload build/zephyr/signed_by_mcuboot_and_b0_s1_image_update.bin
mcumgr -c acm0 image list
```
:exclamation: NOTE: The dev argument may be different for you. For example in Windows it may be COM20 

5. Copy the hash of the newly uploaded image, and use it to confirm it, making the new image run at next reboot, such as:
```
mcumgr -c acm0 image confirm 2348de4f84cb19c1c2721662ad1275da5c21eca749da9b32db20d2c9dffb47c0
```

6. Then reboot the Developement Kit. This will load the new MCUBoot image to its slot. 
7. Reboot the Developement Kit again to load using the new version of MCUBoot.

If you want to update MCUBoot multiple times, you should alternate between signed_by_mcuboot_and_b0_s1_image_update and signed_by_mcuboot_and_b0_s0_image_update.
This is so that you will be able to boot back into the already functioning MCUBoot if the one you upload is not validated correctly.


## Update the network core

1. Build and flash the program
2. To make the update visible, make a change to the network application, which is zephyr/samples/bluetooth/hci_rpmsg. For example, add the line LOG_INF("modification"); to [hci_rpmsg/main.c](https://github.com/nrfconnect/sdk-zephyr/blob/v2.7.99-ncs1/samples/bluetooth/hci_rpmsg/src/main.c#L252)
3. Build again, and upload the new image using mcumgr:
```
mcumgr conn add acm0 type="serial" connstring="dev=/dev/ttyACM0,baud=115200,mtu=512"
mcumgr -c acm0 image list
mcumgr -c acm0 image upload build/zephyr/net_core_app_update.bin
mcumgr -c acm0 image list
```
:exclamation: NOTE: The dev argument may be different for you. For example in Windows it may be COM20 

4. Copy the hash of the newly uploaded image, and use it to confirm it, making the new image run at next reboot, such as:
```
mcumgr -c acm0 image confirm 2348de4f84cb19c1c2721662ad1275da5c21eca749da9b32db20d2c9dffb47c0
```

5. Then reboot the Developement Kit. MCUboot will now see that there is a network core image in the secondary slot, and then transfer it to the network core.
