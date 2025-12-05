#define F_CPU 16000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "UART.h"
#include "twi.h"
#include "rtc_ds1307.h"
#include "vl53_port.h"     // XSHUT_M, XSHUT_R, tof_*, vl53_*
#include "MotorControl.h"  // MotorPWM_Init, Motor_SetSpeedPercent

#define DIST_MIN_MM       30   
#define DIST_MAX_MM       900 
#define DIFF_THR_MM        80

// Sample period is ~200 ms -> 25 samples ? 5 seconds
#define BAD_COUNT_THRESHOLD  25

static void print_u8(const char *k, uint8_t v)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%s=%u ", k, v);
    UART0_SendString(buf);
}

static uint16_t absdiff_u16(uint16_t a, uint16_t b)
{
    return (a > b) ? (a - b) : (b - a);
}

static bool is_valid_distance(uint16_t d)
{
    if (d == 0 || d == 0xFFFF || d >= 8190) return false;  // sensor invalid
    if (d < DIST_MIN_MM)  return false;
    if (d > DIST_MAX_MM)  return false;
    return true;
}

static bool is_posture_ok(uint16_t dM, uint16_t dR)
{
    if (!is_valid_distance(dM)) return false;
    if (!is_valid_distance(dR)) return false;

    uint16_t diff = absdiff_u16(dM, dR);
    if (diff > DIFF_THR_MM) return false;

    return true;
}

static void ToF_ReinitBoth(void)
{
    UART0_SendString("Reinitializing both ToF sensors...\r\n");

    tof_all_shutdown();
    _delay_ms(10);

    tof_release_one(1 << XSHUT_M);
    _delay_ms(10);

    if (vl53_change_address(VL53_ADDR_M) != 0) {
        UART0_SendString("ToF M addr change FAIL in reinit\r\n");
    } else if (vl53_init_and_start(VL53_ADDR_M) != 0) {
        UART0_SendString("ToF M init FAIL in reinit\r\n");
    } else {
        UART0_SendString("ToF M reinit OK\r\n");
    }

    tof_release_one(1 << XSHUT_R);
    _delay_ms(10);

    if (vl53_change_address(VL53_ADDR_R) != 0) {
        UART0_SendString("ToF R addr change FAIL in reinit\r\n");
    } else if (vl53_init_and_start(VL53_ADDR_R) != 0) {
        UART0_SendString("ToF R init FAIL in reinit\r\n");
    } else {
        UART0_SendString("ToF R reinit OK\r\n");
    }
}


int main(void)
{
    UART0_Init();
    UART1_Init();          // for ESP32 (USART1)
    UART0_SendString("\r\n SYSTEM STARTS \r\n");

    MotorPWM_Init();
    Motor_SetSpeedPercent(0);  // ensure motor off

    // ---------- I2C ----------
    twi_init(TW_FREQ_100K, 0);   // 100 kHz on TWI

    // ---------- RTC ----------
    DS1307_Init();
    rtc_time_t newTime;

    // Set initial time once
    newTime.hour = 12;   // 24-hour format
    newTime.min  = 0;
    newTime.sec  = 0;
    newTime.day  = 1;    // day of month (1?31)
    newTime.mon  = 1;    // 1=Jan, 2=Feb, ...
    newTime.year = 25;   // last two digits (2025 -> 25)

    DS1307_SetTime(&newTime);
    UART0_SendString("RTC init OK\r\n");

    tof_gpio_init();        // configure XSHUT pins as outputs
    tof_all_shutdown();     // pull all XSHUT low -> all sensors in reset
    _delay_ms(10);

    // Sensor M: bring up first, move from 0x29 -> 0x2A
    tof_release_one(1 << XSHUT_M);    // only M comes out of reset
    _delay_ms(10);                  

    if (vl53_change_address(VL53_ADDR_M) != 0)
    {
        UART0_SendString("ToF M address change FAILED\r\n");
        while (1) { }
    }

    if (vl53_init_and_start(VL53_ADDR_M) != 0)
    {
        UART0_SendString("ToF M init FAILED\r\n");
        while (1) { }
    }
    UART0_SendString("ToF M ready at 0x2A\r\n");

    //Sensor R: bring up second, move from 0x29 -> 0x2B
    tof_release_one(1 << XSHUT_R);
    _delay_ms(10);

    if (vl53_change_address(VL53_ADDR_R) != 0)
    {
        UART0_SendString("ToF R address change FAILED\r\n");
        while (1) { }
    }

    if (vl53_init_and_start(VL53_ADDR_R) != 0)
    {
        UART0_SendString("ToF R init FAILED\r\n");
        while (1) { }
    }
    UART0_SendString("ToF R ready at 0x2B\r\n");

    UART0_SendString("Both ToF sensors init OK, starting measurements...\r\n");

    char buf[128];
    rtc_time_t t;
    rtc_time_t t1;
    uint8_t bad_count = 0;
    bool motor_on = false;
    while (1)
    {
        // ---------- Read RTC ----------
while (1)
{
    DS1307_GetTime(&t1);
    DS1307_GetTime(&t);

    // 1) Basic range sanity
    if (t.sec > 59 || t.min > 59 || t.hour > 23) {
        // clearly garbage, retry
        continue;
    }

    // 2) Allowable transitions:
    //    - same minute, seconds moved forward by 0 or 1
    //    - normal minute rollover: t1.sec == 59, t.sec == 0, and minute advanced by 0 or 1
    bool ok = false;

    if (t.min == t1.min) {
        // same minute, OK if seconds didn't jump backwards or by a crazy amount
        if (t.sec == t1.sec || t.sec == (uint8_t)(t1.sec + 1)) {
            ok = true;
        }
    } else if (t.min == (uint8_t)(t1.min + 1)) {
        // minute advanced by exactly 1
        if (t1.sec == 59 && t.sec <= 1) {
            // we read across a rollover: 00:59 -> 01:00/01
            ok = true;
        }
    }

    if (ok) {
        break;  // t is good
    }
    volatile bool testt = false;
    testt = true;

    // otherwise: loop and read again
}

        
        
        
        if(t.min >= 1) 
        { 
            UART0_SendString("Starting Music for sitting too long\r\n"); 
            // start music 
            UART1_SendChar('a');
            // reset when user gets up, for now we are just testing for 10s 
            // Set new desired time 
            rtc_time_t newTime = t; 
            newTime.min = 0; 
            newTime.sec = 0; 
            DS1307_SetTime(&newTime); 
            while(newTime.sec < 10) 
            {
                UART0_SendString("user sitting\r\n"); 
                _delay_ms(1000); 
                DS1307_GetTime(&newTime); 
            } 
            newTime.sec = 0; 
            DS1307_SetTime(&newTime);
            UART0_SendString("User got up..\r\n"); 
            UART1_SendChar('b');
        }
        
        print_u8("H", t.hour);
        print_u8("M", t.min);
        print_u8("S", t.sec);

        uint16_t dM = vl53_read_mm(VL53_ADDR_M);
        uint16_t dR = vl53_read_mm(VL53_ADDR_R);
        
        // if both sensors look dead, try reinit
        static uint8_t both_zero_count = 0;

        if ((dM == 0 || dM == 0xFFFF || dM >= 8190) &&
            (dR == 0 || dR == 0xFFFF || dR >= 8190))
        {
            if (both_zero_count < 50) both_zero_count++;  // about 10 seconds at 200 ms
        } 
        else 
        {
            both_zero_count = 0;
        }

        if (both_zero_count >= 25)  // ~5 seconds of both dead
        {
            Motor_SetSpeedPercent(0);   // be safe, stop motor while we reset sensors
            ToF_ReinitBoth();
            both_zero_count = 0;
        }


        bool validM = is_valid_distance(dM);
        bool validR = is_valid_distance(dR);
        bool posture_ok = is_posture_ok(dM, dR);

        uint16_t diff = absdiff_u16(dM, dR);

        snprintf(buf, sizeof(buf),
                 " M=%u%s  R=%u%s  diff=%u  posture=%s\r\n",
                 dM, validM ? "" : " (INV)",
                 dR, validR ? "" : " (INV)",
                 diff,
                 posture_ok ? "OK" : "BAD");
        UART0_SendString(buf);

        if (posture_ok) {
            bad_count = 0;
            if (motor_on) {
                UART1_SendChar('b');
                Motor_SetSpeedPercent(0);
                motor_on = false;
                UART0_SendString("Posture OK -> motor OFF\r\n");
            }
        } else {
            if (bad_count < 255) bad_count++;
            if (!motor_on && bad_count >= BAD_COUNT_THRESHOLD) {
                Motor_SetSpeedPercent(100);   // vibrate
                motor_on = true;
                UART0_SendString("Posture BAD for a while -> motor ON\r\n");
                UART1_SendChar('a');
            }
        }

        _delay_ms(200);   // ~5 samples per second
    }
}
