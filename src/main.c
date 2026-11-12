/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "saadc_limit/saadc_limit.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
	int err;

	err = saadc_limit_init();
	if (err) {
		LOG_ERR("saadc_limit_init, error: %d", err);
		return err;
	}

	LOG_INF("application started on %s, use \"saadc_limit start <threshold> <hysteresis> <ms>\"",
		CONFIG_BOARD_TARGET);

	return 0;
}
