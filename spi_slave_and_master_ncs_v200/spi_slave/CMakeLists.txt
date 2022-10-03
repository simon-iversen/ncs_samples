#
# Copyright (c) 2020 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
#

cmake_minimum_required(VERSION 3.8.2)

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/spm.conf")
  set(spm_CONF_FILE
    prj.conf
    ${CMAKE_CURRENT_LIST_DIR}/spm.conf
  )
endif()

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/mcuboot.conf")
  set(mcuboot_CONF_FILE
    prj.conf
    ${CMAKE_CURRENT_LIST_DIR}/mcuboot.conf
  )
endif()

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${BOARD}.overlay")
  set(mcuboot_DTC_OVERLAY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/${BOARD}.overlay")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(empty_app_core)

target_sources(app PRIVATE src/main.c)
