# Nicla Voice Keyword Spotting

This BrainChip example runs on an Arduino Nicla Voice stacked on a BB15. It
listens on the onboard microphone, computes MFCC features on the nRF52832,
classifies them on the AKD1500, and streams the waveform, the features and the
twelve class scores over USB to a desktop tool.

It recognises ten keywords: `down`, `go`, `left`, `no`, `off`, `on`, `right`,
`stop`, `up`, `yes`. The model also reports `silence` and `unknown`, which are
transmitted but can never trigger a detection.

The model is compiled into the sketch and pushed into the AKD1500 over SPI
(slot 0) at start-up, so this example needs no flasher sketch and leaves the
BB15 flash slots alone.

## Wiring on the Nicla Voice

The library's default pins are the Nicla Vision's, so the sketch passes the
Nicla Voice ones in its `cbb15::BoardConfig`. No library change is needed.

| BB15 signal | Nicla Voice pin |
| --- | --- |
| RESET (power enable) | 10 (A0) |
| AKIDA_CS | 6 (SS) |
| TT_MODE_CS (flash bridge) | 0 (GPIO3) |
| INT | 5 (GPIO0) |

The AKD1500 SPI runs at 8 MHz, the nRF52832's fastest SPI clock, rather than
the library's 25 MHz.

## Build and run

Install the library's dependencies first, as the repository `README.md`
describes; the library needs them even though this example draws on no
display. Then compile and upload from the repository root:

```bash
arduino-cli compile --fqbn arduino:mbed_nicla:nicla_voice --library . examples/BrainChip/NiclaVoiceKeywordSpotting
arduino-cli upload --fqbn arduino:mbed_nicla:nicla_voice --port /dev/cu.usbmodem9AD4C4763 examples/BrainChip/NiclaVoiceKeywordSpotting
```

Then start the desktop tool. `PY` has to be a Python with Tk 8.6 or later, not
necessarily the `python3` on your PATH, and "The Python the desktop tool needs"
below explains why and how to check:

```bash
PY=/path/to/a/python/with/tk/8.6/or/later/bin/python3
"$PY" -m pip install --target ~/.kws-libs "pyserial>=3.5"
PYTHONPATH=~/.kws-libs "$PY" tools/bb15_kws_gui.py --port /dev/cu.usbmodem9AD4C4763
```

Replace the port with the one your operating system assigned. Read that section
before the first run: a Python that cannot draw shows an empty window rather
than an error.

**The sketch takes about ten seconds after reset before it answers anything.**
It loads 516 KB of NDP120 firmware packages from the board's QSPI flash first.
The tool retries across that window and shows `CONNECTING` while it waits.

The USB link runs at **921600 baud**, not the 115200 the library's own examples
use. `Serial` on this board is a UART bridged to USB by the onboard SAMD11, and
at 115200 one result packet takes 37 ms to drain, longer than the NDP120's
24 ms audio chunk period, which breaks the audio stream. The sketch and the
tool both default to 921600; do not attach a text serial monitor while
streaming.

## What runs where

The Nicla Voice microphone reaches the NDP120 only, never the nRF52832
(Nicla Voice datasheet section 4.7), so audio is pulled back out of the NDP120
over SPI using the Arduino core's bundled `NDP` library. That needs all three
firmware packages already on the board's flash: with only the MCU and DSP
packages loaded the audio holding tank never advances. This demo uses none of
the neural-network package's own classes; it is loaded because it carries the
DSP audio flow configuration.

From there everything runs on the nRF52832 and the AKD1500:

| stage | where |
| --- | --- |
| 16 kHz mono capture, 24 ms chunks | NDP120 over SPI |
| DC blocking and block RMS | nRF52832 |
| MFCC, 3 frames per 60 ms block | nRF52832 |
| classification, every third block | AKD1500 on BB15 |

The front end matches BrainChip's spark firmware value for value: 16 kHz, a
320-sample hop, a 640-sample frame, a 640-point real FFT, 40 mel bins from
20 Hz to 4000 Hz, and 49 frames of 10 coefficients. The gating, smoothing,
threshold, debounce and chiming defaults are spark's, hard-coded rather than
configurable. spark is the firmware BrainChip now publishes as
[AkidaTag](https://github.com/Brainchip-Inc/AkidaTag); the `source/` paths the
code comments cite are under `src/` there.

`cbb15::Akida::infer()` returns the potentials already dequantized, which is
what spark softmaxes, so the sketch softmaxes them directly.

`mfcc.{h,cpp}` and `kiss_fft*.{c,h}` are the same files as in the Nicla Vision
examples. They are board independent, and keeping a copy in each sketch folder
keeps each example a self-contained Arduino sketch.

## Reading the display

- **Result card**: the detected keyword, and the ten keyword classes as bars
  against the 0.50 score threshold. `silence` and `unknown` are hidden here
  because they cannot trigger; the device still sends them.
- **Waveform**: the DC-blocked microphone signal over the last 3.6 seconds.
- **SPEECH / IDLE badge**, at the top right of the waveform: spark's voice
  activity state, which decides whether a block reaches the MFCC front end at
  all. It stays on `SPEECH` through short pauses, because the gate holds open
  for 1300 ms after the last block above the RMS threshold, and the `features`
  time in the telemetry row drops to `0 ms` whenever it reads `IDLE`.
- **Level meter**, under the waveform: block RMS in dBFS, with a tick at the
  RMS threshold the gate uses.

The MFCC features themselves are not drawn. The device still computes and
transmits them, and the packet still carries them, so a tool that wants them
has them.

Say a keyword close to the board. A detection needs the class to hold above
0.50 for three consecutive inferences, so a single ambiguous frame will not
fire.

## The keyword model

`spark_kws.h` is byte for byte the same file as
`../NiclaVisionKeywordSpotting/spark_kws.h`: BrainChip's spark keyword spotting
model, the one the AkidaTag runs and the original BrainChip Nicla Voice demo ran
at Akida 2.5.0, mapped for the AKD1500 with Akida 2.19.3 in
`akida.MapMode.HwPr`. That example's README explains the map mode.

The source model is spark's `model_files/kws/akida_model.fbz`, saved by Akida
2.17.0, SHA-256
`a1a6e2d0755decfccbf3d8165ffd9f949365bc3160aa0dbb12d85123df052abf`. The mapped
program data is byte-identical to the AkidaTag's own AKD1500 build of it; only
the program info differs, because it records the engine version. To rebuild the
header, run this from a directory holding that file at `model_files/kws/`, with
`akida==2.19.3` installed (Python 3.10 to 3.12; the package has Linux and
Windows wheels only):

```bash
PYTHONPATH=<repository>/tools python -c '
import akida, export_model
export_model.export(
    "model_files/kws/akida_model.fbz", "spark_kws",
    "<repository>/examples/BrainChip/NiclaVoiceKeywordSpotting",
    labels="down,go,left,no,off,on,right,stop,up,yes,silence,unknown".split(","),
    mode=akida.MapMode.HwPr, notes=["Map mode: HwPr."])'
```

The mapping is deterministic: running it again reproduces the header byte for
byte.

## Memory

The nRF52832 is small: 527,616 bytes of program flash and 64,288 bytes of RAM
are available to a sketch. Built with the Arduino core `arduino:mbed_nicla`
4.6.0:

| | |
| --- | --- |
| flash | about 241 KB, 45% (a few hundred bytes vary with the build path) |
| static RAM | 40,240 bytes, 62%, leaving 24,048 bytes for the heap and stack |
| model | 54,344 bytes in program flash, pushed into the AKD1500 at start-up |

Every audio and MFCC buffer is a fixed-size static, so the static RAM figure
covers them. The Akida runtime allocates its own tensors on the heap while it
loads the model and on every inference. The library's own
`04_KeywordSpotting` is written for the Nicla Vision: its 64 KB audio ring alone
is more than this board's RAM.

## The Python the desktop tool needs

The tool needs one environment that has **both Tk 8.6 or later and `pyserial`**.
`tools/requirements.txt` is the model pipeline's list, not the tool's, so
install `pyserial` on its own.

Apple's system Tcl/Tk 8.5, which `/usr/bin/python3` uses on macOS, draws
nothing: the window opens at the right size and stays blank, with no error.

A plain virtual environment over a standalone CPython build does not solve it
and breaks Tk instead, because Tcl looks for `init.tcl` relative to
`sys.prefix`:

```
_tkinter.TclError: Cannot find a usable init.tcl in the following directories: ...
```

What works whatever the interpreter is to install `pyserial` beside it and put
it on `PYTHONPATH`, launching the base interpreter rather than any virtual
environment's `python`. That leaves `sys.prefix` alone, so Tcl still finds its
scripts:

```bash
PY=/path/to/a/python/with/tk/8.6/or/later/bin/python3
"$PY" -m pip install --target ~/.kws-libs "pyserial>=3.5"
PYTHONPATH=~/.kws-libs "$PY" tools/bb15_kws_gui.py --port /dev/cu.usbmodem9AD4C4763
```

Installing straight into the interpreter also works, but only if that
interpreter is not marked externally managed. Standalone builds, including the
ones `uv` manages, are, and refuse with a PEP 668 error unless you add
`--break-system-packages`:

```bash
"$PY" -m pip install "pyserial>=3.5"   # may refuse, see PEP 668
```

## When the board looks dead

**USB enumeration proves nothing about the sketch.** The Nicla Voice appears on
USB as a composite CMSIS-DAP and serial device provided by the onboard SAMD11
bridge, not by the nRF52832 (datasheet section 4.8). The board can therefore
enumerate, list correctly under `arduino-cli board list`, and hand you a serial
port while the nRF52832 is running something else entirely or nothing at all.

**Listen before concluding anything.** While it is not streaming, the sketch
reports itself every two seconds, so a plain terminal is enough to tell a
healthy board from a wedged one:

```bash
PORT=/dev/cu.usbmodem9AD4C4763
stty -f $PORT 921600 raw && head -c 400 $PORT
```

Three outcomes, and they mean different things:

| what you see | what it means |
| --- | --- |
| `[NiclaVoiceKeywordSpotting] idle ready=1 waiting_for_start_stream` | The board is fine. It is waiting to be asked, and the tool simply needs to ask again. |
| `[NiclaVoiceKeywordSpotting] idle result=FAIL status=0x..` | Bring-up failed and the sketch is saying which stage. `0x81` is the MFCC front end, `0x82` the NDP120 microphone, `0x83` BB15. A power cycle is the first thing to try. |
| nothing at all | Either it is still inside its ten-second boot, or something is genuinely wrong. Wait fifteen seconds and look again before treating it as broken. |

For a `0x83`, the boot log says more. The library logs each BB15 bring-up step
(I/O expander, AKD1500 manufacturer id, IP version), and the sketch then names
the failing step and the library's reason, for example
`detail=board: I/O expander 0x43 not found` when the BB15 does not answer on
I2C or `detail=board: SPI link failed` when the AKD1500 does not answer on SPI.
Reset the board with the terminal above already listening to catch it. If the engine itself hits an unrecoverable error, the library prints
`[AKIDA PANIC]` and its reason once a second instead.

**Do not diagnose this board by sending one command once.** Opening the port
reconfigures the USB bridge's UART, and a request written immediately afterwards
is lost more often than not: measured on a healthy, streaming board, a single
open-write-read attempt came back empty nine times in fifteen. Retrying the
request for a few seconds, which the bundled tool does, brings that down to
roughly one in twelve. A board that fails a single-shot probe is usually a
healthy board that was asked at the wrong moment.

If the terminal shows nothing after a fifteen-second look, then check what is
actually on the board: any other sketch, including a debugging build, leaves
this demo silent in a way that looks exactly like a broken tool. Re-upload this
example.

The board is its own debug probe, so `arduino-cli upload` works over the same
USB cable with no external hardware. openocd finds the onboard CMSIS-DAP at
`0x2341:0x0065` and programs only the sketch's own flash sectors; the QSPI
flash holding the NDP120 packages is never touched.
