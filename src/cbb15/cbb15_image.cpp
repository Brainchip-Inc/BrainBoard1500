#include "cbb15_image.h"

namespace cbb15 {

ImageRegion fullFrame(const FrameFormat& format) {
  ImageRegion r;
  r.width = format.width;
  r.height = format.height;
  return r;
}

ImageRegion centreSquare(const FrameFormat& format) {
  return centreSquare(format, 1.0f);
}

ImageRegion centreSquare(const FrameFormat& format, float fraction) {
  const uint16_t shorter =
      format.width < format.height ? format.width : format.height;
  if (fraction <= 0.0f || fraction > 1.0f) {
    fraction = 1.0f;
  }
  ImageRegion r;
  r.width = r.height = static_cast<uint16_t>(shorter * fraction + 0.5f);
  r.x = static_cast<uint16_t>((format.width - r.width) / 2);
  r.y = static_cast<uint16_t>((format.height - r.height) / 2);
  return r;
}

void rgb565ToRgb(const uint8_t* frame, const FrameFormat& format,
                 const ImageRegion& region, uint8_t* out, uint16_t outWidth,
                 uint16_t outHeight) {
  for (uint16_t oy = 0; oy < outHeight; ++oy) {
    // Source rows covered by this output row (at least one).
    uint32_t y0 = region.y + uint32_t(oy) * region.height / outHeight;
    uint32_t y1 = region.y + uint32_t(oy + 1) * region.height / outHeight;
    if (y1 <= y0) {
      y1 = y0 + 1;
    }
    for (uint16_t ox = 0; ox < outWidth; ++ox) {
      uint32_t x0 = region.x + uint32_t(ox) * region.width / outWidth;
      uint32_t x1 = region.x + uint32_t(ox + 1) * region.width / outWidth;
      if (x1 <= x0) {
        x1 = x0 + 1;
      }
      uint32_t r = 0, g = 0, b = 0, n = 0;
      for (uint32_t y = y0; y < y1; ++y) {
        // With rotate180 the region is read mirrored in both directions.
        const uint32_t sy = format.rotate180 ? (format.height - 1 - y) : y;
        const uint8_t* row = frame + sy * format.width * 2u;
        for (uint32_t x = x0; x < x1; ++x) {
          const uint32_t sx = format.rotate180 ? (format.width - 1 - x) : x;
          const uint8_t* p = row + sx * 2u;
          const uint16_t v = format.bigEndian ? uint16_t((p[0] << 8) | p[1])
                                              : uint16_t((p[1] << 8) | p[0]);
          // Expand 5/6/5 bits to 8 bits with bit replication.
          const uint8_t r5 = (v >> 11) & 0x1F;
          const uint8_t g6 = (v >> 5) & 0x3F;
          const uint8_t b5 = v & 0x1F;
          r += (r5 << 3) | (r5 >> 2);
          g += (g6 << 2) | (g6 >> 4);
          b += (b5 << 3) | (b5 >> 2);
          ++n;
        }
      }
      uint8_t* o = out + (uint32_t(oy) * outWidth + ox) * 3u;
      o[0] = static_cast<uint8_t>((r + n / 2) / n);
      o[1] = static_cast<uint8_t>((g + n / 2) / n);
      o[2] = static_cast<uint8_t>((b + n / 2) / n);
    }
  }
}

uint8_t meanLuma(const uint8_t* rgb, size_t pixels) {
  if (pixels == 0) {
    return 0;
  }
  uint64_t sum = 0;
  for (size_t i = 0; i < pixels; ++i) {
    const uint8_t* p = rgb + i * 3;
    sum += (77u * p[0] + 150u * p[1] + 29u * p[2]) >> 8;
  }
  return static_cast<uint8_t>(sum / pixels);
}

}  // namespace cbb15
