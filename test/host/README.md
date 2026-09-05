# Power-sequence fault tests

Run `python3 test/host/test_power.py` with a C compiler supporting AddressSanitizer
and UndefinedBehaviorSanitizer. No ESP board or IDF installation is needed.

The runner extracts the actual V7 power functions, PCA/TPS helpers and public
power entry points, replacing hardware and time with deterministic stubs. It
checks healthy and delayed starts, both missing power-good signals, stalled I2C,
late readiness, failure of every individual transfer (normal, Carta and VCOM
preparation), shutdown errors and legacy/unsupported API behaviour. It runs at
100 Hz and 1,000 Hz to catch tick-dependent deadlines and zero-tick yields.

This does not test physical supply timing, waveform quality or prove the rails
are off when a bus fault prevents the shutdown commands reaching the expander.
