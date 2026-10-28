// rom_scan: proves that no shipped file contains ROM bytes (release check, see OPTB.md / package-release.sh).
// usage: rom_scan <rom.z64> <start_hex> <end_hex> <window> <file>...
// Reports every file that contains any `window`-byte run of ROM[start, end) (any alignment, any byte order
// of the ROM is NOT considered: the shipped files would have to contain big-endian .z64 bytes, plus the two
// swapped layouts are checked too). Windows with fewer than 8 distinct byte values (padding, zero fill) are
// skipped because they occur in any binary, as are runs that are >60% 0x00/0xFF bytes (small-integer tables). Exit code: 0 clean, 1 hits found, 2 usage/IO error.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t* read_all(const char* path, size_t* len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* buf = malloc(n > 0 ? (size_t)n : 1);
    if (n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf); return NULL; }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

// Probe length K: every window-byte match contains a ROM sub-run of K bytes starting at a ROM offset that is a
// multiple of STRIDE (window >= K + STRIDE - 1).
#define K 32
#define STRIDE 16
#define HBITS 23
#define HSIZE (1u << HBITS)
static const uint64_t B = 1099511628211ull;

typedef struct { uint64_t h; uint32_t off; uint8_t layout; } Entry;
static Entry* table;
static uint8_t* used;
static uint64_t BK; // B^(K-1)

static uint64_t hash_block(const uint8_t* p) { uint64_t h = 0; for (int i = 0; i < K; i++) h = h * B + p[i]; return h; }

static int distinct(const uint8_t* p, size_t n) {
    uint8_t seen[256] = { 0 }; int c = 0;
    for (size_t i = 0; i < n; i++) if (!seen[p[i]]) { seen[p[i]] = 1; c++; }
    return c;
}

// Runs dominated by 0x00/0xFF (padding, small-integer tables such as 0,1,2,3 as 32-bit words) are not
// meaningful matches either.
static int trivial(const uint8_t* p, size_t n) {
    if (distinct(p, n) < 8) return 1;
    size_t z = 0;
    for (size_t i = 0; i < n; i++) if (p[i] == 0x00 || p[i] == 0xFF) z++;
    return z * 10 > n * 6;
}

static void insert(uint64_t h, uint32_t off, uint8_t layout) {
    uint32_t i = (uint32_t)(h >> (64 - HBITS));
    while (used[i]) i = (i + 1) & (HSIZE - 1);
    used[i] = 1; table[i].h = h; table[i].off = off; table[i].layout = layout;
}

int main(int argc, char** argv) {
    if (argc < 6) { fprintf(stderr, "usage: rom_scan <rom> <start_hex> <end_hex> <window> <file>...\n"); return 2; }
    size_t rom_len;
    uint8_t* rom0 = read_all(argv[1], &rom_len);
    if (!rom0) { fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
    size_t start = strtoul(argv[2], NULL, 16), end = strtoul(argv[3], NULL, 16);
    size_t window = strtoul(argv[4], NULL, 10);
    if (end > rom_len || start >= end || window < K + STRIDE - 1) { fprintf(stderr, "bad range/window (window >= %d)\n", K + STRIDE - 1); return 2; }
    // Three layouts: .z64 as-is, .v64 (16-bit swapped), .n64 (32-bit swapped).
    uint8_t* roms[3];
    roms[0] = rom0;
    roms[1] = malloc(rom_len); roms[2] = malloc(rom_len);
    for (size_t i = 0; i + 3 < rom_len; i += 4) {
        roms[1][i] = rom0[i + 1]; roms[1][i + 1] = rom0[i]; roms[1][i + 2] = rom0[i + 3]; roms[1][i + 3] = rom0[i + 2];
        roms[2][i] = rom0[i + 3]; roms[2][i + 1] = rom0[i + 2]; roms[2][i + 2] = rom0[i + 1]; roms[2][i + 3] = rom0[i];
    }
    table = calloc(HSIZE, sizeof(Entry)); used = calloc(HSIZE, 1);
    BK = 1; for (int i = 0; i < K - 1; i++) BK *= B;
    size_t probes = 0, skipped = 0;
    for (int l = 0; l < 3; l++) {
        for (size_t off = start; off + K <= end; off += STRIDE) {
            if (trivial(roms[l] + off, K)) { skipped++; continue; }
            if (probes >= HSIZE / 2) { fprintf(stderr, "range too large\n"); return 2; }
            insert(hash_block(roms[l] + off), (uint32_t)off, (uint8_t)l);
            probes++;
        }
    }
    fprintf(stderr, "rom_scan: ROM 0x%zX-0x%zX, %zu-byte windows, %zu probes (%zu low-entropy skipped), 3 byte orders\n",
            start, end, window, probes, skipped);
    int hits_total = 0;
    for (int a = 5; a < argc; a++) {
        size_t n;
        uint8_t* d = read_all(argv[a], &n);
        if (!d) { fprintf(stderr, "cannot read %s\n", argv[a]); return 2; }
        int hits = 0;
        size_t last_hit_end = 0;
        if (n >= K) {
            uint64_t h = hash_block(d);
            for (size_t j = 0; j + K <= n; j++) {
                if (j > 0) h = (h - d[j - 1] * BK) * B + d[j + K - 1];
                uint32_t i = (uint32_t)(h >> (64 - HBITS));
                while (used[i]) {
                    if (table[i].h == h) {
                        const uint8_t* r = roms[table[i].layout];
                        size_t ro = table[i].off;
                        if (memcmp(r + ro, d + j, K) == 0) {
                            // Extend both ways inside [start, end) and the file.
                            size_t back = 0, fwd = K;
                            while (back < j && back < ro - start && r[ro - back - 1] == d[j - back - 1]) back++;
                            while (j + fwd < n && ro + fwd < end && r[ro + fwd] == d[j + fwd]) fwd++;
                            size_t len = back + fwd;
                            if (len >= window && !trivial(d + j - back, len) && j - back >= last_hit_end) {
                                if (hits < 5) printf("HIT %s @0x%zX: %zu bytes = ROM 0x%zX (%s)\n", argv[a], j - back, len, ro - back,
                                                     table[i].layout == 0 ? "z64" : table[i].layout == 1 ? "v64" : "n64");
                                hits++;
                                last_hit_end = j + fwd;
                            }
                        }
                    }
                    i = (i + 1) & (HSIZE - 1);
                }
            }
        }
        if (hits) { printf("%s: %d ROM run(s) of >= %zu bytes\n", argv[a], hits, window); hits_total += hits; }
        free(d);
    }
    printf("rom_scan: %d file(s) scanned, %d hit(s)\n", argc - 5, hits_total);
    return hits_total ? 1 : 0;
}
