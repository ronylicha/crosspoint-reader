#include <JPEGDEC.h>
#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "JpegSupport.h"

namespace {

std::vector<uint8_t> readFixture(const std::string& name) {
  std::ifstream in(std::string(JPEG_FIXTURE_DIR) + "/" + name, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

int drawNothing(JPEGDRAW*) { return 1; }

struct Fixture {
  const char* file;
  int subsample;
};

// One small synthetic image per sampling layout.
constexpr Fixture DECODABLE[] = {
    {"gray.jpg", 0x00},         {"sampling_1x1.jpg", 0x11}, {"sampling_1x2.jpg", 0x12},
    {"sampling_2x1.jpg", 0x21}, {"sampling_2x2.jpg", 0x22},
};

}  // namespace

TEST(JpegSupport, AcceptsTheLayoutsJpegdecDecodes) {
  for (const int subsample : {0x00, 0x01, 0x11, 0x12, 0x21, 0x22}) {
    EXPECT_TRUE(jpegSubsamplingSupported(subsample)) << std::hex << subsample;
  }
}

TEST(JpegSupport, RejectsOtherLayouts) {
  for (const int subsample : {0x41, 0x14, 0x42, 0x24, 0x31, 0x13, 0x44, 0x02, 0x10, 0xFF}) {
    EXPECT_FALSE(jpegSubsamplingSupported(subsample)) << std::hex << subsample;
  }
}

TEST(JpegSupport, EverySupportedFixtureDecodes) {
  for (const Fixture& fixture : DECODABLE) {
    auto bytes = readFixture(fixture.file);
    ASSERT_FALSE(bytes.empty()) << fixture.file;
    auto jpeg = std::make_unique<JPEGDEC>();
    ASSERT_EQ(jpeg->openRAM(bytes.data(), static_cast<int>(bytes.size()), drawNothing), 1) << fixture.file;
    EXPECT_EQ(jpeg->getSubSample(), fixture.subsample) << fixture.file;
    EXPECT_TRUE(jpegSubsamplingSupported(jpeg->getSubSample())) << fixture.file;
    jpeg->setPixelType(EIGHT_BIT_GRAYSCALE);
    EXPECT_EQ(jpeg->decode(0, 0, 0), 1) << fixture.file;
    jpeg->close();
  }
}

// The layout of the covers that crashed the X4 Pro. It is never decoded here:
// JPEGDEC would divide by a zero MCU size.
TEST(JpegSupport, FourOneOneCoverIsRejectedBeforeDecoding) {
  auto bytes = readFixture("sampling_4x1.jpg");
  ASSERT_FALSE(bytes.empty());
  auto jpeg = std::make_unique<JPEGDEC>();
  ASSERT_EQ(jpeg->openRAM(bytes.data(), static_cast<int>(bytes.size()), drawNothing), 1);
  EXPECT_EQ(jpeg->getSubSample(), 0x41);
  EXPECT_FALSE(jpegSubsamplingSupported(jpeg->getSubSample()));
  jpeg->close();
}
