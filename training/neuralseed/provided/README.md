# Bundled NeuralSeed models

`all_model_data.h` is a local copy of the NeuralSeed-provided model weight
header used for selecting pre-trained models without depending on an external
checkout path.

Use the repo helper from the project root:

```bash
./tools/neuralseed/select_provided_model.py --list
./tools/neuralseed/select_provided_model.py 5
```

The current Daisy firmware is compiled for a single-input GRU10 model. Models
with `input_size: 2` need a firmware change to use their second learned control
input exactly.
