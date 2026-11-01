# The decoder the firmware ships, at the commit platformio.ini pins. Its own
# CMakeLists targets ESP-IDF, so only the sources are fetched (SOURCE_SUBDIR
# names a directory without one).
FetchContent_Declare(
  jpegdec
  GIT_REPOSITORY https://github.com/bitbank2/JPEGDEC.git
  GIT_TAG 86282979224c8a32fd51e091ed5a35b0c699a52b
  SOURCE_SUBDIR src
)
FetchContent_MakeAvailable(jpegdec)

add_library(jpegdec_host STATIC ${jpegdec_SOURCE_DIR}/src/JPEGDEC.cpp)
target_include_directories(jpegdec_host PUBLIC ${jpegdec_SOURCE_DIR}/src)
target_compile_definitions(jpegdec_host PUBLIC __LINUX__)
target_compile_options(jpegdec_host PRIVATE -w)

add_executable(JpegSupportTest JpegSupportTest.cpp)
target_include_directories(JpegSupportTest PRIVATE ${REPO_ROOT}/lib/JpegToBmpConverter)
target_compile_definitions(JpegSupportTest PRIVATE JPEG_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_link_libraries(JpegSupportTest PRIVATE crosspoint_test_common jpegdec_host GTest::gtest_main)
gtest_discover_tests(JpegSupportTest)
