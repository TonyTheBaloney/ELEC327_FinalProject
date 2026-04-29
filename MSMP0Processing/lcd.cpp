#include <stdint.h>
#include <stdio.h>

struct __attribute__((packed)) PedalData {
    uint8_t effectID; // 0 = EQ, 1 = Funk, 2 = Ambient, 3 = Lead, 4 = HiGain
    uint8_t pot0;     // 0-255
    uint8_t pot1;     // 0-255
    uint8_t pot2;     // 0-255
    uint8_t pot3;     // 0-255
};

const char* effectNames[5] = {
    "EQ",           // 0
    "Funk",         // 1
    "Ambient",      // 2
    "Lead",         // 3
    "HiGain"        // 4
};

// A 2D array: paramNames[EffectID][PotNumber]
const char* paramNames[5][4] = {
    {"Level", "Bass", "Mid", "Treble"},        // 0: EQ
    {"Volume", "Wah Depth", "Comp Mix", "Rev Mix"}, // 1: Funk
    {"Volume", "Delay Time", "Rev Level", "Chorus"},// 2: Ambient
    {"Volume", "Gain", "Wet Mix", "Gate Thr"},      // 3: Lead
    {"Volume", "Gain", "Drive", "Tone"}             // 4: HiGain
};

// 3. What to do when data arrives via I2C
// (This would be called in your MSMP0's I2C Receive Interrupt Handler)
void OnDataReceived(uint8_t* rx_buffer) {
    
    // "Cast" the raw bytes back into our organized struct
    PedalData* receivedData = (PedalData*)rx_buffer;
    
    // Safety check so we don't crash the array
    if (receivedData->effectID > 4) return; 

    // --- LCD FORMATTING LOGIC ---
    
    char lcdLine1[16]; // Assuming a 16x2 character LCD
    char lcdLine2[16];

    // Example 1: Print the current Effect Name
    // Output e.g. "Mode: Ambient"
    snprintf(lcdLine1, sizeof(lcdLine1), "Mode: %s", effectNames[receivedData->effectID]);
    
    // Example 2: Convert the 0-255 math back to a percentage for the user
    int percentP0 = (receivedData->pot0 * 100) / 255;
    
    // Output e.g. "Volume: 75%"
    snprintf(lcdLine2, sizeof(lcdLine2), "%s: %d%%", 
             paramNames[receivedData->effectID][0], // Gets the name of Pot 0 for this specific effect
             percentP0);

    // Call your specific MSMP0 LCD library functions here
    // lcd_set_cursor(0, 0);
    // lcd_print(lcdLine1);
    // lcd_set_cursor(0, 1);
    // lcd_print(lcdLine2);
}