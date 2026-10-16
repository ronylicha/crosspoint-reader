add_executable(CoverPackTest
  CoverPackTest.cpp
  ${REPO_ROOT}/lib/CoverPack/CoverPack.cpp
)
target_include_directories(CoverPackTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/CoverPack
)
target_link_libraries(CoverPackTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(CoverPackTest)
