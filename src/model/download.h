// http downloader (WinINet). sends user-agent "samp/0.3", which open.mp's artwork webserver requires
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dl {

// Fetch the response into memory. No plaintext asset is written to disk.
bool FetchBytes(const std::string& url, std::vector<unsigned char>& out);

// a finished async download+verify
struct Result {
    uint32_t crc = 0;   // expected checksum (also identifies which file this is)
    bool isDff = false; // echoed back from the enqueue call
    bool ok = false;    // fetched AND crc-verified against `crc`
    uint32_t bytes = 0; // verified plaintext size
};

using CompletionHook = void(*)(const Result& result);

// queue an async download; the worker fetches, checks crc32 == crc, and posts a Result to Drain.
// The optional hook runs on the downloader worker thread immediately after the Result is queued.
void Enqueue(const std::string& url, const std::string& destPath, uint32_t crc, bool isDff);
void SetCompletionHook(CompletionHook hook);
void Release(uint32_t crc, bool isDff);

// pop all finished results (thread-safe). call from the main thread each frame
std::vector<Result> Drain();

} // namespace dl
