// EverFront: game launch on the EverDrive GBA Pro.
//
// Same sequence as the stock menu (fw 26.0127), worked out on a real
// cart with the slottest ROM (0.1-0.12):
//   rom-db lookup + patch words (before loading: efu: reads use RAM0 > 8 MB)
//   -> load rom into RAM0 in CMOD_MENU (never CMOD_TEST after this)
//   -> save type from rom-db / stock save files / MCU search
//   -> [in-game menu: rom-db patches, hook, microcode, boot mode 'ST']
//   -> set_path rom/save/rtc -> dev_set_cfg -> dev_start -> CMOD_GAME
//   -> boot_manual (stock-style reset; keeps the IRQ vector for the hook)
// The in-game-menu hook and microcode come from the stock menu resident in
// cart RAM1 at run time; nothing of it ships in this rom.

#include "launch.h"
#include "ed/appmain.h"
#include <stdarg.h>

void boot_manual(u32 irq_vector);
void gVsync();

#define LOG_PATH APP_DIR "/log.txt"
#define BAK_EXT ".slotbak"

#define NAME_MAX 255

static void path_join(char *out, const char *dir, const char *name) {
    u32 o = 0;
    for (const char *s = dir; *s && o < 1024; s++) out[o++] = *s;
    if (dir[0] && o < 1024) out[o++] = '/';
    for (const char *s = name; *s && o < 1024; s++) out[o++] = *s;
    out[o] = 0;
}

//****************************************************************************** log

static char logbuf[16 * 1024];
static u32 log_len, log_flushed;

void lgq(const char *fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    u16 n = vsprintf_ed(buf, fmt, ap);
    va_end(ap);
    if (log_len + n >= sizeof (logbuf)) {      // keep the newest
        log_len = log_flushed = 0;
    }
    for (u16 i = 0; i < n; i++) logbuf[log_len++] = buf[i];
}
#define lg lgq

const char *log_text(u32 *len) {
    *len = log_len;
    return logbuf;
}

void log_trim(void) {
    if (ed_fs_file_open((u8 *) LOG_PATH, FA_READ)) return;
    u32 size = ed_fs_file_available();
    u32 keep = 0;
    if (size > 64 * 1024) {
        keep = sizeof (logbuf) - 1;
        if (ed_fs_file_set_ptr(size - keep) || ed_fs_file_read(logbuf, keep)) keep = 0;
    }
    ed_fs_file_close();
    if (!keep) return;
    u32 from = 0;
    while (from < keep && logbuf[from] != '\n') from++;   // start on a whole line
    if (ed_fs_file_open((u8 *) LOG_PATH, FA_WRITE | FA_CREATE_ALWAYS)) return;
    ed_fs_file_write(logbuf + from, keep - from);
    ed_fs_file_close();
    log_len = log_flushed = 0;
}

u8 log_flush(void) {
    if (log_len == log_flushed) return 0;
    ed_fs_dir_make((u8 *) APP_DIR);
    u8 r = ed_fs_file_open((u8 *) LOG_PATH, FA_WRITE | FA_OPEN_APPEND);
    if (r) return r;
    r = ed_fs_file_write(logbuf + log_flushed, log_len - log_flushed);
    u8 r2 = ed_fs_file_close();
    if (!r) log_flushed = log_len;
    return r ? r : r2;
}


//****************************************************************************** strings

static u32 str_len(const char *s) {
    u32 n = 0;
    while (s[n]) n++;
    return n;
}

static void str_cpy(char *d, const char *s) {
    while ((*d++ = *s++));
}

// Appends s to d without letting d (capacity 1025) overflow.
static void str_cat(char *d, const char *s) {
    u32 o = str_len(d);
    while (*s && o < 1024) d[o++] = *s++;
    d[o] = 0;
}

static int str_eq_n(const char *a, const char *b, u32 n) {
    for (u32 i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

__attribute__((unused)) static int ends_with_ci(const char *s, const char *ext) {
    u32 a = str_len(s), b = str_len(ext);
    if (a < b) return 0;
    for (u32 i = 0; i < b; i++) if (lower(s[a - b + i]) != lower(ext[i])) return 0;
    return 1;
}


//****************************************************************************** probe

// Stock menu fw 26.0127 (SysInfo sw 5C3B.61B7): where its in-game-menu
// pieces sit in the menu image (cart RAM1, offsets = menu.bin offsets).
#define FW_DATE         0x5C3B
#define FW_TIME         0x61B7
#define IGM_TABLE_OFF   0xE218  // {micc, combo ptrs x2, hooks x3, hook addr, len}
#define IGM_MICC_OFF    0xDC8C
#define IGM_MICC_LEN    0x600
#define IGM_CHEAT_LEN   0xA00
#define IGM_HOOK_OFF    0xE11C  // hook variant: in-game menu, no cheats
#define IGM_HOOK_LEN    0x60
#define IGM_HOOK_ADDR   0x08000004

static const u32 igm_table_want[8] = {
    0x0200DC8C, 0x0200E12C, 0x0200E188, 0x0200E11C,
    0x0200E178, 0x0200E1D4, 0x08000004, 0x60
};

typedef struct {
    char rom[1025], brm[1025], rtc[1025];
    u8 rom_r, brm_r, rtc_r;
    u32 own_size;
    u8 seek_ok;         // MCU pattern search usable
    u8 igm_ok;          // stock in-game menu pieces found and checked
    u8 hook[IGM_HOOK_LEN];
    u8 micc[IGM_MICC_LEN];
} Probe;

static Probe probe;
static u8 zeros[IGM_CHEAT_LEN];


static void igm_check(const SysInfoIO *inf) {
    u32 tbl[8];
    u8 tpl[IGM_HOOK_LEN];
    probe.igm_ok = 0;
    if (inf->sw_date != FW_DATE || inf->sw_time != FW_TIME) {
        lg("igm: fw not 26.0127, off\n");
        return;
    }
    ed_fci_rd(ADDR_FCI_MENU + IGM_TABLE_OFF, tbl, sizeof (tbl));
    for (int i = 0; i < 8; i++) {
        if (tbl[i] != igm_table_want[i]) {
            lg("igm: table mismatch, off\n");
            return;
        }
    }
    ed_fci_rd(ADDR_FCI_MENU + IGM_HOOK_OFF, tpl, IGM_HOOK_LEN);
    ed_fci_rd(ADDR_FCI_MENU + 4, probe.hook, IGM_HOOK_LEN);
    int diff = 0;
    for (int i = 0; i < IGM_HOOK_LEN; i++) {
        if (i == 0x10 || i == 0x14) continue;
        if (tpl[i] != probe.hook[i]) diff++;
    }
    lgq("igm hook combo bytes %02X %02X (template %02X %02X), other diffs %d\n",
            probe.hook[0x10], probe.hook[0x14], tpl[0x10], tpl[0x14], diff);
    if (diff) {
        // We were started without the in-game menu: use the template with
        // the default SL+SR combo.
        for (int i = 0; i < IGM_HOOK_LEN; i++) probe.hook[i] = tpl[i];
        probe.hook[0x10] = 0xFF;
        probe.hook[0x14] = 0x00;
        lg("igm: template, SL+SR\n");
    }
    ed_fci_rd(ADDR_FCI_MENU + IGM_MICC_OFF, probe.micc, IGM_MICC_LEN);
    probe.igm_ok = 1;
    lg("igm: ready\n");
}


//****************************************************************************** load and scan

enum { SRC_NONE, SRC_SCAN, SRC_MCU, SRC_FILE, SRC_DB };
static const char *src_name[] = {"none", "scan", "mcu", "file", "db"};

typedef struct {
    char path[1025];    // path as given to the cart
    u32 size;
    char title[13];
    char code[5];
    char save_tag[16];
    u32 save_off;
    u8 brm_type;
    u32 brm_size;
    u8 rtc;
    u8 save_src, rtc_src;
    u8 file_brm_type;   // from an existing stock save file
    u8 file_rtc;
    char dir[1025];     // edgba/gamedata/<rom file>
    char brm[1025], rtc_path[1025];
    u8 map_ok;
    u8 igm;
    u8 reset_manual;
    u8 pre_stop;
    u8 verify_bad;      // 2 MB regions whose RAM CRC didn't match the file
    u8 db_found;        // game found in the stock rom-db
    u8 db_gpak;         // its hardware byte (save type bits 0-2, tilt 4, gyro 5, solar 6, rtc 7)
    u8 til, gyr, sol;   // sensors the game uses
    u8 db_flags;
    u16 db_npatch;      // patch words for the in-game menu
    u32 db_off;         // file offset of those words
    u32 hdr_crc;        // CRC-32 of header 0xA0..0x29F (stock db key)
    u32 hdr_code;       // u32 at 0xAC (game code)
    u8 hdr_chk;         // header complement byte 0xBD        // dev_stop before launching (the stock menu doesn't)
} Game;

static Game g;
static void romdb_lookup(void);

static u8 load_rom(void) {
    u8 r = ed_fs_file_open((u8 *) g.path, FA_READ);
    if (r) return r;
    g.size = ed_fs_file_available();
    if (g.size > SIZE_ROM) {
        ed_fs_file_close();
        return 0xE1;
    }
    // One read of the whole file, in menu mode, as the stock menu does. In
    // test mode the cart's system window covers RAM0 from 8 MB up and the
    // top half of a big rom never lands (0.5: 8-16 MB bad, 0-8 MB fine).
    ed_set_cmode(CMOD_MENU);
    r = ed_fs_file_read_fci(ADDR_FCI_ROM, g.size);
    ed_fs_file_close();
    return r;
}

static const struct {
    const char *tag;
    u8 type;
    u32 size;
} save_tags[] = {
    // "_V" is appended at run time so this rom doesn't contain the tags itself.
    {"EEPROM", BRM_TYPE_EEP, BRM_SIZE_EEP},
    {"SRAM_F", BRM_TYPE_SRAM, BRM_SIZE_SRAM},
    {"SRAM", BRM_TYPE_SRAM, BRM_SIZE_SRAM},
    {"FLASH1M", BRM_TYPE_FLA128, BRM_SIZE_FL128},
    {"FLASH512", BRM_TYPE_FLA64, BRM_SIZE_FL64},
    {"FLASH", BRM_TYPE_FLA64, BRM_SIZE_FL64},
};
#define N_TAGS (sizeof (save_tags) / sizeof (save_tags[0]))

static void full_tag(const char *base, char *out) {
    str_cpy(out, base);
    u32 n = str_len(out);
    out[n] = '_';
    out[n + 1] = 'V';
    out[n + 2] = 0;
}

// Same sizes the stock menu uses (8/32/64/128 KB).
static u32 brm_size_of(u8 t) {
    switch (t) {
        case BRM_TYPE_EEP: return BRM_SIZE_EEP;
        case BRM_TYPE_SRAM: return BRM_SIZE_SRAM;
        case BRM_TYPE_FLA64: return BRM_SIZE_FL64;
        case BRM_TYPE_FLA128: return BRM_SIZE_FL128;
    }
    return 0;
}

// Same file names the stock menu uses.
static const char *brm_file_of(u8 t) {
    switch (t) {
        case BRM_TYPE_EEP: return "bram.eep";
        case BRM_TYPE_SRAM: return "bram.srm";
        case BRM_TYPE_FLA64:
        case BRM_TYPE_FLA128: return "bram.fla";
    }
    return "bram.brm";
}

// Asks the cart's MCU to find pat in the open file. Returns the offset or
// 0xFFFFFFFF. Confirms by reading the bytes back.
static u32 mcu_find(const char *pat, u32 fsize, char *ver, u32 ver_size) {
    u8 n = str_len(pat);
    u32 addr = 0xFFFFFFFF;
    u8 r = ed_fs_file_set_ptr(0);
    if (!r) r = ed_fs_file_seek_pat((u8 *) pat, n, fsize, &addr);
    lgq("seek_pat %s [%02X] %X\n", pat, r, addr);
    if (r || addr >= fsize) return 0xFFFFFFFF;
    char buf[16];
    if (ed_fs_file_set_ptr(addr) || ed_fs_file_read(buf, 16)) return 0xFFFFFFFF;
    if (!str_eq_n(buf, pat, n)) return 0xFFFFFFFF;
    if (ver) {
        u32 k = 0;
        for (; k < ver_size - 1 && k < 16 && buf[k] >= 0x20 && buf[k] < 0x7F; k++) ver[k] = buf[k];
        ver[k] = 0;
    }
    return addr;
}

// Slow fallback: MCU search of the rom file (about 11 s per 16 MB miss).
static void mcu_scan(void) {
    if (ed_fs_file_open((u8 *) g.path, FA_READ)) return;
    char t[16];
    for (u32 i = 0; i < N_TAGS; i++) {
        full_tag(save_tags[i].tag, t);
        u32 a = mcu_find(t, g.size, g.save_tag, sizeof (g.save_tag));
        if (a != 0xFFFFFFFF) {
            g.brm_type = save_tags[i].type;
            g.save_off = a;
            g.save_src = SRC_MCU;
            break;
        }
    }
    full_tag("SIIRTC", t);
    if (mcu_find(t, g.size, 0, 0) != 0xFFFFFFFF) {
        g.rtc = 1;
        g.rtc_src = SRC_MCU;
    }
    ed_fs_file_close();
}



static u8 file_exists(const char *dir, const char *name, u32 *size) {
    char p[1025];
    FileInfo fi;
    u8 nm[NAME_MAX + 1];
    path_join(p, dir, name);
    fi.file_name = nm;
    if (ed_fs_file_info((u8 *) p, &fi)) return 0;
    if (size) *size = fi.size;
    return 1;
}

// Sensor carts by game code (first 3 letters), for roms the stock rom-db
// doesn't know (translations, hacks with a changed header).
static void sensors_by_code(void) {
    static const struct { char c[4]; u8 til, gyr, sol; } list[] = {
        {"U3I", 0, 0, 1}, {"U32", 0, 0, 1}, {"U33", 0, 0, 1},   // Boktai 1-3
        {"KYG", 1, 0, 0},                                        // Yoshi Topsy-Turvy / Yoshi no Banyuu Inryoku
        {"KHP", 1, 0, 0},                                        // Koro Koro Puzzle Happy Panechu!
        {"RZW", 0, 1, 0},                                        // WarioWare: Twisted! / Mawaru Made in Wario
    };
    const char *code = (const char *) &g.hdr_code;
    for (u32 i = 0; i < sizeof (list) / sizeof (list[0]); i++)
        if (code[0] == list[i].c[0] && code[1] == list[i].c[1] && code[2] == list[i].c[2]) {
            g.til |= list[i].til;
            g.gyr |= list[i].gyr;
            g.sol |= list[i].sol;
        }
}

static void detect(void) {
    g.brm_type = BRM_TYPE_OFF;
    g.rtc = 0;
    g.save_tag[0] = 0;
    g.save_src = g.rtc_src = SRC_NONE;

    // No test mode after the rom is loaded: entering it and coming back
    // to menu mode damaged RAM0 from 8 MB up (0.10, 0.11). Save type comes
    // from the stock rom-db, then the stock save files, then the MCU.
    g.map_ok = 1;
    g.til = g.gyr = g.sol = 0;
    if (g.db_found) {
        g.brm_type = g.db_gpak & 7;
        g.rtc = g.db_gpak >> 7;
        g.save_src = g.rtc_src = SRC_DB;
        g.til = (g.db_gpak >> 4) & 1;
        g.gyr = (g.db_gpak >> 5) & 1;
        g.sol = (g.db_gpak >> 6) & 1;
    }
    sensors_by_code();

    u32 sz = 0;
    g.file_brm_type = BRM_TYPE_OFF;
    if (file_exists(g.dir, "bram.fla", &sz)) g.file_brm_type = sz > BRM_SIZE_FL64 ? BRM_TYPE_FLA128 : BRM_TYPE_FLA64;
    else if (file_exists(g.dir, "bram.srm", 0)) g.file_brm_type = BRM_TYPE_SRAM;
    else if (file_exists(g.dir, "bram.eep", 0)) g.file_brm_type = BRM_TYPE_EEP;
    g.file_rtc = file_exists(g.dir, "tstamp.rtc", 0);
    lgq("files: brm %u rtc %u\n", g.file_brm_type, g.file_rtc);

    // Above 8 MB (or a bad map) and nothing from the stock files: ask the
    // MCU (slow, ~11 s per 16 MB miss).
    int need = (g.save_src == SRC_NONE && g.file_brm_type == BRM_TYPE_OFF) ||
            (g.rtc_src == SRC_NONE && !g.file_rtc);
    if (need && probe.seek_ok) {
        lg("slow search...\n");
        mcu_scan();
    }

    if (g.db_found) {
        lgq("db says brm %u rtc %u (ours %u/%u)\n", g.db_gpak & 7, g.db_gpak >> 7, g.brm_type, g.rtc);
    }

    if (g.save_src == SRC_NONE && g.file_brm_type != BRM_TYPE_OFF) {
        g.brm_type = g.file_brm_type;
        g.save_src = SRC_FILE;
    }
    if (g.rtc_src == SRC_NONE && g.file_rtc) {
        g.rtc = 1;
        g.rtc_src = SRC_FILE;
    }
    g.brm_size = brm_size_of(g.brm_type);

    path_join(g.brm, g.dir, brm_file_of(g.brm_type));
    path_join(g.rtc_path, g.dir, "tstamp.rtc");
}


//****************************************************************************** rom-db

// The stock menu's per-game database, read from inside the firmware package
// through the cart, the same way the stock menu does. Layout (fw 26.0127):
// u32 count at 0; 8-byte records at offset 8*i, records 2..count-1 checked;
// record = {u32 key, u8 ver, u8 flags, u8 npatch, u8 gpak}; flags bit1: key
// is the header CRC, else the game code; flags bit0: ver must equal header
// byte 0xBD. Patch words for record i follow the table (count*8) after
// those of the records before it.
#define ROMDB_PATH "efu:edgba/syscore/rom-db.dat"
#define ROMDB_MAX 0x1000
static u8 dbtab[ROMDB_MAX * 8];

static u32 crc32_buf(u32 crc, const u8 *p, u32 n) {
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    return ~crc;
}

static void hdr_keys(void) {
    u8 h[0x2C0];
    g.hdr_crc = g.hdr_code = 0;
    g.hdr_chk = 0;
    if (ed_fs_file_open((u8 *) g.path, FA_READ)) return;
    u8 r = ed_fs_file_read(h, sizeof (h));
    ed_fs_file_close();
    if (r) return;
    g.hdr_crc = crc32_buf(0, h + 0xA0, 0x200);
    g.hdr_code = h[0xAC] | (h[0xAD] << 8) | (h[0xAE] << 16) | ((u32) h[0xAF] << 24);
    g.hdr_chk = h[0xBD];
    for (int i = 0; i < 12; i++) g.title[i] = (h[0xA0 + i] >= 0x20 && h[0xA0 + i] < 0x7F) ? h[0xA0 + i] : 0;
    g.title[12] = 0;
    for (int i = 0; i < 4; i++) g.code[i] = (h[0xAC + i] >= 0x20 && h[0xAC + i] < 0x7F) ? h[0xAC + i] : '?';
    g.code[4] = 0;
}

static void romdb_lookup(void) {
    g.db_found = 0;
    g.db_npatch = 0;
    hdr_keys();
    u8 r = ed_fs_file_open((u8 *) ROMDB_PATH, FA_READ);
    if (r) {
        lg("rom-db open [%02X]\n", r);
        return;
    }
    u32 size = ed_fs_file_available();
    u32 hdr[2] = {0, 0};
    r = ed_fs_file_read(hdr, 8);
    u32 count = hdr[0] < ROMDB_MAX ? hdr[0] : ROMDB_MAX;
    if (!r && count > 1) {
        r = ed_fs_file_set_ptr(0);
        if (!r) r = ed_fs_file_read(dbtab, count * 8);
    }
    ed_fs_file_close();
    lgq("rom-db size %u count %u [%02X] crc %08X code %08X chk %02X\n",
            size, count, r, g.hdr_crc, g.hdr_code, g.hdr_chk);
    if (r) return;

    u32 ip = 0;
    for (u32 i = 2; i < count; i++) {
        const u8 *e = dbtab + i * 8;
        u32 key = e[0] | (e[1] << 8) | (e[2] << 16) | ((u32) e[3] << 24);
        u8 ver = e[4], flags = e[5], np = e[6], gp = e[7];
        int match = (flags & 2) ? key == g.hdr_crc : key == g.hdr_code;
        if (match && (flags & 1)) match = ver == g.hdr_chk;
        if (match) {
            g.db_found = 1;
            g.db_gpak = gp;
            g.db_flags = flags;
            g.db_npatch = np;
            g.db_off = count * 8 + ip;
            lg("rom-db: #%u gpak %02X patches %u\n", i, gp, np);
            return;
        }
        ip += np * 4;
    }
    lg("rom-db: not listed\n");
}

static u32 db_words[256];

// Reads this game's patch words into EWRAM. Must run before the rom is
// loaded: opening an efu: file overwrites cart RAM0 from 8 MB up (0.10).
static u8 romdb_read_patches(void) {
    if (!g.db_found || g.db_npatch == 0) return 0;
    u8 r = ed_fs_file_open((u8 *) ROMDB_PATH, FA_READ);
    if (r) return r;
    r = ed_fs_file_set_ptr(g.db_off);
    if (!r) r = ed_fs_file_read(db_words, g.db_npatch * 4);
    ed_fs_file_close();
    if (r) g.db_npatch = 0;
    return r;
}

// Writes this game's in-game-menu patches into the loaded rom (cart RAM0).
static u8 romdb_patch(void) {
    u32 *w = db_words;
    if (!g.db_found || g.db_npatch == 0) return 0;
    u32 n = 0;
    for (u32 i = 0; i < g.db_npatch;) {
        u32 addr = w[i] & 0x1FFFFFF;
        u32 val;
        if (w[i] >> 25) {
            val = i + 1 < g.db_npatch ? w[i + 1] : 0;
            i += 2;
        } else {
            val = 0x03007FF4;
            i += 1;
        }
        ed_fci_wr(ADDR_FCI_ROM + addr, &val, 4);
        lgq("patch %07X = %08X\n", addr, val);
        n++;
    }
    lg("rom patched (%u)\n", n);
    return 0;
}


//****************************************************************************** launch

static void backup_save(void) {
    char bak[1100];
    if (ed_fs_file_test((u8 *) g.brm) != 0) {
        lg("no existing save\n");
        return;
    }
    str_cpy(bak, g.brm);
    str_cat(bak, BAK_EXT);
    if (ed_fs_file_test((u8 *) bak) == 0) {
        lg("backup exists, kept\n");
        return;
    }
    u8 r = ed_fs_file_copy((u8 *) g.brm, (u8 *) bak, FA_WRITE | FA_CREATE_NEW);
    lg("save backup [%02X]\n", r);
}


// The stock menu's in-game-menu install (no cheats): hook into both cart
// banks, microcode + empty cheat area, boot mode 'ST'. The IRQ vector is
// set by boot_manual.
static void igm_install(void) {
    ed_fci_wr(ADDR_FCI_ROM + (IGM_HOOK_ADDR & 0x1FFFFFF), probe.hook, IGM_HOOK_LEN);
    ed_fci_wr(ADDR_FCI_RAM1 + (IGM_HOOK_ADDR & 0x1FFFFFF), probe.hook, IGM_HOOK_LEN);
    ed_fci_wr(ADDR_FCI_MICC, probe.micc, IGM_MICC_LEN);
    ed_fci_wr(ADDR_FCI_MICC + IGM_MICC_LEN, zeros, IGM_CHEAT_LEN);
    ed_bootmod_set(BMOD_GBA_SST);
    lg("igm installed\n");
}

static u8 launch(void) {
    u8 r;
    lgq("---- launch %s ----\n", g.path);

    ed_fs_dir_make((u8 *) "edgba/gamedata");
    ed_fs_dir_make((u8 *) g.dir);
    backup_save();

    // All cart RAM writes in menu mode, as the stock menu does: in test
    // mode writes to RAM0 from 8 MB up don't land in the rom (0.5, 0.7).
    ed_set_cmode(CMOD_MENU);
    if (g.igm) {
        r = romdb_patch();
        lg("patch [%02X]\n", r);
        igm_install();
    }

    DevCfg dc = {0};
    dc.rom_size = g.size;
    dc.brm_size = g.brm_size;
    dc.brm_type = g.brm_type;
    dc.rtc_en = g.rtc;
    // sensors: the cart's own solar sensor and motion sensor stand in for
    // the ones in these carts; 32 is krikzz's default solar sensitivity
    // (SOL_SENS_DEF in his everdrive.h), no tilt flip
    dc.til_en = g.til;
    dc.gyr_en = g.gyr;
    dc.sol_en = g.sol;
    dc.sol_sens = g.sol ? 32 : 0;
    dc.til_flip = 0;
    lg("cfg rom %u brm %u/%u rtc %u tilt %u gyro %u solar %u\n", dc.rom_size, dc.brm_type, dc.brm_size, dc.rtc_en,
            dc.til_en, dc.gyr_en, dc.sol_en);
    lg("igm %u\n", g.igm);

    // Nothing touches the SD card after dev_start, like the stock menu.
    log_flush();

    // Launch commands in menu mode, like the stock menu.
    ed_set_cmode(CMOD_MENU);

    r = ed_dev_set_path((u8 *) g.path, DEV_PATH_ROM);
    if (!r) r = ed_dev_set_path((u8 *) g.brm, DEV_PATH_BRM);
    if (!r) r = ed_dev_set_path((u8 *) g.rtc_path, DEV_PATH_RTC);
    lg("paths [%02X]\n", r);
    if (r) return r;
    r = ed_dev_set_cfg(&dc);
    lg("set_cfg [%02X]\n", r);
    if (r) return r;
    r = ed_dev_start();
    lg("dev_start [%02X]\n", r);
    if (r) return r;

    for (int i = 0; i < 30; i++) gVsync();
    ed_set_cmode(CMOD_GAME);
    boot_manual(g.igm ? IGM_HOOK_ADDR : 0);
    return 0;
}


//****************************************************************************** other systems

// Games for other systems run through the EverDrive's emulators, set up the
// stock menu's way (edgba/edapp/<ext>/): the folder's only .gba file, or
// app.gba when there are several; an optional config.txt with
//   INC_MODE=1|2       1: the rom goes into the cart after the emulator
//                      2: only the rom's path (512 bytes) goes there
//   ROM_ADDR=0x...     where the rom goes (default: right after the emulator)
//   BRM_TYPE=EEP|SRAM|FLA64|FLA128|OFF   the emulator's save type
//   EXE_MODE=1|2       (logged; 1 is assumed)
// PocketNES (.nes) and SMS Advance (.sms, .gg) find their roms by a header
// in front of them; Goomba and others (.gb, .gbc) take the rom as it is.

typedef struct {
    u32 rom_addr;
    u8 brm_type, inc_mode, exe_mode;
} AppCfg;

static int str_eq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

static u32 hex_val(const char *p) {
    u32 v = 0;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    for (;; p++) {
        char c = lower(*p);
        if (c >= '0' && c <= '9') v = v * 16 + c - '0';
        else if (c >= 'a' && c <= 'f') v = v * 16 + c - 'a' + 10;
        else return v;
    }
}

static int key_is(const char *line, const char *key) {
    u32 n = str_len(key);
    return str_eq_n(line, key, n);
}

static void app_config(const char *dir, AppCfg *c) {
    char p[64], buf[512];
    c->rom_addr = 0;
    c->brm_type = 0xFF;
    c->inc_mode = 1;
    c->exe_mode = 1;
    path_join(p, dir, "config.txt");
    if (ed_fs_file_open((u8 *) p, FA_READ)) return;
    u32 n = ed_fs_file_available();
    if (n > sizeof (buf) - 1) n = sizeof (buf) - 1;
    u8 r = ed_fs_file_read(buf, n);
    ed_fs_file_close();
    if (r) return;
    buf[n] = 0;
    for (char *l = buf; *l;) {
        char *e = l;
        while (*e && *e != '\n' && *e != '\r') e++;
        char ch = *e;
        *e = 0;
        if (key_is(l, "INC_MODE=") && (l[9] == '1' || l[9] == '2')) c->inc_mode = l[9] - '0';
        if (key_is(l, "EXE_MODE=") && (l[9] == '1' || l[9] == '2')) c->exe_mode = l[9] - '0';
        if (key_is(l, "ROM_ADDR=")) c->rom_addr = hex_val(l + 9);
        if (key_is(l, "BRM_TYPE=")) {
            const char *v = l + 9;
            if (str_eq(v, "EEP")) c->brm_type = BRM_TYPE_EEP;
            else if (str_eq(v, "SRAM")) c->brm_type = BRM_TYPE_SRAM;
            else if (str_eq(v, "FLA64")) c->brm_type = BRM_TYPE_FLA64;
            else if (str_eq(v, "FLA128")) c->brm_type = BRM_TYPE_FLA128;
            else if (str_eq(v, "OFF")) c->brm_type = BRM_TYPE_OFF;
        }
        l = ch ? e + 1 : e;
    }
}

// The emulator: the folder's only .gba file, else app.gba.
static u8 app_find(const char *dir, char *out) {
    u16 size, gba = 0;
    FileInfo fi;
    u8 name[NAME_MAX + 1], first[NAME_MAX + 1];
    fi.file_name = name;
    u8 r = ed_fs_dir_load((u8 *) dir, DIR_OPT_HIDESYS);
    if (r) return r;
    ed_fs_dir_get_size(&size);
    for (u16 i = 0; i < size; i++) {
        ed_fs_dir_get_recs(i, 1, NAME_MAX);
        if (ed_fs_rx_next_rec(&fi)) break;
        u32 n = str_len((char *) name);
        if (fi.is_dir || n < 5 || name[n - 4] != '.' || lower(name[n - 3]) != 'g' || lower(name[n - 2]) != 'b' ||
                lower(name[n - 1]) != 'a')
            continue;
        if (!gba++) str_cpy((char *) first, (char *) name);
    }
    if (!gba) return 0xE2;
    path_join(out, dir, gba == 1 ? (char *) first : "app.gba");
    return 0;
}

u8 launch_app(const char *path, const char *file_name, const char *ext, LaunchStatus status, const char **err) {
    static u8 zero_game[sizeof (Game)];
    u8 *gp = (u8 *) &g;
    for (u32 i = 0; i < sizeof (Game); i++) gp[i] = zero_game[i];
    char dir[48], app[1025];
    str_cpy(dir, "edgba/edapp/");
    str_cat(dir, ext);
    lgq("---- %s (%s) ----\n", path, dir);

    *err = "No emulator for this system";
    u8 r = app_find(dir, app);
    lgq("emulator %s [%02X]\n", r ? "-" : app, r);
    if (r) return r;
    AppCfg c;
    app_config(dir, &c);
    lgq("config inc %u exe %u brm %u addr %X\n", c.inc_mode, c.exe_mode, c.brm_type, c.rom_addr);

    status("Loading");
    ed_set_cmode(CMOD_MENU);
    *err = "Can't read the emulator";
    r = ed_fs_file_open((u8 *) app, FA_READ);
    if (r) return r;
    u32 app_size = ed_fs_file_available();
    r = app_size > SIZE_ROM / 2 ? 0xE1 : ed_fs_file_read_fci(ADDR_FCI_ROM, app_size);
    // the emulator's save type when config.txt doesn't say: its own save tag
    u8 brm = c.brm_type;
    if (!r && brm == 0xFF) {
        char t[16];
        brm = BRM_TYPE_SRAM;                          // what these emulators use
        for (u32 i = 0; i < N_TAGS; i++) {
            full_tag(save_tags[i].tag, t);
            if (mcu_find(t, app_size, 0, 0) != 0xFFFFFFFF) { brm = save_tags[i].type; break; }
        }
    }
    ed_fs_file_close();
    lgq("app %u bytes [%02X], save type %u\n", app_size, r, brm);
    if (r) return r;

    u32 base = c.rom_addr ? c.rom_addr & 0x1FFFFFF : app_size;
    if (c.inc_mode == 2) {                            // the emulator loads the rom itself
        static char pbuf[512];
        for (u32 i = 0; i < sizeof (pbuf); i++) pbuf[i] = 0;
        str_cpy(pbuf, path);
        ed_fci_wr(ADDR_FCI_ROM + base, pbuf, sizeof (pbuf));
        g.size = base + sizeof (pbuf);
    } else {
        *err = "Can't read the game";
        r = ed_fs_file_open((u8 *) path, FA_READ);
        if (r) return r;
        u32 size = ed_fs_file_available(), skip = (size & 0x3FF) == 0x200 ? 0x200 : 0;   // a copier header
        size -= skip;
        // the emulator's header in front of the rom
        static u8 hdr[64];
        u32 hlen = 0;
        for (u32 i = 0; i < sizeof (hdr); i++) hdr[i] = 0;
        const char *nm = file_name;
        if (str_eq(ext, "nes")) {                     // PocketNES: name[32], size, flags, follow, reserved
            for (u32 i = 0; i < 31 && nm[i]; i++) hdr[i] = nm[i];
            hlen = 48;
            hdr[32] = size; hdr[33] = size >> 8; hdr[34] = size >> 16; hdr[35] = size >> 24;
        } else if (str_eq(ext, "sms") || str_eq(ext, "gg")) {   // SMS Advance: "SMS",1A, size, flags, ..., name at 32
            hdr[0] = 'S'; hdr[1] = 'M'; hdr[2] = 'S'; hdr[3] = 0x1A;
            hdr[4] = size; hdr[5] = size >> 8; hdr[6] = size >> 16; hdr[7] = size >> 24;
            if (str_eq(ext, "gg")) hdr[8] |= 4;
            for (u32 i = 0; i < 31 && nm[i]; i++) hdr[32 + i] = nm[i];
            hlen = 64;
        }
        if (base + hlen + size > SIZE_ROM) { ed_fs_file_close(); *err = "Game too big"; return 0xE1; }
        if (hlen) ed_fci_wr(ADDR_FCI_ROM + app_size, hdr, hlen);
        r = ed_fs_file_set_ptr(skip);
        if (!r) r = ed_fs_file_read_fci(ADDR_FCI_ROM + base + hlen, size);
        ed_fs_file_close();
        g.size = base + hlen + size;
        lgq("rom %u bytes at %X (+%u header) [%02X]\n", size, base, hlen, r);
        if (r) return r;
    }

    str_cpy(g.path, path);
    path_join(g.dir, "edgba/gamedata", file_name);
    g.brm_type = brm;
    g.brm_size = brm_size_of(brm);
    path_join(g.brm, g.dir, brm_file_of(brm));
    path_join(g.rtc_path, g.dir, "tstamp.rtc");
    // No in-game menu for emulated games: it installs, but the emulators
    // (Goomba, PocketNES, SMSAdvance) take over the IRQ vector its hook needs,
    // and there are no rom-db patches to keep it (tried 2026-11-10).
    g.igm = 0;
    status("Starting");
    r = launch();
    *err = "Cart refused the game";
    log_flush();
    return r;
}

//****************************************************************************** api

void launch_init(void) {
    SysInfoIO inf;
    ed_sys_inf(&inf);
    lgq("fw %04X.%04X\n", inf.sw_date, inf.sw_time);
    probe.seek_ok = 1;
    igm_check(&inf);
}

u8 launch_igm_available(void) {
    return probe.igm_ok;
}


u8 launch_game(const char *path, const char *file_name, LaunchStatus status, const char **err) {
    static u8 zero_game[sizeof (Game)];
    u8 *gp = (u8 *) &g;
    for (u32 i = 0; i < sizeof (Game); i++) gp[i] = zero_game[i];
    str_cpy(g.path, path);
    path_join(g.dir, "edgba/gamedata", file_name);
    g.igm = probe.igm_ok;
    lgq("---- %s ----\n", g.path);

    // rom-db first: efu: reads use cart RAM0 above 8 MB as scratch.
    romdb_lookup();
    u8 r = romdb_read_patches();
    if (r) lgq("rom-db patches [%02X]\n", r);

    status("Loading");
    r = load_rom();
    lgq("load [%02X] %u\n", r, g.size);
    if (r) {
        *err = r == 0xE1 ? "ROM too big" : "Can't read the ROM";
        log_flush();
        return r;
    }
    status("Checking save type");
    detect();
    lgq("save %u/%u rtc %u (%s)\n", g.brm_type, g.brm_size, g.rtc, src_name[g.save_src]);
    status("Starting");
    r = launch();
    *err = "Cart refused the game";
    log_flush();
    return r;
}
