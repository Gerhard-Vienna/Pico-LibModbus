/*
 * Copyright © Gerhard Schiller 2024, <gerhard.schiller@pm.me>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This file is part of the modbus server on a Raspberry Pi Picow.
 */

#ifndef BME280_H
#define BME280_H

void initializeBme280(void);
int32_t compensate_temp(int32_t adc_T);
uint32_t compensate_pressure(int32_t adc_P);
uint32_t compensate_humidity(int32_t adc_H);
void read_registers(uint8_t reg, uint8_t *buf, uint16_t len);
void write_register(uint8_t reg, uint8_t data);
void read_compensation_parameters();
void bme280_read_raw(int32_t *humidity, int32_t *pressure, int32_t *temperature);
float dewpoint(float t, float l); // t as deg C, l as humidity %
float absoluteHumidity(float T, float rh); // T as deg C, r as humidity %
float reducedPressure(float ph, int h); // ph as Pressure hPa, h as height m



#endif // BME280_H
