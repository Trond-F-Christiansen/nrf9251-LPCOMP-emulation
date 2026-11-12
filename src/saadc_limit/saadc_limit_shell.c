/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdlib.h>
#include <zephyr/shell/shell.h>

#include "saadc_limit.h"

static int cmd_start(const struct shell *sh, size_t argc, char **argv)
{
	int err;
	long threshold = strtol(argv[1], NULL, 0);
	unsigned long hysteresis = strtoul(argv[2], NULL, 0);
	unsigned long period = strtoul(argv[3], NULL, 0);

	ARG_UNUSED(argc);

	err = saadc_limit_start((int16_t)threshold, (uint16_t)hysteresis, (uint32_t)period);
	if (err) {
		shell_error(sh, "saadc_limit_start, error: %d", err);
		return err;
	}

	shell_print(sh, "threshold: %ld, hysteresis: %lu, period: %lu ms", threshold, hysteresis,
		    period);

	return 0;
}

static int cmd_stop(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	saadc_limit_stop();
	shell_print(sh, "stopped");

	return 0;
}

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	struct saadc_limit_stats stats;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	saadc_limit_stats_get(&stats);
	static const char *const state_str[] = {
		[SAADC_LIMIT_STATE_UNKNOWN] = "unknown",
		[SAADC_LIMIT_STATE_BELOW] = "below",
		[SAADC_LIMIT_STATE_ABOVE] = "above",
	};

	shell_print(sh, "running: %s, state: %s, buffer raw: %d, crossings up: %u, down: %u",
		    (stats.running ? "yes" : "no"), state_str[stats.state], stats.last_raw,
		    stats.crossings_up, stats.crossings_down);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_saadc_limit,
	SHELL_CMD_ARG(start, NULL, "start <threshold> <hysteresis> <period_ms> (raw 12-bit units)", cmd_start,
		      4, 0),
	SHELL_CMD(stop, NULL, "Stop periodic sampling", cmd_stop),
	SHELL_CMD(status, NULL, "Show state and crossing counters", cmd_status),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(saadc_limit, &sub_saadc_limit, "SAADC limit commands", NULL);
