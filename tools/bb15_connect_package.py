"""Package an Akida model for the BrainChip Connect app and NiclaVisionConnect.

The model is mapped for the AKD1500 exactly as export_model.py maps it, in
akida's default map mode unless --map-mode names another, and the serialized
program is split into its program info and program data halves. A multi-pass
program is packaged as it is: the Akida engine runs its passes itself, whether
the program is pushed over SPI or read from a flash slot.
The package is the `.zip` the app sends over Bluetooth:

    <name>/info.yaml
    <name>/<name>_program_info.bin
    <name>/<name>_program_data.bin

The input shape in info.yaml decides which application the sketch stores the
model for, and the app names the model after the `_program_info.bin` prefix,
so `app` in info.yaml is that same name. The zip is byte-for-byte reproducible.

Usage:
    python bb15_connect_package.py model_files/kws/akida_model.fbz \\
        --name brainboard1500_nicla_vision_keyword_spotting \\
        --mfcc-fs 123.56967163085938 --silence-class 10 --unknown-class 11 \\
        --map-mode HwPr
"""

from __future__ import annotations

import argparse
import math
import os
import zipfile

from export_model import SLOT_MAX_DATA, ExportError, map_for_akd1500, split_program

import akida

# The library loads a slot's program data from this offset (kSlotDataOffset).
FLASH_ADDRESS = "0x1000"

# A fixed timestamp, so the same model always produces the same zip.
ZIP_DATE_TIME = (1980, 1, 1, 0, 0, 0)


def info_yaml(
    name: str,
    input_shape: tuple[int, ...],
    output_shape: tuple[int, ...],
    mfcc_fs: float | None,
    silence_class: int | None,
    unknown_class: int | None,
) -> str:
    """Write the info.yaml the app reads before it sends the model.

    Args:
        name: Model name, which is also the prefix of both binaries.
        input_shape: The model's input shape, which picks the application.
        output_shape: The model's output shape.
        mfcc_fs: MFCC full scale of a keyword model, or None for any other.
        silence_class: Index of the silence class, or None.
        unknown_class: Index of the unknown class, or None.

    Returns:
        The YAML text.
    """
    lines = [f"app: {name}", f"flash_address: '{FLASH_ADDRESS}'", "input_shape:"]
    lines += [f"- {value}" for value in input_shape]
    lines.append("output_shape:")
    lines += [f"- {value}" for value in output_shape]
    if mfcc_fs is not None:
        lines.append(f"mfcc_fs: {mfcc_fs!r}")
    if silence_class is not None:
        lines.append(f"silence_class: {silence_class}")
    if unknown_class is not None:
        lines.append(f"unknown_class: {unknown_class}")
    lines += [
        "inference_mode: async",
        "edge_learning:",
        "  enabled: false",
        f"  num_classes: {math.prod(output_shape)}",
        "  num_el_classes: 0",
        "  num_neurons: 1",
    ]
    return "\n".join(lines) + "\n"


def write_package(path: str, name: str, files: dict[str, bytes]) -> None:
    """Write the zip with every file under one folder named after the model.

    Args:
        path: Zip file to write.
        name: Model name, used as the folder inside the zip.
        files: File names and their contents, written in the given order.
    """
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
        for file_name, contents in files.items():
            entry = zipfile.ZipInfo(f"{name}/{file_name}", ZIP_DATE_TIME)
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o644 << 16
            archive.writestr(entry, contents)


def package(
    fbz: str,
    name: str,
    out_dir: str,
    mfcc_fs: float | None = None,
    silence_class: int | None = None,
    unknown_class: int | None = None,
    map_mode: str | None = None,
) -> str:
    """Map a model for the AKD1500 and write its BrainChip Connect package.

    Args:
        fbz: The Akida model to package.
        name: Model name, which the app shows and the board records.
        out_dir: Directory the zip is written to.
        mfcc_fs: MFCC full scale of a keyword model, or None for any other.
        silence_class: Index of the silence class, or None.
        unknown_class: Index of the unknown class, or None.
        map_mode: Name of the akida.MapMode to map with, or None for akida's
            default.

    Returns:
        Path of the zip that was written.

    Raises:
        ExportError: If the program data does not fit a BB15 flash slot.
    """
    model = akida.Model(fbz)
    input_shape = tuple(int(value) for value in model.input_shape)
    output_shape = tuple(int(value) for value in model.output_shape)
    mode = akida.MapMode[map_mode] if map_mode is not None else None
    program, _, _ = map_for_akd1500(model, mode)
    info_size = split_program(program)
    if len(program) - info_size > SLOT_MAX_DATA:
        raise ExportError(f"program data exceeds a flash slot ({SLOT_MAX_DATA} B)")

    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, f"{name}.zip")
    write_package(
        path,
        name,
        {
            "info.yaml": info_yaml(
                name,
                input_shape,
                output_shape,
                mfcc_fs,
                silence_class,
                unknown_class,
            ).encode(),
            f"{name}_program_info.bin": program[:info_size],
            f"{name}_program_data.bin": program[info_size:],
        },
    )
    return path


def main() -> None:
    """Parse the command line and write the package."""
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("fbz")
    parser.add_argument("--name", required=True)
    parser.add_argument("--out", default=".", help="directory for the zip")
    parser.add_argument("--mfcc-fs", type=float, help="keyword models only")
    parser.add_argument("--silence-class", type=int, help="keyword models only")
    parser.add_argument("--unknown-class", type=int, help="keyword models only")
    parser.add_argument(
        "--map-mode",
        choices=list(akida.MapMode.__members__),
        help="akida.MapMode to map with (default: akida's default)",
    )
    args = parser.parse_args()
    print(
        package(
            args.fbz,
            args.name,
            args.out,
            args.mfcc_fs,
            args.silence_class,
            args.unknown_class,
            args.map_mode,
        )
    )


if __name__ == "__main__":
    main()
