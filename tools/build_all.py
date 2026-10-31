#!/usr/bin/env python
"""Regenerate every model and sample source shipped in src/cbb15_models/.

Inputs (all public):
  --devhub          clone of https://github.com/Brainchip-Inc/brainchip_devhub
                    with Git LFS content (the .fbz files)
  --imagenet-labels akida_models/imagenet/imagenet_labels2names.py from the
                    akida_models 1.14.0 package (PyPI)
  --imagenet-zip    imagenet_like.zip, the BrainChip sample pack used by the
                    Dev Hub (data.brainchip.com/dataset-mirror/imagenet_like)
  --mitdb           folder for the MIT-BIH records (PhysioNet, downloaded by
                    the Dev Hub pipeline if missing)
  --speech          folder of 16 kHz WAV files for the voice model calibration

Models exported (Dev Hub akida1 model zoo, mapped for the AKD1500):
  vww        akidanet_vww_qat.fbz                     person detection 96x96x3
  kws        speech_commands_qat.fbz                  12 keywords, 49x10 MFCC
  imagenet   akidanet_imagenet_160_alpha_0.25_qat.fbz 1000 classes 160x160x3,
                                                      HwPr (4 passes)
  ecg        arrhythmia_classification_qat.fbz        N / S / V beats 36x32x1
  face_learn, voice_learn                             see make_learning_models.py

Checked but not exportable: plant_village. It maps to two passes and its
program (1.15 MB) is larger than one 1 MiB flash slot; pushed over SPI it
does not leave room in the 1 MiB AKD1500 SRAM for its 224x224x3 input.
"""
import argparse
import ast
import os
import sys

import akida

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import export_model  # noqa: E402
import make_learning_models  # noqa: E402
import make_imagenet_subset  # noqa: E402
import make_samples  # noqa: E402
import sync_examples  # noqa: E402
import cbb15_mfcc  # noqa: E402

VWW_LABELS = ['no person', 'person']
ECG_LABELS = ['N normal', 'S supraventricular', 'V ventricular']


def imagenet_labels(path):
    tree = ast.parse(open(path, encoding='utf8').read())
    for node in tree.body:
        if isinstance(node, ast.Assign) and node.targets[0].id == 'imagenet_labels':
            d = ast.literal_eval(node.value)
            # First synonym only, to keep the strings short on the display.
            return [d[i].split(',')[0] for i in range(1000)]
    raise ValueError('imagenet_labels not found')


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--devhub', required=True)
    p.add_argument('--imagenet-labels', required=True)
    p.add_argument('--imagenet-zip', required=True)
    p.add_argument('--mitdb', required=True)
    p.add_argument('--speech', required=True)
    p.add_argument('--images', default=None)
    p.add_argument('--out', default=os.path.join(here, '..', 'src', 'cbb15_models'))
    a = p.parse_args()

    zoo = os.path.join(a.devhub, 'akida1', 'model_zoo')
    jobs = [
        ('vww', 'vww/pretrained_models/akidanet_vww_qat.fbz', VWW_LABELS, None),
        ('kws', 'speech_commands/pretrained_models/speech_commands_qat.fbz',
         cbb15_mfcc.LABELS, None),
        ('imagenet', 'imagenet_akidanet/pretrained_models/'
         'akidanet_imagenet_160_alpha_0.25_qat.fbz', imagenet_labels(a.imagenet_labels),
         akida.MapMode.HwPr),
        ('ecg', 'arrhythmia_classification/pretrained_models/'
         'arrhythmia_classification_qat.fbz', ECG_LABELS, None),
    ]
    for name, rel, labels, mode in jobs:
        print(export_model.export(os.path.join(zoo, rel), name, a.out, labels,
                                  source=f'brainchip_devhub akida1/model_zoo/{rel}',
                                  mode=mode))
    try:
        export_model.export(os.path.join(
            zoo, 'plant_village/pretrained_models/akidanet_plant_village_qat.fbz'),
            'plant_village', os.path.join(here, 'build', 'rejected'))
    except export_model.ExportError as e:
        print('plant_village not exported:', e)

    # 100-class ImageNet subset (classes in imagenet_subset.txt).
    labels = imagenet_labels(a.imagenet_labels)
    picked = make_imagenet_subset.read_classes(
        os.path.join(here, 'imagenet_subset.txt'), labels)
    sub = make_imagenet_subset.subset_model(
        os.path.join(a.devhub, make_imagenet_subset.MODEL), picked)
    print(export_model.export(sub, 'imagenet_subset', a.out, [labels[i] for i in picked],
                              source='Dev Hub AkidaNet 160 alpha 0.25, classifier cut to '
                                     f'{len(picked)} classes'))

    args = argparse.Namespace(devhub=a.devhub, speech=a.speech, images=a.images,
                              rate=0.25, neurons=10, out=a.out,
                              fbz_out=os.path.join(here, 'build'))
    for r in make_learning_models.build(args):
        print(r)

    print('vww samples', make_samples.vww(a.devhub, a.out))
    print('imagenet samples', make_samples.imagenet(a.devhub, a.imagenet_zip, a.out))
    print('ecg samples accuracy', make_samples.ecg(a.devhub, a.mitdb, a.out))

    # Refresh the local model copies in the example sketches.
    sys.argv = [sys.argv[0]]
    sync_examples.main()


if __name__ == '__main__':
    main()
