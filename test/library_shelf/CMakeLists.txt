add_executable(LibraryShelfLayoutTest
  LibraryShelfLayoutTest.cpp
  LibraryShelfSdkTest.cpp
  ../../freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp
)
target_include_directories(LibraryShelfLayoutTest PRIVATE
  ../../lib/LibraryShelfLayout
  ../../freeink-sdk/libs/ui/FreeInkUI/include
)
target_link_libraries(LibraryShelfLayoutTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(LibraryShelfLayoutTest)
