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

Local NeuralSeed training:

The NeuralSeed source checkout lives outside this firmware repo at
`/home/harvey/NeuralSeedTraining`, but training artifacts are written inside
this repo under `training/neuralseed/`. The wrapper scripts use the
`neuralseed-train` conda environment, preprocess `data/di.wav` against the
target WAV, train a NeuralSeed-compatible GRU10 model, save checkpoints in the
repo, and export the selected `model_best.json` into
`AudioProcessing/src/NeuralSeedModelData.h`.

```bash
# Distortion/overdrive-style capture; this is the recommended NeuralSeed target.
./tools/neuralseed/train_neuralseed.sh distortion

# Chorus will run, but it is experimental because NeuralSeed is not designed for
# time-based/modulated effects.
./tools/neuralseed/train_neuralseed.sh chorus
```

By default the wrapper trains on GPU index `3` with
`CUDA_DEVICE_ORDER=PCI_BUS_ID`. On this machine, index `3` maps to an A100.
Override the device or epoch count with either `GPU_ID` or `--gpu`:

```bash
GPU_ID=3 ./tools/neuralseed/train_neuralseed.sh distortion --fresh --epochs 2000
GPU_ID=2 ./tools/neuralseed/train_neuralseed.sh distortion --fresh --epochs 2000
./tools/neuralseed/train_neuralseed.sh distortion --fresh --epochs 2000 --gpu 1
```

To verify preprocessing/training without replacing the compiled model header:

```bash
./tools/neuralseed/train_neuralseed.sh distortion --epochs 1 --validation-frequency 1 --no-export
```

To keep training running after disconnecting from SSH, use the nohup launcher:

```bash
./tools/neuralseed/train_neuralseed_nohup.sh distortion --fresh --epochs 300 --gpu 3
./tools/neuralseed/train_neuralseed_nohup.sh chorus --fresh --epochs 300 --gpu 3
```

The nohup launcher writes logs and PID files under `training/neuralseed/logs/`,
which is ignored by git. Follow the latest log for an effect with:

```bash
./tools/neuralseed/tail_neuralseed_log.sh distortion
```

The commit-friendly checkpoints are saved here:

```text
training/neuralseed/results/elec327_distortion-elec327_distortion_gru10/model_best.json
training/neuralseed/results/elec327_distortion-elec327_distortion_gru10/model.json
training/neuralseed/results/elec327_chorus-elec327_chorus_gru10/model_best.json
training/neuralseed/results/elec327_chorus-elec327_chorus_gru10/model.json
```

The generated split WAVs, TensorBoard event files, and test output WAVs are
ignored by git. Override the artifact root with `--artifact-root` if needed.

Changing the NeuralSeed checkpoint:

The board does not load NeuralSeed checkpoints dynamically. The selected
checkpoint is compiled into `AudioProcessing/src/NeuralSeedModelData.h`, then
loaded by `AudioProcessing/src/Main.cpp` during startup. To change checkpoints,
replace the embedded weights, rebuild the firmware, and flash the board again.

The provided NeuralSeed model data is bundled in this repo at
`training/neuralseed/provided/all_model_data.h`. List the bundled models with:

```bash
./tools/neuralseed/select_provided_model.py --list
```

The current firmware is compiled for a single-input GRU10 model
(`RTNeural::GRULayerT<float, 1, 10>`). These bundled models match that native
firmware shape:

```text
1  engl_g25_p0056_GRU10
5  klondc3_snap_g5_p005
7  5150_g25_p012
8  dirtyShirleyMini_clean_p017
9  orangebass_G3_p046
10 bassman_g25_p025_gru10
11 fender57_g5_p008
```

Models `2` and `6` are also GRU10 models, but they were trained with
`input_size: 2`. The selector can export them, but the current firmware only
feeds the first input unless `src/Main.cpp` is changed to pass a learned control
input.

From the `ELEC327_FinalProject` repo root, select a bundled model by number or
exact name. Example: select the Klon snap model:

```bash
./tools/neuralseed/select_provided_model.py 5
```

Then rebuild and flash:

```bash
cd AudioProcessing
nix develop --extra-experimental-features nix-command --extra-experimental-features flakes
make clean
make
make program-dfu
```

GRU8, GRU7, GRU4, or LSTM checkpoints are not drop-in replacements for this
firmware. Those require changing the RTNeural model type and weight-loading
code in `src/Main.cpp`.
