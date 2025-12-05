#include "twi.h"
#include <avr/io.h>
#include <util/delay.h>
#include <stdbool.h>

static inline uint8_t twi_status(void)
{
    return TWSR0 & 0xF8;
}

/* Simple timeout-based wait: returns 0 on success, 1 on timeout */
static uint8_t twi_wait_for_twint(void)
{
    uint16_t timeout = 50000;   // tune as needed

    while (!(TWCR0 & (1 << TWINT)))
    {
        if (--timeout == 0)
        {
            return 1;  // timeout
        }
    }
    return 0;          // OK
}

/* Optional: reset TWI block after a timeout */
static void twi_reset(void)
{
    // Disable TWI
    TWCR0 = 0;
    _delay_ms(1);

    // Re-enable TWI (preserve prescaler + bit rate settings in TWSR0/TWBR0)
    TWCR0 = (1 << TWEN);
}

void twi_init(twi_freq_mode_t mode, bool pullup_en)
{
    if (pullup_en) {
        PORTC |= (1 << PC4) | (1 << PC5);
    } else {
        PORTC &= ~((1 << PC4) | (1 << PC5));
    }

    DDRC &= ~((1 << PC4) | (1 << PC5));   // SDA/SCL as inputs (open-drain)

    TWSR0 = 0x00; // prescaler = 1

    switch (mode) {
        case TW_FREQ_100K:
            TWBR0 = 72;   // ~100kHz @ 16MHz
            break;
        case TW_FREQ_250K:
            TWBR0 = 24;   // ~250kHz
            break;
        case TW_FREQ_400K:
            TWBR0 = 12;   // ~400kHz
            break;
        default:
            TWBR0 = 72;
            break;
    }

    TWCR0 = (1 << TWEN);  // enable TWI
}

uint8_t twi_start(uint8_t addr_rw)
{
    // Send START condition
    TWCR0 = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
    if (twi_wait_for_twint()) {
        twi_reset();
        return 1;  // timeout on START
    }

    // Send SLA+R/W
    TWDR0 = addr_rw;
    TWCR0 = (1 << TWINT) | (1 << TWEN);
    if (twi_wait_for_twint()) {
        twi_reset();
        return 2;  // timeout on address
    }

    uint8_t st = twi_status();

    // 0x18 = SLA+W, ACK received
    // 0x40 = SLA+R, ACK received
    if (st == 0x18 || st == 0x40) {
        return 0; // OK
    }

    // NACK or other error
    return 3;
}

void twi_stop(void)
{
    TWCR0 = (1 << TWINT) | (1 << TWEN) | (1 << TWSTO);
    // no wait here; STOP condition is generated in background
}

uint8_t twi_write(uint8_t data)
{
    TWDR0 = data;
    TWCR0 = (1 << TWINT) | (1 << TWEN);

    if (twi_wait_for_twint()) {
        twi_reset();
        return 1;  // timeout
    }

    // 0x28 = data transmitted, ACK received
    return (twi_status() == 0x28) ? 0 : 2;
}

uint8_t twi_read_ack(void)
{
    TWCR0 = (1 << TWINT) | (1 << TWEN) | (1 << TWEA);  // ACK after read

    if (twi_wait_for_twint()) {
        twi_reset();
        return 0xFF;  // special error value
    }

    return TWDR0;
}

uint8_t twi_read_nack(void)
{
    TWCR0 = (1 << TWINT) | (1 << TWEN);  // NACK after read

    if (twi_wait_for_twint()) {
        twi_reset();
        return 0xFF;  // special error value
    }

    return TWDR0;
}
