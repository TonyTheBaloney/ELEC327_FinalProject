#include "ti_msp_dl_config.h"   
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// --- Config & Data Structures ---
#define LCD_ADDR 0x28 // LCD I2C target address

// Matches the 5-byte data structure sent by the Daisy Seed
struct __attribute__((packed)) PedalData {
    uint8_t effectID;   // 0=EQ, 1=Funk, 2=Ambient, 3=Lead, 4=HiGain
    uint8_t pot0;       // Potentiometer values (0-255)
    uint8_t pot1;
    uint8_t pot2;
    uint8_t pot3;
};
static constexpr uint8_t PEDAL_DATA_SIZE = sizeof(PedalData); 

// UI display strings
static const char* effectNames[5] = { "EQ", "Funk", "Ambient", "Lead", "HiGain" };
static const char* paramNames[5][4] = {
    {"Level",  "Bass",       "Mid",       "Treble"  }, 
    {"Volume", "Wah Depth",  "Comp Mix",  "Rev Mix" }, 
    {"Volume", "Delay Time", "Rev Level", "Chorus"  }, 
    {"Volume", "Gain",       "Wet Mix",   "Gate Thr"}, 
    {"Volume", "Gain",       "Drive",     "Tone"    }, 
};

// --- I2C0 LCD Hardware Driver ---

// Cycles per millisecond derived from the configured CPU clock so timing
// stays correct if CPUCLK_FREQ changes (e.g. when SysConfig is regenerated
// for MSPM0G3507 at a non-32 MHz MCLK).
#define CYCLES_PER_MS (CPUCLK_FREQ / 1000U)

// Transmits an arbitrary number of bytes to the LCD.
// Returns true on success, false if the target NACK'd or the bus errored —
// callers can use this to detect a missing/unpowered LCD instead of hanging.
bool I2C_SendBytes(uint8_t *data, uint16_t length) {
    DL_I2C_startControllerTransfer(I2C_0_INST, LCD_ADDR, DL_I2C_CONTROLLER_DIRECTION_TX, length);
    for (uint16_t i = 0; i < length; i++) {
        while (DL_I2C_isControllerTXFIFOFull(I2C_0_INST)) {}
        DL_I2C_transmitControllerData(I2C_0_INST, data[i]);
    }
    // Wait for the controller itself to finish (IDLE), not just for the bus
    // line to fall idle. BUSY_BUS can clear briefly while our FIFO is still
    // draining, which previously let this function return mid-transfer.
    while (!(DL_I2C_getControllerStatus(I2C_0_INST) & DL_I2C_CONTROLLER_STATUS_IDLE)) {}

    // After IDLE, surface address-NACK / arbitration / bus errors so the
    // caller can stop hammering a dead bus.
    uint32_t status = DL_I2C_getControllerStatus(I2C_0_INST);
    if (status & (DL_I2C_CONTROLLER_STATUS_ERROR |
                  DL_I2C_CONTROLLER_STATUS_ADDR_NACK |
                  DL_I2C_CONTROLLER_STATUS_ARBITRATION_LOST)) {
        return false;
    }
    return true;
}

// Sends a specific command byte to the LCD (prefixed with 0xFE).
// delay_ms is the post-command settling delay required by the controller IC.
void LCD_SendCommand(uint8_t cmd, uint32_t delay_ms) {
    uint8_t buffer[2] = {0xFE, cmd};
    I2C_SendBytes(buffer, 2);
    delay_cycles(delay_ms * CYCLES_PER_MS);
}

// Prints a standard string to the LCD.
void LCD_Print(const char *text) {
    (void)I2C_SendBytes((uint8_t *)text, strlen(text));
}

// Moves cursor to the start of Line 1 (row 0) or Line 2 (row 1).
// Uses the Newhaven PIC's "Set Cursor Position" command: 0xFE 0x45 [pos]
// with pos = 0x00 for line 1, 0x40 for line 2 (datasheet Table of Commands).
// The previous implementation sent 0xFE 0x80 / 0xFE 0xC0 — those are raw
// HD44780/ST7066U DDRAM opcodes and are NOT forwarded by this module's PIC
// firmware, so the cursor never actually moved and line-2 writes landed
// wherever the cursor happened to be after line-1 finished.
void LCD_SetCursor(uint8_t row) {
    uint8_t pos = (row == 0) ? 0x00 : 0x40;
    uint8_t buf[3] = {0xFE, 0x45, pos};
    (void)I2C_SendBytes(buf, 3);
    delay_cycles(1 * CYCLES_PER_MS); // datasheet exec time = 100 µs; rounded up
}

// --- I2C1 Receive State ---
// Double-buffered: bytes accumulate into rxBuffer as they arrive, and only
// on a STOP for a fully-sized packet do we snapshot into readyBuffer. This
// closes the race where main was copying rxBuffer at the same moment the
// next packet's first byte landed in rxBuffer[0] and corrupted the snapshot.
static volatile uint8_t  rxBuffer[PEDAL_DATA_SIZE];    // In-flight packet
static volatile uint8_t  readyBuffer[PEDAL_DATA_SIZE]; // Last completed packet
static volatile uint8_t  rxCount  = 0;                 // Bytes received in current packet
static volatile bool     dataReady = false;            // True iff readyBuffer is valid

// --- UI Update Logic ---

// Formats and draws the effect data to the LCD.
// Output is exactly 16 columns per row to match the display width and to
// fully overwrite whatever was there previously (no leftover characters).
static void DisplayPedalData(const PedalData* d) {
    if (d->effectID > 4) return;

    char line1[17];
    char line2[17];

    // Line 1: "[Effect    ]" — pad to 10 so the brackets always close in the
    // same column even when the name shrinks (e.g. "EQ" vs "Ambient").
    snprintf(line1, sizeof(line1), "[%-10s]", effectNames[d->effectID]);

    // Convert 0-255 raw ADC data to 0-100% for human-readable display.
    int p0_pct = (d->pot0 * 100) / 255;
    int p1_pct = (d->pot1 * 100) / 255;

    // Line 2: pack two "Lbl:NNN%" cells into 16 chars exactly.
    //   %-3.3s  truncates each label to 3 chars (e.g. "Volume" -> "Vol",
    //           "Wah Depth" -> "Wah") so the long names in paramNames[] no
    //           longer overflow the 16-column LCD and silently drop the
    //           second pot off the right edge.
    //   %03d%%  fixed-width 3-digit percent + literal '%' = 4 chars.
    // Total per cell = 3 + 1 + 3 + 1 = 8 chars; two cells = 16 exactly.
    snprintf(line2, sizeof(line2), "%-3.3s:%03d%%%-3.3s:%03d%%",
             paramNames[d->effectID][0], p0_pct,
             paramNames[d->effectID][1], p1_pct);

    LCD_SetCursor(0);
    LCD_Print(line1);

    LCD_SetCursor(1);
    LCD_Print(line2);
}

// --- I2C1 Interrupt Handler (Daisy -> MSPM0) ---

// Triggers automatically during I2C1 hardware events
extern "C" void I2C_1_INST_IRQHandler(void)
{
    // Get the specific interrupt that just fired (Pending, not Enabled!)
    switch (DL_I2C_getPendingInterrupt(I2C_1_INST)) {

        case DL_I2C_IIDX_TARGET_RX_DONE:
            // Accumulate into the in-flight buffer only; do NOT raise
            // dataReady mid-packet — it is published atomically on STOP.
            if (rxCount < PEDAL_DATA_SIZE) {
                rxBuffer[rxCount] = DL_I2C_receiveTargetData(I2C_1_INST);
                rxCount++;
            } else {
                (void)DL_I2C_receiveTargetData(I2C_1_INST); // Discard overflow
            }
            break;

        case DL_I2C_IIDX_TARGET_STOP:
            // Promote rxBuffer -> readyBuffer only when a complete, correctly
            // sized packet was received. Short packets are dropped silently
            // (e.g. address-only probes won't clobber the last good values).
            if (rxCount == PEDAL_DATA_SIZE) {
                memcpy((void*)readyBuffer, (const void*)rxBuffer, PEDAL_DATA_SIZE);
                dataReady = true;
            }
            rxCount = 0; // Reset byte counter for the next packet
            break;

        default:
            break;
    }
}

// --- Main Program Loop ---
int main(void) {
    SYSCFG_DL_init(); // Initialize clocks, pins, and I2C peripherals

    // LCD initialization sequence — datasheet requires ~100 ms power-on
    // settle before the first command. Use CYCLES_PER_MS so this stays
    // correct if MCLK changes during a SysConfig regeneration.
    delay_cycles(100 * CYCLES_PER_MS);
    LCD_SendCommand(0x41, 1);
    LCD_SendCommand(0x51, 2);

    LCD_Print("Waiting for");
    LCD_SetCursor(1);
    LCD_Print("Daisy Seed...");

    // Allow I2C1 to interrupt the main loop
    NVIC_EnableIRQ(I2C_1_INST_INT_IRQN);

    while (1) {
        if (dataReady) {
            // Snapshot from readyBuffer (the post-STOP, complete-packet copy).
            // The IRQ-disable window only protects the memcpy + flag clear;
            // any new packet arriving meanwhile lands in rxBuffer and will
            // be promoted by its own STOP, so we can never tear a packet.
            __disable_irq();
            PedalData snapshot;
            memcpy(&snapshot, (const void*)readyBuffer, PEDAL_DATA_SIZE);
            dataReady = false;
            __enable_irq();

            DisplayPedalData(&snapshot); // Update screen
        }
        __WFI(); // Enter low-power sleep; wakes up on next interrupt
    }
}