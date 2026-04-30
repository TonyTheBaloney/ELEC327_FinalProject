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

// Transmits an arbitrary number of bytes to the LCD
void I2C_SendBytes(uint8_t *data, uint16_t length) {
    DL_I2C_startControllerTransfer(I2C_0_INST, LCD_ADDR, DL_I2C_CONTROLLER_DIRECTION_TX, length);
    for (uint16_t i = 0; i < length; i++) {
        while (DL_I2C_isControllerTXFIFOFull(I2C_0_INST)) {} 
        DL_I2C_transmitControllerData(I2C_0_INST, data[i]);
    }
    while (DL_I2C_getControllerStatus(I2C_0_INST) & DL_I2C_CONTROLLER_STATUS_BUSY_BUS) {}
}

// Sends a specific command byte to the LCD (prefixed with 0xFE)
void LCD_SendCommand(uint8_t cmd, uint32_t delay_ms) {
    uint8_t buffer[2] = {0xFE, cmd}; 
    I2C_SendBytes(buffer, 2);
    delay_cycles(delay_ms * 32000); 
}

// Prints a standard string to the LCD
void LCD_Print(const char *text) {
    I2C_SendBytes((uint8_t *)text, strlen(text));
}

// Moves cursor to the start of Line 1 (row 0) or Line 2 (row 1)
void LCD_SetCursor(uint8_t row) {
    if (row == 0) LCD_SendCommand(0x80, 1); 
    else          LCD_SendCommand(0xC0, 1);
}

// --- I2C1 Receive State ---
static volatile uint8_t  rxBuffer[PEDAL_DATA_SIZE]; // Holds incoming bytes
static volatile uint8_t  rxCount  = 0;              // Tracks bytes received
static volatile bool     dataReady = false;         // Flags when full packet arrives

// --- UI Update Logic ---

// Formats and draws the effect data to the LCD
static void DisplayPedalData(const PedalData* d) {
    if (d->effectID > 4) return;   

    char line1[17]; 
    char line2[17];

    // %-10s pads with trailing spaces to overwrite old characters (prevents flicker)
    snprintf(line1, sizeof(line1), "[%-10s]", effectNames[d->effectID]);
    
    // Convert 0-255 raw ADC data to 0-100%
    int p0_pct = (d->pot0 * 100) / 255;
    int p1_pct = (d->pot1 * 100) / 255;
    
    // Format parameters 0 and 1 for the second line
    snprintf(line2, sizeof(line2), "%s:%-3d%% %s:%-3d%%", 
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
            if (rxCount < PEDAL_DATA_SIZE) {
                rxBuffer[rxCount] = DL_I2C_receiveTargetData(I2C_1_INST);
                rxCount++;
            } else {
                (void)DL_I2C_receiveTargetData(I2C_1_INST); // Discard overflow
            }
            if (rxCount == PEDAL_DATA_SIZE) {
                dataReady = true;
            }
            break;

        case DL_I2C_IIDX_TARGET_STOP:
            rxCount = 0; // Reset byte counter for the next packet
            break;
            
        default:
            break;
    }
}

// --- Main Program Loop ---
int main(void) {
    SYSCFG_DL_init(); // Initialize clocks, pins, and I2C peripherals

    // LCD initialization sequence
    delay_cycles(100 * 32000); 
    LCD_SendCommand(0x41, 1); 
    LCD_SendCommand(0x51, 2); 

    LCD_Print("Waiting for");
    LCD_SetCursor(1);
    LCD_Print("Daisy Seed...");

    // Allow I2C1 to interrupt the main loop
    NVIC_EnableIRQ(I2C_1_INST_INT_IRQN);

    while (1) {
        if (dataReady) {
            // Temporarily disable interrupts while copying data to prevent corruption mid-copy
            __disable_irq();
            PedalData snapshot;
            memcpy(&snapshot, (const void*)rxBuffer, PEDAL_DATA_SIZE);
            dataReady = false;
            __enable_irq();

            DisplayPedalData(&snapshot); // Update screen
        }
        __WFI(); // Enter low-power sleep; wakes up on next interrupt
    }
}