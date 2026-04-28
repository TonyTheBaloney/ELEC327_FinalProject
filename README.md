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

Changing the NeuralSeed checkpoint:

The board does not load NeuralSeed checkpoints dynamically. The selected
checkpoint is compiled into `AudioProcessing/src/NeuralSeedModelData.h`, then
loaded by `AudioProcessing/src/Main.cpp` during startup. To change checkpoints,
replace the embedded weights, rebuild the firmware, and flash the board again.

For the current firmware shape, use a built-in GRU10 model from the NeuralSeed
repo's `NeuralSeed/all_model_data.h`. These are compatible drop-in choices:

```text
1  engl_g25_p0056_GRU10
2  klondc3_is2_gKnob_p024
5  klondc3_snap_g5_p005
6  ts9_driveKnob_p0057
7  5150_g25_p012
8  dirtyShirleyMini_clean_p017
9  orangebass_G3_p046
10 bassman_g25_p025_gru10
11 fender57_g5_p008
```

From the `ELEC327_FinalProject` repo root, set `MODEL_NUM` to the model number
you want, then run this helper script. Example: `MODEL_NUM=6` selects the TS9
checkpoint.

```bash
MODEL_NUM=6

python3 - "$MODEL_NUM" <<'PY'
from pathlib import Path
import sys

num = sys.argv[1]
source_path = Path("../NeuralSeed/NeuralSeed/all_model_data.h")
dest_path = Path("AudioProcessing/src/NeuralSeedModelData.h")
main_path = Path("AudioProcessing/src/Main.cpp")

source = source_path.read_text()
needle = f"Model{num}.rec_weight_ih_l0"
idx = source.index(needle)
start = source.rfind("//========================================================================", 0, idx)
rec_bias = source.index(f"Model{num}.rec_bias", idx)
end = source.index("};", rec_bias) + 2

block = source[start:end].replace(f"Model{num}.", "model.")
name = block.splitlines()[1].replace("//", "").strip()

header = f"""#pragma once

#include <vector>

struct NeuralSeedModelData
{{
    std::vector<std::vector<float>> rec_weight_ih_l0;
    std::vector<std::vector<float>> rec_weight_hh_l0;
    std::vector<std::vector<float>> lin_weight;
    std::vector<float> lin_bias;
    std::vector<std::vector<float>> rec_bias;
}};

static NeuralSeedModelData CreateSelectedNeuralSeedModelData()
{{
    NeuralSeedModelData model;
    // Selected checkpoint: {name}

{block}

    return model;
}}
"""

dest_path.write_text(header)

main = main_path.read_text()
for old_name in ("CreateKlondc3SnapG5ModelData", "CreateSelectedNeuralSeedModelData"):
    main = main.replace(old_name, "CreateSelectedNeuralSeedModelData")
main_path.write_text(main)

print(f"Selected Model{num}: {name}")
PY
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


nix develop --extra-experimental-features nix-command --extra-experimental-features flakes


Check this link for help: https://gemini.google.com/share/752ff1b4bd42
