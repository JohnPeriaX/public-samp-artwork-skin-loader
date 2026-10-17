// crc-32/ieee (poly 0xEDB88320) - matches open.mp's GetFileCRC32Checksum, so our sums equal the server's
#pragma once

#include <cstddef>
#include <cstdint>

namespace crc {

uint32_t Buffer(const void* data, size_t len);
uint32_t File(const char* path); // 0 if the file can't be opened

} // namespace crc
