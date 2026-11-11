/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "saadc/saadc.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
	int err;

	err = saadc_init();
	if (err) {
		LOG_ERR("saadc_init, error: %d", err);
		return err;
	}

	LOG_INF("esig started on %s, use \"saadc sample\"", CONFIG_BOARD_TARGET);

	return 0;
}
