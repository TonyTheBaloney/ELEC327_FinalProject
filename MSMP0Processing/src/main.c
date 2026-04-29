#include "ti_msp_dl_config.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef CPUCLK_FREQ
#define CPUCLK_FREQ 32000000u
#endif

#ifndef I2C_LCD_INST
#define I2C_LCD_INST I2C_0_INST
#endif

#ifndef I2C_DAISY_INST
#define I2C_DAISY_INST I2C_1_INST
#endif

#ifndef I2C_DAISY_INST_INT_IRQN
#define I2C_DAISY_INST_INT_IRQN I2C_1_INST_INT_IRQN
#endif

#ifndef I2C_DAISY_INST_IRQHandler
#define I2C_DAISY_INST_IRQHandler I2C_1_INST_IRQHandler
#endif

#define DAISY_I2C_TARGET_ADDRESS 0x42u
#define LCD_I2C_ADDRESS 0x27u

#define LCD_COLUMNS 16u
#define LCD_ROWS 2u
#define LCD_BACKLIGHT 0x08u
#define LCD_ENABLE 0x04u
#define LCD_RS 0x01u

#define I2C_TIMEOUT_CYCLES 100000u
#define DISPLAY_PAGE_MS 1200u
#define LOOP_DELAY_MS 25u

typedef struct __attribute__((packed)) {
    uint8_t effectID;
    uint8_t pot0;
    uint8_t pot1;
    uint8_t pot2;
    uint8_t pot3;
} PedalData;

static const char *const effectNames[] = {
    "EQ",
    "Funk",
    "Ambient",
    "Lead",
    "HiGain",
};

static const char *const paramNames[5][4] = {
    {"Lvl", "Bass", "Mid", "Treb"},
    {"Vol", "Wah", "Comp", "Rev"},
    {"Vol", "Dly", "Rev", "Chor"},
    {"Vol", "Gain", "Wet", "Gate"},
    {"Vol", "Gain", "Drv", "Tone"},
};

static volatile uint8_t rxBuffer[sizeof(PedalData)];
static volatile uint8_t rxIndex;
static volatile bool frameReady;
static volatile PedalData latestFrame;

static void delay_us(uint32_t us)
{
    while (us-- > 0u) {
        delay_cycles(CPUCLK_FREQ / 1000000u);
    }
}

static void delay_ms(uint32_t ms)
{
    while (ms-- > 0u) {
        delay_cycles(CPUCLK_FREQ / 1000u);
    }
}

static uint8_t pot_percent(uint8_t pot)
{
    return (uint8_t) (((uint16_t) pot * 100u + 127u) / 255u);
}

static bool i2c_lcd_write(const uint8_t *data, uint16_t count)
{
    uint32_t timeout = I2C_TIMEOUT_CYCLES;

    while ((DL_I2C_getControllerStatus(I2C_LCD_INST) &
            DL_I2C_CONTROLLER_STATUS_BUSY_BUS) != 0u) {
        if (timeout-- == 0u) {
            return false;
        }
    }

    DL_I2C_flushControllerTXFIFO(I2C_LCD_INST);

    uint16_t loaded = DL_I2C_fillControllerTXFIFO(
        I2C_LCD_INST, (uint8_t *) data, count);

    DL_I2C_startControllerTransfer(
        I2C_LCD_INST, LCD_I2C_ADDRESS, DL_I2C_CONTROLLER_DIRECTION_TX, count);

    for (uint16_t i = loaded; i < count; ++i) {
        timeout = I2C_TIMEOUT_CYCLES;
        while (DL_I2C_isControllerTXFIFOFull(I2C_LCD_INST)) {
            if (timeout-- == 0u) {
                return false;
            }
        }
        DL_I2C_transmitControllerData(I2C_LCD_INST, data[i]);
    }

    timeout = I2C_TIMEOUT_CYCLES;
    while ((DL_I2C_getControllerStatus(I2C_LCD_INST) &
            DL_I2C_CONTROLLER_STATUS_BUSY_BUS) != 0u) {
        if (timeout-- == 0u) {
            return false;
        }
    }

    return (DL_I2C_getControllerStatus(I2C_LCD_INST) &
            DL_I2C_CONTROLLER_STATUS_ERROR) == 0u;
}

static void lcd_expander_write(uint8_t value)
{
    (void) i2c_lcd_write(&value, 1u);
}

static void lcd_pulse_enable(uint8_t value)
{
    lcd_expander_write(value | LCD_ENABLE);
    delay_us(1u);
    lcd_expander_write(value & (uint8_t) ~LCD_ENABLE);
    delay_us(50u);
}

static void lcd_write4(uint8_t highNibble, bool rs)
{
    uint8_t value = (highNibble & 0xF0u) | LCD_BACKLIGHT;
    if (rs) {
        value |= LCD_RS;
    }
    lcd_pulse_enable(value);
}

static void lcd_send(uint8_t value, bool rs)
{
    lcd_write4(value, rs);
    lcd_write4((uint8_t) (value << 4), rs);
}

static void lcd_command(uint8_t command)
{
    lcd_send(command, false);
    if (command == 0x01u || command == 0x02u) {
        delay_ms(2u);
    }
}

static void lcd_data(uint8_t data)
{
    lcd_send(data, true);
}

static void lcd_set_cursor(uint8_t row, uint8_t column)
{
    static const uint8_t rowOffsets[] = {0x00u, 0x40u, 0x14u, 0x54u};

    if (row >= LCD_ROWS) {
        row = LCD_ROWS - 1u;
    }
    if (column >= LCD_COLUMNS) {
        column = LCD_COLUMNS - 1u;
    }

    lcd_command((uint8_t) (0x80u | (rowOffsets[row] + column)));
}

static void lcd_write_line(uint8_t row, const char *text)
{
    uint8_t i = 0u;

    lcd_set_cursor(row, 0u);
    while (i < LCD_COLUMNS && text[i] != '\0') {
        lcd_data((uint8_t) text[i]);
        ++i;
    }
    while (i < LCD_COLUMNS) {
        lcd_data((uint8_t) ' ');
        ++i;
    }
}

static void lcd_init(void)
{
    delay_ms(50u);

    lcd_write4(0x30u, false);
    delay_ms(5u);
    lcd_write4(0x30u, false);
    delay_us(150u);
    lcd_write4(0x30u, false);
    delay_us(150u);
    lcd_write4(0x20u, false);

    lcd_command(0x28u);
    lcd_command(0x08u);
    lcd_command(0x01u);
    lcd_command(0x06u);
    lcd_command(0x0Cu);
}

static bool pedal_data_is_valid(const PedalData *data)
{
    return data->effectID < (sizeof(effectNames) / sizeof(effectNames[0]));
}

static void display_pedal_data(const PedalData *data, uint8_t page)
{
    char line1[LCD_COLUMNS + 1u];
    char line2[LCD_COLUMNS + 1u];
    const uint8_t effect = data->effectID;
    const uint8_t pots[] = {data->pot0, data->pot1, data->pot2, data->pot3};
    const uint8_t a = (page == 0u) ? 0u : 2u;
    const uint8_t b = (uint8_t) (a + 1u);

    snprintf(line1, sizeof(line1), "Mode:%-10s", effectNames[effect]);
    snprintf(line2, sizeof(line2), "%-4s%3u %-4s%3u",
        paramNames[effect][a], pot_percent(pots[a]),
        paramNames[effect][b], pot_percent(pots[b]));

    lcd_write_line(0u, line1);
    lcd_write_line(1u, line2);
}

static void display_boot_screen(void)
{
    lcd_write_line(0u, "Waiting Daisy");
    lcd_write_line(1u, "I2C addr 0x42");
}

static void daisy_i2c_target_init(void)
{
    rxIndex = 0u;
    frameReady = false;

    DL_I2C_flushTargetRXFIFO(I2C_DAISY_INST);
    DL_I2C_setTargetOwnAddress(I2C_DAISY_INST, DAISY_I2C_TARGET_ADDRESS);
    DL_I2C_setTargetAddressingMode(
        I2C_DAISY_INST, DL_I2C_TARGET_ADDRESSING_MODE_7_BIT);
    DL_I2C_enableTargetOwnAddress(I2C_DAISY_INST);
    DL_I2C_enableTargetClockStretching(I2C_DAISY_INST);
    DL_I2C_setTargetRXFIFOThreshold(
        I2C_DAISY_INST, DL_I2C_RX_FIFO_LEVEL_BYTES_1);
    DL_I2C_clearInterruptStatus(I2C_DAISY_INST,
        DL_I2C_INTERRUPT_TARGET_START |
        DL_I2C_INTERRUPT_TARGET_STOP |
        DL_I2C_INTERRUPT_TARGET_RX_DONE |
        DL_I2C_INTERRUPT_TARGET_RXFIFO_TRIGGER |
        DL_I2C_INTERRUPT_TARGET_RXFIFO_FULL |
        DL_I2C_INTERRUPT_TARGET_RXFIFO_OVERFLOW);
    DL_I2C_enableInterrupt(I2C_DAISY_INST,
        DL_I2C_INTERRUPT_TARGET_START |
        DL_I2C_INTERRUPT_TARGET_STOP |
        DL_I2C_INTERRUPT_TARGET_RX_DONE |
        DL_I2C_INTERRUPT_TARGET_RXFIFO_TRIGGER |
        DL_I2C_INTERRUPT_TARGET_RXFIFO_FULL |
        DL_I2C_INTERRUPT_TARGET_RXFIFO_OVERFLOW);

    NVIC_EnableIRQ(I2C_DAISY_INST_INT_IRQN);
    DL_I2C_enableTarget(I2C_DAISY_INST);
}

static void drain_daisy_rx_fifo(void)
{
    while (!DL_I2C_isTargetRXFIFOEmpty(I2C_DAISY_INST)) {
        const uint8_t byte = DL_I2C_receiveTargetData(I2C_DAISY_INST);

        if (rxIndex < sizeof(rxBuffer)) {
            rxBuffer[rxIndex] = byte;
            ++rxIndex;
        }
    }
}

void I2C_DAISY_INST_IRQHandler(void)
{
    DL_I2C_IIDX pending;

    while ((pending = DL_I2C_getPendingInterrupt(I2C_DAISY_INST)) !=
           DL_I2C_IIDX_NO_INT) {
        switch (pending) {
        case DL_I2C_IIDX_TARGET_START:
            rxIndex = 0u;
            break;

        case DL_I2C_IIDX_TARGET_RX_DONE:
        case DL_I2C_IIDX_TARGET_RXFIFO_TRIGGER:
        case DL_I2C_IIDX_TARGET_RXFIFO_FULL:
            drain_daisy_rx_fifo();
            break;

        case DL_I2C_IIDX_TARGET_STOP:
            drain_daisy_rx_fifo();
            if (rxIndex == sizeof(PedalData)) {
                memcpy((void *) &latestFrame, (const void *) rxBuffer,
                    sizeof(PedalData));
                frameReady = true;
            }
            rxIndex = 0u;
            break;

        case DL_I2C_IIDX_TARGET_RXFIFO_OVERFLOW:
            DL_I2C_flushTargetRXFIFO(I2C_DAISY_INST);
            rxIndex = 0u;
            break;

        default:
            break;
        }
    }
}

int main(void)
{
    PedalData current = {0u, 128u, 128u, 128u, 128u};
    bool haveFrame = false;
    uint8_t page = 0u;
    uint32_t elapsedMs = 0u;

    SYSCFG_DL_init();
    DL_I2C_enableController(I2C_LCD_INST);
    daisy_i2c_target_init();

    lcd_init();
    display_boot_screen();

    while (1) {
        bool updateNow = false;

        if (frameReady) {
            PedalData received;

            __disable_irq();
            memcpy(&received, (const void *) &latestFrame, sizeof(received));
            frameReady = false;
            __enable_irq();

            if (pedal_data_is_valid(&received)) {
                current = received;
                haveFrame = true;
                page = 0u;
                elapsedMs = 0u;
                updateNow = true;
            }
        }

        if (haveFrame && elapsedMs >= DISPLAY_PAGE_MS) {
            updateNow = true;
            elapsedMs = 0u;
            page ^= 1u;
        }

        if (updateNow && haveFrame) {
            display_pedal_data(&current, page);
        }

        delay_ms(LOOP_DELAY_MS);
        elapsedMs += LOOP_DELAY_MS;
    }
}
