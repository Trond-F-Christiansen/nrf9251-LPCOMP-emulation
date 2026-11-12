#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

target_sources(app PRIVATE saadc_limit.c saadc_limit_shell.c)
target_include_directories(app PRIVATE ..)
