add_executable(KOReaderProgressQueueTest
  ProgressQueueTest.cpp
  ${REPO_ROOT}/lib/KOReaderSync/ProgressQueue.cpp
)

target_include_directories(KOReaderProgressQueueTest PRIVATE
  ${REPO_ROOT}/lib/KOReaderSync
)

target_link_libraries(KOReaderProgressQueueTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(KOReaderProgressQueueTest)
