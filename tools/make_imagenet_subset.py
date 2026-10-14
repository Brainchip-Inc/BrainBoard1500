#!/usr/bin/env python
"""Build an ImageNet classifier for your own selection of up to 100 classes.

The Dev Hub AkidaNet (akidanet_imagenet_160_alpha_0.25_qat.fbz) ends with a
FullyConnected layer "classifier" without activation: every output is an
independent potential computed from the same 256 features. Keeping only the
columns (weights, threshold, act_step) of the selected classes therefore gives
exactly the same potentials for these classes as the 1000-class model - no
training is needed and nothing is lost for the chosen classes.

What changes:
  - The model can only answer with the selected classes, so similar classes
    that you do not care about can no longer win.
  - The classifier weights shrink with the number of classes: for 100 classes
    the program is 248 KB instead of 442 KB and the external weights 20.8 KB
    instead of 208 KB, so it can be pushed over SPI quickly (slot 0).

Usage:
  python make_imagenet_subset.py --devhub <brainchip_devhub> \\
      --imagenet-labels imagenet_labels2names.py \\
      --classes imagenet_subset.txt [--name imagenet_subset]

The class file holds one ImageNet index or class name per line (see
imagenet_subset.txt). The result is src/cbb15_models/<name>.h, used by the
example 10_ImageNetSubset.
"""
import argparse
import copy
import os
import sys

import akida
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_all  # noqa: E402
import export_model  # noqa: E402
import sync_examples  # noqa: E402

MODEL = 'akida1/model_zoo/imagenet_akidanet/pretrained_models/akidanet_imagenet_160_alpha_0.25_qat.fbz'
MAX_CLASSES = 104


def read_classes(path, labels):
    picked = []
    for raw in open(path, encoding='utf8'):
        line = raw.split('#', 1)[0].strip()
        if not line:
            continue
        first = line.split()[0]
        if first.isdigit():
            index = int(first)
        else:
            if line not in labels:
                raise ValueError(f'unknown ImageNet class: {line}')
            index = labels.index(line)
        if not 0 <= index < len(labels):
            raise ValueError(f'index out of range: {index}')
        if index in picked:
            raise ValueError(f'class listed twice: {labels[index]}')
        picked.append(index)
    if not 1 <= len(picked) <= MAX_CLASSES:
        raise ValueError(f'{len(picked)} classes; choose 1..{MAX_CLASSES}')
    return picked


def subset_model(fbz, picked):
    d = akida.Model(fbz).to_dict()
    head = d['layers'][-1]
    if head['parameters']['layer_type'] != 'FullyConnected' or \
            head['parameters']['activation'] != 0:
        raise ValueError('expected a FullyConnected classifier without activation')
    v = head['variables']
    weights = np.array(v['weights']['data'])
    v['weights']['data'] = weights[..., picked].tolist()
    v['weights']['shape'] = list(weights[..., picked].shape)
    for name in ('threshold', 'act_step'):
        values = np.array(v[name]['data'])
        v[name]['data'] = values[picked].tolist()
        v[name]['shape'] = [len(picked)]
    head['parameters']['units'] = len(picked)
    return akida.Model.from_dict(d)


def check_identical(full, sub, picked, samples=8, seed=0):
    """Potentials of the subset must equal the selected full-model columns."""
    rng = np.random.default_rng(seed)
    x = rng.integers(0, 256, size=(samples, 160, 160, 3), dtype=np.uint8)
    a = full.forward(x).reshape(samples, -1)[:, picked]
    b = sub.forward(x).reshape(samples, -1)
    return bool(np.array_equal(a, b))


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--devhub', required=True)
    p.add_argument('--imagenet-labels', required=True)
    p.add_argument('--classes', default=os.path.join(here, 'imagenet_subset.txt'))
    p.add_argument('--name', default='imagenet_subset')
    p.add_argument('--out', default=os.path.join(here, '..', 'src', 'cbb15_models'))
    a = p.parse_args()

    labels = build_all.imagenet_labels(a.imagenet_labels)
    picked = read_classes(a.classes, labels)
    fbz = os.path.join(a.devhub, MODEL)
    sub = subset_model(fbz, picked)
    same = check_identical(akida.Model(fbz), sub, picked)
    print(f'{len(picked)} classes, potentials identical to the full model: {same}')
    if not same:
        raise SystemExit('subset potentials differ - not exported')
    names = [labels[i] for i in picked]
    print(export_model.export(
        sub, a.name, a.out, names,
        source=f'Dev Hub {os.path.basename(MODEL)}, classifier cut to {len(picked)} classes',
        notes=['Classes (ImageNet index): ' + ', '.join(str(i) for i in picked[:12]) +
               (' ...' if len(picked) > 12 else '')]))
    # Refresh the local copy in examples/10_ImageNetSubset/src/models.
    sys.argv = [sys.argv[0]]
    sync_examples.main()


if __name__ == '__main__':
    main()
