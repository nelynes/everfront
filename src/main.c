// EverFront: a themeable game shelf (front end) for the EverDrive GBA Pro.
//
// The look comes from a theme file on the SD card (everfront/themes/,
// built by tools/make_theme.py): palette, tiles, the background and bar
// maps, the cart shell and the layout numbers. The Slot theme is Slot's own
// art at 1/3 scale (Slot draws at 720x480, the GBA has 240x160):
// wood background, a 120x71 GBA cart in the middle with the neighbours at
// 0.78 and half off each edge, the game's name above Slot's slot bar, the
// clock on the bar. The shelf moves like Slot's: a critically damped spring
// (omega 16), every cart scaled by its distance from the centre, wrapping
// round at the ends. A drops the cart into the slot the way Slot does: it
// shrinks to the seated size on its way to the lip, then sinks in.
//
// Screen (mode 0):
//   BG3  wood + back of the slot bar        4bpp, prio 3, map 0x6000
//   BG2  carts on the shelf                 8bpp, prio 2, 64x32 map 0x7000
//   BG1  the cart going into the slot       8bpp, prio 1, map 0xF800
//   BG0  front of the bar, text             4bpp, prio 0, map 0x6800
// Carts: each cart near the centre is composed at full size (template +
// label) in EWRAM, then drawn at its current scale into one of four tile
// areas (two 15x9, two 13x7). Drawing goes to a staging buffer; the next
// vblank copies it to VRAM together with the map, so nothing tears.

#include "launch.h"
#include "plat.h"
#include "version.h"

#define REG16(a) (*(vu16 *) (a))
#define REG32(a) (*(vu32 *) (a))
#define DISPCNT  REG16(0x04000000)
#define VCOUNT   REG16(0x04000006)
#define BGCNT(n) REG16(0x04000008 + 2 * (n))
#define BGHOFS(n) REG16(0x04000010 + 4 * (n))
#define BGVOFS(n) REG16(0x04000012 + 4 * (n))
#define KEYINPUT REG16(0x04000130)
#define DMA3SAD  REG32(0x040000D4)
#define DMA3DAD  REG32(0x040000D8)
#define DMA3CNT  REG32(0x040000DC)

#define PAL      ((vu16 *) 0x05000000)
#define VRAM     0x06000000
#define MAP_BG3  ((vu16 *) (VRAM + 0x6000))
#define MAP_BG0  ((vu16 *) (VRAM + 0x6800))
#define MAP_BG2  (VRAM + 0x7000)
#define CHAR8    (VRAM + 0x8000)
#define MAP_BG1  ((vu16 *) (VRAM + 0xF800))

#define K_A 0x0001
#define K_B 0x0002
#define K_RIGHT 0x0010
#define K_LEFT 0x0020
#define K_R 0x0100
#define K_L 0x0200

#ifndef s16
#define s16 short
#endif
#define BAND_ROWS 12              // BG2 rows 0..11 hold the carts; the band's bottom is the foot
#define AREA_H (BAND_ROWS * 8)
#define AREA_TILES (15 * 9)       // a cart's drawing area: at most 135 tiles (120x72, 88x96...)
#define CART_MAX_W 120
#define CART_MAX_H AREA_H
#define CART_PX (AREA_TILES * 64) // pixels in a cart picture (the GBA cart's 120x72)
#define THEMES_DIR APP_DIR "/themes"
#define CONFIG_PATH APP_DIR "/config.txt"
#define K_SELECT 0x0004
#define K_START 0x0008

extern const u8 font[];

#define IWRAM_DATA __attribute__((section(".ibss"), aligned(4)))
#define IWRAM_CODE __attribute__((section(".iwram"), target("arm"), noinline))

//****************************************************************************** helpers

static u32 slen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }
static char low(char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

// The shelf's system: its rom extension ("gba", "gb", "gbc", "nes").
static const char *cur_ext = "gba";

// Length of ".ext" at the end of s if it is the shelf's extension, else 0.
static u32 ext_is(const char *s, const char *ext) {
    u32 n = slen(s), e = slen(ext);
    if (n <= e + 1 || s[n - e - 1] != '.') return 0;
    for (u32 k = 0; k < e; k++) if (low(s[n - e + k]) != ext[k]) return 0;
    return e + 1;
}

// The extension's length with its dot if s is a game of the shelf's system,
// else 0. The GBC shelf takes .gb games too (Goomba runs both).
static u32 ext_len(const char *s) {
    u32 e = ext_is(s, cur_ext);
    if (!e && cur_ext[0] == 'g' && cur_ext[1] == 'b' && cur_ext[2] == 'c' && !cur_ext[3]) e = ext_is(s, "gb");
    return e;
}

static u32 crc32_str(const char *s, u32 n, int lower) {
    u32 crc = 0xFFFFFFFF;
    for (u32 i = 0; i < n; i++) {
        crc ^= (u8) (lower ? low(s[i]) : s[i]);
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    return ~crc;
}

static void vsync(void) {
    while (VCOUNT >= 160);
    while (VCOUNT < 160);
}

static void dma32(u32 dst, const void *src, u32 len) {
    DMA3SAD = (u32) src;
    DMA3DAD = dst;
    DMA3CNT = (len / 4) | 0x84000000;
}

static int seq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void scopy(char *d, const char *s, u32 max) { u32 k = 0; for (; s[k] && k < max - 1; k++) d[k] = s[k]; d[k] = 0; }

static int imod(int a, int n) { int r = a % n; return r < 0 ? r + n : r; }

//****************************************************************************** theme

// The layout numbers, in the file's order (tools/make_theme.py PARAMS).
typedef struct {
    s16 cart_w, cart_h, label_x, label_y, label_w, label_h, label_cols;
    s16 foot_y, pitch, side_permille, omega_x16;
    s16 name_row, name_bank, name_colour, clock_row, clock_col, clock_bank, clock_colour;
    s16 seat_w, seat_h, lip_x16, seated_foot_x16, seat_frames, eject_frames;
    s16 lift, side_dim;               // selected cart raised by lift px; others darkened (BLDY 0-16)
    s16 sys_off_lo, sys_off_hi, sys_count;   // other systems' carts (theme file offset, count)
    s16 sys_row, sys_col;             // the shelf's system name over the bar (row -1: none)
} Theme;

// Another system's cart in the theme file (make_theme.py: 64 bytes each).
typedef struct {
    char ext[4], labels[24];
    u32 cart_off, pal_off;
    s16 w, h, lx, ly, lw, lh, sw, sh;
    s16 pitch, foot_y, name_row, pad;   // 0: the theme's own
    u32 back_off;                     // the shelf's own background map (BG3), 0: the theme's
} SysCart;
#define MAX_SYSCARTS 6
_Static_assert(sizeof (SysCart) == 64, "SysCart");
#define THEME_PARAMS_MIN 24           // files from before lift/side_dim
#define THEME_PARAMS (sizeof (Theme) / 2)

static Theme th, th_base;             // th: the shelf's system's cart; th_base: the theme's GBA cart
static char th_name[24], th_labels[24], th_base_labels[24];
static SysCart th_sys[MAX_SYSCARTS];
static int th_nsys;
static u32 th_base_cart_off;
static u32 th_base_back_off, th_back_shown;   // BG3 map in the theme file: the theme's, and on screen
static u16 th_base_plastic[16];
static char th_path[64];
static u8 th_cart[CART_PX] __attribute__((aligned(4)));
static u16 th_map_front[600];
static u32 th_tiles4;                 // 4bpp tiles in VRAM; text tiles follow them
static u8 io_buf[4096] __attribute__((aligned(4)));

// Cart tile areas, worked out from the theme: two big ones (the pitch wide,
// tall enough for the cart and its lift), two for side-size carts.
#define AREAS 4
static u8 area_tw[AREAS], area_th[AREAS];
static u16 area_tile[AREAS];
static int cols;                      // map columns per shelf position (pitch / 8)

#define CART_W (th.cart_w)
#define CART_H (th.cart_h)
#define LABEL_X (th.label_x)
#define LABEL_Y (th.label_y)
#define LABEL_W (th.label_w)
#define LABEL_H (th.label_h)
#define LABEL_COLS (th.label_cols)

static int ceil8(int v) { return (v + 7) >> 3; }

static const char *theme_check(void) {
    if (th.pitch <= 0 || th.pitch > 120 || (th.pitch & 7)) return "Theme: pitch";
    if (th.cart_w <= 0 || th.cart_w > th.pitch || th.cart_w > CART_MAX_W || (th.cart_w & 3) || th.cart_h <= 0 ||
            th.lift < 0 || th.cart_h + th.lift > CART_MAX_H || th.cart_w * th.cart_h > CART_PX)
        return "Theme: cart size";
    if (th.side_permille <= 0 || th.side_permille > 1000 || th.side_dim < 0 || th.side_dim > 16) return "Theme: sides";
    if (th.label_x < 0 || th.label_y < 0 || th.label_x + th.label_w > th.cart_w || th.label_y + th.label_h > th.cart_h ||
            th.label_cols < 2 || th.label_cols * 4 > 144)
        return "Theme: label";
    if (th.foot_y < th.cart_h + th.lift || th.foot_y > 160) return "Theme: foot";
    if (th.seat_w <= 0 || th.seat_w > th.cart_w || th.seat_h <= 0 || th.seat_h > th.cart_h) return "Theme: seated size";
    if (th.seat_frames <= 0 || th.eject_frames <= 0) return "Theme: frames";
    if (th.name_row < 0 || th.name_row > 19 || th.clock_row < 0 || th.clock_row > 19 || th.clock_col < 0 || th.clock_col > 24)
        return "Theme: text rows";
    // the areas: big = pitch x (cart + lift); small = side size, centred
    // in the pitch on whole tiles (same odd/even as the pitch's columns)
    cols = th.pitch / 8;
    int sw = (th.cart_w * th.side_permille + 500) / 1000, sh = (th.cart_h * th.side_permille + 500) / 1000;
    int stw = ceil8(sw);
    stw += (cols - stw) & 1;
    int btw = ceil8(th.cart_w);                 // the big areas: the cart's own width, not the pitch
    btw += (cols - btw) & 1;
    area_tw[0] = area_tw[1] = btw;
    area_th[0] = area_th[1] = ceil8(th.cart_h + th.lift);
    area_tw[2] = area_tw[3] = stw;
    area_th[2] = area_th[3] = ceil8(sh);
    u32 t = 1;
    for (int a = 0; a < AREAS; a++) {
        if (area_tw[a] * area_th[a] > AREA_TILES) return "Theme: cart too big";
        area_tile[a] = t;
        t += area_tw[a] * area_th[a];
    }
    if (t > (0xF800 - 0x8000) / 64) return "Theme: cart areas too big";
    return 0;
}

// Reads `size` bytes at `off` of the open file to dst, through io_buf
// (16/32-bit copies: VRAM and palette take no byte writes).
static u8 read_to(u32 dst, u32 off, u32 size) {
    if (plat_seek(off)) return 1;
    while (size) {
        u32 n = size < sizeof (io_buf) ? size : sizeof (io_buf);
        if (plat_read(io_buf, (n + 3) & ~3)) return 1;
        dma32(dst, io_buf, (n + 3) & ~3);
        dst += n;
        size -= n;
    }
    return 0;
}

// Loads the theme into the palette, VRAM and memory. 0, or what is wrong.
static const char *theme_load(const char *path) {
    u32 h[24];
    if (plat_open(path, 0)) return "Theme file missing";
    const char *err = 0;
    if (plat_read(h, 96) || h[0] != 0x48544645 || (h[1] & 0xFFFF) != 1 || (h[1] >> 16) < THEME_PARAMS_MIN ||
            (h[1] >> 16) > 64) {
        err = "Theme file: wrong format";
        goto done;
    }
    for (int k = 0; k < 24; k++) { th_name[k] = ((u8 *) h)[8 + k]; th_labels[k] = ((u8 *) h)[32 + k]; }
    th_name[23] = th_labels[23] = 0;
    u32 sec[5][2];
    for (int k = 0; k < 5; k++) { sec[k][0] = h[14 + 2 * k]; sec[k][1] = h[15 + 2 * k]; }
    u32 np = h[1] >> 16;
    if (plat_read(io_buf, (np * 2 + 3) & ~3)) { err = "Theme file: read"; goto done; }
    for (u32 k = 0; k < THEME_PARAMS; k++) ((s16 *) &th)[k] = k < np ? ((s16 *) io_buf)[k] : 0;
    if (np <= (u32) ((s16 *) &th.sys_row - (s16 *) &th)) th.sys_row = -1;   // older files: no system name
    if ((err = theme_check())) goto done;
    if (sec[0][1] > 112 * 2 || sec[1][1] % 32 || sec[1][1] / 32 + 54 > 0x6000 / 32 ||
            sec[2][1] != 1200 || sec[3][1] != 1200 || sec[4][1] != (u32) (th.cart_w * th.cart_h)) {
        err = "Theme file: sections";
        goto done;
    }
    th_tiles4 = sec[1][1] / 32;
    th_nsys = 0;
    if (th.sys_count > 0) {
        u32 so = (u16) th.sys_off_lo | ((u32) (u16) th.sys_off_hi << 16);
        th_nsys = th.sys_count < MAX_SYSCARTS ? th.sys_count : MAX_SYSCARTS;
        if (read_to((u32) th_sys, so, th_nsys * sizeof (SysCart))) { err = "Theme file: read"; goto done; }
        for (int k = 0; k < th_nsys; k++) { th_sys[k].ext[3] = 0; th_sys[k].labels[23] = 0; }
    }
    th_base = th;
    scopy(th_base_labels, th_labels, sizeof (th_base_labels));
    th_base_cart_off = sec[4][0];
    th_base_back_off = sec[2][0];
    scopy(th_path, path, sizeof (th_path));
    if (read_to(0x05000000, sec[0][0], sec[0][1]) ||
            read_to(VRAM, sec[1][0], sec[1][1]) ||
            read_to((u32) th_map_front, sec[3][0], 1200) ||
            read_to((u32) th_cart, sec[4][0], sec[4][1])) {
        err = "Theme file: read";
        goto done;
    }
    for (int k = 0; k < 16; k++) th_base_plastic[k] = PAL[96 + k];
    // BG3 map: 30x20 into the 32x32 map
    if (plat_seek(sec[2][0]) || plat_read(io_buf, 1200)) { err = "Theme file: read"; goto done; }
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++)
            MAP_BG3[y * 32 + x] = y < 20 && x < 30 ? ((u16 *) io_buf)[y * 30 + x] : 0;
    th_back_shown = sec[2][0];
done:
    plat_close();
    lgq("theme '%s': %s\n", th_name, err ? err : "ok");
    return err;
}

static int src_w, src_h;              // the composed carts' size (set with the cart)

// The cart for the shelf's system: the theme's own picture for it, or the
// GBA cart with plain labels when the theme has none. 0, or what is wrong.
static const char *cart_select(void) {
    const SysCart *sc = 0;
    int old_name_row = th.name_row;
    if (!seq(cur_ext, "gba"))
        for (int k = 0; k < th_nsys; k++) if (seq(th_sys[k].ext, cur_ext)) sc = &th_sys[k];
    th = th_base;
    if (sc) {
        th.cart_w = sc->w; th.cart_h = sc->h;
        th.label_x = sc->lx; th.label_y = sc->ly; th.label_w = sc->lw; th.label_h = sc->lh;
        th.seat_w = sc->sw; th.seat_h = sc->sh;
        if (sc->pitch > 0) th.pitch = sc->pitch;
        if (sc->foot_y > 0) th.foot_y = sc->foot_y;
        if (sc->name_row > 0) th.name_row = sc->name_row;
    }
    const char *err = theme_check();
    if (err) { th = th_base; theme_check(); sc = 0; }
    scopy(th_labels, sc ? sc->labels : seq(cur_ext, "gba") ? th_base_labels : "", sizeof (th_labels));
    if (plat_open(th_path, 0)) return "Theme file missing";
    u8 r = read_to((u32) th_cart, sc ? sc->cart_off : th_base_cart_off, th.cart_w * th.cart_h);
    if (sc && !r) r = read_to(0x05000000 + 96 * 2, sc->pal_off, 32);
    // the background: the system's own (a lower shelf for taller carts) or the theme's
    u32 bo = sc && sc->back_off ? sc->back_off : th_base_back_off;
    if (!r && bo != th_back_shown) {
        if (plat_seek(bo) || plat_read(io_buf, 1200)) r = 1;
        else {
            for (int y = 0; y < 20; y++)
                for (int x = 0; x < 30; x++) MAP_BG3[y * 32 + x] = ((u16 *) io_buf)[y * 30 + x];
            th_back_shown = bo;
        }
    }
    plat_close();
    for (int x = 0; x < 30; x++)                    // the name's old row back to the theme's
        if (old_name_row >= 0 && old_name_row < 20)
            MAP_BG0[old_name_row * 32 + x] = th_map_front[old_name_row * 30 + x];
    BGVOFS(2) = (u16) (AREA_H - th.foot_y) & 511;
    if (!sc) for (int k = 0; k < 16; k++) PAL[96 + k] = th_base_plastic[k];
    src_w = th.cart_w;
    src_h = th.cart_h;
    lgq("cart for %s: %s %dx%d\n", cur_ext, sc ? "theme's" : "GBA", th.cart_w, th.cart_h);
    return r ? "Theme file: read" : err;
}

//****************************************************************************** games

static int name_cmp(const char *a, const char *b) {
    while (*a && low(*a) == low(*b)) { a++; b++; }
    return (u8) low(*a) - (u8) low(*b);
}

#ifdef PLAT_MINI
// Mini: every name kept in a pool and sorted here (FatFs lists in disk order).
#define MAX_GAMES 600
#define POOL (32 * 1024)

static char pool[POOL];
static u16 game_off[MAX_GAMES];
static u32 lab_off[MAX_GAMES];        // label offset in the pack: ~0 unknown, 0 none
static u32 game_crc[MAX_GAMES];       // crc32 of the file name (favourites, recent)
static int game_count;
static char folder[64];

static const char *game_name(int i) { return pool + game_off[i] * 4; }

static u8 list_games(const char *dir) {
    u8 r, is_dir;
    char name[256];
    u32 used = 0;

    game_count = 0;
    r = plat_dir_open(dir);
    if (r) return r;
    while (game_count < MAX_GAMES && (r = plat_dir_next(name, sizeof (name), &is_dir)) == 0) {
        if (is_dir || name[0] == '.' || !ext_len(name)) continue;
        u32 n = slen(name) + 1;
        if (used + n > POOL) break;
        lab_off[game_count] = 0xFFFFFFFF;
        game_off[game_count++] = used / 4;
        for (u32 k = 0; k < n; k++) pool[used + k] = name[k];
        used = (used + n + 3) & ~3;
    }
    if (r > 1) return r;
    // sorted by name, ignoring case (insertion sort: few hundred names, once)
    for (int a = 1; a < game_count; a++) {
        u16 v = game_off[a];
        int b = a - 1;
        while (b >= 0 && name_cmp(pool + game_off[b] * 4, pool + v * 4) > 0) { game_off[b + 1] = game_off[b]; b--; }
        game_off[b + 1] = v;
    }
    for (int g = 0; g < game_count; g++) game_crc[g] = crc32_str(game_name(g), slen(game_name(g)), 0);
    return 0;
}

static void games_reopen(void) {}

#else
// Pro: the cart keeps the folder's sorted listing; we keep only where each
// .gba sits in it and fetch a name when it is needed (8 kept at a time).
// No name pool, so thousands of games fit.
#define MAX_GAMES 3000

static u16 game_idx[MAX_GAMES];       // index in the cart's directory listing
static u32 lab_off[MAX_GAMES];        // label offset in the pack: ~0 unknown, 0 none
static u32 game_crc[MAX_GAMES];       // crc32 of the file name (favourites, recent)
static int game_count;
static char folder[64];

static struct { int game; char name[256]; } name_cache[8];
static int name_next;

static const char *game_name(int i) {
    for (int k = 0; k < 8; k++) if (name_cache[k].game == i) return name_cache[k].name;
    int k = name_next;
    name_next = (name_next + 1) & 7;
    name_cache[k].game = i;
    if (plat_dir_name(game_idx[i], name_cache[k].name, sizeof (name_cache[k].name))) name_cache[k].name[0] = 0;
    return name_cache[k].name;
}

static u8 list_games(const char *dir) {
    u8 r, is_dir;
    char name[256];

    game_count = 0;
    for (int k = 0; k < 8; k++) name_cache[k].game = -1;
    r = plat_dir_open(dir);
    if (r) return r;
    while (game_count < MAX_GAMES && (r = plat_dir_next(name, sizeof (name), &is_dir)) == 0) {
        if (is_dir || name[0] == '.' || !ext_len(name)) continue;
        lab_off[game_count] = 0xFFFFFFFF;
        game_crc[game_count] = crc32_str(name, slen(name), 0);
        game_idx[game_count++] = plat_dir_pos();
    }
    if (r > 1) return r;
    return 0;
}

// After another folder was listed (the themes), list the games' folder again
// so the indices point at the right names.
static void games_reopen(void) {
    plat_dir_open(folder);
    for (int k = 0; k < 8; k++) name_cache[k].game = -1;
}
#endif

static void game_path(int i, char *out, int slash) {
    u32 o = 0;
    if (slash) out[o++] = '/';
    for (const char *s = folder; *s; s++) out[o++] = *s;
    if (folder[0]) out[o++] = '/';
    for (const char *s = game_name(i); *s && o < 1020; s++) out[o++] = *s;
    out[o] = 0;
}

// The name to show: the file name without its extension, without anything
// in (round) or [square] brackets, and with No-Intro's "X, The - Y" read
// as "The X - Y".
static void game_title(int i, char *out, u32 max) {
    const char *s = game_name(i);
    u32 n = slen(s) - ext_len(s);
    char t[256];
    u32 o = 0;
    int depth = 0;
    for (u32 k = 0; k < n && o < sizeof (t) - 1; k++) {
        char c = s[k];
        if (c == '(' || c == '[') { depth++; continue; }
        if ((c == ')' || c == ']') && depth) { depth--; continue; }
        if (depth) continue;
        if (c == ' ' && (o == 0 || t[o - 1] == ' ')) continue;
        if (c == '_') c = ' ';
        t[o++] = c;
    }
    while (o && t[o - 1] == ' ') o--;
    t[o] = 0;
    // "Legend of Zelda, The - The Minish Cap" -> "The Legend of Zelda - The Minish Cap"
    for (u32 k = 0; k + 5 <= o; k++) {
        if (t[k] == ',' && t[k + 1] == ' ' && t[k + 2] == 'T' && t[k + 3] == 'h' && t[k + 4] == 'e' &&
                (t[k + 5] == 0 || t[k + 5] == ' ')) {
            const char *pre = "The ";
            u32 q = 0;
            for (const char *p = pre; *p && q < max - 1; p++) out[q++] = *p;
            for (u32 j = 0; j < k && q < max - 1; j++) out[q++] = t[j];
            for (u32 j = k + 5; j < o && q < max - 1; j++) out[q++] = t[j];
            out[q] = 0;
            return;
        }
    }
    u32 q = 0;
    for (; q < o && q < max - 1; q++) out[q] = t[q];
    out[q] = 0;
}

//****************************************************************************** label pack

static char pack_path[48];            // everfront/<the theme's label pack>
#define PACK_PATH pack_path
#define PACK_MAX 4000
static u32 pack_keys[PACK_MAX * 2];
static u32 pack_count;

static void pack_open(void) {
    u32 h[4];
    pack_count = 0;
    if (!th_labels[0]) { lgq("no label pack for %s\n", cur_ext); return; }
    u32 o = 0;
    for (const char *p = APP_DIR "/"; *p; p++) pack_path[o++] = *p;
    for (const char *p = th_labels; *p && o < sizeof (pack_path) - 1; p++) pack_path[o++] = *p;
    pack_path[o] = 0;
    if (plat_open(PACK_PATH, 0)) {
        lgq("no label pack\n");
        return;
    }
    if (plat_read(h, 16) == 0 && h[0] == 0x424C4645 && (h[1] & 0xFFFF) == 3 &&
            (h[1] >> 16) == LABEL_W && (h[2] & 0xFFFF) == LABEL_H && (h[2] >> 16) == LABEL_COLS) {
        u32 n = h[3] < PACK_MAX ? h[3] : PACK_MAX;
        if (plat_read(pack_keys, n * 8) == 0) pack_count = n;
    }
    plat_close();
    lgq("label pack: %u keys\n", pack_count);
}

static u32 pack_find(u32 key) {
    int lo = 0, hi = (int) pack_count - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        u32 k = pack_keys[m * 2];
        if (k == key) return pack_keys[m * 2 + 1];
        if (k < key) lo = m + 1;
        else hi = m - 1;
    }
    return 0;
}

static u32 rom_code(int i) {
    char p[1025];
    u32 code = 0;
    game_path(i, p, 0);
    if (plat_open(p, 0)) return 0;
    if (plat_seek(0xAC) == 0) plat_read(&code, 4);
    plat_close();
    return code;
}

// Full name, then title only, then (reading the rom header) game code.
// Remembered per game, so the rom is opened at most once.
static u32 label_offset(int i) {
    if (lab_off[i] != 0xFFFFFFFF) return lab_off[i];
    u32 off = 0;
    if (pack_count) {
        const char *s = game_name(i);
        u32 n = slen(s) - ext_len(s);
        off = pack_find((crc32_str(s, n, 1) & 0x3FFFFFFF) | 0x80000000);
        if (!off) {
            u32 t = 0;
            while (t < n && !(s[t] == ' ' && (s[t + 1] == '(' || s[t + 1] == '['))) t++;
            off = pack_find((crc32_str(s, t, 1) & 0x3FFFFFFF) | 0xC0000000);
        }
        if (!off && seq(cur_ext, "gba")) {          // the header's game code: GBA roms only
            u32 code = rom_code(i);
            if (code) off = pack_find(code);
        }
    }
    lab_off[i] = off;
    return off;
}

//****************************************************************************** composed carts

// Four composed carts, full size, absolute palette indices. Slot k = pos & 3
// (shelf position, so four neighbours never share one), label colours at
// 112 + k * 36.
static u8 comp[4][CART_PX] __attribute__((aligned(4)));
static int comp_game[4] = {-1, -1, -1, -1};
static int composed;
static u8 label_buf[2 * 36 + CART_PX] __attribute__((aligned(4)));

static u16 mix(u32 h, int dark) {
    u32 r = 10 + (h & 15), g = 10 + ((h >> 4) & 15), b = 10 + ((h >> 8) & 15);
    if (dark) { r /= 4; g /= 4; b /= 4; }
    return r | (g << 5) | (b << 10);
}

static void plain_label(int i, u8 *dst, u8 base) {
    char t[96];
    game_title(i, t, sizeof (t));
    u32 h = crc32_str(t, slen(t), 0);
    PAL[base] = mix(h, 0);
    PAL[base + 1] = mix(h, 1);
    for (int y = 0; y < LABEL_H; y++)
        for (int x = 0; x < LABEL_W; x++)
            dst[(LABEL_Y + y) * CART_W + LABEL_X + x] = base;
    // as many 8-px characters and 10-px lines as the label holds (11 x 4 on
    // the GBA cart), broken at spaces where possible
    int cols = LABEL_W / 8, rows = (LABEL_H - 3) / 10;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    if (rows > 6) rows = 6;
    int top = LABEL_Y + (LABEL_H - rows * 10) / 2 + 1;
    const char *p = t;
    for (int line = 0; line < rows && *p; line++) {
        int n = slen(p), take = n < cols ? n : cols;
        if (n > cols) {
            int sp = take;
            while (sp > 0 && p[sp] != ' ') sp--;
            if (sp > 2) take = sp;
        }
        int x0 = LABEL_X + (LABEL_W - take * 8) / 2;
        int y0 = top + line * 10;
        for (int c = 0; c < take; c++) {
            u8 ch = p[c];
            if (ch < 32 || ch > 127) ch = '?';
            for (int r = 0; r < 8; r++) {
                u8 bits = font[(ch - 32) * 8 + r];
                for (int b = 0; b < 8; b++)
                    if (bits & (0x80 >> b)) dst[(y0 + r) * CART_W + x0 + c * 8 + b] = base + 1;
            }
        }
        p += take;
        while (*p == ' ') p++;
    }
}

// What the shelf shows: all games, the favourites, or the recently played
// (newest first). view[] holds game indices; positions wrap over it.
#define VIEW_ALL 0
#define VIEW_FAV 1
#define VIEW_RECENT 2
#define MAX_RECENT 20
static u16 view[MAX_GAMES];
static int view_n, view_mode;
static u8 fav[MAX_GAMES];
static u16 recent[MAX_RECENT];
static int recent_n;

static int n_games(void) { return view_n; }
static int game_at(int pos) { return view[imod(pos, view_n)]; }

static const u8 *compose(int pos) {
    int k = pos & 3, g = game_at(pos);
    u8 *dst = comp[k];
    if (comp_game[k] == g) return dst;
    comp_game[k] = g;
    composed++;
    for (u32 b = 0; b < (u32) (CART_W * CART_H); b += 4) *(u32 *) (dst + b) = *(const u32 *) (th_cart + b);
    u8 base = 112 + k * LABEL_COLS;
    u32 off = label_offset(g);
    u8 ok = 0;
    if (off && plat_open(PACK_PATH, 0) == 0) {
        ok = plat_seek(off) == 0 && plat_read(label_buf, (2 * LABEL_COLS + LABEL_W * LABEL_H + 3) & ~3) == 0;
        plat_close();
    }
    if (!ok) {
        plain_label(g, dst, base);
        return dst;
    }
    const u16 *lp = (const u16 *) label_buf;
    for (int c = 0; c < LABEL_COLS; c++) PAL[base + c] = lp[c];
    const u8 *src = label_buf + 2 * LABEL_COLS;
    for (int y = 0; y < LABEL_H; y++)
        for (int x = 0; x < LABEL_W; x++) {
            u8 v = src[y * LABEL_W + x];
            if (v != 255) dst[(LABEL_Y + y) * CART_W + LABEL_X + x] = base + v;
        }
    return dst;
}

//****************************************************************************** scaled drawing (IWRAM, ARM)

void draw_scaled(u8 *dst, int tw, int th, const u8 *src, int w, int h, int lift) __attribute__((long_call));


// Draws src (a composed src_w x src_h cart) at w x h into dst, a tw x th area in
// 8bpp tile order, centred, its foot `lift` px above the area's bottom.
// Nearest neighbour. Each
// source row is copied to IWRAM first; output goes out a word (4 px) at a
// time through a per-column index table (src_w = a transparent pixel).
IWRAM_CODE void draw_scaled(u8 *dst, int tw, int th, const u8 *src, int w, int h, int lift) {
    int aw = tw * 8, ah = th * 8;
    u32 *d32 = (u32 *) dst;
    u8 xs[128] __attribute__((aligned(4)));
    u32 row[CART_MAX_W / 4 + 1];                    // a row + 4 zero bytes
    int sw = src_w, sh = src_h, words = sw >> 2;
    u8 *rb = (u8 *) row;
    if (lift < 0) lift = 0;
    if (lift > ah) lift = ah;
    if (w > aw) w = aw;
    if (h > ah - lift) h = ah - lift;
    int x0 = (aw - w) / 2, y0 = ah - lift - h;
    int nwords = tw * th * 16;
    if (w <= 0 || h <= 0) {
        for (int k = 0; k < nwords; k++) d32[k] = 0;
        return;
    }
    for (int k = 0; k < (y0 >> 3) * tw * 16; k++) d32[k] = 0;
    u32 step = (sw << 16) / w, acc = step / 2;
    for (int x = 0; x < aw; x++) {
        if (x < x0 || x >= x0 + w) xs[x] = sw;
        else { xs[x] = acc >> 16; acc += step; }
    }
    row[words] = 0;
    u32 ystep = (sh << 16) / h, yacc = ystep / 2;
    int stride = tw * 16;                           // words per tile row of tiles
    // rows above the cart inside its first tile row
    for (int py = y0 & ~7; py < y0; py++) {
        u32 *o = d32 + (py >> 3) * stride + (py & 7) * 2;
        for (int t = 0; t < tw; t++, o += 16) { o[0] = 0; o[1] = 0; }
    }
    int y1 = y0 + h;
    for (int py = y1; py < ah; py++) {               // below the cart (the lift)
        u32 *o = d32 + (py >> 3) * stride + (py & 7) * 2;
        for (int t = 0; t < tw; t++, o += 16) { o[0] = 0; o[1] = 0; }
    }
    for (int py = y0; py < y1; py++, yacc += ystep) {
        const u32 *s = (const u32 *) (src + (yacc >> 16) * sw);
        for (int k = 0; k < words; k++) row[k] = s[k];
        u32 *o = d32 + (py >> 3) * stride + (py & 7) * 2;
        const u8 *xp = xs;
        for (int t = 0; t < tw; t++, o += 16, xp += 8) {
            o[0] = rb[xp[0]] | (rb[xp[1]] << 8) | (rb[xp[2]] << 16) | (rb[xp[3]] << 24);
            o[1] = rb[xp[4]] | (rb[xp[5]] << 8) | (rb[xp[6]] << 16) | (rb[xp[7]] << 24);
        }
    }
}

//****************************************************************************** areas

static u8 stage_big[2][AREA_TILES * 64] IWRAM_DATA;
static u8 stage_small[2][AREA_TILES * 64] __attribute__((aligned(4)));
static u8 *const stage[AREAS] = {stage_big[0], stage_big[1], stage_small[0], stage_small[1]};

static struct { int pos, game, w, lift; u8 pend; } area[AREAS];
static int reserved_area = -1;        // the area showing the cart in the slot

static void areas_reset(void) {
    for (int a = 0; a < AREAS; a++) { area[a].pos = 0x7FFFFFFF; area[a].game = -1; area[a].w = 0; area[a].pend = 0; }
}

static int cart_h(int w) { return (w * CART_H + CART_W / 2) / CART_W; }

static int area_fits(int a, int w, int h, int lift) { return w <= area_tw[a] * 8 && h + lift <= area_th[a] * 8; }

static void area_draw(int a, int pos, int w, int h, int lift) {
    draw_scaled(stage[a], area_tw[a], area_th[a], compose(pos), w, h, lift);
    area[a].pos = pos;
    area[a].game = game_at(pos);
    area[a].w = w;
    area[a].lift = lift;
    area[a].pend = 1;
}

//****************************************************************************** shelf state

static int target;                    // shelf position of the selected cart (wraps by game count)
static float scroll, vel;             // Slot's spring
static int last_dir = 1;
static int hidden_pos = 0x7FFFFFFF;   // the cart in the slot (BG1), not on the shelf

static u16 map_shadow[2][BAND_ROWS * 32] IWRAM_DATA __attribute__((aligned(4)));
static u16 hofs_next;
static u32 sig_prev;                  // what the BG2 map shows (plan)
static u16 win_h, win_v;              // the selected cart's box (side_dim themes)
static u8 map_dirty;

static int sel_game(void) { return game_at(target); }

static int scroll_px(void) {
    float p = scroll * th.pitch;
    return (int) (p >= 0 ? p + 0.5f : p - 0.5f);
}

static void map_put(int pos, int a) {
    int tw = area_tw[a], th = area_th[a];
    int c0 = pos * cols + (cols - tw) / 2, r0 = BAND_ROWS - th;
    for (int ty = 0; ty < th; ty++)
        for (int tx = 0; tx < tw; tx++) {
            int col = (c0 + tx) & 63;
            map_shadow[col >> 5][(r0 + ty) * 32 + (col & 31)] = area_tile[a] + ty * tw + tx;
        }
}

// Works out which carts show at the current scroll, draws any that changed
// into their areas and prepares the map for the next vblank.
static void plan(void) {
    int n = n_games();
    if (!n) return;
    int px = scroll_px();
    hofs_next = (u16) (px + th.pitch / 2 - 120) & 511;
    struct { int pos, w, lift; int a; } vis[4];
    int nv = 0;
    int base = (int) (scroll >= 0 ? scroll : scroll - 1);
    for (int pos = base - 2; pos <= base + 2; pos++) {
        if (pos == hidden_pos) continue;
        if (n == 1 && pos != target) continue;
        float off = pos - scroll;
        float t = off < 0 ? -off : off;
        if (t > 1) t = 1;
        int w = (int) (CART_W * (1.0f - (1.0f - th.side_permille / 1000.0f) * t) + 0.5f);
        int lift = (int) (th.lift * (1.0f - t) + 0.5f);
        int cx = 120 + pos * th.pitch - px;
        if (cx + w / 2 <= 0 || cx - w / 2 >= 240) continue;
        if (t < 0.5f) {                             // the cart nearest the centre
            int h = cart_h(w), x1 = cx - w / 2, x2 = cx + (w + 1) / 2;
            if (x1 < 0) x1 = 0;
            if (x2 > 240) x2 = 240;
            win_h = (x1 << 8) | x2;
            win_v = ((th.foot_y - lift - h) << 8) | (th.foot_y - lift);
        }
        if (nv < 4) { vis[nv].pos = pos; vis[nv].w = w; vis[nv].lift = lift; vis[nv].a = -1; nv++; }
    }
    u8 used[AREAS] = {0};
    // 1: areas that already hold the cart at this size
    for (int i = 0; i < nv; i++)
        for (int a = 0; a < AREAS; a++)
            if (!used[a] && a != reserved_area && area[a].pos == vis[i].pos && area[a].game == game_at(vis[i].pos) &&
                    area[a].w == vis[i].w && area[a].lift == vis[i].lift) {
                vis[i].a = a; used[a] = 1; break;
            }
    // 2: carts already on screen at another size keep their picture for now;
    // one of them (the furthest off) is redrawn per frame, so a move costs
    // at most one big redraw a frame and each moving cart updates at 30 Hz
    int stale_best = -1, stale_d = 0;
    for (int i = 0; i < nv; i++) {
        if (vis[i].a >= 0) continue;
        for (int a = 0; a < AREAS; a++)
            if (!used[a] && a != reserved_area && area[a].pos == vis[i].pos && area[a].game == game_at(vis[i].pos)) {
                vis[i].a = a; used[a] = 1;
                int d = vis[i].w - area[a].w, dl = vis[i].lift - area[a].lift;
                if (d < 0) d = -d;
                if (dl < 0) dl = -dl;
                d += dl + 1;
                if (d > stale_d) { stale_d = d; stale_best = i; }
                break;
            }
    }
    if (stale_best >= 0) {
        int i = stale_best, a = vis[i].a, w = vis[i].w, h = cart_h(w);
        if (area_fits(a, w, h, vis[i].lift)) area_draw(a, vis[i].pos, w, h, vis[i].lift);
        else { vis[i].a = -1; used[a] = 0; }        // outgrew its area: move it below
    }
    // 3: new carts; side-size carts prefer the small areas
    for (int i = 0; i < nv; i++) {
        if (vis[i].a >= 0) continue;
        int w = vis[i].w, h = cart_h(w), best = -1, best_score = -1;
        for (int a = 0; a < AREAS; a++) {
            if (used[a] || a == reserved_area || !area_fits(a, w, h, vis[i].lift)) continue;
            int score = a >= 2 ? 2 : 0;
            if (score > best_score) { best = a; best_score = score; }
        }
        if (best < 0) {
            // every area it fits is taken: move a cart that would fit a free
            // smaller area out of a big one
            for (int j = 0; j < nv && best < 0; j++) {
                int a = vis[j].a;
                if (j == i || a < 0 || a >= 2 || !area_fits(a, w, h, vis[i].lift)) continue;
                int wj = vis[j].w, hj = cart_h(wj);
                for (int b2 = 2; b2 < AREAS; b2++)
                    if (!used[b2] && b2 != reserved_area && area_fits(b2, wj, hj, vis[j].lift)) {
                        used[b2] = 1;
                        vis[j].a = b2;
                        area_draw(b2, vis[j].pos, wj, hj, vis[j].lift);
                        best = a;
                        break;
                    }
            }
            if (best < 0) continue;
        }
        used[best] = 1;
        vis[i].a = best;
        area_draw(best, vis[i].pos, w, h, vis[i].lift);
    }
    // the map, when what shows where has changed
    u32 sig = 2166136261u;
    for (int i = 0; i < nv; i++) sig = (sig ^ (u32) (vis[i].pos * 8 + vis[i].a + 1)) * 16777619u;
    if (sig == sig_prev && !map_dirty) return;
    sig_prev = sig;
    u32 *ms = (u32 *) map_shadow;
    for (u32 k = 0; k < sizeof (map_shadow) / 4; k++) ms[k] = 0;
    for (int i = 0; i < nv; i++) if (vis[i].a >= 0) map_put(vis[i].pos, vis[i].a);
    map_dirty = 1;
}

// In vblank: the drawn areas, the map and the scroll, all at once.
static void apply(void) {
    for (int a = 0; a < AREAS; a++)
        if (area[a].pend) {
            dma32(CHAR8 + 64 * area_tile[a], stage[a], area_tw[a] * area_th[a] * 64);
            area[a].pend = 0;
        }
    if (map_dirty) {
        dma32(MAP_BG2, map_shadow[0], sizeof (map_shadow[0]));
        dma32(MAP_BG2 + 0x800, map_shadow[1], sizeof (map_shadow[1]));
        map_dirty = 0;
    }
    BGHOFS(2) = hofs_next;
    REG16(0x04000040) = win_h;                     // WIN0H
    REG16(0x04000044) = win_v;                     // WIN0V
}

static int settled(void) {
    float d = scroll - target;
    return d > -0.002f && d < 0.002f && vel > -0.01f && vel < 0.01f;
}

// Close enough to act on A or SELECT (the spring's tail is long): snap.
static int nearly_settled(void) {
    float d = scroll - target;
    if (d < -0.1f || d > 0.1f) return 0;
    scroll = target;
    vel = 0;
    return 1;
}

static void physics(void) {
    const float dt = 1.0f / 60;
    float omega = th.omega_x16 / 16.0f;
    float acc = -2 * omega * vel - omega * omega * (scroll - target);
    vel += acc * dt;
    scroll += vel * dt;
    if (settled()) { scroll = target; vel = 0; }
}

//****************************************************************************** text

static u8 tbuf[32 * 32] __attribute__((aligned(4)));
#define DYN_NAME (th_tiles4)
#define DYN_CLOCK (th_tiles4 + 30)
#define DYN_SYS (th_tiles4 + 36)
#define SYS_TILES 18

static void tset(u8 *buf, int x, int y, u8 v) {
    u8 *p = buf + (x >> 3) * 32 + (y & 7) * 4 + ((x & 7) >> 1);
    if (x & 1) *p = (*p & 0x0F) | (v << 4);
    else *p = (*p & 0xF0) | v;
}

// One centred line on NAME_ROW: 7 px per character, 6 when that does not
// fit, cut with "..." when even that does not.
static void name_text(const char *s, u8 colour) {
    char t[48];
    int n = slen(s), sp = 7;
    if (n * 7 > 236) sp = 6;
    int maxc = 236 / sp;
    if (n > maxc) {
        int keep = maxc - 3;
        while (keep > 0 && s[keep - 1] == ' ') keep--;
        for (int k = 0; k < keep; k++) t[k] = s[k];
        t[keep] = t[keep + 1] = t[keep + 2] = '.';
        n = keep + 3;
    } else {
        for (int k = 0; k < n; k++) t[k] = s[k];
    }
    for (u32 k = 0; k < 30 * 32; k++) tbuf[k] = 0;
    int x0 = (240 - n * sp) / 2;
    if (x0 < 0) x0 = 0;
    for (int c = 0; c < n; c++) {
        u8 ch = t[c];
        if (ch < 32 || ch > 127) ch = '?';
        for (int r = 0; r < 8; r++) {
            u8 bits = font[(ch - 32) * 8 + r];
            for (int b = 0; b < 8; b++) {
                int x = x0 + c * sp + b;
                if ((bits & (0x80 >> b)) && x < 240) tset(tbuf, x, r, colour);
            }
        }
    }
    dma32(VRAM + DYN_NAME * 32, tbuf, 30 * 32);
    for (int x = 0; x < 30; x++) MAP_BG0[th.name_row * 32 + x] = (DYN_NAME + x) | (th.name_bank << 12);
}

static void show_name(void) {
    char t[96];
    if (!n_games()) { name_text("NO GAMES FOUND", th.name_colour); return; }
    int g = sel_game();
    if (fav[g]) { t[0] = '*'; t[1] = ' '; game_title(g, t + 2, sizeof (t) - 2); }   // a favourite
    else game_title(g, t, sizeof (t));
    name_text(t, th.name_colour);
}

static u8 clock_on = 1;               // settings: show the clock

static void clock_text(void) {
    u8 tm[5], hr, mn;
    char s[6] = "--:--";
    if (!clock_on) for (int k = 0; k < 5; k++) s[k] = ' ';
    else if (plat_time(tm) == 0 && (hr = tm[3], mn = tm[4], (hr >> 4) <= 2) && (hr & 15) <= 9 && hr <= 0x23 && (mn >> 4) <= 5 && (mn & 15) <= 9) {
        s[0] = '0' + (hr >> 4); s[1] = '0' + (hr & 15); s[3] = '0' + (mn >> 4); s[4] = '0' + (mn & 15);
    }
    // drawn over the bar's own tiles (6 tiles from the theme's clock column)
    int col0 = th.clock_col;
    for (int k = 0; k < 6; k++) {
        u16 e = th_map_front[th.clock_row * 30 + col0 + k];
        const u8 *src = (const u8 *) (VRAM + (e & 1023) * 32);
        for (int b = 0; b < 32; b++) tbuf[k * 32 + b] = src[b];
    }
    int x0 = 6 * 8 - 5 * 8 - 2;
    for (int c = 0; c < 5; c++) {
        u8 ch = s[c];
        for (int r = 0; r < 8; r++) {
            u8 bits = font[(ch - 32) * 8 + r];
            for (int b = 0; b < 8; b++)
                if (bits & (0x80 >> b)) tset(tbuf, x0 + c * 8 + b, r, th.clock_colour);
        }
    }
    dma32(VRAM + DYN_CLOCK * 32, tbuf, 6 * 32);
    for (int k = 0; k < 6; k++) MAP_BG0[th.clock_row * 32 + col0 + k] = (DYN_CLOCK + k) | (th.clock_bank << 12);
}

// The shelf's system name over the bar, on the bar's own tiles (themes with sys_row).
static const char *sys_label = "GAME BOY ADVANCE";

static void sys_text(void) {
    if (th.sys_row < 0 || th.sys_row > 19 || th.sys_col < 0 || th.sys_col + SYS_TILES > 30) return;
    for (int k = 0; k < SYS_TILES; k++) {
        u16 e = th_map_front[th.sys_row * 30 + th.sys_col + k];
        const u8 *src = (const u8 *) (VRAM + (e & 1023) * 32);
        for (int b = 0; b < 32; b++) tbuf[k * 32 + b] = src[b];
    }
    for (int c = 0; sys_label[c] && c < SYS_TILES - 1; c++) {   // 4 px in, like the bar's own text
        u8 ch = sys_label[c];
        if (ch < 32 || ch > 127) ch = '?';
        for (int r = 0; r < 8; r++) {
            u8 bits = font[(ch - 32) * 8 + r];
            for (int b = 0; b < 8; b++)
                if (bits & (0x80 >> b)) tset(tbuf, 4 + c * 8 + b, r, th.clock_colour);
        }
    }
    dma32(VRAM + DYN_SYS * 32, tbuf, SYS_TILES * 32);
    for (int k = 0; k < SYS_TILES; k++) MAP_BG0[th.sys_row * 32 + th.sys_col + k] = (DYN_SYS + k) | (th.clock_bank << 12);
}

//****************************************************************************** input

static u16 keys_prev;
static int rep_timer, rep_count;
static const u8 rep_gap[4] = {7, 5, 4, 3};    // Slot: 400 ms, then 110/85/65/50 ms

static u16 keys_read(void) {
    u16 k = ~KEYINPUT & 0x3FF;
    u16 press = k & ~keys_prev;
    if (k & (K_LEFT | K_RIGHT)) {
        if (press & (K_LEFT | K_RIGHT)) { rep_timer = 24; rep_count = 0; }
        else if (--rep_timer <= 0) {
            rep_timer = rep_gap[rep_count < 3 ? rep_count : 3];
            rep_count++;
            press |= k & (K_LEFT | K_RIGHT);
        }
    }
    keys_prev = k;
    return press;
}

//****************************************************************************** into the slot

// Slot's insert (slot_chrome.rs): the foot travels from the shelf foot to the
// seated foot; the cart shrinks to the seated size by the time it reaches
// the lip, pauses there, then sinks in. The Slot theme at 1/3 scale: foot
// 115 -> 189.7, lip 140.7, seated 80x47, 27 frames (0.45 s).
#define FOOT0 ((float) (th.foot_y - th.lift))
#define SEATED_FOOT (th.seated_foot_x16 / 16.0f)
#define LIP (th.lip_x16 / 16.0f)
#define CATCH ((LIP - FOOT0) / (SEATED_FOOT - FOOT0))
#define SEAT_FRAMES (th.seat_frames)
#define EJECT_FRAMES (th.eject_frames)

static float ease(float u) { return u * u * u * (u * (u * 6 - 15) + 10); }

static float travel(float seat) {
    const float in = 0.42f, out = 0.62f, creep = 0.03f;
    if (seat < in) return CATCH * ease(seat / in);
    if (seat < out) return CATCH + creep * (seat - in) / (out - in);
    float caught = CATCH + creep;
    return caught + (1 - caught) * ease((seat - out) / (1 - out));
}

static int slot_area;

static void seat_frame(float seat) {
    float tr = travel(seat);
    float k = tr / CATCH;
    if (k > 1) k = 1;
    int w = (int) (CART_W + (th.seat_w - CART_W) * k + 0.5f);
    int h = (int) (CART_H + (th.seat_h - CART_H) * k + 0.5f);
    float foot = FOOT0 + (SEATED_FOOT - FOOT0) * tr;
    draw_scaled(stage[slot_area], area_tw[slot_area], area_th[slot_area], compose(target), w, h, 0);
    area[slot_area].pos = 0x7FFFFFFF;               // no longer a shelf picture
    area[slot_area].pend = 1;
    vsync();
    apply();
    BGVOFS(1) = (u16) (area_th[slot_area] * 8 - (int) (foot + 0.5f)) & 511;
}

static void slot_in(void) {
    // the selected cart's area goes to BG1, redrawn without its lift and
    // placed at the lifted foot, in the same vblank as the shelf drops it
    slot_area = 0;
    for (int a = 0; a < AREAS; a++) if (area[a].pos == target) slot_area = a;
    int tw = area_tw[slot_area], tht = area_th[slot_area];
    for (int k = 0; k < 32 * 32; k++) MAP_BG1[k] = 0;
    for (int ty = 0; ty < tht; ty++)
        for (int tx = 0; tx < tw; tx++)
            MAP_BG1[ty * 32 + tx] = area_tile[slot_area] + ty * tw + tx;
    BGHOFS(1) = (u16) (tw * 4 - 120) & 511;
    hidden_pos = target;
    reserved_area = slot_area;
    draw_scaled(stage[slot_area], tw, tht, compose(target), CART_W, CART_H, 0);
    area[slot_area].pos = 0x7FFFFFFF;
    area[slot_area].pend = 1;
    plan();                                          // the shelf without it
    vsync();
    apply();
    BGVOFS(1) = (u16) (tht * 8 - (int) (th.foot_y - th.lift)) & 511;
    DISPCNT |= 1 << 9;
    name_text("", th.name_colour);
    for (int f = 1; f <= SEAT_FRAMES; f++) seat_frame((float) f / SEAT_FRAMES);
}

static void slot_out(void) {
    for (int f = EJECT_FRAMES - 1; f >= 0; f--) seat_frame((float) f / EJECT_FRAMES);
    hidden_pos = 0x7FFFFFFF;
    reserved_area = -1;
    areas_reset();
    plan();
    vsync();
    apply();
    DISPCNT &= ~(1 << 9);
}

static void status(const char *msg) {
    name_text(msg, th.name_colour);
    vsync();
}

// everfront/favourites.txt and recent.txt: one rom file name per line.
// everfront/favourites.txt, recent.txt for GBA; favourites-gb.txt... for the others.
static char fav_path[48], recent_path[48];

static void list_paths(void) {
    const char *e = seq(cur_ext, "gba") ? "" : cur_ext;
    for (int w = 0; w < 2; w++) {
        char *o = w ? recent_path : fav_path;
        u32 n = 0;
        for (const char *q = APP_DIR "/"; *q; q++) o[n++] = *q;
        for (const char *q = w ? "recent" : "favourites"; *q; q++) o[n++] = *q;
        if (*e) { o[n++] = '-'; for (const char *q = e; *q; q++) o[n++] = *q; }
        for (const char *q = ".txt"; *q; q++) o[n++] = *q;
        o[n] = 0;
    }
}
#define FAV_PATH fav_path
#define RECENT_PATH recent_path

static int game_by_name(const char *name) {
    u32 c = crc32_str(name, slen(name), 0);
    for (int i = 0; i < game_count; i++) if (game_crc[i] == c && seq(game_name(i), name)) return i;
    return -1;
}

static void list_read(const char *path, void (*each)(int g)) {
    char *buf = (char *) label_buf;                  // free outside compose()
    const u32 bufsize = sizeof (label_buf);
    if (plat_open(path, 0)) return;
    u32 n = plat_size();
    if (n > bufsize - 1) n = bufsize - 1;
    u8 r = plat_read(buf, n);
    plat_close();
    if (r) return;
    buf[n] = 0;
    for (char *p = buf; *p;) {
        char *e = p;
        while (*e && *e != '\n' && *e != '\r') e++;
        char c = *e;
        *e = 0;
        int g = *p ? game_by_name(p) : -1;
        if (g >= 0) each(g);
        p = c ? e + 1 : e;
        while (*p == '\n' || *p == '\r') p++;
    }
}

static void fav_set(int g) { fav[g] = 1; }
static void recent_push(int g) { if (recent_n < MAX_RECENT) recent[recent_n++] = g; }

static void list_write(const char *path, int (*next)(int k)) {
    char *buf = (char *) label_buf;                  // free outside compose()
    u32 n = 0;
    for (int k = 0, g; (g = next(k)) >= 0; k++) {
        const char *s = game_name(g);
        u32 l = slen(s);
        if (n + l + 1 > sizeof (label_buf)) break;
        for (u32 i = 0; i < l; i++) buf[n++] = s[i];
        buf[n++] = '\n';
    }
    if (plat_open(path, 1)) return;
    plat_write(buf, n);
    plat_close();
}

static int fav_next(int k) {
    for (int g = 0; g < game_count; g++) if (fav[g] && k-- == 0) return g;
    return -1;
}
static int recent_next(int k) { return k < recent_n ? recent[k] : -1; }

static void recent_add(int g) {
    int k = 0;
    while (k < recent_n && recent[k] != g) k++;
    if (k == recent_n && recent_n < MAX_RECENT) recent_n++;
    if (k == recent_n) k = recent_n - 1;
    for (; k > 0; k--) recent[k] = recent[k - 1];
    recent[0] = g;
    list_write(RECENT_PATH, recent_next);
}

// Builds view[] for view_mode, keeping game `keep` selected if it is in it.
// An empty favourites or recent list shows everything.
static void view_build(int keep) {
    view_n = 0;
    if (view_mode == VIEW_FAV) for (int g = 0; g < game_count; g++) { if (fav[g]) view[view_n++] = g; }
    if (view_mode == VIEW_RECENT) for (int k = 0; k < recent_n; k++) view[view_n++] = recent[k];
    if (!view_n) for (int g = 0; g < game_count; g++) view[view_n++] = g;
    int at = 0;
    for (int k = 0; k < view_n; k++) if (view[k] == keep) at = k;
    target = at;
    scroll = target;
    vel = 0;
}

static void launch_selected(void) {
    char path[1025];
    int g = sel_game();
    slot_in();
    game_path(g, path, 1);
    const char *err = 0;
    lgq("A: %s\n", game_name(g));
    recent_add(g);
    log_flush();                                     // what was picked is on the card even if the launch hangs
    u8 r = seq(cur_ext, "gba") ? launch_game(path, game_name(g), status, &err)
                               : launch_app(path, game_name(g), cur_ext, status, &err);
    lgq("launch error %02X: %s\n", r, err ? err : "-");
    games_reopen();                                  // emulator launches list another folder
    log_flush();
    char msg[40];
    u32 n = 0;
    for (const char *s = err ? err : "Error"; *s && n < 26; s++) msg[n++] = *s;
    for (const char *s = " - B"; *s; s++) msg[n++] = *s;
    msg[n] = 0;
    name_text(msg, th.name_colour);
    while (!(keys_read() & K_B)) vsync();
    slot_out();
    show_name();
    lgq("back to the shelf\n");
    log_flush();
}

//****************************************************************************** themes and settings

static void theme_show(void);
static u16 dispcnt_shelf;             // BG0, BG2, BG3 (+ window 0 when sides are darkened)
static void theme_failed(const char *err);

#define MAX_THEMES 16
static char theme_files[MAX_THEMES][24];
static int theme_count;
static char theme_file[24] = "slot.thm";



static void theme_path(char *out, const char *file) {
    u32 o = 0;
    for (const char *p = THEMES_DIR "/"; *p; p++) out[o++] = *p;
    for (const char *p = file; *p && o < 63; p++) out[o++] = *p;
    out[o] = 0;
}

// The .thm files in everfront/themes, sorted.
static void themes_list(void) {
    char name[256];
    u8 is_dir;
    theme_count = 0;
    if (plat_dir_open(THEMES_DIR)) return;
    while (theme_count < MAX_THEMES && plat_dir_next(name, sizeof (name), &is_dir) == 0) {
        u32 n = slen(name);
        if (is_dir || n < 5 || n > 23 || name[n - 4] != '.' || low(name[n - 3]) != 't' ||
                low(name[n - 2]) != 'h' || low(name[n - 1]) != 'm')
            continue;
        scopy(theme_files[theme_count++], name, 24);
    }
    for (int a = 1; a < theme_count; a++)
        for (int b = a; b > 0 && name_cmp(theme_files[b - 1], theme_files[b]) > 0; b--) {
            char t[24];
            scopy(t, theme_files[b], 24);
            scopy(theme_files[b], theme_files[b - 1], 24);
            scopy(theme_files[b - 1], t, 24);
        }
}

static char sys_pref[4] = "gba";      // config: the shelf's system at start-up

// everfront/config.txt: "theme=<file>" lines (anything else is ignored).
static void config_read(void) {
    char buf[256] __attribute__((aligned(4)));
    if (plat_open(CONFIG_PATH, 0)) return;
    u32 n = plat_size();
    if (n > sizeof (buf) - 1) n = sizeof (buf) - 1;
    u8 r = plat_read(buf, n);
    plat_close();
    if (r) return;
    buf[n] = 0;
    for (char *p = buf; *p;) {
        char *e = p;
        while (*e && *e != '\n' && *e != '\r') e++;
        char c = *e;
        *e = 0;
        if (p[0] == 't' && p[1] == 'h' && p[2] == 'e' && p[3] == 'm' && p[4] == 'e' && p[5] == '=' && p[6])
            scopy(theme_file, p + 6, sizeof (theme_file));
        if (seq(p, "clock=off")) clock_on = 0;
        if (seq(p, "show=favourites")) view_mode = VIEW_FAV;
        if (seq(p, "show=recent")) view_mode = VIEW_RECENT;
        if (p[0] == 's' && p[1] == 'y' && p[2] == 's' && p[3] == 't' && p[4] == 'e' && p[5] == 'm' && p[6] == '=')
            scopy(sys_pref, p + 7, sizeof (sys_pref));
        if (seq(p, "clock=on")) clock_on = 1;
        p = c ? e + 1 : e;
    }
}

static void config_write(void) {
    char buf[96];
    u32 n = 0;
    for (const char *p = "theme="; *p; p++) buf[n++] = *p;
    for (const char *p = theme_file; *p; p++) buf[n++] = *p;
    buf[n++] = '\n';
    for (const char *p = clock_on ? "clock=on\n" : "clock=off\n"; *p; p++) buf[n++] = *p;
    for (const char *p = view_mode == VIEW_FAV ? "show=favourites\n" : view_mode == VIEW_RECENT ? "show=recent\n" : "show=all\n"; *p; p++) buf[n++] = *p;
    for (const char *p = "system="; *p; p++) buf[n++] = *p;
    for (const char *p = cur_ext; *p; p++) buf[n++] = *p;
    buf[n++] = '\n';
    if (plat_open(CONFIG_PATH, 1)) return;
    plat_write(buf, n);
    plat_close();
}

static void shelf_reset(void) {
    for (int k = 0; k < 4; k++) comp_game[k] = -1;
    for (int g = 0; g < game_count; g++) lab_off[g] = 0xFFFFFFFF;
    areas_reset();
    sig_prev = 0;
    map_dirty = 1;
    hidden_pos = 0x7FFFFFFF;
    reserved_area = -1;
}

// SELECT: the next theme in everfront/themes, kept in config.txt.
// (Re)loads the current theme_file onto the screen; back to `fallback` if
// that fails. Returns 1 when theme_file loaded.
static int theme_reload(const char *fallback) {
    char path[64];
    vsync();
    DISPCNT = 0x80;
    for (u32 k = 0; k < 0x18000; k += 4) *(vu32 *) (VRAM + k) = 0;
    for (int k = 0; k < 256; k++) PAL[k] = 0;
    theme_path(path, theme_file);
    int ok = theme_load(path) == 0;
    if (!ok) {
        scopy(theme_file, fallback, sizeof (theme_file));
        theme_path(path, theme_file);
        const char *err = theme_load(path);
        if (err) theme_failed(err);
    }
    theme_show();
    cart_select();
    pack_open();
    shelf_reset();
    scroll = target;
    vel = 0;
    plan();
    show_name();
    clock_text();
    sys_text();
    log_flush();
    vsync();
    apply();
    DISPCNT = dispcnt_shelf;
    return ok;
}


// START: the log on screen (for a photo when something goes wrong; the Mini
// build can't write it to the card). Mode 3, 30 x 20 characters, newest at
// the bottom; up/down scroll, B or START back to the shelf.
// Full-screen text pages (mode 3) for the log and the settings menu. The
// shelf's VRAM is lost; theme_reload() puts it back afterwards.
#define FB ((vu16 *) VRAM)

static void fb_clear(void) { for (int k = 0; k < 240 * 160; k++) FB[k] = 0x0842; }

static void fb_text(int x, int y, const char *s, u16 colour) {
    for (int c = 0; s[c] && x + c * 8 < 240; c++) {
        u8 ch = s[c];
        if (ch < 32 || ch > 127) ch = '?';
        for (int r = 0; r < 8; r++)
            for (int b = 0; b < 8; b++)
                if (font[(ch - 32) * 8 + r] & (0x80 >> b)) FB[(y + r) * 240 + x + c * 8 + b] = colour;
    }
}

static u16 page_keys(u16 *held) {
    vsync();
    u16 k = ~KEYINPUT & 0x3FF, press = k & ~*held;
    *held = k;
    return press;
}

// The log, newest at the bottom; up/down scroll, B back.
static void log_page(void) {
    u32 len;
    lgq("log viewed\n");
    const char *t = log_text(&len);
    static u16 starts[1024];                         // line starts, wrapped at 30 columns
    int nl = 0;
    u32 p = 0;
    while (p < len && nl < 1024) {
        starts[nl++] = p;
        u32 c = 0;
        while (p < len && t[p] != '\n' && c < 30) { p++; c++; }
        if (p < len && t[p] == '\n') p++;
    }
    int rows = 19, top = nl > rows ? nl - rows : 0, redraw = 1;
    u16 held = ~KEYINPUT & 0x3FF;
    while (1) {
        if (redraw) {
            fb_clear();
            fb_text(0, 0, "EverFront log   B:back  Up/Dn", 0x6F7B);
            for (int row = 0; row < rows && top + row < nl; row++) {
                char line[31];
                u32 q = starts[top + row], c = 0;
                for (; c < 30 && q < len && t[q] != '\n'; c++, q++) line[c] = t[q];
                line[c] = 0;
                fb_text(0, 8 + row * 8, line, 0x7FFF);
            }
            DISPCNT = 3 | (1 << 10);
            redraw = 0;
        }
        u16 pr = page_keys(&held);
        if (pr & (K_B | K_START)) break;
        if ((held & 0x40) && top > 0) { top--; redraw = 1; }
        if ((held & 0x80) && top + rows < nl) { top++; redraw = 1; }
    }
}

// START: settings. Up/down pick, left/right change, A on an action, B or
// START close (the choices are kept in config.txt).
static void settings(void) {
    char cur[24], pick[24];
    scopy(cur, theme_file, sizeof (cur));
    scopy(pick, theme_file, sizeof (pick));
    themes_list();
    games_reopen();
    u8 clock0 = clock_on;
    int sel_item = 0, redraw = 1, items = 5;
    int view0 = view_mode;
    u16 held = ~KEYINPUT & 0x3FF;
    lgq("START: settings\n");
    vsync();
    DISPCNT = 0x80;
    while (1) {
        if (redraw) {
            char title[40], v[32];
            u32 n = 0;
            for (const char *q = "EverFront " EVERFRONT_VERSION " ("; *q; q++) title[n++] = *q;
            for (const char *q = plat_name(); *q; q++) title[n++] = *q;
            title[n++] = ')';
            title[n] = 0;
            fb_clear();
            fb_text(8, 8, title, 0x6F7B);
            const char *names[5] = {"Theme", "Show", "Clock", "Show log", "Close"};
            for (int it = 0; it < items; it++) {
                int y = 32 + it * 16;
                u16 col = it == sel_item ? 0x7FFF : 0x5294;
                if (it == sel_item) fb_text(8, y, ">", 0x03FF);
                fb_text(24, y, names[it], col);
                v[0] = 0;
                if (it == 0) {                       // the theme's file name, without .thm
                    u32 k = 0;
                    v[k++] = '<'; v[k++] = ' ';
                    for (u32 m = 0; pick[m] && pick[m] != '.' && k < 20; m++) v[k++] = m ? pick[m] : (pick[m] >= 'a' && pick[m] <= 'z' ? pick[m] - 32 : pick[m]);
                    v[k++] = ' '; v[k++] = '>'; v[k] = 0;
                }
                if (it == 1) scopy(v, view_mode == VIEW_FAV ? "< Favourites >" : view_mode == VIEW_RECENT ? "< Recent >" : "< All games >", sizeof (v));
                if (it == 2) scopy(v, clock_on ? "< On >" : "< Off >", sizeof (v));
                fb_text(120, y, v, col);
            }
            fb_text(8, 120, "Up/Dn: choose  L/R: change", 0x4A52);
            fb_text(8, 132, "A: select   B: back", 0x4A52);
            DISPCNT = 3 | (1 << 10);
            redraw = 0;
        }
        u16 pr = page_keys(&held);
        if (pr & (K_B | K_START)) break;
        if (pr & 0x40) { sel_item = (sel_item + items - 1) % items; redraw = 1; }
        if (pr & 0x80) { sel_item = (sel_item + 1) % items; redraw = 1; }
        int d = (pr & K_RIGHT) ? 1 : (pr & K_LEFT) ? -1 : 0;
        if (d && sel_item == 0 && theme_count > 1) {
            int c = 0;
            for (int k = 0; k < theme_count; k++) if (seq(theme_files[k], pick)) c = k;
            scopy(pick, theme_files[(c + d + theme_count) % theme_count], sizeof (pick));
            redraw = 1;
        }
        if (d && sel_item == 1) { view_mode = (view_mode + 3 + d) % 3; redraw = 1; }
        if (d && sel_item == 2) { clock_on ^= 1; redraw = 1; }
        if (pr & K_A) {
            if (sel_item == 3) { log_page(); redraw = 1; }
            if (sel_item == 4) break;
        }
    }
    keys_prev = ~KEYINPUT & 0x3FF;
    int changed = !seq(pick, cur) || clock0 != clock_on || view0 != view_mode;
    if (view0 != view_mode) { lgq("show %d\n", view_mode); view_build(view_n ? sel_game() : 0); }
    if (!seq(pick, cur)) lgq("theme %s\n", pick);
    scopy(theme_file, pick, sizeof (theme_file));
    if (theme_reload(cur) && changed) config_write();
}

//****************************************************************************** systems

// Shelves: one per system with games in its folder. Other systems than GBA
// run through the EverDrive's emulators in edgba/edapp/<ext>/ (Pro only).
typedef struct { const char *name, *ext, *dir[3]; } System;
static const System systems[] = {
    {"GAME BOY ADVANCE", "gba", {"Game Boy Advance", "GBA", ""}},
    {"GAME BOY", "gb", {"Game Boy", "GB", 0}},
    {"GAME BOY COLOR", "gbc", {"Game Boy Color", "GBC", 0}},
    {"NES", "nes", {"NES", "Nintendo", 0}},
    {"MASTER SYSTEM", "sms", {"Master System", "SMS", 0}},
    {"GAME GEAR", "gg", {"Game Gear", "GG", 0}},
};
#define NSYS 6
static u8 sys_dir[NSYS];              // which of its folders has games; 0xFF: no shelf
static int sys_cur, name_hold;

#ifndef PLAT_MINI
// edgba/edapp/<ext>/ holds an emulator (a .gba file)
static int emulator_present(const char *ext) {
    char path[32], name[256];
    u8 is_dir;
    u32 n = 0;
    for (const char *q = "edgba/edapp/"; *q; q++) path[n++] = *q;
    for (const char *q = ext; *q; q++) path[n++] = *q;
    path[n] = 0;
    if (plat_dir_open(path)) return 0;
    const char *keep = cur_ext;
    cur_ext = "gba";
    int found = 0;
    while (!found && plat_dir_next(name, sizeof (name), &is_dir) == 0) found = !is_dir && ext_len(name);
    cur_ext = keep;
    return found;
}
#endif

static void set_folder(int s) {
    scopy(folder, systems[s].dir[sys_dir[s]], sizeof (folder));
}

// Which systems get a shelf (GBA always, if it has games anywhere).
static void systems_scan(void) {
    for (int s = 0; s < NSYS; s++) {
        sys_dir[s] = 0xFF;
#ifdef PLAT_MINI
        if (s) continue;                              // the Mini build: GBA only for now
#else
        if (s && !emulator_present(systems[s].ext)) continue;
#endif
        cur_ext = systems[s].ext;
        for (int d = 0; d < 3 && systems[s].dir[d]; d++) {
            scopy(folder, systems[s].dir[d], sizeof (folder));
            if (list_games(folder) == 0 && game_count) { sys_dir[s] = d; break; }
        }
        lgq("shelf %s: %s\n", systems[s].ext, sys_dir[s] == 0xFF ? "none" : systems[s].dir[sys_dir[s]]);
    }
    if (sys_dir[0] == 0xFF) sys_dir[0] = 0;
}

// Makes s the shelf's system: its games, cart, labels, favourites, recent.
static void sys_enter(int s) {
    sys_cur = s;
    cur_ext = systems[s].ext;
    sys_label = systems[s].name;
    set_folder(s);
    list_games(folder);
    lgq("folder '%s': %u games\n", folder, game_count);
    for (int g = 0; g < game_count; g++) fav[g] = 0;
    recent_n = 0;
    list_paths();
    list_read(FAV_PATH, fav_set);
    list_read(RECENT_PATH, recent_push);
    games_reopen();
    cart_select();
    pack_open();
    view_build(view_mode == VIEW_RECENT && recent_n ? recent[0] : 0);
    shelf_reset();
}

// Up/down: the next system's shelf.
static void sys_switch(int d) {
    int s = sys_cur;
    do s = (s + d + NSYS) % NSYS; while (s != sys_cur && sys_dir[s] == 0xFF);
    if (s == sys_cur) return;
    lgq("shelf -> %s\n", systems[s].ext);
    vsync();
    DISPCNT &= ~(1 << 10);                            // carts hidden while the shelf is rebuilt
    sys_enter(s);
    plan();
    name_text(systems[s].name, th.name_colour);
    name_hold = 90;
    vsync();
    sys_text();
    apply();
    DISPCNT = dispcnt_shelf;
    config_write();
}

//****************************************************************************** main

static void screen_init(void) {
    DISPCNT = 0x80;
    // Coming back from a game without a power cycle, its hardware may still be
    // busy: emulators (PocketNES, SMSAdvance) leave HBlank DMA rewriting the
    // scroll registers every line, which garbles the top of the screen. Stop
    // the DMA, put the display's other registers back, hide every sprite.
    for (int c = 0; c < 4; c++) REG32(0x040000B8 + 12 * c) = 0;
    for (u32 a = 0x04000010; a < 0x04000056; a += 2) REG16(a) = 0;   // scroll, affine, windows, mosaic, blend
    REG16(0x04000020) = REG16(0x04000026) = REG16(0x04000030) = REG16(0x04000036) = 0x100;  // affine identity
    for (u32 k = 0; k < 0x400; k += 8) *(vu16 *) (0x07000000 + k) = 0x0200;              // sprites off
    for (u32 k = 0; k < 0x18000; k += 4) *(vu32 *) (VRAM + k) = 0;
    BGCNT(3) = 3 | (0 << 2) | (12 << 8);
    BGCNT(2) = 2 | (2 << 2) | (1 << 7) | (14 << 8) | (1 << 14);
    BGCNT(1) = 1 | (2 << 2) | (1 << 7) | (31 << 8);
    BGCNT(0) = 0 | (0 << 2) | (13 << 8);
    BGHOFS(0) = BGVOFS(0) = BGHOFS(3) = BGVOFS(3) = 0;
}

// The theme's bar front on BG0 and the shelf's foot.
static void theme_show(void) {
    dispcnt_shelf = (1 << 8) | (1 << 10) | (1 << 11);
    if (th.side_dim) {
        // darken BG2 outside window 0, which follows the selected cart
        REG16(0x04000050) = 0x0004 | (3 << 6);      // BLDCNT: BG2, brightness down
        REG16(0x04000054) = th.side_dim;           // BLDY
        REG16(0x04000048) = 0x001F;                // WININ: everything, no effect
        REG16(0x0400004A) = 0x003F;                // WINOUT: everything + effect
        dispcnt_shelf |= 1 << 13;
    } else {
        REG16(0x04000050) = 0;
    }
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++)
            MAP_BG0[y * 32 + x] = y < 20 && x < 30 ? th_map_front[y * 30 + x] : 0;
    BGVOFS(2) = (u16) (AREA_H - th.foot_y) & 511;
    src_w = th.cart_w;
    src_h = th.cart_h;
}

// Without a usable theme: say so in plain text and stop.
static void theme_failed(const char *err) {
    th.name_row = 9; th.name_bank = 5; th.name_colour = 1;
    th_tiles4 = 1;
    PAL[0] = 0x1084;
    PAL[81] = 0x7FFF;
    for (u32 k = 0; k < 0x18000; k += 4) *(vu32 *) (VRAM + k) = 0;
    name_text(err, 1);
    DISPCNT = 1 << 8;
    log_flush();
    while (1) vsync();
}

int main(void) {
    screen_init();
    plat_init();
    log_trim();
    {
        // a divider for each session: version, cart, date and time from the cart's clock
        u8 t[5];
        plat_time(t);
        lgq("\n==== EverFront %s (%s) 20%x%x-%x%x-%x%x %x%x:%x%x ====\n", EVERFRONT_VERSION, plat_name(),
                t[0] >> 4, t[0] & 15, t[1] >> 4, t[1] & 15, t[2] >> 4, t[2] & 15, t[3] >> 4, t[3] & 15, t[4] >> 4, t[4] & 15);
    }
    launch_init();
    config_read();
    char tpath[64];
    theme_path(tpath, theme_file);
    const char *terr = theme_load(tpath);
    if (terr && !seq(theme_file, "slot.thm")) {      // the chosen one is gone: back to Slot
        scopy(theme_file, "slot.thm", sizeof (theme_file));
        theme_path(tpath, theme_file);
        terr = theme_load(tpath);
    }
    if (terr) theme_failed(terr);
    theme_show();

    systems_scan();
    int start = 0;
    for (int s = 0; s < NSYS; s++) if (sys_dir[s] != 0xFF && seq(systems[s].ext, sys_pref)) start = s;
    sys_enter(start);
    lgq("show %d: %d games, %d recent\n", view_mode, view_n, recent_n);
    log_flush();

    plan();
    show_name();
    clock_text();
    sys_text();
    vsync();
    apply();
    DISPCNT = dispcnt_shelf;

    int tick = 0, shown = target, idle = 0;
    while (1) {
        vsync();
        apply();
        if (++tick >= 600) { tick = 0; clock_text(); }
        u16 k = keys_read();
        int n = n_games();
        if (n > 1) {
            if (k & K_RIGHT) { target++; last_dir = 1; }
            if (k & K_LEFT) { target--; last_dir = -1; }
            // L/R: five carts at once, shown as a one-cart move
            if (k & K_R) { target += 5; scroll = target - 1; vel = 0; last_dir = 1; }
            if (k & K_L) { target -= 5; scroll = target + 1; vel = 0; last_dir = -1; }
        }
        if (target != shown) { shown = target; name_hold = 0; show_name(); }
        if (name_hold && --name_hold == 0) show_name();
        if ((k & (0x40 | 0x80)) && nearly_settled()) {   // up/down: another system's shelf
            sys_switch(k & 0x40 ? -1 : 1);
            shown = target;
            continue;
        }
        if ((k & K_SELECT) && n) {                  // favourite on/off
            int g = sel_game();
            fav[g] ^= 1;
            lgq("SELECT: %s %s\n", fav[g] ? "favourite" : "not favourite", game_name(g));
            list_write(FAV_PATH, fav_next);
            show_name();
            if (view_mode == VIEW_FAV && !fav[g]) { view_build(g); shelf_reset(); plan(); show_name(); }
        }
        if ((k & K_START) && nearly_settled()) { settings(); continue; }
        if (n && (k & K_A) && nearly_settled()) {
            plan();                                  // the snapped shelf, so the slot starts from it
            launch_selected();
            continue;
        }
        physics();
        // anything logged while browsing reaches the card once the shelf has been still for 2 s
        if (!settled() || k) idle = 0;
        else if (++idle == 120) log_flush();
#ifdef PERF
        composed = 0; REG16(0x0400010A) = 0; REG16(0x04000108) = 0; REG16(0x0400010A) = 0x81;
#endif
        plan();
#ifdef PERF
        {
            static int worst; int dv = composed ? 0 : REG16(0x04000108) * 100 / 4389; composed = 0;
            if (dv > worst || (k & K_B)) { worst = dv; char t[16]; t[0] = '0' + worst / 100; t[1] = '0' + worst / 10 % 10; t[2] = '0' + worst % 10; t[3] = 0; name_text(t, th.name_colour); }
        }
#endif
        // while resting, compose the next cart in the direction of travel
        if (n > 2 && settled()) compose(target + 2 * last_dir);
    }
}
