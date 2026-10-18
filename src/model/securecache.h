// Protected R3 asset cache: AES-256-GCM at rest with a per-user DPAPI-protected key.
// Plain DFF/TXD files exist only as short-lived native-loader staging files.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace securecache {

// Directory containing protected asset blobs and the DPAPI-wrapped local key.
const std::string& Dir();

// Protected on-disk path for one asset.
std::string PathFor(uint32_t checksum, bool isDff);

// Store verified plaintext as an authenticated encrypted blob. No plaintext file is retained.
bool Store(uint32_t checksum, bool isDff, const std::vector<unsigned char>& plaintext);

// Return true when a protected blob exists and authenticates/decrypts to the expected CRC.
bool HasValid(uint32_t checksum, bool isDff);

// Decrypt one protected asset into the exact native cache path needed by samp.dll.
// The caller must remove it after RenderWare/native loading has consumed it.
bool Materialize(uint32_t checksum, bool isDff, const std::string& nativePath);

// Remove a plaintext native staging file after runtime objects have been loaded.
void ReleasePlaintext(const std::string& nativePath);

} // namespace securecache
