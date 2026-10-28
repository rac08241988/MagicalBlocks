// ROM picker validation: byte-order normalization (.z64/.v64/.n64) + SHA1 check against the USA dump.
// No ROM data is embedded here - only the expected hash.
#include "mtc_game.h"

#include <array>
#include <cstring>
#include <fstream>
#include <algorithm>

namespace {
    struct Sha1 {
        uint32_t h[5] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u };
        uint64_t total = 0;
        uint8_t block[64];
        size_t block_len = 0;

        static uint32_t rol(uint32_t v, int s) { return (v << s) | (v >> (32 - s)); }

        void process(const uint8_t* p) {
            uint32_t w[80];
            for (int i = 0; i < 16; i++) {
                w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) | (uint32_t(p[i * 4 + 2]) << 8) | p[i * 4 + 3];
            }
            for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
            uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
            for (int i = 0; i < 80; i++) {
                uint32_t f, k;
                if (i < 20)      { f = (b & c) | (~b & d);           k = 0x5A827999u; }
                else if (i < 40) { f = b ^ c ^ d;                    k = 0x6ED9EBA1u; }
                else if (i < 60) { f = (b & c) | (b & d) | (c & d);  k = 0x8F1BBCDCu; }
                else             { f = b ^ c ^ d;                    k = 0xCA62C1D6u; }
                uint32_t t = rol(a, 5) + f + e + k + w[i];
                e = d; d = c; c = rol(b, 30); b = a; a = t;
            }
            h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
        }

        void update(const uint8_t* data, size_t len) {
            total += len;
            while (len > 0) {
                size_t n = std::min(len, 64 - block_len);
                memcpy(block + block_len, data, n);
                block_len += n; data += n; len -= n;
                if (block_len == 64) { process(block); block_len = 0; }
            }
        }

        std::array<uint8_t, 20> finish() {
            uint64_t bits = total * 8;
            uint8_t pad = 0x80;
            update(&pad, 1);
            uint8_t zero = 0;
            while (block_len != 56) update(&zero, 1);
            uint8_t len_be[8];
            for (int i = 0; i < 8; i++) len_be[i] = uint8_t(bits >> (56 - 8 * i));
            update(len_be, 8);
            std::array<uint8_t, 20> out;
            for (int i = 0; i < 5; i++) {
                out[i * 4 + 0] = uint8_t(h[i] >> 24); out[i * 4 + 1] = uint8_t(h[i] >> 16);
                out[i * 4 + 2] = uint8_t(h[i] >> 8);  out[i * 4 + 3] = uint8_t(h[i]);
            }
            return out;
        }
    };

    std::string trim(std::string s) {
        while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
        return s;
    }
}

std::string mtc::sha1_hex(std::span<const uint8_t> data) {
    Sha1 s;
    s.update(data.data(), data.size());
    auto d = s.finish();
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (uint8_t b : d) { out += hex[b >> 4]; out += hex[b & 15]; }
    return out;
}

bool mtc::normalize_rom_byte_order(std::vector<uint8_t>& data) {
    if (data.size() < 0x1000) return false;
    data.resize((data.size() + 3) & ~size_t(3));
    const uint8_t* p = data.data();
    if (p[0] == 0x80 && p[1] == 0x37 && p[2] == 0x12 && p[3] == 0x40) {
        return true;                                   // .z64 (big endian)
    }
    if (p[0] == 0x37 && p[1] == 0x80 && p[2] == 0x40 && p[3] == 0x12) {
        for (size_t i = 0; i < data.size(); i += 2) std::swap(data[i], data[i + 1]);   // .v64 (byteswapped)
        return true;
    }
    if (p[0] == 0x40 && p[1] == 0x12 && p[2] == 0x37 && p[3] == 0x80) {
        for (size_t i = 0; i < data.size(); i += 4) {  // .n64 (little endian / word swapped)
            std::swap(data[i], data[i + 3]);
            std::swap(data[i + 1], data[i + 2]);
        }
        return true;
    }
    return false;
}

mtc::RomCheck mtc::check_rom_file(const std::filesystem::path& path) {
    RomCheck ret{};
    std::ifstream f{ path, std::ios::binary | std::ios::ate };
    if (!f.good()) { ret.result = RomCheckResult::FailedToOpen; return ret; }
    std::streamsize size = f.tellg();
    if (size <= 0 || size > 128 * 1024 * 1024) { ret.result = size == 0 ? RomCheckResult::EmptyFile : (size < 0 ? RomCheckResult::FailedToOpen : RomCheckResult::NotARom); return ret; }
    f.seekg(0);
    ret.normalized.resize(size_t(size));
    if (!f.read(reinterpret_cast<char*>(ret.normalized.data()), size)) { ret.result = RomCheckResult::FailedToOpen; return ret; }

    if (!normalize_rom_byte_order(ret.normalized)) {
        ret.normalized.clear();
        ret.result = RomCheckResult::NotARom;
        return ret;
    }

    ret.internal_name = trim(std::string(reinterpret_cast<const char*>(ret.normalized.data()) + 0x20, 20));
    ret.game_code = std::string(reinterpret_cast<const char*>(ret.normalized.data()) + 0x3B, 4);
    ret.sha1 = sha1_hex(ret.normalized);

    if (ret.sha1 == rom_sha1_usa) {
        ret.result = RomCheckResult::Good;
    }
    else if (ret.internal_name.find("TETRIS") != std::string::npos && ret.game_code.size() == 4 && ret.game_code.substr(1, 2) == "MT") {
        ret.result = RomCheckResult::WrongVersion;
    }
    else {
        ret.result = RomCheckResult::WrongGame;
    }
    return ret;
}

std::string mtc::rom_check_message(const RomCheck& check) {
    switch (check.result) {
        case RomCheckResult::Good:
            return "ROM verified: Magical Tetris Challenge (USA).";
        case RomCheckResult::FailedToOpen:
            return "That file couldn't be opened. Please check that it exists and isn't in use by another program.";
        case RomCheckResult::EmptyFile:
            return "That file is empty (0 bytes), so it isn't a ROM.\n\nThe copy or download probably failed. Please dump or copy your ROM again and select the new file.";
        case RomCheckResult::NotARom:
            return "That file doesn't look like a Nintendo 64 ROM.\n\nPlease select an uncompressed .z64, .v64 or .n64 dump (not a .zip/.7z archive).";
        case RomCheckResult::WrongGame:
            return "That ROM isn't Magical Tetris Challenge" +
                (check.internal_name.empty() ? std::string{} : " (it reports itself as \"" + check.internal_name + "\")") +
                ".\n\nPlease select your own dump of Magical Tetris Challenge for the N64 (USA).";
        case RomCheckResult::WrongVersion:
            return "This is Magical Tetris Challenge, but not the supported version (game code " + check.game_code + ").\n\n"
                   "Only the North American (USA, NMTE) cartridge is supported right now. Japanese/European versions, "
                   "hacks, translations and bad dumps are not.\n\nExpected SHA1: " + std::string{ rom_sha1_usa } +
                   "\nYour file:     " + check.sha1;
    }
    return "Unknown error.";
}
