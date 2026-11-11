/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/adc.h>

#include "saadc.h"

LOG_MODULE_REGISTER(saadc, CONFIG_SAADC_LOG_LEVEL);

#define SAADC_NODE DT_PATH(zephyr_user)

BUILD_ASSERT(DT_NODE_HAS_PROP(SAADC_NODE, io_channels),
	     "zephyr,user must have an io-channels property");

static const struct adc_dt_spec adc_spec = ADC_DT_SPEC_GET_BY_IDX(SAADC_NODE, 0);
static bool initialized;

int saadc_init(void)
{
	int err;

	if (!adc_is_ready_dt(&adc_spec)) {
		LOG_ERR("ADC device %s is not ready", adc_spec.dev->name);
		return -ENODEV;
	}

	err = adc_channel_setup_dt(&adc_spec);
	if (err) {
		LOG_ERR("adc_channel_setup_dt, error: %d", err);
		return err;
	}

	initialized = true;

	return 0;
}

int saadc_sample(struct saadc_sample *sample)
{
	int err;
	int16_t raw = 0;
	int32_t millivolts;
	struct adc_sequence sequence = {
		.buffer = &raw,
		.buffer_size = sizeof(raw),
	};

	if (!sample) {
		return -EINVAL;
	}

	if (!initialized) {
		return -EACCES;
	}

	err = adc_sequence_init_dt(&adc_spec, &sequence);
	if (err) {
		return err;
	}

	err = adc_read_dt(&adc_spec, &sequence);
	if (err) {
		LOG_ERR("adc_read_dt, error: %d", err);
		return err;
	}

	millivolts = raw;
	err = adc_raw_to_millivolts_dt(&adc_spec, &millivolts);
	if (err) {
		LOG_ERR("adc_raw_to_millivolts_dt, error: %d", err);
		return err;
	}

	sample->raw = raw;
	sample->millivolts = millivolts;

	LOG_DBG("Raw: %d, voltage: %d mV", raw, millivolts);

	return 0;
}
