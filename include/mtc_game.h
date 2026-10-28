#ifndef __MTC_GAME_H__
#define __MTC_GAME_H__

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace mtc {
    // The only supported dump: Magical Tetris Challenge (USA), game code NMTE, big-endian .z64 layout.
    constexpr const char* rom_sha1_usa = "ca3fbd17406031a88e04bb79959d851550b641d0";
    // XXH3-64 of the same (byte-order normalized) ROM; this is what librecomp itself checks.
    constexpr uint64_t rom_xxh3_usa = 0x37FDE08DC8AEC6F3ULL;
    constexpr const char* rom_internal_name = "MAGICAL TETRIS";
    constexpr const char8_t* game_id = u8"mtc.n64.us";
    constexpr const char* mod_game_id = "mtc";

    enum class RomCheckResult {
        Good,
        FailedToOpen,
        NotARom,
        EmptyFile,        // 0 bytes (e.g. a failed copy or download)
        WrongGame,        // not Magical Tetris at all
        WrongVersion,     // Magical Tetris, but JP/EU/... or a modified/bad dump
    };

    struct RomCheck {
        RomCheckResult result;
        std::string sha1;          // of the normalized (.z64 big-endian) data
        std::string internal_name; // header name, trimmed
        std::string game_code;     // e.g. "NMTE"
        std::vector<uint8_t> normalized;
    };

    // Reads a .z64/.v64/.n64 file, normalizes byte order to .z64 and checks the SHA1.
    RomCheck check_rom_file(const std::filesystem::path& path);
    std::string rom_check_message(const RomCheck& check);

    std::string sha1_hex(std::span<const uint8_t> data);
    bool normalize_rom_byte_order(std::vector<uint8_t>& data); // false if not an N64 ROM
}

#endif
