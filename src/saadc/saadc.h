/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef SAADC_H_
#define SAADC_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Result of one SAADC conversion. */
struct saadc_sample {
	/** Raw ADC value. */
	int16_t raw;
	/** Raw value converted to millivolts. */
	int32_t millivolts;
};

/**
 * @brief Set up the SAADC channel. Must be called before saadc_sample().
 *
 * @retval 0 on success.
 * @retval -ENODEV if the ADC device is not ready.
 * @return Negative errno from the ADC driver on other failures.
 */
int saadc_init(void);

/**
 * @brief Sample the SAADC channel once.
 *
 * @param[out] sample Conversion result.
 *
 * @retval 0 on success.
 * @retval -EINVAL if @p sample is NULL.
 * @retval -EACCES if saadc_init() has not succeeded.
 * @return Negative errno from the ADC driver on other failures.
 */
int saadc_sample(struct saadc_sample *sample);

#ifdef __cplusplus
}
#endif

#endif /* SAADC_H_ */
