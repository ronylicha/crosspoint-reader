#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace GameSession {

enum class Game : uint8_t { Chess = 1, Checkers = 2, Backgammon = 3 };

inline constexpr size_t MAX_PAYLOAD = 192;
inline constexpr size_t HEADER_SIZE = 16;
using Buffer = std::array<uint8_t, MAX_PAYLOAD>;
using EncodedBuffer = std::array<uint8_t, HEADER_SIZE + MAX_PAYLOAD>;

namespace detail {
inline bool validGame(const Game game) {
  return game == Game::Chess || game == Game::Checkers || game == Game::Backgammon;
}

inline uint32_t updateChecksum(uint32_t crc, const uint8_t* data, const size_t size) {
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
    }
  }
  return crc;
}

inline uint32_t recordChecksum(const uint8_t* encoded, const size_t payloadSize) {
  return updateChecksum(updateChecksum(0xFFFFFFFFU, encoded, 12), encoded + HEADER_SIZE, payloadSize) ^ 0xFFFFFFFFU;
}
}  // namespace detail

inline uint32_t checksum(const uint8_t* data, const size_t size) {
  if (!data && size != 0) return 0;
  return detail::updateChecksum(0xFFFFFFFFU, data, size) ^ 0xFFFFFFFFU;
}

inline bool encode(const Game game, const bool vsAi, const uint8_t* payload, const size_t payloadSize,
                   EncodedBuffer& encoded, size_t& encodedSize) {
  if (!detail::validGame(game) || !payload || payloadSize == 0 || payloadSize > MAX_PAYLOAD) return false;

  // Copy first so a payload held in encoded's payload area remains usable.
  for (size_t i = 0; i < payloadSize; ++i) encoded[HEADER_SIZE + i] = payload[i];
  encoded[0] = 'X';
  encoded[1] = 'P';
  encoded[2] = 'G';
  encoded[3] = 'S';
  encoded[4] = 1;
  encoded[5] = static_cast<uint8_t>(game);
  encoded[6] = vsAi ? 1 : 0;
  encoded[7] = 0;
  encoded[8] = static_cast<uint8_t>(payloadSize);
  encoded[9] = static_cast<uint8_t>(payloadSize >> 8);
  encoded[10] = 0;
  encoded[11] = 0;
  const uint32_t crc = detail::recordChecksum(encoded.data(), payloadSize);
  for (size_t i = 0; i < 4; ++i) encoded[12 + i] = static_cast<uint8_t>(crc >> (8 * i));
  encodedSize = HEADER_SIZE + payloadSize;
  return true;
}

inline bool decode(const Game game, const bool vsAi, const uint8_t* encoded, const size_t encodedSize, uint8_t* payload,
                   const size_t expectedSize) {
  if (!detail::validGame(game) || !encoded || !payload || expectedSize == 0 || expectedSize > MAX_PAYLOAD ||
      encodedSize != HEADER_SIZE + expectedSize) {
    return false;
  }
  if (encoded[0] != 'X' || encoded[1] != 'P' || encoded[2] != 'G' || encoded[3] != 'S' || encoded[4] != 1 ||
      encoded[5] != static_cast<uint8_t>(game) || encoded[6] != (vsAi ? 1 : 0) || encoded[7] != 0 || encoded[10] != 0 ||
      encoded[11] != 0) {
    return false;
  }
  const size_t storedSize = static_cast<size_t>(encoded[8]) | (static_cast<size_t>(encoded[9]) << 8);
  if (storedSize != expectedSize) return false;
  uint32_t storedCrc = 0;
  for (size_t i = 0; i < 4; ++i) storedCrc |= static_cast<uint32_t>(encoded[12 + i]) << (8 * i);
  if (storedCrc != detail::recordChecksum(encoded, storedSize)) return false;

  for (size_t i = 0; i < storedSize; ++i) payload[i] = encoded[HEADER_SIZE + i];
  return true;
}

bool load(Game game, bool vsAi, uint8_t* payload, size_t expectedSize);
bool save(Game game, bool vsAi, const uint8_t* payload, size_t payloadSize);

}  // namespace GameSession
