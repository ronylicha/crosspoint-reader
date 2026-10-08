add_executable(GamesTest
  GameSessionTest.cpp
  GamesActivityTest.cpp
  ${REPO_ROOT}/lib/GameSession/GameSession.cpp
  ${REPO_ROOT}/src/activities/games/ChessActivity.cpp
  ${REPO_ROOT}/src/activities/games/CheckersActivity.cpp
  ${REPO_ROOT}/src/activities/games/BackgammonActivity.cpp
)
target_include_directories(GamesTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src/activities/games
  ${REPO_ROOT}/lib/GameSession
)
target_link_libraries(GamesTest PRIVATE GTest::gtest_main crosspoint_test_common)
option(GAMES_SANITIZE "Run native game scenarios with address and undefined-behavior sanitizers" OFF)
if(GAMES_SANITIZE)
  target_compile_options(GamesTest PRIVATE -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined)
  target_link_options(GamesTest PRIVATE -fsanitize=address,undefined)
endif()
gtest_discover_tests(GamesTest)
