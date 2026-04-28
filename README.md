# ELEC 327 Custom Digital Pedal

This firmware runs on a Daisy Seed and cycles through five effects:
Overdrive, Chorus, Reverb, Phaser, and a NeuralSeed GRU amp/pedal capture.
The NeuralSeed model weights are compiled into the firmware; the board does
not load model files dynamically at runtime.

To build and flash:
1. Clone the repository.
2. Run `git submodule update --init --recursive` to get libDaisy, DaisySP, and their nested dependencies.
3. Install Nix, then run `nix develop --extra-experimental-features nix-command --extra-experimental-features flakes` in the `AudioProcessing` directory.
4. Build the libraries with `make -C libDaisy` and `make -C DaisySP`.
5. Build the firmware with `make`.
6. Put the Daisy Seed into USB DFU mode, then run `make program-dfu`.

The firmware is located in the `AudioProcessing` directory, and the main source
file is `src/Main.cpp`. The Makefile compiles this source file and links it with
libDaisy, DaisySP, and the embedded RTNeural headers.

NeuralSeed controls:
- Button D3 cycles through the effects, including NeuralSeed as the fifth preset.
- Toggle D2 enables passthrough/bypass.
- Toggle D1 enables editing of the current effect's saved pot values.
- Pot A1 controls NeuralSeed input gain from 0x to 3x.
- Pot A3 controls NeuralSeed dry/wet mix.
- Pot A5 controls NeuralSeed output level.
- Pot A7 is reserved for a future trained model parameter or tone control.


nix develop --extra-experimental-features nix-command --extra-experimental-features flakes


Check this link for help: https://gemini.google.com/share/752ff1b4bd42
