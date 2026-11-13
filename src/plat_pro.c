// EverFront platform layer for the EverDrive GBA Pro: its MCU does the
// SD card's file system; these wrap krikzz's driver (ed/everdrive.c).
#include "plat.h"
#include "ed/appmain.h"

static u16 dir_size, dir_i;

void plat_init(void) {
    ed_set_cmode(CMOD_MENU);
    ed_fifo_flush();
}

const char *plat_name(void) { return "Pro"; }

u8 plat_dir_open(const char *path) {
    u8 r = ed_fs_dir_load((u8 *) path, DIR_OPT_SORTED | DIR_OPT_HIDESYS);
    if (r) return r;
    ed_fs_dir_get_size(&dir_size);
    dir_i = 0;
    return 0;
}

u8 plat_dir_next(char *name, u32 max, u8 *is_dir) {
    FileInfo fi;
    if (dir_i >= dir_size) return 1;
    fi.file_name = (u8 *) name;
    ed_fs_dir_get_recs(dir_i++, 1, max > 255 ? 255 : max - 1);
    u8 r = ed_fs_rx_next_rec(&fi);
    if (r) return r;
    *is_dir = fi.is_dir;
    return 0;
}

u16 plat_dir_pos(void) { return dir_i - 1; }

u8 plat_dir_name(u16 idx, char *name, u32 max) {
    u8 d;
    u16 keep = dir_i;
    dir_i = idx;
    u8 r = plat_dir_next(name, max, &d);
    dir_i = keep;
    return r;
}

u8 plat_open(const char *path, u8 write) {
    return ed_fs_file_open((u8 *) path, write ? FA_WRITE | FA_CREATE_ALWAYS : FA_READ);
}

u8 plat_read(void *dst, u32 len) { return ed_fs_file_read(dst, len); }
u8 plat_seek(u32 off) { return ed_fs_file_set_ptr(off); }
u32 plat_size(void) { return ed_fs_file_available(); }
u8 plat_write(const void *src, u32 len) { return ed_fs_file_write((void *) src, len); }
void plat_close(void) { ed_fs_file_close(); }

u8 plat_time(u8 t[5]) {
    RtcTime r;
    ed_rtc_get(&r);
    t[0] = r.yar; t[1] = r.mon; t[2] = r.dom; t[3] = r.hur; t[4] = r.min;
    return 0;
}
