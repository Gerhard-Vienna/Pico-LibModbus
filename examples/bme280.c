/*
 * Copyright © Gerhard Schiller 2024, <gerhard.schiller@pm.me>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This file is part of the modbus server on a Raspberry Pi Picow.
 * It contains the code to read and interpret weather data from a bme280
 * humidity/temperature/pressure sensor.
 *
 * Derived values - pressure at sealevel, absolute humidity and dewpoint
 * are calculated.
 * The code to talk to the bme280 chip is from the pico sdk,
 * modified to use i2c instead of spi.
 *
 * The original copyright notice for this code is below.
 */

 /**
  * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
  *
  * SPDX-License-Identifier: BSD-3-Clause
  */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "hardware/i2c.h"
#include "bme280.h"

 /* Example code to talk to a bme280 humidity/temperature/pressure sensor.
  *
  *   NOTE: Ensure the device is capable of being driven at 3.3v NOT 5v. The Pico
  *   GPIO (and therefore SPI) cannot be used at 5v.
  *
  *   You will need to use a level shifter on the I2C lines if you want to run the
  *   board at 5v.
  *
  *   Connections on Raspberry Pi Pico board, other boards may vary.
  *
  *   GPIO PICO_DEFAULT_I2C_SDA_PIN (on Pico this is GP4 (pin 6)) -> SDA on BMP280
  *   board
  *   GPIO PICO_DEFAULT_I2C_SCK_PIN (on Pico this is GP5 (pin 7)) -> SCL on
  *   BMP280 board
  *   3.3v (pin 36) -> VCC on BMP280 board
  *   GND (pin 38)  -> GND on BMP280 board
  *
  *   This code uses a bunch of register definitions, and some compensation code derived
  *   from the Bosch datasheet which can be found here.
  *   https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme280-ds002.pdf
  */

// device has default i2c bus address of 0x76
#define ADDR _u(0x76)

int32_t     t_fine;

uint16_t    dig_T1;
int16_t     dig_T2, dig_T3;
uint16_t    dig_P1;
int16_t     dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
uint8_t     dig_H1, dig_H3;
int8_t      dig_H6;
int16_t     dig_H2, dig_H4, dig_H5;


void initializeBme280(void)
{
#if !defined(i2c_default) || !defined(PICO_DEFAULT_I2C_SDA_PIN) || !defined(PICO_DEFAULT_I2C_SCL_PIN)
    #warning i2c / bme280_i2c example requires a board with I2C pins
    puts("Default I2C pins were not defined");
#else
    // I2C is "open drain", pull ups to keep signal high when no data is being sent
    i2c_init(i2c_default, 100 * 1000);
    gpio_set_function(PICO_DEFAULT_I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(PICO_DEFAULT_I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(PICO_DEFAULT_I2C_SDA_PIN);
    gpio_pull_up(PICO_DEFAULT_I2C_SCL_PIN);

    // See if I2C is working - interrograte the device for its I2C ID number, should be 0x60
    uint8_t id;
    read_registers(0xD0, &id, 1);
    printf("BME280 Chip-ID is 0x%x\n\n", id);

    read_compensation_parameters();

    /*
     * The datasheet says:
     *
     * 3.5.1 Weather monitoring
     * Description: Only a very low data rate is needed. Power consumption is minimal.
     * Noise of pressure  values is of no concern.
     * Humidity, pressure and temperature are monitored.
     *
     * Suggested settings for weather monitoring
     * Sensor mode:             forced mode, 1 sample / minute
     * Oversampling settings:   pressure ×1, temperature ×1, humidity ×1
     * IIR filter settings:     filter off
     *
     *
     * Register 0xF2 “ctrl_hum”
     * The “ctrl_hum” register sets the humidity data acquisition options of the device.
     * Humidity oversampling:    x1 -> 0000 0001
     *
     * Register 0xF4 “ctrl_meas”
     * The “ctrl_meas” register sets the pressure and temperature data acquisition options of the device
     * Temperature oversampling: x1 -> 001. ....
     * Preasure oversampling:    x1 -> ...0 01..
     * Mode:                 forced -> .... ..10
     *
     * Register 0xF5 “config”: leave at 0: IIR filter off, SPI disabled
     */

    write_register(0xF2, 0x1);  // set humidity oversampling
#endif
}

/* The following compensation functions are required to convert from the raw ADC
* data from the chip to something usable. Each chip has a different set of
* compensation parameters stored on the chip at point of manufacture, which are
* read from the chip at startup and used in these routines.
*/
int32_t compensate_temp(int32_t adc_T) {
    int32_t var1, var2, T;
    var1 = ((((adc_T >> 3) - ((int32_t) dig_T1 << 1))) * ((int32_t) dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t) dig_T1)) * ((adc_T >> 4) - ((int32_t) dig_T1))) >> 12) * ((int32_t) dig_T3))
    >> 14;

    t_fine = var1 + var2;
    T = (t_fine * 5 + 128) >> 8;
    return T;
}

uint32_t compensate_pressure(int32_t adc_P) {
    int32_t var1, var2;
    uint32_t p;
    var1 = (((int32_t) t_fine) >> 1) - (int32_t) 64000;
    var2 = (((var1 >> 2) * (var1 >> 2)) >> 11) * ((int32_t) dig_P6);
    var2 = var2 + ((var1 * ((int32_t) dig_P5)) << 1);
    var2 = (var2 >> 2) + (((int32_t) dig_P4) << 16);
    var1 = (((dig_P3 * (((var1 >> 2) * (var1 >> 2)) >> 13)) >> 3) + ((((int32_t) dig_P2) * var1) >> 1)) >> 18;
    var1 = ((((32768 + var1)) * ((int32_t) dig_P1)) >> 15);
    if (var1 == 0)
        return 0;

    p = (((uint32_t) (((int32_t) 1048576) - adc_P) - (var2 >> 12))) * 3125;
    if (p < 0x80000000)
        p = (p << 1) / ((uint32_t) var1);
    else
        p = (p / (uint32_t) var1) * 2;

    var1 = (((int32_t) dig_P9) * ((int32_t) (((p >> 3) * (p >> 3)) >> 13))) >> 12;
    var2 = (((int32_t) (p >> 2)) * ((int32_t) dig_P8)) >> 13;
    p = (uint32_t) ((int32_t) p + ((var1 + var2 + dig_P7) >> 4));

    return p;
}

uint32_t compensate_humidity(int32_t adc_H) {
    int32_t v_x1_u32r;
    v_x1_u32r = (t_fine - ((int32_t) 76800));
    v_x1_u32r = (((((adc_H << 14) - (((int32_t) dig_H4) << 20) - (((int32_t) dig_H5) * v_x1_u32r)) +
    ((int32_t) 16384)) >> 15) * (((((((v_x1_u32r * ((int32_t) dig_H6)) >> 10) * (((v_x1_u32r *
    ((int32_t) dig_H3))
    >> 11) + ((int32_t) 32768))) >> 10) + ((int32_t) 2097152)) *
    ((int32_t) dig_H2) + 8192) >> 14));
    v_x1_u32r = (v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) * ((int32_t) dig_H1)) >> 4));
    v_x1_u32r = (v_x1_u32r < 0 ? 0 : v_x1_u32r);
    v_x1_u32r = (v_x1_u32r > 419430400 ? 419430400 : v_x1_u32r);

    return (uint32_t) (v_x1_u32r >> 12);
}

#ifdef i2c_default
void write_register(uint8_t reg, uint8_t data) {
    uint8_t buf[2];
    buf[0] = reg;
    buf[1] = data;

    i2c_write_blocking(i2c_default, ADDR, buf, 2, false);  // true to keep master
}

void read_registers(uint8_t reg, uint8_t *buf, uint16_t len) {
    // For this particular device, we send the device the register we want to read
    // first, then subsequently read from the device. The register is auto incrementing
    // so we don't need to keep sending the register we want, just the first.

    i2c_write_blocking(i2c_default, ADDR, &reg, 1, true);  // true to keep master control of bus
    i2c_read_blocking(i2c_default, ADDR, buf, len, false); // false - finished with bus
}

/* This function reads the manufacturing assigned compensation parameters from the device */
void read_compensation_parameters() {
    uint8_t buffer[26];

    read_registers(0x88, buffer, 24);

    dig_T1 = buffer[0] | (buffer[1] << 8);
    dig_T2 = buffer[2] | (buffer[3] << 8);
    dig_T3 = buffer[4] | (buffer[5] << 8);

    dig_P1 = buffer[6] | (buffer[7] << 8);
    dig_P2 = buffer[8] | (buffer[9] << 8);
    dig_P3 = buffer[10] | (buffer[11] << 8);
    dig_P4 = buffer[12] | (buffer[13] << 8);
    dig_P5 = buffer[14] | (buffer[15] << 8);
    dig_P6 = buffer[16] | (buffer[17] << 8);
    dig_P7 = buffer[18] | (buffer[19] << 8);
    dig_P8 = buffer[20] | (buffer[21] << 8);
    dig_P9 = buffer[22] | (buffer[23] << 8);

    dig_H1 = buffer[25];

    read_registers(0xE1, buffer, 8);

    dig_H2 = buffer[0] | (buffer[1] << 8);
    dig_H3 = (int8_t) buffer[2];
    dig_H4 = buffer[3] << 4 | (buffer[4] & 0xf);
    dig_H5 = (buffer[5] >> 4) | (buffer[6] << 4);
    dig_H6 = (int8_t) buffer[7];
}

void bme280_read_raw(int32_t *humidity, int32_t *pressure, int32_t *temperature) {
    uint8_t buffer[8];

    read_registers(0xF7, buffer, 8);
    *pressure = ((uint32_t) buffer[0] << 12) | ((uint32_t) buffer[1] << 4) | (buffer[2] >> 4);
    *temperature = ((uint32_t) buffer[3] << 12) | ((uint32_t) buffer[4] << 4) | (buffer[5] >> 4);
    *humidity = (uint32_t) buffer[6] << 8 | buffer[7];
}
#endif

/*
* https://rechneronline.de/barometer/taupunkt.php
*
* Die Formel zur Berechnung ist:
* Temperatur über 0 ° C: k2=17.62, k3=243.12
* Temperatur 0 ° C oder darunter: k2=22.46, k3=272.62
* Temperatur t, Luftfeuchtigkeit l
* Taupunkt = k3*((k2*t)/(k3+t)+ln(l/100))/((k2*k3)/(k3+t)-ln(l/100))
*/

#define ln(x) log(x)

float dewpoint(float t, float l) // t as deg C, l as humidity %
{
    float dp;
    float k2, k3;

    if(t > 0.0){
        k2 = 17.62;
        k3 = 243.12;
    }
    else{
        k2 = 22.46;
        k3 = 272.62;
    }

    dp = k3*((k2*t)/(k3+t)+ln(l/100))/((k2*k3)/(k3+t)-ln(l/100));

    return dp;
}

/*
* https://carnotcycle.wordpress.com/2012/08/04/how-to-convert-relative-humidity-to-absolute-humidity/
*
* Absolute Humidity (grams/m3) =
* (6.112 × e^[(17.67 × T)/(T+243.5)] × rh × 2.1674)
* -------------------------------------------------
*                ( 273.15+T)
*/

#define const_e  2.71828

float absoluteHumidity(float T, float rh) // T as deg C, r as humidity %
{
    float aH;

    aH = 6.112 * pow(const_e, ((17.67 * T)/(T + 243.5)) ) * rh * 2.1674;
    aH /= (273.15 + T);

    return aH;
}

/*
*
* https://www.cosmos-indirekt.de/Physik-Schule/Barometrische_H%C3%B6henformel
*
* p0: Luftdruck auf Meereshöhe
* ph: Luftdruck auf Stationsniveau
* h:  Seehöhe der Station
*
* ph[hPa] = p0[hPa] * (1 - (h[m] * 0.0065 [K/m]) / 288.15 [K])^5.255
*
* umgeformt:
* p0[hPa] = ph[hPa] / (1 - (h[m] * 0.0065 [K/m]) / 288.15 [K])^5.255
*
*/

float reducedPressure(float ph, int h) // ph as Pressure hPa, h as height m
{
    float p0;

    p0 = ph / pow(1 - (((float)h * 0.0065) / 288.15), 5.255);

    return p0;
}
