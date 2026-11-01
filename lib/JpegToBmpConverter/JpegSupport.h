#pragma once

// Luma sampling factors (JPEGDEC::getSubSample()) the decoder can lay out.
// For any other value, such as 4:1:1 (0x41), JPEGDEC leaves the MCU size at 0
// and divides by it: a hardware exception on the ESP32-S3, garbage elsewhere.
// 0x00 is grayscale and 0x01 a single-sampled RGB/CMYK image.
constexpr bool jpegSubsamplingSupported(const int subsample) {
  switch (subsample) {
    case 0x00:
    case 0x01:
    case 0x11:
    case 0x12:
    case 0x21:
    case 0x22:
      return true;
    default:
      return false;
  }
}
