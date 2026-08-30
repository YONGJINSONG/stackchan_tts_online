#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tutor_qidx {

constexpr char MAGIC[4] = {'Q', 'I', 'D', 'X'};
constexpr uint16_t VERSION = 1;
constexpr uint16_t FLAGS = 0;
constexpr size_t SAMPLE_COUNT = 4;
constexpr size_t SAMPLE_WINDOW_BYTES = 256;

#pragma pack(push, 1)
struct Header {
  char magic[4];
  uint16_t version;
  uint16_t headerSize;
  uint16_t entrySize;
  uint16_t flags;
  uint32_t sourceSize;
  uint64_t sourceMtime;
  uint32_t sourceSampleCrc32[SAMPLE_COUNT];
  uint32_t recordCount;
  uint32_t entriesCrc32;
  uint32_t reserved;
};

struct Entry {
  uint32_t offset;
  uint32_t idHash;
  uint32_t categoryHash;
  uint8_t level;
  uint8_t reserved[3];
};
#pragma pack(pop)

static_assert(sizeof(Header) == 52, "qidx header layout changed");
static_assert(sizeof(Entry) == 16, "qidx entry layout changed");

}  // namespace tutor_qidx
