#ifndef TWI_H
#define TWI_H

#include <avr/io.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    TW_FREQ_100K = 0,
    TW_FREQ_250K,
    TW_FREQ_400K
} twi_freq_mode_t;

void    twi_init(twi_freq_mode_t mode, bool pullup_en);

uint8_t twi_start(uint8_t addr_rw);

void    twi_stop(void);

uint8_t twi_write(uint8_t data);

uint8_t twi_read_ack(void);

uint8_t twi_read_nack(void);

#endif
