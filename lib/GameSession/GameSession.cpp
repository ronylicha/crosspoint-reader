#include "GameSession.h"

#include <HalStorage.h>
#include <Logging.h>

namespace GameSession {
namespace {
constexpr const char* MODULE = "GAME_SAVE";

struct Paths {
  const char* primary;
  const char* temporary;
  const char* backup;
};

constexpr Paths PATHS[3][2] = {
    {{"/.crosspoint/game-chess-local.bin", "/.crosspoint/game-chess-local.bin.tmp",
      "/.crosspoint/game-chess-local.bin.bak"},
     {"/.crosspoint/game-chess-ai.bin", "/.crosspoint/game-chess-ai.bin.tmp", "/.crosspoint/game-chess-ai.bin.bak"}},
    {{"/.crosspoint/game-checkers-local.bin", "/.crosspoint/game-checkers-local.bin.tmp",
      "/.crosspoint/game-checkers-local.bin.bak"},
     {"/.crosspoint/game-checkers-ai.bin", "/.crosspoint/game-checkers-ai.bin.tmp",
      "/.crosspoint/game-checkers-ai.bin.bak"}},
    {{"/.crosspoint/game-backgammon-local.bin", "/.crosspoint/game-backgammon-local.bin.tmp",
      "/.crosspoint/game-backgammon-local.bin.bak"},
     {"/.crosspoint/game-backgammon-ai.bin", "/.crosspoint/game-backgammon-ai.bin.tmp",
      "/.crosspoint/game-backgammon-ai.bin.bak"}}};

const Paths* pathsFor(const Game game, const bool vsAi) {
  if (!detail::validGame(game)) return nullptr;
  return &PATHS[static_cast<uint8_t>(game) - 1][vsAi ? 1 : 0];
}

bool readRecord(const char* path, const Game game, const bool vsAi, const size_t payloadSize, EncodedBuffer& encoded) {
  if (!Storage.exists(path)) return false;
  HalFile file;
  const size_t recordSize = HEADER_SIZE + payloadSize;
  if (!Storage.openFileForRead(MODULE, path, file) || file.isDirectory() || file.size() != recordSize ||
      file.read(encoded.data(), recordSize) != static_cast<int>(recordSize)) {
    LOG_ERR(MODULE, "Failed to read complete record: %s", path);
    return false;
  }
  // The payload destination is identical to its source: validate without an extra buffer.
  if (!decode(game, vsAi, encoded.data(), recordSize, encoded.data() + HEADER_SIZE, payloadSize)) {
    LOG_ERR(MODULE, "Invalid record: %s", path);
    return false;
  }
  return true;
}

bool validFile(const char* path, const Game game, const bool vsAi, const size_t payloadSize) {
  EncodedBuffer encoded;
  return readRecord(path, game, vsAi, payloadSize, encoded);
}

bool writeRecord(const char* path, const EncodedBuffer& encoded, const size_t encodedSize) {
  bool complete = false;
  {
    HalFile file;
    if (!Storage.openFileForWrite(MODULE, path, file)) {
      LOG_ERR(MODULE, "Failed to open temporary record: %s", path);
      return false;
    }
    const size_t written = file.write(encoded.data(), encodedSize);
    file.flush();
    // Close before renaming, and use close's return value to detect sync failures.
    const bool closed = file.close();
    complete = written == encodedSize && closed;
  }
  if (!complete) {
    LOG_ERR(MODULE, "Failed to write complete record: %s", path);
    if (Storage.exists(path) && !Storage.remove(path)) LOG_ERR(MODULE, "Failed to remove incomplete record: %s", path);
  }
  return complete;
}

bool installRecord(const Paths& paths, const Game game, const bool vsAi, const size_t payloadSize) {
  if (Storage.exists(paths.primary) && validFile(paths.primary, game, vsAi, payloadSize)) {
    if (Storage.exists(paths.backup) && !Storage.remove(paths.backup)) {
      LOG_ERR(MODULE, "Failed to rotate backup: %s", paths.backup);
      return false;
    }
    if (!Storage.rename(paths.primary, paths.backup)) {
      LOG_ERR(MODULE, "Failed to preserve previous record: %s", paths.primary);
      return false;
    }
  }
  // FAT replacement removes an existing target before rename; backup and temp remain recoverable.
  if (!Storage.replaceFile(paths.temporary, paths.primary)) {
    LOG_ERR(MODULE, "Failed to install record: %s", paths.primary);
    return false;
  }
  return true;
}

bool preservePending(const Paths& paths, const Game game, const bool vsAi, const size_t payloadSize) {
  if (!Storage.exists(paths.temporary) || !validFile(paths.temporary, game, vsAi, payloadSize)) return true;
  // A complete temporary file is the newest pending snapshot, even with a valid primary.
  return installRecord(paths, game, vsAi, payloadSize);
}
}  // namespace

bool load(const Game game, const bool vsAi, uint8_t* payload, const size_t expectedSize) {
  const Paths* paths = pathsFor(game, vsAi);
  if (!paths || !payload || expectedSize == 0 || expectedSize > MAX_PAYLOAD) {
    LOG_ERR(MODULE, "Invalid load parameters");
    return false;
  }
  if (!Storage.ready()) {
    LOG_ERR(MODULE, "SD unavailable during load");
    return false;
  }
  EncodedBuffer encoded;
  if (!readRecord(paths->temporary, game, vsAi, expectedSize, encoded) &&
      !readRecord(paths->primary, game, vsAi, expectedSize, encoded) &&
      !readRecord(paths->backup, game, vsAi, expectedSize, encoded)) {
    return false;
  }
  return decode(game, vsAi, encoded.data(), HEADER_SIZE + expectedSize, payload, expectedSize);
}

bool save(const Game game, const bool vsAi, const uint8_t* payload, const size_t payloadSize) {
  EncodedBuffer encoded;
  size_t encodedSize = 0;
  const Paths* paths = pathsFor(game, vsAi);
  if (!paths || !encode(game, vsAi, payload, payloadSize, encoded, encodedSize)) {
    LOG_ERR(MODULE, "Invalid save parameters");
    return false;
  }
  if (!Storage.ready() || !Storage.ensureDirectoryExists("/.crosspoint")) {
    LOG_ERR(MODULE, "SD unavailable during save");
    return false;
  }
  if (!preservePending(*paths, game, vsAi, payloadSize) || !writeRecord(paths->temporary, encoded, encodedSize)) {
    return false;
  }
  return installRecord(*paths, game, vsAi, payloadSize);
}
}  // namespace GameSession
