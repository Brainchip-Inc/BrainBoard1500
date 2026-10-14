#!/usr/bin/env python
"""Copy the generated models into the example sketches that use them.

Every example keeps local copies of its models and sample inputs directly in
its sketch folder, next to the .ino, and includes them as "<name>.h". (The
Arduino IDE opens these headers as tabs; the large ones can take a moment.)

The master copies live in src/cbb15_models/ and are written by
export_model.py, make_learning_models.py, make_imagenet_subset.py and
make_samples.py. Run this script after regenerating any model, so that the
flasher and the example sketches always use identical models (the flash slot
header check compares CRCs and would refuse a mismatch).

Usage:  python sync_examples.py           copy and report
        python sync_examples.py --check   only report differences (exit 1)
"""
import argparse
import filecmp
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))
MASTER = os.path.join(ROOT, 'src', 'cbb15_models')
EXAMPLES = os.path.join(ROOT, 'examples')
# Matches <cbb15_models/x.h>, "src/models/x.h" and "x.h"; only names that
# exist in src/cbb15_models count as models.
INCLUDE = re.compile(
    r'#include\s+["<](?:cbb15_models/|src/models/)?([A-Za-z0-9_]+\.h)[">]')


def is_model(name):
    return os.path.exists(os.path.join(MASTER, name))


def used_models(sketch_dir):
    names = set()
    for f in os.listdir(sketch_dir):
        if f.endswith('.ino'):
            text = open(os.path.join(sketch_dir, f), encoding='utf8').read()
            names.update(n for n in INCLUDE.findall(text) if is_model(n))
    return sorted(names)


def local_include(match):
    name = match.group(1)
    return f'#include "{name}"' if is_model(name) else match.group(0)


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--check', action='store_true')
    a = p.parse_args()
    stale = 0
    for sketch in sorted(os.listdir(EXAMPLES)):
        sketch_dir = os.path.join(EXAMPLES, sketch)
        if not os.path.isdir(sketch_dir):
            continue
        models = used_models(sketch_dir)
        if not models:
            continue
        dest = sketch_dir
        for name in models:
            src = os.path.join(MASTER, name)
            dst = os.path.join(dest, name)
            if not os.path.exists(src):
                sys.exit(f'{sketch}: {name} not found in src/cbb15_models')
            same = os.path.exists(dst) and filecmp.cmp(src, dst, shallow=False)
            if same:
                continue
            stale += 1
            print(f'{sketch}: {name} {"differs" if os.path.exists(dst) else "missing"}')
            if not a.check:
                os.makedirs(dest, exist_ok=True)
                shutil.copyfile(src, dst)
        if not a.check:
            # Use the local copies.
            for f in os.listdir(sketch_dir):
                if f.endswith('.ino'):
                    path = os.path.join(sketch_dir, f)
                    text = open(path, encoding='utf8').read()
                    new = INCLUDE.sub(local_include, text)
                    if new != text:
                        open(path, 'w', encoding='utf8', newline='\n').write(new)
    print(f'{stale} file(s) {"out of date" if a.check else "updated"}')
    if a.check and stale:
        sys.exit(1)


if __name__ == '__main__':
    main()
