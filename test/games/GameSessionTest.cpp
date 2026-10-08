#include <GameSession.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

namespace {
class SessionTest : public ::testing::Test {
 protected:
  const char* primary = "/.crosspoint/game-chess-local.bin";
  std::array<uint8_t, 4> oldData{1, 2, 3, 4}, newData{4, 3, 2, 1};
  void SetUp() override {
    host::fs = {};
    host::reset();
  }
  bool save(const std::array<uint8_t, 4>& data) {
    return GameSession::save(GameSession::Game::Chess, false, data.data(), data.size());
  }
  std::array<uint8_t, 4> load() {
    std::array<uint8_t, 4> result{};
    EXPECT_TRUE(GameSession::load(GameSession::Game::Chess, false, result.data(), result.size()));
    return result;
  }
};

TEST_F(SessionTest, CodecRoundTripEveryGameAndMode) {
  for (const auto game : {GameSession::Game::Chess, GameSession::Game::Checkers, GameSession::Game::Backgammon}) {
    for (bool ai : {false, true}) {
      GameSession::EncodedBuffer record{};
      size_t size = 0;
      ASSERT_TRUE(GameSession::encode(game, ai, oldData.data(), oldData.size(), record, size));
      std::array<uint8_t, 4> result{};
      EXPECT_TRUE(GameSession::decode(game, ai, record.data(), size, result.data(), result.size()));
      EXPECT_EQ(result, oldData);
      EXPECT_FALSE(GameSession::decode(game, !ai, record.data(), size, result.data(), result.size()));
    }
  }
}
TEST_F(SessionTest, RejectsEverySingleByteCorruptionWithoutChangingOutput) {
  GameSession::EncodedBuffer record{};
  size_t size = 0;
  ASSERT_TRUE(GameSession::encode(GameSession::Game::Chess, false, oldData.data(), oldData.size(), record, size));
  for (size_t i = 0; i < size; ++i) {
    auto corrupt = record;
    corrupt[i] ^= 1;
    auto result = newData;
    EXPECT_FALSE(
        GameSession::decode(GameSession::Game::Chess, false, corrupt.data(), size, result.data(), result.size()))
        << i;
    EXPECT_EQ(result, newData) << i;
  }
}
TEST_F(SessionTest, RejectsTruncationTrailingBytesWrongGameAndInvalidParameters) {
  GameSession::EncodedBuffer record{};
  size_t size = 7;
  ASSERT_TRUE(GameSession::encode(GameSession::Game::Chess, false, oldData.data(), oldData.size(), record, size));
  for (size_t count = 0; count < size; ++count) {
    auto result = newData;
    EXPECT_FALSE(
        GameSession::decode(GameSession::Game::Chess, false, record.data(), count, result.data(), result.size()));
    EXPECT_EQ(result, newData);
  }
  auto result = newData;
  EXPECT_FALSE(
      GameSession::decode(GameSession::Game::Chess, false, record.data(), size + 1, result.data(), result.size()));
  EXPECT_FALSE(
      GameSession::decode(GameSession::Game::Checkers, false, record.data(), size, result.data(), result.size()));
  EXPECT_FALSE(
      GameSession::encode(static_cast<GameSession::Game>(0), false, oldData.data(), oldData.size(), record, size));
  EXPECT_FALSE(GameSession::encode(GameSession::Game::Chess, false, nullptr, 4, record, size));
  EXPECT_FALSE(GameSession::encode(GameSession::Game::Chess, false, oldData.data(), 0, record, size));
  EXPECT_FALSE(
      GameSession::encode(GameSession::Game::Chess, false, oldData.data(), GameSession::MAX_PAYLOAD + 1, record, size));
}
TEST_F(SessionTest, KnownCrcVectorAndMaximumPayload) {
  const char* data = "123456789";
  EXPECT_EQ(GameSession::checksum(reinterpret_cast<const uint8_t*>(data), 9), 0xCBF43926U);
  GameSession::Buffer payload{};
  payload.fill(0xA5);
  GameSession::EncodedBuffer record{};
  size_t size = 0;
  ASSERT_TRUE(GameSession::encode(GameSession::Game::Chess, true, payload.data(), payload.size(), record, size));
  GameSession::Buffer output{};
  EXPECT_TRUE(GameSession::decode(GameSession::Game::Chess, true, record.data(), size, output.data(), output.size()));
  EXPECT_EQ(output, payload);
}
TEST_F(SessionTest, IndependentFilesForGamesAndModes) {
  for (const auto game : {GameSession::Game::Chess, GameSession::Game::Checkers, GameSession::Game::Backgammon}) {
    for (bool ai : {false, true}) {
      auto data = oldData;
      data[0] = static_cast<uint8_t>(game) * 2 + ai;
      ASSERT_TRUE(GameSession::save(game, ai, data.data(), data.size()));
    }
  }
  EXPECT_EQ(host::fs.files.size(), 6U);
  for (const auto game : {GameSession::Game::Chess, GameSession::Game::Checkers, GameSession::Game::Backgammon}) {
    for (bool ai : {false, true}) {
      std::array<uint8_t, 4> data{};
      ASSERT_TRUE(GameSession::load(game, ai, data.data(), data.size()));
      EXPECT_EQ(data[0], static_cast<uint8_t>(game) * 2 + ai);
    }
  }
}
TEST_F(SessionTest, ShortWriteAndCloseFailureKeepPreviousRecord) {
  ASSERT_TRUE(save(oldData));
  host::fs.shortWrite = true;
  EXPECT_FALSE(save(newData));
  EXPECT_EQ(load(), oldData);
  host::fs.shortWrite = false;
  host::fs.failClose = true;
  EXPECT_FALSE(save(newData));
  EXPECT_EQ(load(), oldData);
}
TEST_F(SessionTest, StorageUnavailableAndOpenFailureKeepOutputUntouched) {
  ASSERT_TRUE(save(oldData));
  host::fs.ready = false;
  EXPECT_FALSE(save(newData));
  auto result = newData;
  EXPECT_FALSE(GameSession::load(GameSession::Game::Chess, false, result.data(), result.size()));
  EXPECT_EQ(result, newData);
  host::fs.ready = true;
  host::fs.failOpenWrite = true;
  EXPECT_FALSE(save(newData));
  EXPECT_EQ(load(), oldData);
  host::fs.failDirectory = true;
  EXPECT_FALSE(save(newData));
}
TEST_F(SessionTest, RenameFailureLeavesPrimaryAndCompleteTemporary) {
  ASSERT_TRUE(save(oldData));
  host::fs.failRename = true;
  EXPECT_FALSE(save(newData));
  EXPECT_EQ(load(), newData);
  host::fs.files.erase(primary);
  EXPECT_EQ(load(), newData);
}
TEST_F(SessionTest, ReplacementFailureLoadsCompleteTemporaryBeforeBackup) {
  ASSERT_TRUE(save(oldData));
  host::fs.failReplace = true;
  EXPECT_FALSE(save(newData));
  EXPECT_EQ(load(), newData);
  host::fs.files.erase(std::string(primary) + ".tmp");
  EXPECT_EQ(load(), oldData);
}
TEST_F(SessionTest, CorruptPrimaryAndTemporaryFallbackToBackup) {
  ASSERT_TRUE(save(oldData));
  ASSERT_TRUE(save(newData));
  host::fs.files[primary][0] = 0;
  host::fs.files[std::string(primary) + ".tmp"] = {0, 1};
  EXPECT_EQ(load(), oldData);
}
TEST_F(SessionTest, PendingValidTemporarySurvivesNextShortWriteWithCorruptPrimary) {
  ASSERT_TRUE(save(oldData));
  host::fs.failReplace = true;
  EXPECT_FALSE(save(newData));
  host::fs.files[primary] = {0, 1};
  host::fs.failReplace = false;
  host::fs.shortWrite = true;
  EXPECT_FALSE(save(oldData));
  EXPECT_EQ(load(), newData);
}
TEST_F(SessionTest, FailedBackupRotationPreservesLatestAndCanRetry) {
  ASSERT_TRUE(save(oldData));
  ASSERT_TRUE(save(newData));
  host::fs.failRemove = true;
  EXPECT_FALSE(save(oldData));
  EXPECT_EQ(load(), oldData);
  host::fs.failRemove = false;
  ASSERT_TRUE(save(oldData));
  EXPECT_EQ(load(), oldData);
}
TEST_F(SessionTest, NewerTemporarySurvivesNextShortWriteWithValidOldPrimary) {
  ASSERT_TRUE(save(oldData));
  host::fs.failRename = true;
  EXPECT_FALSE(save(newData));
  EXPECT_EQ(load(), newData);
  host::fs.failRename = false;
  host::fs.shortWrite = true;
  EXPECT_FALSE(save(oldData));
  EXPECT_EQ(load(), newData);
}
TEST_F(SessionTest, FailedPendingInstallationDoesNotTruncateExistingTemporary) {
  ASSERT_TRUE(save(oldData));
  host::fs.failRename = true;
  EXPECT_FALSE(save(newData));
  const int writes = host::fs.writes;
  EXPECT_FALSE(save(oldData));
  EXPECT_EQ(host::fs.writes, writes);
  EXPECT_EQ(load(), newData);
  host::fs.failRename = false;
  ASSERT_TRUE(save(oldData));
  EXPECT_EQ(load(), oldData);
}
TEST_F(SessionTest, ShortReadCorruptRecordsNeverModifyLoadDestination) {
  ASSERT_TRUE(save(oldData));
  host::fs.shortRead = true;
  auto output = newData;
  EXPECT_FALSE(GameSession::load(GameSession::Game::Chess, false, output.data(), output.size()));
  EXPECT_EQ(output, newData);
  host::fs.shortRead = false;
  host::fs.files[primary][0] = 0;
  EXPECT_FALSE(GameSession::load(GameSession::Game::Chess, false, output.data(), output.size()));
  EXPECT_EQ(output, newData);
}
}  // namespace
