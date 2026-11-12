# SAADC threshold crossing monitor on nRF9251

Monitors the SAADC input AIN1 (P1.1) on the application core and reports threshold crossings,
like a low-power comparator (LPCOMP, which the nRF9251 does not have). A shell on VCOM0 (115200)
controls it.

    west build -p -b nrf9251dk/nrf9251/cpuapp

## How it works

- RTC130 (32.768 kHz clock) triggers each SAADC sample through DPPI (`nrfx_gppi`), without the CPU.
  The same DPPI channel also clears the RTC, so COMPARE[0] repeats every period. No interrupt is
  enabled for the RTC.
- The SAADC writes into a one-sample buffer that END restarts into by shortcut, so there are no
  buffer interrupts.
- Only the SAADC limit events (LIMITH/LIMITL) interrupt the CPU. The ISR re-arms the limits so
  that an interrupt is raised only when the input crosses the threshold, not while it stays on one
  side:
  - Input below: only LIMITH (threshold + hysteresis/2) is armed.
  - Input above: only LIMITL (threshold - hysteresis/2) is armed.
  - After start the state is unknown and both are armed. The first sample outside the hysteresis
    band sets the initial state.

## Shell

Values are raw 12-bit SAADC units.

    saadc_limit start <threshold> <hysteresis> <period_ms>
    saadc_limit status
    saadc_limit stop

`status` shows the state (unknown/below/above), the live value in the result buffer and the
number of upward and downward crossings since start.

## Choosing the input pin

The pin is selected by the AIN index used in `saadc_limit_init()` (`NRFX_ANALOG_EXTERNAL_AIN1`).
The index to pin mapping is defined per chip by `SAADC_EXTERNAL_AIN_PSELS` in
`modules/hal/nordic/nrfx/bsp/stable/soc/nrfx_soc_defines.h`. For the nRF9251 (nRF9220 branch),
AIN0-AIN7 are P1.0-P1.7. The SAADC register itself takes a port and pin, so there is no AIN code
in the hardware.

1. Pick a pin the datasheet marks as an analog input (ADC AIN[n]).
2. Set the `SAADC_EXTERNAL_AIN` definition to the chosen AIN index (e.g., `NRFX_ANALOG_EXTERNAL_AIN1`).

The input range is 0 to 1.8 V.

## Design note: external RTC, not the SAADC internal timer

The sampling period comes from RTC130 routed over DPPI, even though the SAADC has an internal
sample timer (`internal_timer_cc`, continuous mode) that would remove the timer and the DPPI
route. The external timer is kept because:

- The internal timer interval is limited to about 128 us (about 7.8 kS/s at the shortest).
- Continuous mode keeps the SAADC sampling all the time. Triggering one sample per period lets
  the SAADC idle in between, which is the point of a low-power monitor.

The internal timer would make sense for fast, continuous sampling where power matters less.

RTC130 is used instead of TIMER130 because TIMER runs on the high-frequency clock (HFCLK), while
the RTC runs on the 32.768 kHz clock.

The shell period is in milliseconds. It is converted to RTC ticks (32768 Hz) and limited to the
24-bit counter, so it can be 1 ms up to about 512 s.
