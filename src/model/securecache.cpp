#include "model/securecache.h"

#include "core/log.h"
#include "model/crc32.h"

#include <windows.h>
#include <shlobj.h>
#include <bcrypt.h>
#include <dpapi.h>

#include <cstdio>
#include <cstring>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")

namespace securecache {
namespace {

constexpr unsigned char kMagic[4] = {'C', 'S', 'A', 'G'};
constexpr uint8_t kVersion = 1;
constexpr size_t kKeyBytes = 32;
constexpr size_t kNonceBytes = 12;
constexpr size_t kTagBytes = 16;
constexpr size_t kHeaderBytes = 44;
constexpr size_t kAadBytes = 16;

#pragma pack(push, 1)
struct Header {
    unsigned char magic[4];
    uint8_t version;
    uint8_t flags;
    uint16_t reserved;
    uint32_t plaintextSize;
    uint32_t checksum;
    unsigned char nonce[kNonceBytes];
    unsigned char tag[kTagBytes];
};
#pragma pack(pop)
static_assert(sizeof(Header) == kHeaderBytes, "secure asset header must stay stable");

std::string g_dir;
std::vector<unsigned char> g_key;

void MakeDirs(const std::string& path) {
    for (size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '\\' || path[i] == '/') {
            if (i > 0) CreateDirectoryA(path.substr(0, i).c_str(), nullptr);
        }
    }
    CreateDirectoryA(path.c_str(), nullptr);
}

bool ReadWholeFile(const std::string& path, std::vector<unsigned char>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    if (std::fseek(f, 0, SEEK_END) != 0) { std::fclose(f); return false; }
    const long size = std::ftell(f);
    if (size < 0 || size > 0x7FFFFFFF) { std::fclose(f); return false; }
    std::rewind(f);
    out.resize(static_cast<size_t>(size));
    const size_t got = out.empty() ? 0 : std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    return got == out.size();
}

bool WriteWholeFile(const std::string& path, const unsigned char* data, size_t size) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const size_t written = size ? std::fwrite(data, 1, size, f) : 0;
    const bool ok = written == size && std::fflush(f) == 0;
    std::fclose(f);
    return ok;
}

bool EnsureKey() {
    if (g_key.size() == kKeyBytes) return true;
    const std::string keyPath = Dir() + "key.dpapi";
    std::vector<unsigned char> blob;
    if (ReadWholeFile(keyPath, blob)) {
        DATA_BLOB in{ static_cast<DWORD>(blob.size()), blob.data() };
        DATA_BLOB out{};
        if (CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr,
                               CRYPTPROTECT_UI_FORBIDDEN, &out)) {
            if (out.cbData == kKeyBytes) {
                g_key.assign(out.pbData, out.pbData + out.cbData);
                LocalFree(out.pbData);
                return true;
            }
            LocalFree(out.pbData);
        }
    }

    unsigned char raw[kKeyBytes] = {};
    if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, raw, sizeof(raw), BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
        return false;

    DATA_BLOB in{ static_cast<DWORD>(sizeof(raw)), raw };
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"custommodels R3 asset key", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &out))
        return false;

    const bool wrote = WriteWholeFile(keyPath, out.pbData, out.cbData);
    LocalFree(out.pbData);
    if (!wrote) return false;

    g_key.assign(raw, raw + sizeof(raw));
    SecureZeroMemory(raw, sizeof(raw));
    return true;
}

bool OpenAes(BCRYPT_ALG_HANDLE& alg, BCRYPT_KEY_HANDLE& key, std::vector<unsigned char>& keyObject) {
    if (!EnsureKey()) return false;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0))) return false;
    const ULONG modeBytes = static_cast<ULONG>(sizeof(BCRYPT_CHAIN_MODE_GCM));
    if (!BCRYPT_SUCCESS(BCryptSetProperty(alg, BCRYPT_CHAINING_MODE,
                                           reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                                           modeBytes, 0))) {
        BCryptCloseAlgorithmProvider(alg, 0);
        alg = nullptr;
        return false;
    }
    ULONG objectBytes = 0;
    ULONG ignored = 0;
    if (!BCRYPT_SUCCESS(BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH,
                                          reinterpret_cast<PUCHAR>(&objectBytes), sizeof(objectBytes),
                                          &ignored, 0))) {
        BCryptCloseAlgorithmProvider(alg, 0);
        alg = nullptr;
        return false;
    }
    keyObject.resize(objectBytes);
    if (!BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(alg, &key, keyObject.data(), objectBytes,
                                                   g_key.data(), static_cast<ULONG>(g_key.size()), 0))) {
        keyObject.clear();
        BCryptCloseAlgorithmProvider(alg, 0);
        alg = nullptr;
        return false;
    }
    return true;
}

void CloseAes(BCRYPT_ALG_HANDLE alg, BCRYPT_KEY_HANDLE key, std::vector<unsigned char>& keyObject) {
    if (key) BCryptDestroyKey(key);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    if (!keyObject.empty()) SecureZeroMemory(keyObject.data(), keyObject.size());
    keyObject.clear();
}

bool Crypt(bool encrypt, const Header& header, const std::vector<unsigned char>& input,
           std::vector<unsigned char>& output) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    std::vector<unsigned char> keyObject;
    if (!OpenAes(alg, key, keyObject)) return false;

    unsigned char nonce[kNonceBytes];
    std::memcpy(nonce, header.nonce, sizeof(nonce));
    output.resize(input.size());
    unsigned long done = 0;

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth{};
    BCRYPT_INIT_AUTH_MODE_INFO(auth);
    auth.pbNonce = nonce;
    auth.cbNonce = sizeof(nonce);
    auth.pbAuthData = const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(&header));
    auth.cbAuthData = static_cast<ULONG>(kAadBytes);
    auth.pbTag = const_cast<unsigned char*>(header.tag);
    auth.cbTag = kTagBytes;

    NTSTATUS status;
    if (encrypt) {
        status = BCryptEncrypt(key,
                               const_cast<unsigned char*>(input.data()), static_cast<ULONG>(input.size()),
                               &auth, nonce, sizeof(nonce), output.data(), static_cast<ULONG>(output.size()),
                               &done, 0);
    } else {
        status = BCryptDecrypt(key,
                               const_cast<unsigned char*>(input.data()), static_cast<ULONG>(input.size()),
                               &auth, nonce, sizeof(nonce), output.data(), static_cast<ULONG>(output.size()),
                               &done, 0);
    }
    SecureZeroMemory(nonce, sizeof(nonce));
    CloseAes(alg, key, keyObject);
    if (!BCRYPT_SUCCESS(status) || done != input.size()) {
        output.clear();
        return false;
    }
    return true;
}

bool DecryptFile(uint32_t checksum, bool isDff, std::vector<unsigned char>& plaintext) {
    std::vector<unsigned char> blob;
    if (!ReadWholeFile(PathFor(checksum, isDff), blob) || blob.size() < sizeof(Header)) return false;
    Header header{};
    std::memcpy(&header, blob.data(), sizeof(header));
    if (std::memcmp(header.magic, kMagic, sizeof(kMagic)) != 0 || header.version != kVersion ||
        ((header.flags & 1u) != (isDff ? 1u : 0u)) || header.checksum != checksum ||
        header.plaintextSize != blob.size() - sizeof(Header)) return false;

    std::vector<unsigned char> cipher(blob.begin() + sizeof(Header), blob.end());
    if (!Crypt(false, header, cipher, plaintext)) return false;
    if (crc::Buffer(plaintext.data(), plaintext.size()) != checksum) {
        SecureZeroMemory(plaintext.data(), plaintext.size());
        plaintext.clear();
        return false;
    }
    return true;
}

} // namespace

const std::string& Dir() {
    if (!g_dir.empty()) return g_dir;
    char docs[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, 0, docs))) {
        std::string base = std::string(docs) + "\\GTA San Andreas User Files\\SAMP\\cache\\customskin037\\protected";
        MakeDirs(base);
        g_dir = base + "\\";
        CS_LOGI("securecache: protected asset directory %s", g_dir.c_str());
    } else {
        CS_LOGE("securecache: failed to resolve Documents folder");
    }
    return g_dir;
}

std::string PathFor(uint32_t checksum, bool isDff) {
    char name[40] = {0};
    std::snprintf(name, sizeof(name), "%08X.%s.asset", checksum, isDff ? "dff" : "txd");
    return Dir() + name;
}

bool Store(uint32_t checksum, bool isDff, const std::vector<unsigned char>& plaintext) {
    if (!checksum || plaintext.empty() || crc::Buffer(plaintext.data(), plaintext.size()) != checksum) return false;
    Header header{};
    std::memcpy(header.magic, kMagic, sizeof(kMagic));
    header.version = kVersion;
    header.flags = isDff ? 1u : 0u;
    header.plaintextSize = static_cast<uint32_t>(plaintext.size());
    header.checksum = checksum;
    if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, header.nonce, sizeof(header.nonce), BCRYPT_USE_SYSTEM_PREFERRED_RNG))) return false;

    std::vector<unsigned char> cipher;
    if (!Crypt(true, header, plaintext, cipher)) return false;

    const std::string path = PathFor(checksum, isDff);
    const std::string part = path + ".part";
    FILE* f = std::fopen(part.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(&header, 1, sizeof(header), f) == sizeof(header) &&
                    std::fwrite(cipher.data(), 1, cipher.size(), f) == cipher.size() &&
                    std::fflush(f) == 0;
    std::fclose(f);
    if (!ok || !MoveFileExA(part.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileA(part.c_str());
        return false;
    }
    return true;
}

bool HasValid(uint32_t checksum, bool isDff) {
    std::vector<unsigned char> plain;
    const bool ok = DecryptFile(checksum, isDff, plain);
    if (!plain.empty()) SecureZeroMemory(plain.data(), plain.size());
    return ok;
}

bool Materialize(uint32_t checksum, bool isDff, const std::string& nativePath) {
    std::vector<unsigned char> plain;
    if (!DecryptFile(checksum, isDff, plain)) {
        CS_LOGE("securecache: authentication/decryption failed 0x%08X %s", checksum, isDff ? "DFF" : "TXD");
        return false;
    }
    const std::string part = nativePath + ".secure.part";
    const bool wrote = WriteWholeFile(part, plain.data(), plain.size());
    SecureZeroMemory(plain.data(), plain.size());
    plain.clear();
    if (!wrote || !MoveFileExA(part.c_str(), nativePath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileA(part.c_str());
        return false;
    }
    return true;
}

void ReleasePlaintext(const std::string& nativePath) {
    if (!nativePath.empty()) DeleteFileA(nativePath.c_str());
}

} // namespace securecache
