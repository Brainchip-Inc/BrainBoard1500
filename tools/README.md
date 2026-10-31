# BrainBoard1500 tools

Python tools that produce the generated sources in `src/cbb15_models/` and the
patched Akida Engine in `src/`. They need Python 3.10–3.12 and the packages in
`requirements.txt` (the `akida` 2.19.3 package maps models for the AKD1500 and
also contains the engine sources).

| Script | Purpose |
|---|---|
| `export_model.py` | map an `.fbz` model for the AKD1500 (hardware only) and write a header-only C++ model (`<name>.h`) |
| `make_learning_models.py` | build the on-chip learning models `face_learn` and `voice_learn` |
| `make_imagenet_subset.py` | cut the ImageNet classifier to your own selection of up to 100 classes (`imagenet_subset.txt`), no training, identical potentials |
| `make_samples.py` | sample inputs for the self tests (VWW tiles, ImageNet-like images, MIT-BIH beats) |
| `build_all.py` | regenerate everything shipped in `src/cbb15_models/` |
| `cbb15_mfcc.py` | NumPy reference of the speech_commands MFCC (`cbb15::Mfcc`) |
| `cbb15_speaker.py` | NumPy reference of the speaker code (`cbb15::SpeakerEncoder`) |
| `make_tts_calibration.ps1` | spoken calibration words from the Windows speech synthesizer |
| `sync_examples.py` | copy the models from `src/cbb15_models/` next to the example sketches (`examples/<sketch>/`) (run by `build_all.py` and `make_imagenet_subset.py`; `--check` only reports) |
| `patch_engine.py` | copy the Akida Engine 2.19.3 + FlatBuffers 2.0.8 into `src/` and apply `PATCHES.md` |

## Regenerating all models

```
git lfs install
git clone https://github.com/Brainchip-Inc/brainchip_devhub.git
pip install -r requirements.txt
pip download --no-deps akida_models==1.14.0       # for the ImageNet class names
# extract akida_models/imagenet/imagenet_labels2names.py from the archive
curl -LO https://data.brainchip.com/dataset-mirror/imagenet_like/imagenet_like.zip
powershell -File make_tts_calibration.ps1 -OutDir speech
python build_all.py --devhub brainchip_devhub \
    --imagenet-labels imagenet_labels2names.py \
    --imagenet-zip imagenet_like.zip \
    --mitdb mitdb --speech speech
```

The MIT-BIH records (about 100 MB) are downloaded from PhysioNet by the Dev
Hub pipeline on the first run. Mapping is deterministic for the Dev Hub
models; the learning models get freshly initialised learning layers, so flash
them again after regenerating.

## Exporting your own model

```
python export_model.py my_model.fbz --name my_model --labels "a,b,c"
```

Then `#include "my_model.h"` in your sketch, run
`python sync_examples.py` (or copy the header next to your `.ino`), and
use `cbb15::models::myModel()`
(flash slot) or `cbb15::models::myModelWithProgram()` (flasher, slot 0).
The exporter rejects program data larger than a flash slot (978,944 bytes).
