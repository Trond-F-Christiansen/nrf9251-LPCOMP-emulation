# esig: SAADC on nRF9251

Samples the SAADC (AIN1, P1.1) on the application core, with a shell on VCOM0 (115200).

    west build -p -b nrf9251dk/nrf9251/cpuapp

Shell:

    saadc sample
