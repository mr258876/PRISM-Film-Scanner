/*
 * Copyright (c) 2026 mr258876
 * SPDX-License-Identifier: MIT
 */

#ifndef _AD9826_SPI_H_
#define _AD9826_SPI_H_

typedef struct 
{
    char adcclk_pin;
    char clk_pin;
    char data_pin;
    char load_pin;
    gpio_function_t adcclk_pin_func;
} AD9826_SPI_Handle;

void ad9826_spi_init(char clk_pin, char data_pin, char load_pin);
uint16_t ad9826_read_data(char clk_pin, char data_pin, char load_pin, char addr);
void ad9826_write_data(char clk_pin, char data_pin, char load_pin, char addr, uint16_t data);

void ad9826_spi_init_handle(AD9826_SPI_Handle *handle);
void ad9826_spi_deinit_handle(AD9826_SPI_Handle *handle);
inline uint16_t ad9826_read_data_handle(AD9826_SPI_Handle *handle, char addr) { return ad9826_read_data(handle->clk_pin, handle->data_pin, handle->load_pin, addr); };
inline void ad9826_write_data_handle(AD9826_SPI_Handle *handle, char addr, uint16_t data) { return ad9826_write_data(handle->clk_pin, handle->data_pin, handle->load_pin, addr, data); };

#endif
