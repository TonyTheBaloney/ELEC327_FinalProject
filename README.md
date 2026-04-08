# ELEC 327 Custom Digital Pedal

To install:
1. Clone the repository
2. Run `git submodule update --init --recursive` to get the libDaisy
3. Install nix, and run `nix-shell` in the project directory to set up the development environment
4. Navigate to the AudioProcessing directory and run `make` to build the firmware
5. Follow the instructions in the libDaisy documentation to flash the firmware onto the Daisy Seed
The firmware is located in the `AudioProcessing` directory, and the main source file is `Blink.cpp`. The Makefile is set up to compile this source file and link it with the libDaisy and DaisySP libraries.