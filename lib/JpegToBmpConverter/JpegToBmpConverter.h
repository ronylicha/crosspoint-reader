#pragma once

#include <HalStorage.h>

class Print;
class ZipFile;

class JpegToBmpConverter {
  static bool jpegFileToBmpStreamInternal(HalFile& jpegFile, Print& bmpOut, int targetWidth, int targetHeight,
                                          bool oneBit, bool crop = true, bool originalThresholds = false);

 public:
  static bool jpegFileToBmpStream(HalFile& jpegFile, Print& bmpOut, bool crop = true, bool originalThresholds = false);
  // Convert with custom target size (for thumbnails)
  static bool jpegFileToBmpStreamWithSize(HalFile& jpegFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight);
  // Convert to 1-bit BMP (black and white only, no grays) for fast home screen rendering
  static bool jpegFileTo1BitBmpStreamWithSize(HalFile& jpegFile, Print& bmpOut, int targetMaxWidth,
                                              int targetMaxHeight);
  // True only when the header parses and describes a JPEG no conversion can
  // handle (size or subsampling), so a caller can stop retrying it.
  static bool hasUnsupportedLayout(HalFile& jpegFile);
};
