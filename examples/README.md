# BrainBoard1500 examples

All examples run on an Arduino Nicla Vision stacked on a BB15 rev 2 and print
every result to the serial monitor (115200 baud) and to an SSD1306 128x64 OLED
at I2C address 0x3C. Every example starts with the SPI link test (AKD1500
manufacturer id) and shows the power of the 0.8 V AKD1500 core rail.

| Example | Model (slot) | Input | LEDs |
|---|---|---|---|
| `01_SpiLinkTest` | – | – | – |
| `02_ModelFlasher` | writes slots 1–4 | sample images | green when done |
| `03_PersonDetection` | vww (1) | camera, full frame → 96x96 | green = person |
| `04_KeywordSpotting` | kws (2) | PDM microphone → MFCC 49x10 | green = keyword |
| `05_ImageNetClassification` | imagenet (3) | camera, centre 210x210 → 160x160 | – |
| `06_EcgArrhythmia` | ecg (0, SPI push) | MIT-BIH test beats | green = normal, red = ectopic |
| `07_FaceLearning` | face_learn (4) | camera, centre 240x240 → 96x96 | green = you, blue = someone else |
| `08_VoiceLearning` | voice_learn (0, SPI push) | PDM microphone → speaker code | green = you, blue = someone else |
| `09_MaxDutyCycle` | vww (1) | input written once into AKD1500 SRAM, 15-job pipeline | – |
| `10_ImageNetSubset` | imagenet_subset (0, SPI push) | camera, centre 210x210 → 160x160, 100 own classes | green = sure |
| `11_PowerModes` | vww (1) | sample image resident in AKD1500 SRAM; 100 % / 10 % / idle / sleep / off | red / yellow / green / blue / white |

Each example selects where its model comes from with one constant:

```cpp
// 1..4 = BB15 external flash slot (written by 02_ModelFlasher)
// 0    = do not use the flash, push the model into the AKD1500 via SPI
constexpr uint8_t kModelSlot = 1;
```

The camera examples rotate the image by 180°: hold the Nicla Vision with the
micro-USB connector pointing down.

The learning examples are controlled with single letters on the serial
monitor (`m` me, `o` other, `b` background, `s` save, `l` load, `f` forget).
Teach every class several times; each learning step stores one example in one
neuron (10 neurons per class).
