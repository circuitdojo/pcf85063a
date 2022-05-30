#
# Copyright (c) 2020 Circuit Dojo LLC
#
# SPDX-License-Identifier: Apache-2.0
#

zephyr_include_directories(include)

add_subdirectory_ifdef(CONFIG_PCF85063A drivers/rtc)