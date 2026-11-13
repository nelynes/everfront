// EverFront launcher for the EverDrive GBA X5 Mini, stage 1: load the rom
// into the cart's PSRAM and start it. No saves yet: the save chip is
// switched off, so nothing on the cart or the card changes.
#include "../launch.h"
#include "../plat.h"
#include "../fatfs/ff.h"
#include "../edmini/everdrive.h"
#include "../edmini/disk.h"
#include <stdarg.h>

FIL *plat_mini_file(void);
void boot_manual(u32 irq_vector);
u16 vsprintf_ed(char *buf, const char *fmt, va_list ap);

//****************************************************************************** log (memory only)

static char logbuf[8 * 1024];
static u32 log_len;

void lgq(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    u16 n = vsprintf_ed(buf, fmt, ap);
    va_end(ap);
    if (n > sizeof (buf)) n = sizeof (buf);
    if (log_len + n > sizeof (logbuf)) {        // keep the newest half
        u32 keep = sizeof (logbuf) / 2;
        for (u32 i = 0; i < keep; i++) logbuf[i] = logbuf[log_len - keep + i];
        log_len = keep;
    }
    for (u16 i = 0; i < n; i++) logbuf[log_len++] = buf[i];
}

u8 log_flush(void) { return 0; }              // stage 1 writes nothing to the card
void log_trim(void) {}

const char *log_text(u32 *len) { *len = log_len; return logbuf; }

void launch_init(void) {}
u8 launch_igm_available(void) { return 0; }   // the X5 Mini has no in-game menu

u8 launch_app(const char *path, const char *file_name, const char *ext, LaunchStatus status, const char **err) {
    (void) path; (void) file_name; (void) ext; (void) status;
    *err = "Not on the Mini yet";
    return 1;
}

//****************************************************************************** launch

#define ROM ((vu16 *) 0x08000000)
#define BANK (16 * 1024 * 1024)
#define MAX_ROM (32 * 1024 * 1024)

// Games with a real-time clock (game code, first 3 letters).
static const char *const rtc_games[] = {
    "AXV", "AXP", "BPE",              // Pokemon Ruby, Sapphire, Emerald
    "U3I", "U32", "U33",              // Boktai 1-3
    "BR4", "BKA", "BKM",              // Rockman EXE 4.5, Sennen Kazoku, Koro Koro Puzzle
    0
};

static u8 is_rtc_game(const u8 *code) {
    for (int i = 0; rtc_games[i]; i++)
        if (code[0] == rtc_games[i][0] && code[1] == rtc_games[i][1] && code[2] == rtc_games[i][2]) return 1;
    return 0;
}

static u16 sum16(const vu16 *p, u32 n) {
    u16 s = 0;
    for (u32 i = 0; i < n; i++) s += p[i];
    return s;
}

static u32 tmp[128];                          // one sector, word aligned

// The file's 512 bytes at off, against what the cart now holds at at.
static u8 check(FIL *f, u32 off, const vu16 *at) {
    UINT br;
    if (f_lseek(f, off) || f_read(f, tmp, 512, &br) || br == 0) return 2;
    const u16 *t = (const u16 *) tmp;
    for (u32 i = 0; i < br / 2; i++) if (t[i] != at[i]) return 1;
    return 0;
}

const char *mini_err;

static u8 load(FIL *f, u32 size, LaunchStatus status) {
    char msg[24];
    for (u32 base = 0; base < size; base += BANK) {
        u32 len = size - base > BANK ? BANK : size - base;
        ed_set_rom_bank(base ? 1 : 0);
        if (f_lseek(f, base)) return 1;
        for (u32 done = 0; done < len;) {
            u32 n = len - done > 0x80000 ? 0x80000 : len - done;
            u32 whole = n & ~511;
            UINT br;
            if (whole) {
                FRESULT r = f_read(f, (void *) (0x08000000 + done), whole, &br);
                if (r || br != whole) { lgq("read at %X: %u (%X)\n", base + done, r, br); return 2; }
            }
            if (n & 511) {                      // the last part sector: through memory, 16-bit writes
                FRESULT r = f_read(f, tmp, n & 511, &br);
                if (r) return 2;
                vu16 *d = ROM + (done + whole) / 2;
                const u16 *t = (const u16 *) tmp;
                for (u32 i = 0; i < ((n & 511) + 1) / 2; i++) d[i] = t[i];
            }
            done += n;
            u32 pct = (base + done) / (size / 100 + 1), k = 0;
            for (const char *p = "Loading "; *p; p++) msg[k++] = *p;
            if (pct >= 100) msg[k++] = '1';
            if (pct >= 10) msg[k++] = '0' + pct / 10 % 10;
            msg[k++] = '0' + pct % 10;
            msg[k++] = '%';
            msg[k] = 0;
            status(msg);
        }
    }
    return 0;
}

u8 launch_game(const char *path, const char *file_name, LaunchStatus status, const char **err) {
    FIL *f = plat_mini_file();
    *err = "Can't open the game";
    lgq("---- %s\n", file_name);
    status("Opening");
    if (plat_open(path, 0)) return 1;
    u32 size = f_size(f);
    u8 head[0xC0];
    UINT br;
    if (f_read(f, (u32 *) tmp, 0xC0, &br) || br != 0xC0) { plat_close(); return 2; }
    for (int i = 0; i < 0xC0; i++) head[i] = ((u8 *) tmp)[i];
    char title[13], code[5];
    for (int i = 0; i < 12; i++) title[i] = head[0xA0 + i] >= 32 && head[0xA0 + i] < 127 ? head[0xA0 + i] : '.';
    for (int i = 0; i < 4; i++) code[i] = head[0xAC + i] >= 32 && head[0xAC + i] < 127 ? head[0xAC + i] : '.';
    title[12] = code[4] = 0;
    lgq("size %X, title %s, code %s\n", size, title, code);
    *err = "Game over 32 MB";
    if (size > MAX_ROM || size < 0xC0) { plat_close(); return 3; }

    *err = "Load failed";
    u8 r = load(f, size, status);
    lgq("load %u\n", r);
    if (r) { ed_set_rom_bank(0); plat_close(); return 4; }

    // check: start, middle and end of each 16 MB part, through each mapping
    ed_set_rom_bank(0);
    u8 c0 = check(f, 0, ROM);
    u32 last = (size < BANK ? size : BANK) - 512;
    u8 c1 = check(f, last & ~1, ROM + (last & ~1) / 2);
    lgq("check bank0 start %u end %u, sum %04X\n", c0, c1, sum16(ROM, 256));
    if (size > BANK) {
        u8 hi_direct = check(f, BANK, (const vu16 *) 0x09000000);
        ed_set_rom_bank(1);
        u8 hi_bank = check(f, BANK, ROM);
        ed_set_rom_bank(0);
        lgq("check 2nd 16MB: at 0x09000000 %u, bank 1 %u\n", hi_direct, hi_bank);
    }
    plat_close();
    if (c0 || c1) { *err = "Load check failed"; return 5; }

    u8 rtc = is_rtc_game(head + 0xAC);
    lgq("rtc %u, save off (stage 1)\n", rtc);
    status("Starting");
    ed_set_save_type(0);                       // stage 1: no save chip
    ed_set_rom_mask(size);
    if (rtc) ed_rtc_on(); else ed_rtc_off();
    ed_set_rom_bank(0);
    diskPowerDown();
    ed_lock_regs();
    boot_manual(0);                            // clears RAM and IO like a cold start, then 0x08000000
    return 6;                                  // not reached
}
