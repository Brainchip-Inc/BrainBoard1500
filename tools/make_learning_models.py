#!/usr/bin/env python
"""Build the two on-chip learning models of the BrainBoard1500 examples.

Akida learns only in a final FullyConnected layer whose inputs are binary
(1-bit). Both models therefore reuse a trained Dev Hub network as feature
extractor, turn its last feature layer into a 1-bit (binary) layer, and
append a learning layer:

  face_learn   backbone: Dev Hub VWW AkidaNet (96x96x3), layer separable_13
               (256 features after global average pooling)
               classes: 0 = me, 1 = other person, 2 = background
  voice_learn  input: the 304-bit speaker code of tools/cbb15_speaker.py
               (spectral envelope + pitch, computed on the Nicla Vision)
               classes: 0 = my voice, 1 = other voice
               A keyword network is trained to ignore the speaker; on two
               synthetic voices its features reached 77 % speaker accuracy,
               the speaker code 91-98 %.

Binarisation (face model): a 1-bit Akida layer fires when its potential exceeds the
threshold. The threshold of every feature channel is set to the potential
percentile that makes the channel fire on `--rate` of a calibration set, so
that every feature carries information. The learning layer is configured
with AkidaUnsupervised; num_weights follows the measured number of active
features per input.

Calibration data (all public or generated locally):
  images: tiles of akida1/model_zoo/vww/docs/vww_sample_mosaic.png and the
          BrainChip "imagenet_like" sample pack (optional --images folder)
  speech: any folder of 16 kHz mono WAV files (--speech), for example words
          generated with tools/make_tts_calibration.ps1; used for the centre
          of the speaker code quantiser

The result is written as .fbz and exported to src/cbb15_models/.
"""
import argparse
import copy
import glob
import os
import sys
import wave

import akida
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
import cbb15_mfcc  # noqa: E402
import cbb15_speaker  # noqa: E402
import export_model  # noqa: E402

FACE_CLASSES = ['me', 'other person', 'background']
VOICE_CLASSES = ['my voice', 'other voice']


# --- Calibration data --------------------------------------------------------

def image_augmentations(img, size):
    """Crops and flips of one RGB image, each resized to size x size."""
    out = []
    w, h = img.size
    for flip in (False, True):
        im = img.transpose(Image.FLIP_LEFT_RIGHT) if flip else img
        for s in (1.0, 0.8, 0.6):
            cw, ch = int(w * s), int(h * s)
            for ox, oy in ((0, 0), (w - cw, h - ch), ((w - cw) // 2, (h - ch) // 2)):
                out.append(np.array(im.crop((ox, oy, ox + cw, oy + ch)).resize(
                    (size, size), Image.BILINEAR)))
    return out


def image_calibration(devhub, image_dir, size):
    mosaic = Image.open(os.path.join(
        devhub, 'akida1/model_zoo/vww/docs/vww_sample_mosaic.png')).convert('RGB')
    w, h = mosaic.size
    tiles = [mosaic.crop((c * w // 5, r * h // 2, (c + 1) * w // 5, (r + 1) * h // 2))
             for r in range(2) for c in range(5)]
    if image_dir:
        for f in sorted(glob.glob(os.path.join(image_dir, '*.jpg'))):
            tiles.append(Image.open(f).convert('RGB'))
    samples = [a for t in tiles for a in image_augmentations(t, size)]
    return np.stack(samples).astype(np.uint8)


def read_wav(path):
    with wave.open(path) as w:
        if w.getframerate() != 16000 or w.getnchannels() != 1 or w.getsampwidth() != 2:
            raise ValueError(f'{path}: need 16 kHz mono 16-bit')
        return np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)


def speaker_centre(speech_dir):
    """Mean spectral envelope of the calibration speech (quantiser centre)."""
    envs = [cbb15_speaker.analyse(read_wav(f))[0]
            for f in sorted(glob.glob(os.path.join(speech_dir, '*.wav')))]
    if not envs:
        raise ValueError(f'no WAV files in {speech_dir}')
    return np.mean(np.stack(envs), axis=0)


# --- Model surgery -----------------------------------------------------------

def without_head(model_dict, head):
    d = copy.deepcopy(model_dict)
    d['layers'] = [l for l in d['layers'] if l['name'] != head]
    return d


def binarised_backbone(fbz, feature_layer, head, calib, rate):
    """Returns (model, mean active features) with a calibrated 1-bit layer."""
    base = akida.Model(fbz).to_dict()
    d = without_head(base, head)
    if d['layers'][-1]['name'] != feature_layer:
        raise ValueError(f'expected {feature_layer} before {head}')
    # Potentials of the feature layer: same model with the activation off.
    probe = copy.deepcopy(d)
    probe['layers'][-1]['parameters']['activation'] = 0
    pot = akida.Model.from_dict(probe).forward(calib)
    pot = pot.reshape(len(calib), -1).astype(np.int64)

    layer = d['layers'][-1]
    bits = layer['variables']['threshold']['bitwidth']
    limit = 2 ** (bits - 1) - 1
    thr = np.percentile(pot, 100.0 * (1.0 - rate), axis=0)
    thr = np.clip(np.floor(thr), -limit, limit).astype(np.int64)
    layer['parameters']['act_bits'] = 1
    layer['variables']['threshold']['data'] = [int(v) for v in thr]
    model = akida.Model.from_dict(d)
    spikes = model.forward(calib).reshape(len(calib), -1)
    return model, float(spikes.astype(bool).sum(axis=1).mean())


def add_learning_head(model, classes, neurons, num_weights, competition):
    model.add(akida.FullyConnected(units=classes * neurons, name='learning',
                                   activation=False))
    model.compile(optimizer=akida.AkidaUnsupervised(
        num_weights=int(num_weights), num_classes=classes,
        learning_competition=competition))
    return int(num_weights)


def build(args):
    zoo = os.path.join(args.devhub, 'akida1', 'model_zoo')
    os.makedirs(args.fbz_out, exist_ok=True)
    results = []

    # Face model: binary VWW features -> learning layer. num_weights follows
    # the number of active features (about 0.8 of them).
    calib = image_calibration(args.devhub, args.images, 96)
    model, active = binarised_backbone(
        os.path.join(zoo, 'vww/pretrained_models/akidanet_vww_qat.fbz'),
        'separable_13', 'predictions', calib, args.rate)
    nw = add_learning_head(model, len(FACE_CLASSES), args.neurons,
                           max(8, round(0.8 * active)), 0.1)
    model.save(os.path.join(args.fbz_out, 'face_learn.fbz'))
    results.append(export_model.export(
        model, 'face_learn', args.out, FACE_CLASSES,
        source='Dev Hub akidanet_vww_qat.fbz + binary separable_13 + learning head',
        notes=[f'Learning: {len(FACE_CLASSES)} classes x {args.neurons} neurons, '
               f'num_weights {nw}, competition 0.1, '
               f'{active:.1f} of 256 features active on average.']))

    # Voice model: speaker code -> learning layer. The code has 70 active
    # bits (2 per band, 6 for pitch); half of them as num_weights measured best.
    centre = speaker_centre(args.speech)
    model = akida.Model()
    model.add(akida.InputData(input_shape=(1, 1, cbb15_speaker.NUM_BITS), input_bits=1))
    active = 2 * cbb15_speaker.NUM_BANDS + 6
    nw = add_learning_head(model, len(VOICE_CLASSES), args.neurons, active // 2, 0.1)
    model.save(os.path.join(args.fbz_out, 'voice_learn.fbz'))
    centre_c = ', '.join(f'{v:.2f}f' for v in centre)
    extra = ('// Per-band centre (dB) of the speaker code quantiser, measured on the\n'
             '// calibration speech. Pass it to cbb15::SpeakerEncoder::begin().\n'
             f'constexpr float voice_learn_band_centre[{len(centre)}] = {{\n'
             f'    {centre_c}}};\n')
    results.append(export_model.export(
        model, 'voice_learn', args.out, VOICE_CLASSES,
        source='InputData(304 bits) + learning head, input from cbb15::SpeakerEncoder',
        notes=[f'Learning: {len(VOICE_CLASSES)} classes x {args.neurons} neurons, '
               f'num_weights {nw}, competition 0.1.'],
        extra=extra))
    return results


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--devhub', required=True, help='brainchip_devhub checkout (with LFS)')
    p.add_argument('--speech', required=True, help='folder of 16 kHz WAV files')
    p.add_argument('--images', default=None, help='optional folder of extra .jpg images')
    p.add_argument('--rate', type=float, default=0.25,
                   help='target firing rate per binary feature (default 0.25)')
    p.add_argument('--neurons', type=int, default=10, help='neurons per class')
    p.add_argument('--out', default=os.path.join(here, '..', 'src', 'cbb15_models'))
    p.add_argument('--fbz-out', default=os.path.join(here, 'build'))
    for r in build(p.parse_args()):
        print(r)


if __name__ == '__main__':
    main()
