/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/shell/shell.h>

#include "saadc.h"

static int cmd_saadc_sample(const struct shell *sh, size_t argc, char **argv)
{
	int err;
	struct saadc_sample sample;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	err = saadc_sample(&sample);
	if (err) {
		shell_error(sh, "saadc_sample, error: %d", err);
		return err;
	}

	shell_print(sh, "raw: %d, voltage: %d mV", sample.raw, sample.millivolts);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_saadc,
	SHELL_CMD(sample, NULL, "Sample the SAADC and print the result", cmd_saadc_sample),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(saadc, &sub_saadc, "SAADC commands", NULL);
