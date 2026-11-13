// EverFront platform layer for the EverDrive GBA X5 Mini. The cart only
// gives raw SD sectors (krikzz's edmini driver), so FatFs (read-only) does
// the file system here. Stage 1: nothing is ever written to the card.
#include "../plat.h"
#include "../launch.h"
#include "../fatfs/ff.h"
#include "../edmini/everdrive.h"

static FATFS fs __attribute__((aligned(4)));
static FIL fil __attribute__((aligned(4)));
static DIR dir;
static FILINFO fno;
static u8 mounted;

void plat_init(void) {
    ed_init();
    FRESULT r = f_mount(&fs, "", 1);
    mounted = r == FR_OK;
    lgq("EverFront (Mini) fpga %04X, mount %u, fat type %u\n", ed_get_fpga_ver(), r, mounted ? fs.fs_type : 0);
}

const char *plat_name(void) { return "Mini"; }

u8 plat_dir_open(const char *path) {
    if (!mounted) return 0x40;
    FRESULT r = f_opendir(&dir, path);
    if (r) lgq("opendir '%s': %u\n", path, r);
    return r;
}

u8 plat_dir_next(char *name, u32 max, u8 *is_dir) {
    for (;;) {
        FRESULT r = f_readdir(&dir, &fno);
        if (r) { lgq("readdir: %u\n", r); return r + 1; }
        if (!fno.fname[0]) { f_closedir(&dir); return 1; }
        if (fno.fattrib & (AM_HID | AM_SYS)) continue;
        u32 k = 0;
        for (; fno.fname[k] && k < max - 1; k++) name[k] = fno.fname[k];
        name[k] = 0;
        *is_dir = (fno.fattrib & AM_DIR) != 0;
        return 0;
    }
}

u8 plat_open(const char *path, u8 write) {
    if (!mounted) return 0x40;
    if (write) return 0x41;                 // stage 1: read-only
    return f_open(&fil, path, FA_READ);
}

u8 plat_read(void *dst, u32 len) {
    UINT br = 0;
    FRESULT r = f_read(&fil, dst, len, &br);
    if (r) return r;
    for (u8 *p = (u8 *) dst + br; br < len; br++) *p++ = 0;   // past the end: zeros
    return 0;
}

u8 plat_seek(u32 off) { return f_lseek(&fil, off); }
u32 plat_size(void) { return f_size(&fil); }
u8 plat_write(const void *src, u32 len) { (void) src; (void) len; return 0x41; }
void plat_close(void) { f_close(&fil); }

// For the launcher: the open file itself.
FIL *plat_mini_file(void) { return &fil; }

//****************************************************************************** clock

// The cart's RTC answers like a real cartridge's (Seiko S-3511 on the GPIO
// port at 0x080000C4) once ed_rtc_on() maps it in. Bits: 0 SCK, 1 SIO, 2 CS.
#define RTC_DATA (*(vu16 *) 0x080000C4)
#define RTC_DIR  (*(vu16 *) 0x080000C6)
#define RTC_CTRL (*(vu16 *) 0x080000C8)

static void rtc_cmd(u8 v) {
    for (int b = 7; b >= 0; b--) {             // command: msb first
        u16 bit = ((v >> b) & 1) << 1;
        RTC_DATA = bit | 4;
        RTC_DATA = bit | 4;
        RTC_DATA = bit | 4;
        RTC_DATA = bit | 5;
    }
}

static u8 rtc_byte(void) {
    u8 v = 0;
    for (int b = 0; b < 8; b++) {              // data: lsb first
        RTC_DATA = 4;
        RTC_DATA = 4;
        RTC_DATA = 4;
        RTC_DATA = 4;
        RTC_DATA = 4;
        RTC_DATA = 5;
        v |= ((RTC_DATA >> 1) & 1) << b;
    }
    return v;
}

u8 plat_time(u8 tm[5]) {
    u8 t[7];
    ed_rtc_on();
    RTC_CTRL = 1;                              // GPIO readable
    RTC_DIR = 7;
    RTC_DATA = 1;
    RTC_DATA = 5;
    rtc_cmd(0x65);                             // read date and time
    RTC_DIR = 5;                               // SIO in
    for (int k = 0; k < 7; k++) t[k] = rtc_byte();
    RTC_DIR = 7;
    RTC_DATA = 1;
    RTC_CTRL = 0;
    tm[0] = t[0]; tm[1] = t[1] & 0x1F; tm[2] = t[2] & 0x3F; tm[3] = t[4] & 0x3F; tm[4] = t[5] & 0x7F;
    return 0;
}
