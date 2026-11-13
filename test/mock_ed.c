// Fake EverDrive for emulator tests of the shelf (never on a cart).
// The label pack is appended to the emulator image at 0x08100000.
#include "../src/ed/appmain.h"
#include "mock_names.h"
#define NN (sizeof (mock_names) / sizeof (mock_names[0]))
// Files appended to the emulator image at 0x08100000 by build_mock.sh:
// u32 count, then count x {char name[96], u32 offset (from 0x08100000), u32 size}.
#define FILES ((const u8 *) 0x08100000)
typedef struct { char name[96]; u32 off, size; } MockFile;

static int seq(const u8 *a, const char *b) { while (*a && *a == (u8) *b) { a++; b++; } return *a == (u8) *b; }
static u32 slen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }
static int open_kind;            // 1 file from the table, 2 rom, 3 log, 5 config
static const MockFile *open_file;
static u32 fptr;
static char config[128];          // everfront/config.txt lives in RAM
static u32 config_len;
static u8 config_writing;

static u32 nfiles(void) { return *(const u32 *) FILES; }
static const MockFile *file_at(u32 i) { return (const MockFile *) (FILES + 4 + i * sizeof (MockFile)); }

static int prefixed(const char *name, const u8 *dir) {    // name is dir/<something>
    u32 n = slen((const char *) dir);
    for (u32 k = 0; k < n; k++) if ((u8) name[k] != dir[k]) return 0;
    return name[n] == '/';
}

void ed_set_cmode(u16 m) { (void) m; }
void ed_fifo_flush() {}
void ed_sys_inf(SysInfoIO *inf) { u8 *p = (u8 *) inf; for (u32 i = 0; i < sizeof (*inf); i++) p[i] = 0; }
void ed_fci_rd(u32 a, void *d, u32 l) { (void) a; u8 *p = d; for (u32 i = 0; i < l; i++) p[i] = 0; }
void ed_fci_wr(u32 a, void *s, u32 l) { (void) a; (void) s; (void) l; }
u8 ed_fs_dir_make(u8 *p) { (void) p; return 0; }
static int dir_kind;             // 0 games, 1 table directory
static const u8 *dir_name;
static u16 dir_list[32], dir_n;
u8 ed_fs_dir_load(u8 *p, u8 o) {
    (void) o;
    if (seq(p, "Game Boy Advance")) { dir_kind = 0; return 0; }
    dir_n = 0;
    for (u32 i = 0; i < nfiles() && dir_n < 32; i++) if (prefixed(file_at(i)->name, p)) dir_list[dir_n++] = i;
    if (!dir_n) return 0x10;
    dir_kind = 1;
    dir_name = p;
    return 0;
}
void ed_fs_dir_get_size(u16 *s) { *s = dir_kind ? dir_n : NN; }
static u16 rec_i;
void ed_fs_dir_get_recs(u16 s, u16 a, u16 m) { (void) a; (void) m; rec_i = s; }
u8 ed_fs_rx_next_rec(FileInfo *fi) {
    const char *n = dir_kind ? file_at(dir_list[rec_i])->name + slen((const char *) dir_name) + 1 : mock_names[rec_i];
    u32 k = 0;
    for (; n[k]; k++) fi->file_name[k] = n[k];
    fi->file_name[k] = 0;
    fi->is_dir = 0;
    fi->size = 0x800000;
    return 0;
}
u8 ed_fs_file_open(u8 *p, u8 m) {
    fptr = 0;
    if (*p == '/') p++;
    static u8 seeded;                 // a config.txt in the table starts the RAM one
    if (!seeded) {
        seeded = 1;
        for (u32 i = 0; i < nfiles(); i++)
            if (seq((const u8 *) file_at(i)->name, "everfront/config.txt"))
                for (u32 k = 0; k < file_at(i)->size && config_len < sizeof (config); k++)
                    config[config_len++] = FILES[file_at(i)->off + k];
    }
    u32 n = slen((char *) p);
    if (seq(p, "everfront/config.txt")) {
        if (m & FA_WRITE) { open_kind = 5; config_writing = 1; config_len = 0; return 0; }
        if (!config_len) return 0x04;
        open_kind = 5; config_writing = 0;
        return 0;
    }
    for (u32 i = 0; i < nfiles(); i++)
        if (seq(p, file_at(i)->name)) { open_kind = 1; open_file = file_at(i); return 0; }
    if (n > 4 && p[n - 1] == 'a') { open_kind = 2; return 0; }
    if (seq(p, "everfront/log.txt")) { open_kind = 3; return 0; }
    return 0x04;
}
u8 ed_fs_file_close() { return 0; }
u8 ed_fs_file_set_ptr(u32 a) { fptr = a; return 0; }
u8 ed_fs_file_read(void *d, u32 l) {
    u8 *p = d;
    for (u32 i = 0; i < l; i++)
        p[i] = open_kind == 1 ? FILES[open_file->off + fptr + i] : open_kind == 5 ? (fptr + i < config_len ? config[fptr + i] : 0) : 0;
    if (open_kind == 2 && fptr == 0xAC && l == 4) { p[0] = 'X'; p[1] = 'X'; p[2] = 'X'; p[3] = 'X'; }
    fptr += l;
    return 0;
}
u8 ed_fs_file_write(void *s, u32 l) {
    if (open_kind == 5 && config_writing)
        for (u32 i = 0; i < l && config_len < sizeof (config); i++) config[config_len++] = ((u8 *) s)[i];
    return 0;
}
u32 ed_fs_file_available() { return open_kind == 5 ? config_len : open_kind == 1 ? open_file->size : 0x800000; }
u8 ed_fs_file_read_fci(u32 a, u32 l) { (void) a; (void) l; return 0; }
u8 ed_fs_file_info(u8 *p, FileInfo *fi) { (void) p; (void) fi; return 0x04; }
u8 ed_fs_file_test(u8 *p) { (void) p; return 0x04; }
u8 ed_fs_file_copy(u8 *s, u8 *d, u8 m) { (void) s; (void) d; (void) m; return 0; }
u8 ed_fs_file_seek_pat(u8 *pat, u8 n, u32 fs, u32 *a) { (void) pat; (void) n; (void) fs; *a = 0xFFFFFFFF; return 0; }
u8 ed_dev_set_path(u8 *p, DevPathType t) { (void) p; (void) t; return 0; }
u8 ed_dev_set_cfg(DevCfg *c) { (void) c; return 0; }
u8 ed_dev_start() { return 0; }
void ed_bootmod_set(u16 m) { (void) m; }
void ed_fci_crc(u32 a, u32 l, u32 *c) { (void) a; (void) l; *c = 0; }
u32 ed_get_ticks() { static u32 t; return t += 5; }
void ed_usb_wr(void *d, u16 l) { (void) d; (void) l; }

void ed_rtc_get(RtcTime *t) {
    t->yar = 0x26; t->mon = 0x10; t->dom = 0x29; t->hur = 0x11; t->min = 0x00; t->sec = 0;
}
