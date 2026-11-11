#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

target_sources(app PRIVATE saadc.c)
target_sources_ifdef(CONFIG_SAADC_SHELL app PRIVATE saadc_shell.c)
target_include_directories(app PRIVATE ..)
