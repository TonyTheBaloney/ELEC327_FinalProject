#include <ti/devices/msp/msp.h>
#include <ti/driverlib/driverlib.h>
#include <ti/driverlib/m0p/dl_core.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// =============================================================================
// Board / Peripheral Definitions
// =============================================================================

#define CPUCLK_FREQ 32000000U

#define I2C_0_INST I2C0
#define I2C_0_INST_INT_IRQN I2C0_INT_IRQn

#define I2C_1_INST I2C1
#define I2C_1_INST_INT_IRQN I2C1_INT_IRQn

// I2C0 — PA0=SCL, PA1=SDA
#define I2C0_SCL_IOMUX IOMUX_PINCM1 // PINCM value from table
#define I2C0_SDA_IOMUX IOMUX_PINCM2
#define I2C0_SDA_PINCM_PF IOMUX_PINCM1_PF_I2C0_SDA // PF3
#define I2C0_SCL_PINCM_PF IOMUX_PINCM2_PF_I2C0_SCL // PF3

// I2C1 — PA17=SCL, PA18=SDA
#define I2C1_SCL_IOMUX IOMUX_PINCM18 // verify these in table
#define I2C1_SDA_IOMUX IOMUX_PINCM19
#define I2C1_SCL_PINCM_PF IOMUX_PINCM16_PF_I2C1_SCL // 0x3
#define I2C1_SDA_PINCM_PF IOMUX_PINCM17_PF_I2C1_SDA // 0x4 ← critical

// Guard against redefining delay_cycles if the SDK already defines it
#ifndef delay_cycles
#define delay_cycles(n) DL_Common_delayCycles(n)
#endif

#define CYCLES_PER_MS (CPUCLK_FREQ / 1000U)
#define POWER_STARTUP_DELAY 16U

// =============================================================================
// Config & Data Structures
// =============================================================================

#define LCD_ADDR 0x28
#define NUM_EFFECTS 5
#define NUM_PARAMS 4
#define PEDAL_DATA_SIZE 5U

typedef struct __attribute__((packed))
{
    uint8_t effectID;
    uint8_t pot0;
    uint8_t pot1;
    uint8_t pot2;
    uint8_t pot3;
} PedalData;

static const char *effectNames[NUM_EFFECTS] = {
    "EQ", "Funk", "Ambient", "Lead", "HiGain"};

static const char *paramNames[NUM_EFFECTS][NUM_PARAMS] = {
    {"Level", "Bass", "Mid", "Treble"},
    {"Volume", "Wah Depth", "Comp Mix", "Rev Mix"},
    {"Volume", "Delay Time", "Rev Level", "Chorus"},
    {"Volume", "Gain", "Wet Mix", "Gate Thr"},
    {"Volume", "Gain", "Drive", "Tone"},
};

// =============================================================================
// I2C0 Peripheral Init — Controller (Master) -> LCD at 50 KHz
// =============================================================================

static void I2C0_Controller_Init(void)
{
    DL_GPIO_initPeripheralInputFunctionFeatures(
        I2C0_SDA_IOMUX,
        I2C0_SDA_PINCM_PF | IOMUX_PINCM_PC_CONNECTED | IOMUX_PINCM_INENA_ENABLE,
        DL_GPIO_INVERSION_DISABLE,
        DL_GPIO_RESISTOR_NONE,
        DL_GPIO_HYSTERESIS_DISABLE,
        DL_GPIO_WAKEUP_DISABLE);

    DL_GPIO_initPeripheralInputFunctionFeatures(
        I2C0_SCL_IOMUX,
        I2C0_SCL_PINCM_PF | IOMUX_PINCM_PC_CONNECTED | IOMUX_PINCM_INENA_ENABLE,
        DL_GPIO_INVERSION_DISABLE,
        DL_GPIO_RESISTOR_NONE,
        DL_GPIO_HYSTERESIS_DISABLE,
        DL_GPIO_WAKEUP_DISABLE);

    DL_I2C_selectClockSource(I2C_0_INST, DL_I2C_CLOCK_BUSCLK);
    DL_I2C_selectClockDivider(I2C_0_INST, DL_I2C_CLOCK_DIVIDE_1);
    DL_I2C_setTimerPeriod(I2C_0_INST, 63); // 50kHz
    DL_I2C_enableControllerClockStretching(I2C_0_INST);

    // Set trigger levels — no flush needed after clean reset
    I2C_0_INST->MASTER.MFIFOCTL =
        I2C_MFIFOCTL_TXTRIG_LEVEL_1 |
        I2C_MFIFOCTL_RXTRIG_LEVEL_1;

    DL_I2C_enableController(I2C_0_INST);
}

// =============================================================================
// I2C1 Peripheral Init — Target (Slave) <- Daisy Seed
// =============================================================================

static void I2C1_Target_Init(void)
{
    DL_GPIO_initPeripheralInputFunctionFeatures(
        I2C1_SCL_IOMUX,
        IOMUX_PINCM16_PF_I2C1_SCL | IOMUX_PINCM_PC_CONNECTED | IOMUX_PINCM_INENA_ENABLE,
        DL_GPIO_INVERSION_DISABLE,
        DL_GPIO_RESISTOR_NONE,
        DL_GPIO_HYSTERESIS_DISABLE,
        DL_GPIO_WAKEUP_DISABLE);

    DL_GPIO_initPeripheralInputFunctionFeatures(
        I2C1_SDA_IOMUX,
        IOMUX_PINCM17_PF_I2C1_SDA | IOMUX_PINCM_PC_CONNECTED | IOMUX_PINCM_INENA_ENABLE,
        DL_GPIO_INVERSION_DISABLE,
        DL_GPIO_RESISTOR_NONE,
        DL_GPIO_HYSTERESIS_DISABLE,
        DL_GPIO_WAKEUP_DISABLE);

    DL_I2C_selectClockSource(I2C_1_INST, DL_I2C_CLOCK_BUSCLK);
    DL_I2C_selectClockDivider(I2C_1_INST, DL_I2C_CLOCK_DIVIDE_1);

    // Set and enable our own address — Daisy Seed must write to this address
    DL_I2C_setTargetOwnAddress(I2C_1_INST, 0x48);
    DL_I2C_enableTargetOwnAddress(I2C_1_INST);

    // Clock stretching: hold SCL low if ISR hasn't drained the RX FIFO yet
    DL_I2C_enableTargetClockStretching(I2C_1_INST);

    // RX_DONE fires per byte; STOP fires at end of transaction
    // Both are needed — see IRQ handler comments for the reasoning
    DL_I2C_enableInterrupt(
        I2C_1_INST,
        DL_I2C_INTERRUPT_TARGET_RX_DONE |
            DL_I2C_INTERRUPT_TARGET_STOP);

    DL_I2C_enableTarget(I2C_1_INST);
}

// =============================================================================
// Board Init
// =============================================================================

static void MyBoard_Init(void)
{
    DL_SYSCTL_setSYSOSCFreq(SYSCTL_CLKSTATUS_SYSOSCFREQ_SYSOSC32M);

    // Step 1: Enable power to all peripherals first
    GPIOA->GPRCM.PWREN = GPIO_PWREN_KEY_UNLOCK_W | GPIO_PWREN_ENABLE_ENABLE;
    I2C0->GPRCM.PWREN = I2C_PWREN_KEY_UNLOCK_W | I2C_PWREN_ENABLE_ENABLE;
    I2C1->GPRCM.PWREN = I2C_PWREN_KEY_UNLOCK_W | I2C_PWREN_ENABLE_ENABLE;

    // Step 2: Required delay after power enable before register access
    delay_cycles(POWER_STARTUP_DELAY);

    // Step 3: Now safe to reset
    GPIOA->GPRCM.RSTCTL =
        (GPIO_RSTCTL_KEY_UNLOCK_W | GPIO_RSTCTL_RESETSTKYCLR_CLR |
         GPIO_RSTCTL_RESETASSERT_ASSERT);
    I2C0->GPRCM.RSTCTL =
        (I2C_RSTCTL_KEY_UNLOCK_W | I2C_RSTCTL_RESETSTKYCLR_CLR |
         I2C_RSTCTL_RESETASSERT_ASSERT);
    I2C1->GPRCM.RSTCTL =
        (I2C_RSTCTL_KEY_UNLOCK_W | I2C_RSTCTL_RESETSTKYCLR_CLR |
         I2C_RSTCTL_RESETASSERT_ASSERT);

    delay_cycles(POWER_STARTUP_DELAY);

    GPIOA->GPRCM.PWREN = GPIO_PWREN_KEY_UNLOCK_W | GPIO_PWREN_ENABLE_ENABLE;
    I2C0->GPRCM.PWREN = I2C_PWREN_KEY_UNLOCK_W | I2C_PWREN_ENABLE_ENABLE;
    I2C1->GPRCM.PWREN = I2C_PWREN_KEY_UNLOCK_W | I2C_PWREN_ENABLE_ENABLE;

    // Step 4: Configure
    I2C0_Controller_Init();
    I2C1_Target_Init();
}

// =============================================================================
// I2C0 LCD Hardware Driver
// =============================================================================

static bool I2C_SendBytes(uint8_t *data, uint16_t length)
{
    uint16_t i;
    uint32_t status;

    DL_I2C_startControllerTransfer(
        I2C_0_INST, LCD_ADDR, DL_I2C_CONTROLLER_DIRECTION_TX, length);

    for (i = 0; i < length; i++)
    {
        while (DL_I2C_isControllerTXFIFOFull(I2C_0_INST))
        {
            status = DL_I2C_getControllerStatus(I2C_0_INST);
            if (status & (DL_I2C_CONTROLLER_STATUS_ERROR |
                          DL_I2C_CONTROLLER_STATUS_ARBITRATION_LOST))
            {
                return false;
            }
        }
        DL_I2C_transmitControllerData(I2C_0_INST, data[i]);
    }

    // Wait for IDLE not BUSY_BUS — BUSY_BUS clears while FIFO still drains
    while (!(DL_I2C_getControllerStatus(I2C_0_INST) &
             DL_I2C_CONTROLLER_STATUS_IDLE))
    {
        status = DL_I2C_getControllerStatus(I2C_0_INST);
        if (status & (DL_I2C_CONTROLLER_STATUS_ERROR |
                      DL_I2C_CONTROLLER_STATUS_ARBITRATION_LOST))
        {
            return false;
        }
    }

    // DL_I2C_CONTROLLER_STATUS_ADDR_NACK is not in this SDK version —
    // DL_I2C_CONTROLLER_STATUS_ERROR covers address NACK and data NACK
    if (DL_I2C_getControllerStatus(I2C_0_INST) &
        (DL_I2C_CONTROLLER_STATUS_ERROR |
         DL_I2C_CONTROLLER_STATUS_ARBITRATION_LOST))
    {
        return false;
    }
    return true;
}

static void LCD_SendCommand(uint8_t cmd, uint32_t delay_ms)
{
    uint8_t buffer[2] = {0xFE, cmd};
    I2C_SendBytes(buffer, 2);
    delay_cycles(delay_ms * CYCLES_PER_MS);
}

static void LCD_Print(const char *text)
{
    (void)I2C_SendBytes((uint8_t *)text, (uint16_t)strlen(text));
}

// NHD PIC command: 0xFE 0x45 [pos]  (datasheet Table of Commands)
// pos=0x00 for line 1, pos=0x40 for line 2
static void LCD_SetCursor(uint8_t row)
{
    uint8_t pos = (row == 0) ? 0x00 : 0x40;
    uint8_t buf[3] = {0xFE, 0x45, pos};
    (void)I2C_SendBytes(buf, 3);
    delay_cycles(1 * CYCLES_PER_MS);
}

// =============================================================================
// I2C1 Receive State (double-buffered to close STOP/copy race)
// =============================================================================

static volatile uint8_t rxBuffer[PEDAL_DATA_SIZE];    // in-flight packet
static volatile uint8_t readyBuffer[PEDAL_DATA_SIZE]; // last complete packet
static volatile uint8_t rxCount = 0;
static volatile bool dataReady = false;

// =============================================================================
// UI Update
// =============================================================================

static void DisplayPedalData(const PedalData *d)
{
    char line1[17];
    char line2[17];
    int p0_pct;
    int p1_pct;

    if (d->effectID >= NUM_EFFECTS)
        return;

    snprintf(line1, sizeof(line1), "[%-10s]", effectNames[d->effectID]);

    p0_pct = (d->pot0 * 100) / 255;
    p1_pct = (d->pot1 * 100) / 255;

    // %-3.3s truncates label to 3 chars so two cells fit exactly 16 columns:
    // 3(label) + 1(:) + 3(digits) + 1(%) = 8 chars per cell, 16 total
    snprintf(line2, sizeof(line2), "%-3.3s:%03d%%%-3.3s:%03d%%",
             paramNames[d->effectID][0], p0_pct,
             paramNames[d->effectID][1], p1_pct);

    LCD_SetCursor(0);
    LCD_Print(line1);
    LCD_SetCursor(1);
    LCD_Print(line2);
}

// =============================================================================
// I2C1 IRQ Handler
// =============================================================================

void I2C1_IRQHandler(void)
{
    switch (DL_I2C_getPendingInterrupt(I2C_1_INST))
    {

    case DL_I2C_IIDX_TARGET_RX_DONE:
        // Accumulate bytes into rxBuffer — do NOT set dataReady here.
        // A packet is only valid once STOP confirms the transaction ended.
        if (rxCount < PEDAL_DATA_SIZE)
        {
            rxBuffer[rxCount++] = DL_I2C_receiveTargetData(I2C_1_INST);
        }
        else
        {
            (void)DL_I2C_receiveTargetData(I2C_1_INST);
        }
        break;

    case DL_I2C_IIDX_TARGET_STOP:
        // Atomically promote rxBuffer -> readyBuffer only for full packets.
        // Short packets (e.g. address probes) are silently dropped so the
        // last good display values are preserved.
        if (rxCount == PEDAL_DATA_SIZE)
        {
            memcpy((void *)readyBuffer, (const void *)rxBuffer,
                   PEDAL_DATA_SIZE);
            dataReady = true;
        }
        rxCount = 0;
        break;

    default:
        break;
    }
}

// =============================================================================
// Main
// =============================================================================

int main(void)

{
    PedalData snapshot;

    MyBoard_Init();

    // NHD datasheet: 100ms power-on settle before first command
    delay_cycles(100 * CYCLES_PER_MS);

    LCD_SendCommand(0x41, 1); // Display on   (exec time: 100us)
    LCD_SendCommand(0x51, 2); // Clear screen  (exec time: 1.5ms)

    LCD_Print("Waiting for");
    LCD_SetCursor(1);
    LCD_Print("Daisy Seed...");

    // Enable I2C1 interrupts — done here so the target only starts responding
    // after the LCD is initialised and the startup message is shown
    NVIC_EnableIRQ(I2C_1_INST_INT_IRQN);

    while (1)
    {

        if (dataReady)
        {
            dataReady = false;  // clear before processing to avoid missing next packet
            PedalData data;
            memcpy(&data, (const void *)readyBuffer, PEDAL_DATA_SIZE);
            __enable_irq();

            DisplayPedalData(&snapshot);
        }
        __WFI();
    }
}