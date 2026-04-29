# ELEC 327 Custom Digital Pedal

To install:
1. Clone the repository
2. Run `git submodule update --init --recursive` to get the libDaisy
3. Install nix, and run `nix develop --extra-experimental-features nix-command --extra-experimental-features flakes` in the project directory to set up the development environment
4. Navigate to the AudioProcessing directory and run `make` to build the firmware, then `make flash` to flash it onto the Daisy Seed
5. Follow the instructions in the libDaisy documentation to flash the firmware onto the Daisy Seed
The firmware is located in the `AudioProcessing` directory, and the main source file is `Blink.cpp`. The Makefile is set up to compile this source file and link it with the libDaisy and DaisySP libraries.


nix develop --extra-experimental-features nix-command --extra-experimental-features flakes


Check this link for help: https://gemini.google.com/share/752ff1b4bd42

## MSPM0 LCD display firmware

The MSPM0-side receiver/display code is in `MSMP0Processing`. It listens for
the Daisy Seed's 5-byte I2C payload at address `0x42` and drives a 16x2 I2C LCD
with the current effect mode and potentiometer percentages.
