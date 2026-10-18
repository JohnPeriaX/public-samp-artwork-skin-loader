#include "model/download.h"

#include "core/log.h"
#include "model/crc32.h"
#include "model/securecache.h"

#include <windows.h>
#include <wininet.h>

#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace dl {

bool FetchBytes(const std::string& url, std::vector<unsigned char>& out) {
    // open.mp's webserver rejects anything whose user-agent isn't exactly "samp/0.3"
    HINTERNET hNet = InternetOpenA("SAMP/0.3", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hNet) {
        CS_LOGE("dl: InternetOpen failed (%lu)", GetLastError());
        return false;
    }

    HINTERNET hUrl = InternetOpenUrlA(hNet, url.c_str(), nullptr, 0,
                                      INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hUrl) {
        CS_LOGE("dl: InternetOpenUrl failed for %s (%lu)", url.c_str(), GetLastError());
        InternetCloseHandle(hNet);
        return false;
    }

    out.clear();
    unsigned char buf[65536];
    DWORD read = 0;
    while (InternetReadFile(hUrl, buf, sizeof(buf), &read) && read > 0)
        out.insert(out.end(), buf, buf + read);

    InternetCloseHandle(hUrl);
    InternetCloseHandle(hNet);
    CS_LOGI("dl: %s -> memory (%zu bytes)", url.c_str(), out.size());
    return !out.empty();
}

namespace {

struct Job {
    std::string url;
    std::string dest;
    uint32_t crc = 0;
    bool isDff = false;
};

std::mutex g_mtx;                 // guards g_jobs, g_done, g_pending, g_hook
std::condition_variable g_cv;
std::deque<Job> g_jobs;           // pending downloads (worker consumes)
std::vector<Result> g_done;       // finished downloads (main thread drains)
std::unordered_set<uint64_t> g_pending; // (crc << 1) | isDff, covers queued + in-flight jobs
CompletionHook g_hook = nullptr;  // optional native-client completion callback
std::once_flag g_workerOnce;

// single detached worker: fetches queued files serially and crc-checks each. it only touches the
// queues + its own file, never game state, so nothing is shared unsafely with the main thread
void WorkerLoop() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lk(g_mtx);
            g_cv.wait(lk, [] { return !g_jobs.empty(); });
            job = g_jobs.front();
            g_jobs.pop_front();
        }
        Result r;
        r.crc = job.crc;
        r.isDff = job.isDff;
        std::vector<unsigned char> bytes;
        if (FetchBytes(job.url, bytes) && crc::Buffer(bytes.data(), bytes.size()) == job.crc) {
            r.bytes = static_cast<uint32_t>(bytes.size());
            r.ok = securecache::Store(job.crc, job.isDff, bytes);
            if (r.ok)
                CS_LOGI("dl: encrypted asset committed 0x%08X %s; plaintext never written to disk",
                        job.crc, job.isDff ? "DFF" : "TXD");
        }
        if (!bytes.empty()) SecureZeroMemory(bytes.data(), bytes.size());
        CompletionHook hook = nullptr;
        {
            std::lock_guard<std::mutex> lk(g_mtx);
            g_pending.erase((static_cast<uint64_t>(job.crc) << 1) | (job.isDff ? 1u : 0u));
            g_done.push_back(r);
            hook = g_hook;
        }
        if (hook) hook(r);
    }
}

} // namespace

void Enqueue(const std::string& url, const std::string& destPath, uint32_t crc, bool isDff) {
    std::call_once(g_workerOnce, [] { std::thread(WorkerLoop).detach(); });
    const uint64_t key = (static_cast<uint64_t>(crc) << 1) | (isDff ? 1u : 0u);
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        if (!g_pending.insert(key).second) {
            CS_LOGI("dl: duplicate suppressed 0x%08X %s", crc, isDff ? "DFF" : "TXD");
            return;
        }
        g_jobs.push_back(Job{ url, destPath, crc, isDff });
    }
    g_cv.notify_one();
}

void SetCompletionHook(CompletionHook hook) {
    std::lock_guard<std::mutex> lk(g_mtx);
    g_hook = hook;
}

void Release(uint32_t crc, bool isDff) {
    const uint64_t key = (static_cast<uint64_t>(crc) << 1) | (isDff ? 1u : 0u);
    std::lock_guard<std::mutex> lk(g_mtx);
    g_pending.erase(key);
}

std::vector<Result> Drain() {
    std::vector<Result> out;
    std::lock_guard<std::mutex> lk(g_mtx);
    out.swap(g_done);
    return out;
}

} // namespace dl
