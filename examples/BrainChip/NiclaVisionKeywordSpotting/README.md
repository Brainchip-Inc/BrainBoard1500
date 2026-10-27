# Nicla Vision Keyword Spotting

A BrainChip example for the Nicla Vision on a BrainBoard1500. It listens on the
onboard PDM microphone, computes MFCC features on the STM32H747, classifies
them on the AKD1500, and streams the waveform, the features and the twelve
class scores over USB to a desktop tool, `tools/bb15_kws_gui.py`.

It recognises ten keywords: `down`, `go`, `left`, `no`, `off`, `on`, `right`,
`stop`, `up`, `yes`. The model also reports `silence` and `unknown`, which are
transmitted but can never trigger a detection.

The signal pipeline is the one BrainChip's spark firmware runs on the AkidaTag,
value for value: the same DC blocker, 960-sample blocks, RMS speech gate, MFCC
front end, spectrogram ring, inference period, smoothing, debounce and
chiming. The one exception is the speech gate's RMS threshold, which is set
from this board's measured noise floor; "Microphone gain" below explains why.
This is a different model and front end from the library's own
`04_KeywordSpotting`, which runs the Dev Hub speech commands model.

## The model

`spark_kws.h` is BrainChip's spark keyword spotting model, the same model the
AkidaTag runs, mapped for the AKD1500 with Akida 2.19.3. It is pushed into the
AKD1500 over SPI at start-up (slot 0), so the demo writes nothing to the BB15
flash and needs no flasher. The program is 54,344 bytes and is compiled into
the sketch, so it lives in STM32H747 program flash.

The source model is spark's `model_files/kws/akida_model.fbz`, saved by Akida
2.17.0, SHA-256
`a1a6e2d0755decfccbf3d8165ffd9f949365bc3160aa0dbb12d85123df052abf`. Akida
2.19.3 reads it unchanged, and its software outputs on 256 test inputs are
identical to Akida 2.17.0's. The header was made with the library's own
exporter, from a directory holding that file at `model_files/kws/`, with
`akida==2.19.3` installed:

```bash
python <repository>/tools/export_model.py model_files/kws/akida_model.fbz \
    --name spark_kws \
    --labels "down,go,left,no,off,on,right,stop,up,yes,silence,unknown" \
    --out <repository>/examples/BrainChip/NiclaVisionKeywordSpotting
```

The mapping is deterministic: running it again reproduces the header byte for
byte.

## Build and run

Install the library's dependencies first, as the repository `README.md`
describes. Then compile and upload from the repository root:

```bash
arduino-cli compile --fqbn arduino:mbed_nicla:nicla_vision --library . examples/BrainChip/NiclaVisionKeywordSpotting
arduino-cli upload --fqbn arduino:mbed_nicla:nicla_vision --port /dev/cu.usbmodem101 examples/BrainChip/NiclaVisionKeywordSpotting
```

The sketch brings the BrainBoard up with the library's default configuration,
which fits the BB15 rev 2 it was tested on, power enable on pin 15 included.

Then start the desktop tool. `PY` has to be a Python with Tk 8.6 or later, not
necessarily the `python3` on your PATH; "The Python the desktop tool needs"
below explains why and how to check:

```bash
PY=/path/to/a/python/with/tk/8.6/or/later/bin/python3
"$PY" -m pip install --target ~/.kws-libs "pyserial>=3.5"
PYTHONPATH=~/.kws-libs "$PY" tools/bb15_kws_gui.py --port /dev/cu.usbmodem101
```

Replace the port with the one your operating system assigned. The tool reads
the board out of the config packet and names it in its window.

There is no boot wait: the sketch answers as soon as the host opens the port.

`Serial` here is native USB CDC on the STM32H747, so the baud value is not
load-bearing: the link runs at USB speed whatever is asked for. The sketch and
the tool both say 921600.

## The Python the desktop tool needs

The tool needs one environment that has **both Tk 8.6 or later and
`pyserial`**.

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
environment's `python`, as the commands above do. That leaves `sys.prefix`
alone, so Tcl still finds its scripts.

## What runs where

The Nicla Vision's MP34DT06JTR microphone is wired to the STM32H747's DFSDM
peripheral, so audio arrives in the application processor directly and the
core's bundled `PDM` library is the whole capture path. The DFSDM delivers 256
samples per half transfer, and `py_audio_init()` will not accept a double
buffer smaller than 1024 bytes, so the demo asks for exactly that: 512 samples,
32 ms of audio per callback.

Two details of that library shape the sketch:

- `setBufferSize()` must be called before `begin()`. `begin()` captures the
  buffer pointer before `py_audio_init()` runs, and resizing reallocates the
  buffer, which would leave the library filling freed memory.
- `onReceive()` runs in interrupt context, so it only raises a flag. The read,
  the filtering, the MFCC and the inference all happen in `loop()`.

From there everything runs on the STM32H747 and the AKD1500:

| stage | where |
| --- | --- |
| 16 kHz mono capture, 32 ms buffers | STM32H747 DFSDM |
| DC blocking and block RMS | STM32H747 |
| MFCC, 3 frames per 60 ms block | STM32H747 |
| classification, every third block | AKD1500, through `cbb15::Akida` |

The front end matches spark: 16 kHz, a 320-sample hop, a 640-sample frame, a
640-point real FFT, 40 mel bins from 20 Hz to 4000 Hz, and 49 frames of 10
coefficients. The gating, smoothing, debounce and chiming defaults are spark's,
hard-coded rather than configurable.

`cbb15::Akida::infer()` returns the potentials already dequantized, which is
what spark softmaxes, so the sketch softmaxes them directly.

`mfcc.{h,cpp}` and `kiss_fft*.{c,h}` are the same files as in
`NiclaVisionConnect`. They are board independent, and keeping a copy in each
sketch folder keeps each example a self-contained Arduino sketch.

## Microphone gain

`PDM.setGain()` is not a decibel figure. The library turns it into a right
shift of the DFSDM output, `attenuation = 8 - gain / 3` clamped at 0, over a
default attenuation of 5 that applies when `setGain()` is never called. So only
every third step changes anything, this demo's 12 gives an attenuation of 4,
and 24 or above gives an attenuation of 0, which is the loudest the library
goes.

`kPdmGain` and `kRmsThreshold` are one setting written as two constants, but
they do not scale by the same factor. The gain is a right shift applied to
everything the DFSDM produces, so speech scales with it exactly, while about
250 counts of the quiet-room floor are fixed in the board and do not move with
the gain. Going from gain 8 to 12 made speech four times larger and the floor
about 2.2 times larger. The gate has to hold its ratio to the noise rather than
to speech, so it is spark's 550 doubled, 1100.

The floor belongs to the room and the moment rather than to the board, so
`kRmsThreshold` is worth re-measuring wherever the demo is set up. If a quieter
room or a farther talker leaves the SPEECH badge dark, lower `kRmsThreshold`
before touching `kPdmGain`: the model goes on seeing the MFCC magnitudes it
was validated against, where raising the gain changes them. Raising the gain
also stops paying: at gain 15 most utterances at arm's length reach full
scale, and a clipped waveform produces wrong features however loud it is.

## Packets are built whole before they are sent

Every `Serial.write()` on native USB CDC is its own blocking USB transfer.
Writing a result packet field by field costs hundreds of them, sixteen times a
second, and a host that closes the port mid-packet can leave the sketch blocked
in that path for good. So the sketch builds each packet in `g_packet` and hands
it over in one `Serial.write(buffer, size)`.

## Reading the display

- **Result card**: the detected keyword, and the ten keyword classes as bars
  against the 0.50 score threshold. `silence` and `unknown` are hidden here
  because they cannot trigger; the device still sends them.
- **Waveform**: the DC-blocked microphone signal over the last 3.6 seconds.
- **SPEECH / IDLE badge**, at the top right of the waveform: the voice activity
  state, which decides whether a block reaches the MFCC front end at all. It
  stays on `SPEECH` through short pauses, because the gate holds open for
  1300 ms after the last block above the RMS threshold, and the `features` time
  in the telemetry row drops to `0 ms` whenever it reads `IDLE`.
- **Level meter**, under the waveform: block RMS in dBFS, with a tick at the
  RMS threshold the gate uses.
- **dropped**, in the telemetry row: PDM buffers the sketch never saw, counted
  when the capture interrupt lands on a buffer the previous one has not been
  read out of yet.

Say a keyword close to the board. A detection needs the class to hold above
0.50 for three consecutive inferences, so a single ambiguous frame will not
fire.

## When the board looks dead

While it is not streaming, the sketch reports itself every two seconds, so a
plain terminal is enough to tell a healthy board from a wedged one:

```bash
PORT=/dev/cu.usbmodem101
stty -f $PORT 921600 raw && head -c 400 $PORT
```

| what you see | what it means |
| --- | --- |
| `[NiclaVisionKeywordSpotting] idle ready=1 waiting_for_start_stream` | The board is fine, and the tool simply needs to ask again. |
| `[NiclaVisionKeywordSpotting] idle result=FAIL status=0x..` | Bring-up failed and the sketch is saying which stage. `0x81` is the MFCC front end, `0x82` the PDM microphone, `0x83` the BrainBoard. |
| nothing at all | Nothing is running the sketch. Re-upload it. |

The boot log names the failing BrainBoard step and the library's reason, for
example `detail=board: SPI link failed`. A link failure is most often
mechanical: the BB15 header pins seat shallow in the Nicla Vision sockets.
`examples/01_SpiLinkTest` is the shortest way to confirm the link on its own.
