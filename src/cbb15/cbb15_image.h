// BrainBoard1500 - camera frame preprocessing for the Akida vision models.
//
// The Nicla Vision GC2145 delivers RGB565 frames. The helpers below turn a
// rectangular region of such a frame into the uint8 RGB tensor
// [height][width][3] that the Akida models expect (no normalisation: the
// models rescale internally). Resampling is an area average (box filter),
// which keeps fine detail stable when a large frame is shrunk.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace cbb15 {

struct ImageRegion {
  uint16_t x = 0;
  uint16_t y = 0;
  uint16_t width = 0;
  uint16_t height = 0;
};

struct FrameFormat {
  uint16_t width = 320;
  uint16_t height = 240;
  // GC2145 RGB565 arrives high byte first.
  bool bigEndian = true;
  // Rotate the image by 180 degrees. With the Nicla Vision held upright,
  // micro-USB connector at the bottom, the sensor image is upside down.
  bool rotate180 = true;
};

// Whole frame (full field of view).
ImageRegion fullFrame(const FrameFormat& format);
// Largest centred square of the frame.
ImageRegion centreSquare(const FrameFormat& format);
// Centred square that covers `fraction` (0..1] of the shorter side.
ImageRegion centreSquare(const FrameFormat& format, float fraction);

// Converts `region` of an RGB565 frame to an RGB888 tensor of outWidth x
// outHeight pixels. Any aspect ratio change is a plain stretch, exactly like
// a resize to the model input size during training.
void rgb565ToRgb(const uint8_t* frame, const FrameFormat& format,
                 const ImageRegion& region, uint8_t* out, uint16_t outWidth,
                 uint16_t outHeight);

// Mean luminance (0..255) of an RGB888 tensor, handy for exposure checks.
uint8_t meanLuma(const uint8_t* rgb, size_t pixels);

}  // namespace cbb15
