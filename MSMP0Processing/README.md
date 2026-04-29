# MSPM0 Display Firmware

This folder contains the MSPM0-side firmware for the pedal display board. The
folder name is kept as `MSMP0Processing` because it already existed in the
repository, but the target device is the MSPM0L1345 shown in the schematic.

## Protocol from Daisy Seed

The Daisy Seed code sends a packed 5-byte I2C frame to target address `0x42`:

| Byte | Meaning |
| --- | --- |
| 0 | Effect ID: `0=EQ`, `1=Funk`, `2=Ambient`, `3=Lead`, `4=HiGain` |
| 1 | Pot 1 value, `0..255` |
| 2 | Pot 2 value, `0..255` |
| 3 | Pot 3 value, `0..255` |
| 4 | Pot 4 value, `0..255` |

The MSPM0 receives that frame as an I2C target and prints the current effect
and potentiometer percentages to a 16x2 HD44780-compatible LCD with an I2C
PCF8574 backpack. Line 1 shows the current mode. Line 2 alternates between pot
1/2 and pot 3/4 every 1.2 seconds.

## Schematic Pin Mapping

Configure the MSPM0L1345 project in SysConfig with these I2C peripherals:

| Function | MSPM0 peripheral | Pins from schematic | Role |
| --- | --- | --- | --- |
| LCD header `J4` | `I2C0` | `PA1=SCL`, `PA0=SDA` | Controller |
| Daisy Seed bus | `I2C1` | `PA15=SCL`, `PA16=SDA` | Target at `0x42` |

The schematic shows external pullups on the Daisy/MSPM0 bus. If your LCD module
does not include I2C pullups, add pullups on `I2C0_SCL` and `I2C0_SDA`.

## Expected SysConfig Names

`src/main.c` includes `ti_msp_dl_config.h` and defaults to the generated names
below:

```c
I2C_LCD_INST   -> I2C_0_INST
I2C_DAISY_INST -> I2C_1_INST
```

If your SysConfig instance names are different, either rename them to `I2C_0`
and `I2C_1`, or add these macros in `ti_msp_dl_config.h`/project defines:

```c
#define I2C_LCD_INST I2C_0_INST
#define I2C_DAISY_INST I2C_1_INST
#define I2C_DAISY_INST_INT_IRQN I2C_1_INST_INT_IRQN
#define I2C_DAISY_INST_IRQHandler I2C_1_INST_IRQHandler
```

## Import/Build

1. Create or open an MSPM0L1345 DriverLib project in TI Code Composer Studio or
   CCS Theia.
2. Configure SysConfig using the pin table above.
3. Add `src/main.c` from this folder to the MSPM0 project.
4. Confirm the LCD backpack address. The code uses `0x27`; change
   `LCD_I2C_ADDRESS` in `src/main.c` to `0x3F` if your module uses that address.
5. Build and flash the MSPM0.

If the display stays on `Waiting Daisy`, check the Daisy I2C address and make
sure the Daisy bus is wired to MSPM0 `I2C1`, not the LCD `I2C0` bus.
