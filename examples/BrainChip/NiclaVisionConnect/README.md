# Nicla Vision with the BrainChip Connect app

A BrainChip example for the Nicla Vision on a BrainBoard1500, driven entirely
from the BrainChip Connect phone app over Bluetooth Low Energy instead of from
a desktop tool over USB. It advertises as `BrainBoard1500`, answers the app's
commands, receives its models from the app, stores them in BB15 flash, and
reports detections back. It runs two applications.

**Keyword Spotting** recognises ten keywords, `down`, `go`, `left`, `no`, `off`,
`on`, `right`, `stop`, `up`, `yes`. The model also reports `silence` and
`unknown`, which are scored but can never trigger a detection. The audio
pipeline is the `NiclaVisionKeywordSpotting` example's, value for value: the
same DC blocker, 960-sample blocks, RMS speech gate, MFCC front end,
spectrogram ring, inference period, smoothing, debounce and chiming. The app's
App Controls change the six detector settings live.

**Vision Human Detection** scores the camera against the visual wake words
model and reports a person when it sees one. The camera's whole 320x240 field
of view is squeezed to the model's 96x96 input and turned half a turn, exactly
as the library's `03_PersonDetection` does: the Dev Hub trained the model on
whole images resized to 96x96, and the camera is mounted USB-side down. While
the app's camera view is open, the board sends it a live grayscale preview of
what the model was given.

The sketch chooses the application from the model's input shape, so another
demo is one more input shape, one more flash slot and its own pipeline.

## The models this example needs

**No model is compiled into this sketch.** The app sends each one over
Bluetooth as a `.zip`, and the board writes it into BB15 flash and loads it from
there. A model survives a power cycle and a firmware update, so it only has to
be sent once, and each application has its own slot, so sending one leaves the
other alone.

The keyword spotting package is
`models/brainboard1500_nicla_vision_keyword_spotting.zip`. It is BrainChip's
spark keyword spotting model, the same model as `NiclaVisionKeywordSpotting`'s
`spark_kws.h`, mapped for the AKD1500 with Akida 2.19.3 in
`akida.MapMode.HwPr`: the package's two halves together are byte for byte that
header's program. It was made with
`tools/bb15_connect_package.py`, from a directory holding spark's
`model_files/kws/akida_model.fbz` (SHA-256
`a1a6e2d0755decfccbf3d8165ffd9f949365bc3160aa0dbb12d85123df052abf`), with
`akida==2.19.3` installed:

```bash
python <repository>/tools/bb15_connect_package.py model_files/kws/akida_model.fbz \
    --name brainboard1500_nicla_vision_keyword_spotting \
    --mfcc-fs 123.56967163085938 --silence-class 10 --unknown-class 11 \
    --map-mode HwPr --out <repository>/examples/BrainChip/NiclaVisionConnect/models
```

The human detection package is
`models/brainboard1500_nicla_vision_human_detection.zip`. It is the BrainChip
Dev Hub visual wake words model, the same model as `03_PersonDetection`'s
`vww.h`, mapped for the AKD1500 with Akida 2.19.3 in `akida.MapMode.AllNps`: the
package's two halves together are byte for byte that header's program. It was
made with `tools/bb15_connect_package.py`, from the Dev Hub's
`akida1/model_zoo/vww/pretrained_models/akidanet_vww_qat.fbz` (SHA-256
`90531d024b3359f836651748ff867aeaf0019aa0dd7067b9849fb5a3f5381b22`), with
`akida==2.19.3` installed:

```bash
python <repository>/tools/bb15_connect_package.py akidanet_vww_qat.fbz \
    --name brainboard1500_nicla_vision_human_detection \
    --map-mode AllNps --out <repository>/examples/BrainChip/NiclaVisionConnect/models
```

`AllNps` is a single pass. `HwPr`, which the keyword model uses, maps this model
into three passes, and the library runs single-pass programs only.

Both zips are reproducible: the same model gives the same bytes.

The archive holds `info.yaml` and a `<name>_program_info.bin` /
`<name>_program_data.bin` pair, which is the shape the app reads. `mfcc_fs`,
`silence_class` and `unknown_class` in `info.yaml` are the keyword model's own,
and the pipeline refuses a keyword model without `mfcc_fs`.

**Which application a model is for is decided by the input shape in
`info.yaml`**, which the app writes before it sends any bytes:

| `input_shape` | application | BB15 flash slot |
| --- | --- | --- |
| `[49, 10, 1]` | Keyword Spotting | 2 |
| `[96, 96, 3]` | Vision Human Detection | 1 |

A model whose shape matches no application is refused as soon as its program
info has arrived, before any of the model data is sent.

**The model must be built for the Akida engine this library carries, which is
2.19.3.** A model built for another engine version is refused: the serialized
program format differs between engine versions, and the engine halts the core
on a program it cannot read, so the sketch checks the version before the
engine sees it. That includes the models built for the earlier BrainBoard
library at Akida 2.5.0, and AkidaTag's models, built at Akida 2.17.0. The board
reports the refusal over its serial port and keeps running.

## Where the model is stored

Each model is stored in the library's own flash slot format (see "Model
storage: flash slots" in the repository `README.md`), so the library's examples
see it as an ordinary slot: `01_SpiLinkTest` lists it, and `02_ModelFlasher`
overwrites it cleanly.

| offset in the slot | what |
| --- | --- |
| `0x00000` | the library's slot header: name, sizes and CRC-32 of both halves |
| `0x00100` | this sketch's record: input and output shape, MFCC scale, silence and unknown classes, full model name, then the program info |
| `0x01000` | the program data, which the AKD1500 reads itself through its SPI master |

`flash_address` in `info.yaml` must therefore be `0x1000`, which is the library's
fixed data offset; anything else is refused at START with `ERR_PARAM`.

Each application uses the slot `02_ModelFlasher` gives to the library's own
model of the same kind, keyword spotting slot 2 and human detection slot 1, so
the two sketches replace each other's models and nothing else. After this
sketch has stored its keyword model, `04_KeywordSpotting` reports "flash slot
holds another model" until `02_ModelFlasher` is run again. The human detection
model is upstream's own program, so `03_PersonDetection` runs from the slot this
sketch wrote. After `02_ModelFlasher` has run, this sketch logs that slots 1 and
2 hold models another sketch stored and waits for the app to send its own
again. The slots' user areas, where `08_VoiceLearning` keeps learned weights,
are never touched.

Only one program fits in the Akida fabric, so nothing is loaded until the app
asks for an application to run, and starting the other one is a model swap.
Each slot's program info is read once at boot and kept in memory, so starting an
application costs no flash bridge time: the AKD1500 reads the program data out
of flash itself as it is programmed. Measured on the bench, a swap takes 13 ms
for the keyword model and 46 ms for the human detection one, including one
inference on a blank input, which is how the board satisfies itself the model
really runs before it says so.

## How the model arrives

The board speaks the same model transfer protocol as AkidaTag, so one path in
the app serves both boards. It is specified in `docs/ble-model-transfer.md` in
the AkidaTag firmware repository, which is the contract both sides are built
from; what follows is what a reader of this sketch needs.

A session is the metadata characteristics, then the program info, then the
model data, in that order on one connection. Each half is sent a block at a
time: every write to `f000aa01` opens with the four byte little-endian offset
of the bytes behind it, and at each block boundary the board answers on
`f000aa02` with a fourteen byte status record saying what it did and which byte
it expects next.

**The phone never assumes a block size.** The board reports its own in every
status, and here that is one flash sector, 4,096 bytes, because a sector is the
least the BrainBoard's flash can commit and therefore all of a model the board
ever has to hold. A block is erased, programmed and read back before the board
acknowledges it, so an acknowledged block is one that is genuinely in flash.

`f000aa03` carries the two control messages, START and ABORT. START names the
half and its length. An interrupted transfer is started again from zero rather
than resumed.

The board answers `DONE` when the whole file is stored and checked, and
`READY` only once it has programmed the AKD1500 with the model and scored one
inference with it. Those mean different things and the app shows them
differently: `DONE` is the progress bar reaching 100%, `READY` is the update
having worked.

Two things follow from committing a block at a time, and both are deliberate:

- **From the moment the data half starts, the board has no model for that
  application.** The slot header naming the stored model is erased before the
  first data sector is, because a header describing bytes that are being
  overwritten is a lie. The board says so on its serial port, takes the
  application out of the list it gives the app, and stops scoring. A transfer
  that is then interrupted leaves the board with nothing to run, which is the
  truth, and the next boot reports exactly that rather than a corrupted model.
- **Nothing wedges.** A phone that disconnects part way, or simply stops
  writing, costs the board a staging block and its counters. It goes back to
  advertising and takes a fresh transfer.

The slot header is written last, once the data and the record behind it have
been read back and checked, which is the order the library's own slot writer
uses: a valid header always means a complete model.

ArduinoBLE gives a write handler no way to answer at ATT level, so a malformed
control frame comes back as `ERR_PARAM` on the status characteristic, and a
START that arrives before the phone has subscribed to the status characteristic
is refused in silence, because a board that cannot report on a transfer cannot
report that either. The serial log says which happened.

## Build and run

Install the library's dependencies first, as the repository `README.md`
describes, and ArduinoBLE and Arduino_PowerManagement from the Library Manager.
Then compile and upload from the repository root:

```bash
arduino-cli compile --fqbn arduino:mbed_nicla:nicla_vision --library . examples/BrainChip/NiclaVisionConnect
arduino-cli upload --fqbn arduino:mbed_nicla:nicla_vision --port /dev/cu.usbmodem101 examples/BrainChip/NiclaVisionConnect
```

The sketch brings the BrainBoard up with the library's default configuration,
which fits the BB15 rev 2 it was tested on, power enable on pin 15 included.

Copy both packages in `models/` to the phone. Then open the app, pick
`BrainBoard1500` from the device list, connect, and send a package from the
Model Update screen. Each model that arrives adds its application to the home
screen, so send both to get both; Run Application starts whichever one you
pick.

The serial port carries a short log of what the board is doing, at 115200 baud.
It is not needed to run the demo. Each boot opens with `previous_run=`, which
says whether the last run ended by being asked to restart (`commanded`), was
cut short without asking (`unexpected`), or had no predecessor because the
board lost power (`cold`).

## What the LED says

The RGB LED is driven directly and is active low.

| Colour | Meaning |
| --- | --- |
| Blue, one short flash every two seconds | advertising, no phone connected |
| Green, steady | a phone is connected |
| Blue, fast blink | a model transfer is running |
| Red, one short flash | a keyword was detected, or a person is in view |
| Red, slow blink | setup failed; the serial log says at which stage |

## Battery

The percentage comes from the board's MAX1726x fuel gauge and the charging
state from its PF1550 PMIC. `kBatteryCapacityMah` in `device_status.cpp` has to
match the cell actually fitted, or the percentage will be wrong.

Whether a cell is fitted at all cannot be read on this board while it runs from
USB: the gauge sits on the system rail, so with no cell it reports that rail as
a full, healthy battery and its battery-present bit agrees. A board on USB with
no cell therefore reads as a charged one.

## The camera

Human detection takes frames from the camera with the library's
`cbb15::CameraStream`, which captures the next frame into a second buffer while
the current one is scored, so the sketch's loop never waits for the camera and
keeps answering the phone. The two 320x240 capture buffers are 300 kB, more than
half the RAM, so they are taken only while human detection runs and given back
when it stops. Measured on the bench, scoring one frame takes about 24 ms: 8 ms
to turn the capture into the model's input and 16 ms for the inference, 5.5 ms
of it in the AKD1500 itself.

If the camera does not answer at boot, the board says so on its serial port and
carries on with keyword spotting alone; Vision Human Detection then does not
start, and the serial port says why.

## What the board reports

Both applications report events, not state. Keyword spotting sends a keyword
when one is spoken and says nothing while nobody is speaking. Human detection
sends a person when one arrives in front of the camera and says nothing while
the view is clear.

A keyword is reported once its smoothed score has held at or above 0.50 for
three inferences in a row. A person is reported once the person score has held
at or above 0.50 for three frames in a row, the same threshold and the same
count. Someone who stays in view is not reported again: where a keyword arms the
detector again by ending, a person does not end, so the detector arms again only
once the score has fallen back for three frames. Arriving, leaving and arriving
again is two detections, and standing still is one.

**Nothing is sent when the person leaves, by design.** No end-of-detection
message exists, exactly as none exists when a spoken word stops, so the phone
shows the last person seen until it sees the next one. The LED is the live
indicator instead: it follows every scored frame, so it flashes while a person
is in front of the camera and stays quiet when the view is clear.

## The camera preview frame

Preview frames ride the same notify characteristic as the microphone waveform,
`6e400003-b5a3-f393-e0a9-e50e24dcca9e`, and are told apart from the text frames
by the first byte and from the waveform by the second. The app implements this
layout, so a board change that alters it breaks the phone.

| offset | size | field |
| --- | --- | --- |
| 0 | 1 | magic, `0x42` |
| 1 | 1 | command, `0x0D` for a preview |
| 2 | 2 | uint16 LE image sequence, one per image |
| 4 | 2 | uint16 LE byte offset of this chunk within the image |
| 6 | 1 | image width in pixels |
| 7 | 1 | image height in pixels |
| 8 | 1 | pixel format, `0` for 8-bit grayscale |
| 9 | 1 | reserved, zero |
| 10 | .. | pixels, up to 229 of them |

Every chunk carries the geometry, so a reader needs no state beyond the image
it is assembling: start a new image of `width * height` bytes when the sequence
changes, write each chunk at its own offset, and render when it is full. At
96x96 an image is 9,216 pixels in 41 notifications, 9,626 bytes on the wire once
the headers are counted. A chunk needs a link MTU of at least 242 bytes, which
the app asks for; ArduinoBLE cuts a notification short to fit a smaller one.

**Every image the board sends is whole.** The camera produces frames far faster
than the link carries them, so a frame offered while one is still going out is
dropped rather than queued or spliced in. What the phone receives is therefore
a sequence of complete images, each the newest one available when its turn
came, and the backlog never grows however slow the link is.

The board sends at most four notifications per pass of its loop, so a link slow
to accept them delays the preview and nothing else: inference keeps running at
its own rate and detections keep going out on their own frames ahead of any
image. The serial log reports what the preview is actually achieving every five
seconds, as `[preview] fps= bytes_per_s= dropped=`.

Sending a notification blocks the sketch until the Bluetooth controller has a
buffer free, because `HCIClass::sendAclPkt` busy-waits on its packet credits in
ArduinoBLE, and that wait has no timeout. It is why the preview drops rather
than queues, and it is one reason detections are events rather than a reading
per frame.

## What the app cannot do against this board

The app's edge learning and firmware update screens will not work here, because
this example exposes neither service. Everything else the app offers is
supported: discovery, connection, device information, battery, the application
list and each application's details, running and stopping either detector, the
live microphone waveform, the camera preview, and the six keyword spotting App
Controls.

Leaving the device screen does not drop the Bluetooth link, so the board stays
connected and, as a peripheral serving one central, stops advertising. If the
app then reaches its "Device Disconnected" notice, its RECONNECT button cannot
find the board and restarting the app is what recovers. The board needs nothing
doing to it: its boot log will show no restart, and the LED stays green because
the link really is still up.
