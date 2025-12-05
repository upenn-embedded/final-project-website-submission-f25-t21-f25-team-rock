#include "rtc_ds1307.h"
#include "twi.h"
#include <stdbool.h>

void DS1307_I2C_init(void)
{
    twi_init(TW_FREQ_100K, false);
}

void time_i2c_write_single(uint8_t dev, uint8_t reg, uint8_t *data)
{
    if (twi_start((dev << 1) | 0)) return;        // SLA+W
    if (twi_write(reg))            { twi_stop(); return; }
    if (twi_write(*data))          { twi_stop(); return; }
    twi_stop();
}

void time_i2c_write_multi(uint8_t dev, uint8_t start_reg, uint8_t *buf, uint8_t len)
{
    if (twi_start((dev << 1) | 0)) return;
    if (twi_write(start_reg))      { twi_stop(); return; }
    for (uint8_t i = 0; i < len; i++) {
        if (twi_write(buf[i]))     { twi_stop(); return; }
    }
    twi_stop();
}

void time_i2c_read_single(uint8_t dev, uint8_t reg, uint8_t *data)
{
    if (twi_start((dev << 1) | 0)) return;
    if (twi_write(reg))            { twi_stop(); return; }
    if (twi_start((dev << 1) | 1)) return;        // SLA+R
    *data = twi_read_nack();
    twi_stop();
}

void time_i2c_read_multi(uint8_t dev, uint8_t start_reg, uint8_t *buf, uint8_t len)
{
    if (twi_start((dev << 1) | 0)) return;
    if (twi_write(start_reg))      { twi_stop(); return; }
    if (twi_start((dev << 1) | 1)) return;
    for (uint8_t i = 0; i < len; i++) {
        buf[i] = (i == len - 1) ? twi_read_nack() : twi_read_ack();
    }
    twi_stop();
}
