#!/usr/bin/env python3
"""Host fault tests for the actual V7 power functions and I2C helpers.

Only IDF/hardware calls are replaced. Build at both common FreeRTOS tick rates;
this cannot prove physical supply timing or shutdown with a broken I2C bus.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
def without_includes(text):
    return re.sub(r'^#include.*$', '', text, flags=re.M)
def region(path, start, end):
    text = (ROOT/path).read_text()
    return text[text.index(start):text.index(end)]

source = (ROOT/'test/host/power_test.c').read_text()
helpers = without_includes((ROOT/'src/board/pca9555.c').read_text())
helpers += region('src/board/tps65185.c', 'static uint8_t i2c_master_read_slave', 'int8_t tps_read_thermistor').replace('i2c_master_read_slave', 'tps_read_slave').replace('i2c_master_write_slave', 'tps_write_slave')
board = region('src/board/epd_board_v7.c', 'static esp_err_t epd_board_set_ctrl_checked', 'static float epd_board_ambient_temperature')
api = region('src/epdiy.c', 'esp_err_t epd_poweron_checked', 'void epd_init(')
source = source.replace('// INJECT_HELPERS', helpers).replace('// INJECT_BOARD', board).replace('// INJECT_API', api)
with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp)/'power.c'
    cpp.write_text(source)
    for hz in (100, 1000):
        exe = Path(tmp)/f'power-{hz}'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-function', '-fsanitize=address,undefined',
                        f'-DTICK_HZ={hz}', '-I', str(ROOT/'src/board'),
                        str(cpp), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
