#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "pico/time.h"

#include "AD9826_SPI.h"

void ad9826_spi_init(char clk_pin, char data_pin, char load_pin)
{
    gpio_init(clk_pin);
    gpio_init(data_pin);
    gpio_init(load_pin);

    gpio_set_dir(clk_pin, GPIO_OUT);
    gpio_set_dir(data_pin, GPIO_OUT);
    gpio_set_dir(load_pin, GPIO_OUT);

    gpio_put(clk_pin, 0);
    gpio_put(data_pin, 0);
    gpio_put(load_pin, 1);
}

void ad9826_spi_init_handle(AD9826_SPI_Handle *handle)
{ 
    handle->adcclk_pin_func = gpio_get_function(handle->adcclk_pin);    // Save previous function of ADCCLK pin to restore later if needed
    gpio_set_function(handle->adcclk_pin, GPIO_FUNC_PWM);               // Set ADCCLK pin to PWM function for ADC clock generation

    int pwm = pwm_gpio_to_slice_num(handle->adcclk_pin);    // Get PWM slice number for the ADCCLK pin

    pwm_set_wrap(pwm, 3);
    pwm_set_chan_level(pwm, (handle->adcclk_pin % 2), 2);   // GPIO % 2 to get PWM CHA or CHB
    pwm_set_clkdiv(pwm, 5);                                 // 125MHz Sys CLK / 4 Cycles / 5 -> 6.25Mhz PWM
    pwm_set_enabled(pwm, true);

    return ad9826_spi_init(handle->clk_pin, handle->data_pin, handle->load_pin);
}

void ad9826_spi_deinit_handle(AD9826_SPI_Handle *handle)
{
    int pwm = pwm_gpio_to_slice_num(handle->adcclk_pin);
    pwm_set_enabled(pwm, false);                                    // Disable PWM for ADC clock
    gpio_set_function(handle->adcclk_pin, handle->adcclk_pin_func); // Restore previous function of ADCCLK pin

    return;
}

uint16_t ad9826_read_data(char clk_pin, char data_pin, char load_pin, char addr)
{
    uint16_t result = 0;

    gpio_set_dir(data_pin, GPIO_OUT);
    gpio_put(clk_pin, 0);

    gpio_put(load_pin, 0);  // SLOAD Low, frame start
    gpio_put(data_pin, 1);  // SDATA High, indicate read op
    sleep_us(1);            // Sleep 1us
    gpio_put(clk_pin, 1);   // SCLK Rise, Sample operation bit
    sleep_us(1);            // Sleep 1us
    gpio_put(clk_pin, 0);   // SCLK Fall

    for (int i = 2; i >= 0; i--)
    {
        // 3 bit address
        gpio_put(data_pin, (1 & (addr >> i)));
        sleep_us(1);
        gpio_put(clk_pin, 1);
        sleep_us(1);
        gpio_put(clk_pin, 0);
    }

    gpio_put(data_pin, 0);
    gpio_set_dir(data_pin, GPIO_IN);
    sleep_us(1);            // Sleep 1us

    for (int i = 2; i >= 0; i--)
    {
        // 3 dummy cycles before data
        gpio_put(clk_pin, 1);
        sleep_us(1);
        gpio_put(clk_pin, 0);
        sleep_us(1);
    }

    for (int i = 8; i >= 0; i--)
    {
        // 9 bit data
        gpio_put(clk_pin, 1);           // Rise SCLK
        result |= gpio_get(data_pin);   // Sample 1 bit data
        result = result << 1;
        sleep_us(1);
        gpio_put(clk_pin, 0);
        sleep_us(1);
    }

    gpio_put(load_pin, 1);


    return result;
}

void ad9826_write_data(char clk_pin, char data_pin, char load_pin, char addr, uint16_t data)
{
    gpio_set_dir(data_pin, GPIO_OUT);
    gpio_put(clk_pin, 0);

    gpio_put(load_pin, 0);  // SLOAD Low, frame start
    gpio_put(data_pin, 0);  // SDATA Low, indicate write op
    sleep_us(1);            // Sleep 1us
    gpio_put(clk_pin, 1);   // SCLK Rise, Sample operation bit
    sleep_us(1);            // Sleep 1us
    gpio_put(clk_pin, 0);   // SCLK Fall
    sleep_us(1);            // Sleep 1us

    for (int i = 2; i >= 0; i--)
    {
        // 3 bit address
        gpio_put(data_pin, (1 & (addr >> i)));
        sleep_us(1);
        gpio_put(clk_pin, 1);
        sleep_us(1);
        gpio_put(clk_pin, 0);
    }

    gpio_put(data_pin, 0);
    sleep_us(1);            // Sleep 1us

    for (int i = 2; i >= 0; i--)
    {
        // 3 dummy cycles before data
        gpio_put(clk_pin, 1);
        sleep_us(1);
        gpio_put(clk_pin, 0);
        sleep_us(1);
    }

    for (int i = 8; i >= 0; i--)
    {
        // 9 bit data
        gpio_put(data_pin, 1 & (data >> i));
        gpio_put(clk_pin, 1);
        sleep_us(1);
        gpio_put(clk_pin, 0);
        sleep_us(1);
    }

    gpio_put(load_pin, 1);
}
