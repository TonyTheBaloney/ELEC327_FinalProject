#include <ti/devices/msp/msp.h>
#include <ti/driverlib/driverlib.h>
#include <ti/driverlib/m0p/dl_core.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// Board / Peripheral Definitions
#define CPUCLK_FREQ 32000000U

#define I2C_0_INST I2C0

#define I2C_1_INST I2C1

#define MSPM0_ADDR 0x48
// I2C0 — PA0=SCL, PA1=SDA
#define I2C0_SDA_IOMUX IOMUX_PINCM1
#define I2C0_SCL_IOMUX IOMUX_PINCM2                // PINCM value from table
#define I2C0_SDA_PINCM_PF IOMUX_PINCM1_PF_I2C0_SDA // PF3
#define I2C0_SCL_PINCM_PF IOMUX_PINCM2_PF_I2C0_SCL // PF3

// I2C1 — PA17=SCL, PA18=SDA
#define I2C1_SCL_IOMUX IOMUX_PINCM16 // verify these in table
#define I2C1_SDA_IOMUX IOMUX_PINCM17
#define I2C1_SCL_PINCM_PF IOMUX_PINCM16_PF_I2C1_SCL // 0x3
#define I2C1_SDA_PINCM_PF IOMUX_PINCM17_PF_I2C1_SDA // 0x4 ← critical

// Guard against redefining delay_cycles if the SDK already defines it
#ifndef delay_cycles
#define delay_cycles(n) DL_Common_delayCycles(n)
#endif

#define CYCLES_PER_MS (CPUCLK_FREQ / 1000U)
#define POWER_STARTUP_DELAY 16U

// Config & Data Structures
#define LCD_ADDR 0x28
#define NUM_EFFECTS 6
#define NUM_PARAMS 4
#define PEDAL_DATA_SIZE 6U

#define TOGGLE_PASSTHROUGH_MASK (1 << 0)
#define TOGGLE_EDITING_MASK (1 << 1)

typedef struct __attribute__((packed))
{
    uint8_t effectID;
    uint8_t pot0;
    uint8_t pot1;
    uint8_t pot2;
    uint8_t pot3;
    uint8_t states; // Bitfield for toggle states.
} PedalData;

static const char *effectNames[NUM_EFFECTS] = {
    "EQ", "Funk", "Ambient", "Lead", "HiGain", "Overdrive"};

static const char *paramNames[NUM_EFFECTS][NUM_PARAMS] = {
    {"Level", "Bass", "Mid", "Treble"},
    {"Volume", "Wah Depth", "Comp Mix", "Rev Mix"},
    {"Volume", "Delay Time", "Rev Level", "Chorus"},
    {"Volume", "Gain", "Wet Mix", "Gate Thr"},
    {"Volume", "Gain", "Drive", "Tone"},
    {"Volume", "Wet", "Output", "-"}, // NeuralSeed params are custom, so just label generically
};

/**
 * Initialize I2C0 as a controller to control the LCD
 */
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

    I2C_0_INST->MASTER.MFIFOCTL =
        I2C_MFIFOCTL_TXTRIG_LEVEL_1 |
        I2C_MFIFOCTL_RXTRIG_LEVEL_1;

    DL_I2C_enableController(I2C_0_INST);
}

// I2C1 Peripheral Init Target
static void I2C1_Target_Init(void)
{
    DL_GPIO_initPeripheralInputFunctionFeatures(
        IOMUX_PINCM16,
        IOMUX_PINCM16_PF_I2C1_SCL | IOMUX_PINCM_PC_CONNECTED | IOMUX_PINCM_INENA_ENABLE,
        DL_GPIO_INVERSION_DISABLE,
        DL_GPIO_RESISTOR_NONE,
        DL_GPIO_HYSTERESIS_DISABLE,
        DL_GPIO_WAKEUP_DISABLE);

    DL_GPIO_initPeripheralInputFunctionFeatures(
        IOMUX_PINCM17,
        IOMUX_PINCM17_PF_I2C1_SDA | IOMUX_PINCM_PC_CONNECTED | IOMUX_PINCM_INENA_ENABLE,
        DL_GPIO_INVERSION_DISABLE,
        DL_GPIO_RESISTOR_NONE,
        DL_GPIO_HYSTERESIS_DISABLE,
        DL_GPIO_WAKEUP_DISABLE);

    DL_I2C_selectClockSource(I2C_1_INST, DL_I2C_CLOCK_BUSCLK);
    DL_I2C_selectClockDivider(I2C_1_INST, DL_I2C_CLOCK_DIVIDE_1);

    // Set our own address
    DL_Common_updateReg(&I2C1->SLAVE.SOAR, MSPM0_ADDR, I2C_SOAR_OAR_MASK);
    I2C1->SLAVE.SOAR |= I2C_SOAR_OAREN_ENABLE;

    // Clock stretching: hold SCL low if ISR hasn't drained the RX FIFO yet
    I2C1->SLAVE.SCTR |= I2C_SCTR_SCLKSTRETCH_ENABLE;

    I2C1->CPU_INT.IMASK |= DL_I2C_INTERRUPT_TARGET_RX_DONE |
                           DL_I2C_INTERRUPT_TARGET_STOP;

    I2C1->SLAVE.SCTR |= I2C_SCTR_ACTIVE_ENABLE;
}

// Board Init
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

/**
 * A function to send bytes through i2c
 *
 * Returns true if the transmission was successful, false if an error or arbitration loss occurred.
 */
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

// I2C1 Receive State (double-buffered to close STOP/copy race)
static volatile uint8_t rxBuffer[PEDAL_DATA_SIZE];    // in-flight packet
static volatile uint8_t readyBuffer[PEDAL_DATA_SIZE]; // last complete packet
static volatile uint8_t rxCount = 0;
static volatile bool dataReady = false;

// Display State for LCD

#define PAGE_INTERVAL_MS 2000U
#define LOCK_DURATION_MS 2000U

static uint32_t displayTimer = 0;
static uint32_t lockTimer = 0;
static bool displayLocked = false;
static uint8_t displayPage = 0; // 0 = pot1/2, 1 = pot2/3
static PedalData lastData = {0};

// Called once per ms from main loop — drives page cycling
static void DisplayTick(void)
{
    displayTimer++;

    if (displayLocked)
    {
        if (displayTimer - lockTimer >= LOCK_DURATION_MS)
            displayLocked = false;
    }
    else
    {
        // Idle: cycle between the two pages every PAGE_INTERVAL_MS
        if (displayTimer % PAGE_INTERVAL_MS == 0)
            displayPage ^= 1;
    }
}

// =============================================================================
// Display Update
// =============================================================================

static void FormatPct(char *out, size_t len, uint8_t raw)
{
    snprintf(out, len, "%d%%", (raw * 100) / 255);
}

static void DisplayPedalData(const PedalData *d)
{
    char line1[17];
    char line2[17];
    char vVol[5];
    char vA[5], vB[5];
    const char *nameA;
    const char *nameB;
    uint8_t rawA, rawB;

    bool passthrough = (d->states & TOGGLE_PASSTHROUGH_MASK) != 0;
    bool canEdit = (d->states & TOGGLE_EDITING_MASK) != 0;

    if (d->effectID >= NUM_EFFECTS)
        return;

    // This formats the first line
    // The format is "EffectName [P/E] V: 00%"
    FormatPct(vVol, sizeof(vVol), d->pot0);

    const char *modeTag = "";
    if (passthrough)
        modeTag = "[P]";
    else if (canEdit)
        modeTag = "[E]";
    else
        modeTag = "   ";

    snprintf(line1, sizeof(line1), "%-8.8s%s V:%s",
             effectNames[d->effectID], modeTag, vVol);

    // Format for second line
    // If we have passthrough, just show a bypass message
    if (passthrough)
    {
        snprintf(line2, sizeof(line2), "  -- BYPASS --  ");
    }
    else
    {
        // Else display the menu we have
        if (displayPage == 0)
        {
            rawA = d->pot1;
            nameA = paramNames[d->effectID][1];
            rawB = d->pot2;
            nameB = paramNames[d->effectID][2];
        }
        else
        {
            rawA = d->pot2;
            nameA = paramNames[d->effectID][2];
            rawB = d->pot3;
            nameB = paramNames[d->effectID][3];
        }

        FormatPct(vA, sizeof(vA), rawA);
        FormatPct(vB, sizeof(vB), rawB);

        snprintf(line2, sizeof(line2), "%.4s:%-4s%.4s:%-4s",
                 nameA, vA, nameB, vB);
    }

    // Send to LCD
    LCD_SetCursor(0);
    LCD_Print(line1);
    LCD_SetCursor(1);
    LCD_Print(line2);
}

/**
 * Handles a new PedalData packet from the Daisy Seed, updating the display state
 */
static void HandleNewPacket(const PedalData *d)
{
    bool pot1Changed = (d->pot1 != lastData.pot1);
    bool pot2Changed = (d->pot2 != lastData.pot2);
    bool hasPot3 = (paramNames[d->effectID][3][0] != '-');      // check if param 3 is "None"
    bool pot3Changed = (d->pot3 != lastData.pot3) && (hasPot3); // ignore pot3 changes if param 3 is "None"
    bool effectChanged = (d->effectID != lastData.effectID);

    if (effectChanged)
    {
        // Effect switch: reset to page 0, no lock — let user see all params
        displayPage = 0;

        // Lock if the new effect doesn't have a pot3
        displayLocked = !hasPot3;
    }
    else if (pot1Changed && !pot3Changed)
    {
        // pot1 moving: lock to page 0 (pot1+pot2)
        displayPage = 0;
        displayLocked = true;
        lockTimer = displayTimer;
    }
    else if (pot3Changed && !pot1Changed)
    {
        // pot3 moving: lock to page 1 (pot2+pot3)
        displayPage = 1;
        displayLocked = true;
        lockTimer = displayTimer;
    }
    else if (pot2Changed)
    {
        // pot2 is shared — stay on current page, just lock it
        displayLocked = true;
        lockTimer = displayTimer;
    }
    else if (pot1Changed && pot3Changed)
    {
        // Both outer pots moving simultaneously — stay on current page
        displayLocked = true;
        lockTimer = displayTimer;
    }

    lastData = *d;
    DisplayPedalData(d);
}
/**
 * Apply the current effect state to the DSP objects.
 */
void I2C1_IRQHandler(void)
{
    switch (DL_I2C_getPendingInterrupt(I2C_1_INST))
    {

    case DL_I2C_IIDX_TARGET_RX_DONE:
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
    NVIC_EnableIRQ(I2C1_INT_IRQn);

    while (1)
    {
        if (dataReady)
        {
            PedalData data;
            __disable_irq();
            memcpy(&data, (const void *)readyBuffer, PEDAL_DATA_SIZE);
            dataReady = false;
            __enable_irq();

            HandleNewPacket(&data);
        }

        DisplayTick();

        // Disable interrupts and enter low-power wait until the next I2C packet arrives.
        __disable_irq();
        if (!dataReady)
            __WFI();
        __enable_irq();
    }
}