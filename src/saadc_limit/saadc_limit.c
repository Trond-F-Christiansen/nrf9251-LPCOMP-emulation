/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * SAADC limit monitoring where the CPU is not part of the sampling loop and does not see any
 * buffer events:
 *
 *   RTC130 COMPARE[0] --(nrfx_gppi / DPPI)--> SAADC TASKS_SAMPLE
 *                     \--> RTC130 TASKS_CLEAR (restarts the period)
 *
 * RTC130 runs on the 32.768 kHz clock, so no high-frequency clock is needed for the sample timing.
 *
 * The SAADC result buffer is a single sample. END is shortcut to START, so the hardware keeps
 * overwriting the same sample without any interrupt. Only the LIMITH and LIMITL events raise an
 * interrupt, and the ISR reads and prints the sample that is in the buffer at that time.
 *
 * The limits are re-armed from the ISR so that the module behaves like a comparator (LPCOMP):
 * an interrupt is raised only when the input crosses the threshold, not while it stays on one
 * side. While the input is below, only LIMITH (threshold + hysteresis/2) is armed. While it is
 * above, only LIMITL (threshold - hysteresis/2) is armed.
 *
 * nrfx is used for the setup (init, channel, calibration, input/resolution configuration). The
 * running phase is driven directly through the nrfx SAADC HAL, because the nrfx driver always
 * enables the STARTED/END interrupts and needs buffers supplied from the CPU.
 */

#include <errno.h>
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <nrfx_saadc.h>
#include <nrfx_rtc.h>
#include <helpers/nrfx_gppi.h>
#include <dmm.h>

#include "saadc_limit.h"

LOG_MODULE_REGISTER(saadc_limit, CONFIG_SAADC_LIMIT_LOG_LEVEL);


/* SAADC pin from datasheet gpio maping, NRFX_ANALOG_EXTERNAL_AIN0-AIN7 = P1.0 - P1.7 */
#define SAADC_EXTERNAL_AIN NRFX_ANALOG_EXTERNAL_AIN1
#define SAADC_NODE DT_NODELABEL(adc)
#define SAADC_CHANNEL 0
#define SAADC_CHANNEL_MASK BIT(SAADC_CHANNEL)
#define LIMIT_INT_MASK (NRF_SAADC_INT_CH0LIMITH | NRF_SAADC_INT_CH0LIMITL)
#define HW_TIMEOUT_US 2000

static nrfx_rtc_t rtc = NRFX_RTC_INSTANCE(NRF_RTC130);
static nrfx_gppi_handle_t gppi_handle;

/* One sample, placed in the DMA-accessible RAM region of the adc node. Written by EasyDMA. */
static nrf_saadc_value_t sample_buf DMM_MEMORY_SECTION(SAADC_NODE);

static atomic_t running;
static bool initialized;
static atomic_t crossings_up;
static atomic_t crossings_down;
static atomic_t state;

/* Crossing levels, set before the SAADC interrupt is enabled and read only by the ISR. */
static int16_t arm_low;
static int16_t arm_high;

static const char *const state_str[] = {
	[SAADC_LIMIT_STATE_UNKNOWN] = "unknown",
	[SAADC_LIMIT_STATE_BELOW] = "below",
	[SAADC_LIMIT_STATE_ABOVE] = "above",
};

/**
 * @brief Read the current SAADC sample from the buffer.
 *
 * This function reads the value from the DMA-accessible buffer where the SAADC stores its result.
 *
 * @return int16_t raw sample value from the SAADC buffer
 */
static inline int16_t sample_read(void)
{
	return *(volatile nrf_saadc_value_t *)&sample_buf;
}

static int16_t clamp_i16(int32_t value)
{
	return (int16_t)CLAMP(value, INT16_MIN, INT16_MAX);
}

/**
 * @brief Program the SAADC limits for the given state.
 *
 * Only the limit that can end the current state is armed, the other one is disabled by setting it
 * to the end of the range. The unknown state arms both, so the first sample outside the
 * hysteresis band decides the initial state.
 *
 * @param s Current state of the input.
 */
static void limits_arm(enum saadc_limit_state s)
{
	int16_t low = INT16_MIN;
	int16_t high = INT16_MAX;

	if (s != SAADC_LIMIT_STATE_ABOVE) {
		high = arm_high;
	}

	if (s != SAADC_LIMIT_STATE_BELOW) {
		low = arm_low;
	}

	nrf_saadc_channel_limits_set(NRF_SAADC, SAADC_CHANNEL, low, high);
}

/**
 * @brief SAADC interrupt service routine for handling LIMITH and LIMITL events.
 *
 * Each event is a threshold crossing (or the initial state). The limits are re-armed for the new
 * state before the events are cleared, so no further interrupt is raised until the input crosses
 * back.
 *
 * @param arg Unused argument, required by the IRQ_CONNECT macro.
 */
static void saadc_isr(const void *arg)
{
	nrf_saadc_event_t event_high = nrf_saadc_limit_event_get(SAADC_CHANNEL,
								 NRF_SAADC_LIMIT_HIGH);
	nrf_saadc_event_t event_low = nrf_saadc_limit_event_get(SAADC_CHANNEL,
								NRF_SAADC_LIMIT_LOW);
	enum saadc_limit_state current = atomic_get(&state);
	enum saadc_limit_state next = current;

	ARG_UNUSED(arg);

	if (nrf_saadc_event_check(NRF_SAADC, event_high)) {
		next = SAADC_LIMIT_STATE_ABOVE;
	} else if (nrf_saadc_event_check(NRF_SAADC, event_low)) {
		next = SAADC_LIMIT_STATE_BELOW;
	}

	if (next != current) {
		limits_arm(next);
		atomic_set(&state, next);

		if (current == SAADC_LIMIT_STATE_UNKNOWN) {
			LOG_INF("Initial state: %s, buffer raw %d", state_str[next], sample_read());
		} else if (next == SAADC_LIMIT_STATE_ABOVE) {
			atomic_inc(&crossings_up);
			LOG_INF("UP crossing, buffer raw %d", sample_read());
		} else {
			atomic_inc(&crossings_down);
			LOG_INF("DOWN crossing, buffer raw %d", sample_read());
		}
	}

	nrf_saadc_event_clear(NRF_SAADC, event_high);
	nrf_saadc_event_clear(NRF_SAADC, event_low);
}

/**
 * @brief Busy-wait loop for the specified SAADC event. Used on start / stop commands to verify
 * event occurrence. Busy-waits instead of interrupt-driven as start/stop events are intentionally
 * not included in the SAADC interrupt mask.
 *
 * @param event The SAADC event to wait for
 * @return true if the event occurred within the timeout
 * @return false if the event did not occur within the timeout
 */
static bool event_wait(nrf_saadc_event_t event)
{
	for (uint32_t i = 0; i < HW_TIMEOUT_US; i++) {
		if (nrf_saadc_event_check(NRF_SAADC, event)) {
			return true;
		}
		k_busy_wait(1);
	}

	return false;
}

/**
 * @brief Dummy handler for the SAADC advanced mode events.
 * This handler is never called because the actual SAADC interrupt is serviced by saadc_isr().
 *
 * @param event The SAADC event (unused)
 * @note This function is provided to satisfy the nrfx_saadc_advanced_mode_set API, but it is
 * never called.
 */
static void dummy_handler(const nrfx_saadc_evt_t *event)
{
	ARG_UNUSED(event);
}

/**
 * @brief Dummy handler for the RTC driver, which requires one. No RTC interrupt is enabled.
 *
 * @param event_type RTC event (unused)
 * @param p_context  Context (unused)
 * @note This function is provided to satisfy the nrfx_rtc_init API, but it is never called.
 */
static void rtc_dummy_handler(nrf_rtc_event_t event_type, void *p_context)
{
	ARG_UNUSED(event_type);
	ARG_UNUSED(p_context);
}

/**
 * @brief Initialize the SAADC limit module.
 *
 * This function sets up the SAADC channel, advanced mode, RTC, and GPPI connection required
 * for the SAADC limit functionality. It must be called before using saadc_limit_start().
 *
 * @return int 0 on success, negative error code on failure
 */
int saadc_limit_init(void)
{
	int err;

	nrfx_saadc_channel_t channel = NRFX_SAADC_DEFAULT_CHANNEL_SE(SAADC_EXTERNAL_AIN,
								      SAADC_CHANNEL);
	nrfx_saadc_adv_config_t adv_config = NRFX_SAADC_DEFAULT_ADV_CONFIG;
	nrfx_rtc_config_t rtc_cfg = NRFX_RTC_DEFAULT_CONFIG;

	if (initialized) {
		return -EALREADY;
	}

	err = nrfx_saadc_init(0);
	if (err) {
		LOG_ERR("nrfx_saadc_init, error: %d", err);
		return err;
	}

	IRQ_CONNECT(DT_IRQN(SAADC_NODE), DT_IRQ(SAADC_NODE, priority), saadc_isr, NULL, 0);

	/*
	 * Internal reference voltage is 1.2V, so gain 1/2 gives 0-2.4V range, which covers the
	 * 0-1.8V input range of the gpios.
	 */
	channel.channel_config.gain = NRF_SAADC_GAIN1_2;

	err = nrfx_saadc_channels_config(&channel, 1);
	if (err) {
		LOG_ERR("nrfx_saadc_channels_config, error: %d", err);
		return err;
	}

	err = nrfx_saadc_offset_calibrate(NULL);
	if (err) {
		LOG_WRN("nrfx_saadc_offset_calibrate, error: %d", err);
	}

	/*
	 * Let nrfx write the channel input, resolution and oversampling registers once. Its
	 * interrupt mask and buffer handling are replaced by this module at start time.
	 */
	err = nrfx_saadc_advanced_mode_set(SAADC_CHANNEL_MASK, NRF_SAADC_RESOLUTION_12BIT,
					   &adv_config, dummy_handler);
	if (err) {
		LOG_ERR("nrfx_saadc_advanced_mode_set, error: %d", err);
		return err;
	}

	nrf_saadc_int_set(NRF_SAADC, 0);

	/* RTC130 triggers the SAADC samples */
	rtc_cfg.prescaler = NRF_RTC_FREQ_TO_PRESCALER(NRF_RTC_INPUT_FREQ);
	err = nrfx_rtc_init(&rtc, &rtc_cfg, rtc_dummy_handler);
	if (err) {
		LOG_ERR("nrfx_rtc_init, error: %d", err);
		return err;
	}

	/* Allocate GPPI channel for RTC130 COMPARE0 -> SAADC SAMPLE connection */
	err = nrfx_gppi_conn_alloc(nrfx_rtc_event_address_get(&rtc, NRF_RTC_EVENT_COMPARE_0),
				   nrf_saadc_task_address_get(NRF_SAADC, NRF_SAADC_TASK_SAMPLE),
				   &gppi_handle);
	if (err) {
		LOG_ERR("nrfx_gppi_conn_alloc, error: %d", err);
		return err;
	}

	/* The same DPPI channel also clears the RTC, which makes COMPARE[0] periodic. */
	err = nrfx_gppi_ep_attach(nrfx_rtc_task_address_get(&rtc, NRF_RTC_TASK_CLEAR),
				  gppi_handle);
	if (err) {
		LOG_ERR("nrfx_gppi_ep_attach, error: %d", err);
		return err;
	}

	LOG_INF("RTC130 COMPARE0 -> SAADC SAMPLE and RTC CLEAR GPPI connection allocated");

	initialized = true;

	return 0;
}

/**
 * @brief Start periodic SAADC sampling and monitor threshold crossings.
 *
 * @param threshold  Raw 12-bit threshold value.
 * @param hysteresis Total width of the hysteresis band in raw units, 0 for none.
 * @param period_ms  Sampling period in milliseconds.
 * @return int  0 on success, negative errno on failure.
 */
int saadc_limit_start(int16_t threshold, uint16_t hysteresis, uint32_t period_ms)
{
	int err;
	uint64_t ticks = ((uint64_t)period_ms * NRF_RTC_INPUT_FREQ + 500U) / 1000U;

	if (!initialized) {
		return -EACCES;
	}

	/* nrfx_rtc_cc_set() wraps values above the 24-bit counter, so reject them here. */
	if (period_ms == 0 || ticks > NRF_RTC_COUNTER_MAX) {
		return -EINVAL;
	}

	saadc_limit_stop();

	arm_low = clamp_i16((int32_t)threshold - hysteresis / 2);
	arm_high = clamp_i16((int32_t)threshold + hysteresis / 2);
	atomic_set(&crossings_up, 0);
	atomic_set(&crossings_down, 0);
	atomic_set(&state, SAADC_LIMIT_STATE_UNKNOWN);
	limits_arm(SAADC_LIMIT_STATE_UNKNOWN);

	nrf_saadc_event_clear(NRF_SAADC, NRF_SAADC_EVENT_STARTED);
	nrf_saadc_event_clear(NRF_SAADC, NRF_SAADC_EVENT_STOPPED);
	nrf_saadc_event_clear(NRF_SAADC, nrf_saadc_limit_event_get(SAADC_CHANNEL,
								   NRF_SAADC_LIMIT_HIGH));
	nrf_saadc_event_clear(NRF_SAADC, nrf_saadc_limit_event_get(SAADC_CHANNEL,
								   NRF_SAADC_LIMIT_LOW));

	/* One sample buffer, reused forever: END restarts the SAADC into the same buffer. */
	nrf_saadc_buffer_init(NRF_SAADC, &sample_buf, 1);
	nrf_saadc_enable(NRF_SAADC);
	nrf_saadc_shorts_enable(NRF_SAADC, NRF_SAADC_SHORT_END_START_MASK);
	nrf_saadc_int_set(NRF_SAADC, LIMIT_INT_MASK);
	nrf_saadc_task_trigger(NRF_SAADC, NRF_SAADC_TASK_START);

	if (!event_wait(NRF_SAADC_EVENT_STARTED)) {
		LOG_ERR("SAADC did not report STARTED");
		saadc_limit_stop();
		return -ETIMEDOUT;
	}

	err = nrfx_rtc_cc_set(&rtc, 0, (uint32_t)ticks, false);
	if (err) {
		LOG_ERR("nrfx_rtc_cc_set, error: %d", err);
		saadc_limit_stop();
		return err;
	}

	atomic_set(&running, true);
	nrfx_gppi_conn_enable(gppi_handle);
	nrfx_rtc_counter_clear(&rtc);
	nrfx_rtc_enable(&rtc);

	return 0;
}

/**
 * @brief Stop periodic SAADC sampling and disable the monitoring.
 *
 * This function stops the RTC, disables the GPPI connection, and stops the SAADC.
 * It is safe to call even if the sampling was not started or if the start failed.
 */
void saadc_limit_stop(void)
{
	if (!atomic_get(&running)) {
		/* Also covers the failed start path: make sure nothing is left enabled. */
		nrf_saadc_int_set(NRF_SAADC, 0);
		nrf_saadc_shorts_disable(NRF_SAADC, NRF_SAADC_SHORT_END_START_MASK);
		if (nrf_saadc_enable_check(NRF_SAADC)) {
			nrf_saadc_task_trigger(NRF_SAADC, NRF_SAADC_TASK_STOP);
			(void)event_wait(NRF_SAADC_EVENT_STOPPED);
			nrf_saadc_disable(NRF_SAADC);
		}
		return;
	}

	atomic_set(&running, false);

	nrfx_rtc_disable(&rtc);
	nrfx_gppi_conn_disable(gppi_handle);

	nrf_saadc_int_set(NRF_SAADC, 0);
	nrf_saadc_shorts_disable(NRF_SAADC, NRF_SAADC_SHORT_END_START_MASK);
	nrf_saadc_event_clear(NRF_SAADC, NRF_SAADC_EVENT_STOPPED);
	nrf_saadc_task_trigger(NRF_SAADC, NRF_SAADC_TASK_STOP);
	if (!event_wait(NRF_SAADC_EVENT_STOPPED)) {
		LOG_WRN("SAADC did not report STOPPED");
	}

	nrf_saadc_disable(NRF_SAADC);
}

/**
 * @brief Get a snapshot of the SAADC monitoring statistics.
 *
 * This function fills the provided structure with the current number of upward and downward
 * threshold crossings, the last raw sample, the current state relative to the threshold, and
 * whether periodic sampling is running.
 *
 * @param stats Pointer to the structure to be filled with the snapshot.
 */
void saadc_limit_stats_get(struct saadc_limit_stats *stats)
{
	stats->crossings_up = atomic_get(&crossings_up);
	stats->crossings_down = atomic_get(&crossings_down);
	stats->state = atomic_get(&state);
	stats->last_raw = sample_read();
	stats->running = atomic_get(&running);
}
