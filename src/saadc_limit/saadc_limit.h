/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef SAADC_LIMIT_H_
#define SAADC_LIMIT_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief State of the monitored input relative to the threshold. */
enum saadc_limit_state {
	/** No sample outside the hysteresis band has been seen since start. */
	SAADC_LIMIT_STATE_UNKNOWN,
	/** Input is below the threshold (below threshold - hysteresis/2 after the last crossing). */
	SAADC_LIMIT_STATE_BELOW,
	/** Input is above the threshold (above threshold + hysteresis/2 after the last crossing). */
	SAADC_LIMIT_STATE_ABOVE,
};

/** @brief Crossing counters and the last conversion result of the monitoring. */
struct saadc_limit_stats {
	/** Number of upward crossings since start. */
	uint32_t crossings_up;
	/** Number of downward crossings since start. */
	uint32_t crossings_down;
	/** Current state of the input. */
	enum saadc_limit_state state;
	/** Raw value currently in the result buffer (live read). */
	int16_t last_raw;
	/** True while periodic sampling is running. */
	bool running;
};

/**
 * @brief Initialize the nrfx SAADC driver, configure AIN1 and run offset calibration.
 *
 * @retval 0 on success.
 * @return Negative errno on failure.
 */
int saadc_limit_init(void);

/**
 * @brief Start periodic sampling and report threshold crossings, like a comparator.
 *
 * An interrupt is raised only when the input crosses the threshold, not while it stays on one
 * side. The upward crossing level is @p threshold + @p hysteresis / 2 and the downward crossing
 * level is @p threshold - @p hysteresis / 2.
 *
 * @param threshold  Raw 12-bit threshold value.
 * @param hysteresis Total width of the hysteresis band in raw units, 0 for none.
 * @param period_ms  Sampling period in milliseconds.
 *
 * @retval 0 on success.
 * @retval -EINVAL if @p period_ms is zero.
 * @return Negative errno on other failures.
 */
int saadc_limit_start(int16_t threshold, uint16_t hysteresis, uint32_t period_ms);

/** @brief Stop periodic sampling. */
void saadc_limit_stop(void);

/**
 * @brief Get a snapshot of the counters.
 *
 * @param[out] stats Snapshot.
 */
void saadc_limit_stats_get(struct saadc_limit_stats *stats);

#ifdef __cplusplus
}
#endif

#endif /* SAADC_LIMIT_H_ */
