/*
 * smmwe-install - standalone graphical installer for the SMM:WE port.
 *
 * usage: smmwe-install <gamedir>              install the APK found in "Setup/"
 *        smmwe-install <gamedir> --message T  show a message screen
 *
 * Install steps:
 *   1. find the first *.apk in <gamedir>/Setup/
 *   2. check it contains assets/game.droid and lib/arm64-v8a/libyoyo.so
 *   3. write <gamedir>/data/smmwe.apk with only what the port needs (assets and
 *      lib/arm64-v8a); game.droid and libyoyo.so are stored uncompressed so
 *      every launch skips ~130 MB of decompression
 *   4. move the original APK to <gamedir>/Setup/installed/ (it can be
 *      deleted to free space)
 *
 * Exit codes: 0 installed, 1 error (shown on screen), 2 nothing to install.
 */
#define _GNU_SOURCE
#include <SDL2/SDL.h>
#include <ctype.h>
#include <math.h>
#include <zip.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include "font8x8_basic.h"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

/* ------------------------------------------------------------------ UI -- */
#define SETUP_DIR "Setup"          /* the user puts the APK here */
#define INSTALLED_DIR "installed"       /* original APK after install */
#define OUT_APK "data/smmwe.apk"        /* what gmloader runs */

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *font_tex;
static int W = 640, H = 480;
static int LW = 320, LH = 240;
static int ui_ok;

#define MAX_LINES 8
static char lines[MAX_LINES][128];
static int n_lines;
static char title[64] = "SMM: WORLD ENGINE";
static char status[128];
static double progress = -1;
static int is_error;

static void ttf_load(const char *path);
static char g_gamedir[900] = ".";

static void ui_init(void)
{
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        printf("[install] no display: %s\n", SDL_GetError());
        return;
    }
    SDL_DisplayMode dm;
    if (SDL_GetDesktopDisplayMode(0, &dm) == 0 && dm.w > 0 && dm.h > 0) {
        W = dm.w;
        H = dm.h;
    }
#ifdef SHOT
    if (getenv("SHOT_W")) { W = atoi(getenv("SHOT_W")); H = atoi(getenv("SHOT_H")); }
#endif
    win = SDL_CreateWindow("smmwe-install", 0, 0, W, H, SDL_WINDOW_FULLSCREEN);
    if (!win) {
        printf("[install] no window: %s\n", SDL_GetError());
        return;
    }
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren)
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) {
        printf("[install] no renderer: %s\n", SDL_GetError());
        return;
    }
    SDL_GetRendererOutputSize(ren, &W, &H);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    LH = 240;
    LW = (int)((long)LH * W / H);
    if (LW < 240) LW = 240;
    SDL_RenderSetLogicalSize(ren, LW, LH);

    /* 8x8 font atlas: 128 glyphs in one row, white on transparent */
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, 128 * 8, 8, 32, SDL_PIXELFORMAT_RGBA8888);
    if (!s) return;
    uint32_t *px = s->pixels;
    int pitch = s->pitch / 4;
    for (int c = 0; c < 128; c++)
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
                px[y * pitch + c * 8 + x] = (font8x8_basic[c][y] & (1 << x)) ? 0xFFFFFFFF : 0x00000000;
    font_tex = SDL_CreateTextureFromSurface(ren, s);
    SDL_FreeSurface(s);
    if (!font_tex) return;
    SDL_SetTextureBlendMode(font_tex, SDL_BLENDMODE_BLEND);
    {
        char fp[1024];
        snprintf(fp, sizeof(fp), "%s/resources/fonts/NewSuperMarioFontU.ttf", g_gamedir);
#ifdef SHOT
        if (getenv("SHOT_FONT")) snprintf(fp, sizeof(fp), "%s", getenv("SHOT_FONT"));
#endif
        ttf_load(fp);
    }

    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i))
            SDL_GameControllerOpen(i);
    ui_ok = 1;
}

/* Drawing happens on a small 240-line canvas scaled up with nearest
   filtering, so everything is chunky pixels at any screen size. */
static double total_mb = 0;

static void col(uint32_t c) { SDL_SetRenderDrawColor(ren, c >> 16, (c >> 8) & 255, c & 255, 255); }
static void box(int x, int y, int w, int h, uint32_t c) { SDL_Rect r = { x, y, w, h }; col(c); SDL_RenderFillRect(ren, &r); }
static void hl(int x0, int x1, int y, uint32_t c) { col(c); SDL_RenderDrawLine(ren, x0, y, x1, y); }
static void vl(int x, int y0, int y1, uint32_t c) { col(c); SDL_RenderDrawLine(ren, x, y0, x, y1); }

#define WHITE  0xfcfcfc
#define YELLOW 0xfcd848
#define BLACK  0x000000
#define CAVE   0x008088
#define CAVEL  0xb4f0ec

static void glyphs(const char *t, int x, int y, uint32_t c)
{
    SDL_SetTextureColorMod(font_tex, c >> 16, (c >> 8) & 255, c & 255);
    for (; *t; t++, x += 8) {
        unsigned char ch = (unsigned char)*t;
        if (ch >= 128) ch = '?';
        SDL_Rect src = { ch * 8, 0, 8, 8 }, dst = { x, y, 8, 8 };
        SDL_RenderCopy(ren, font_tex, &src, &dst);
    }
}

/* names and paths keep their case, everything else is shouted */
static void caps(const char *in, char *out, size_t n)
{
    size_t i = 0;
    while (in[i] && i + 1 < n) {
        size_t e = i;
        while (in[e] && in[e] != ' ') e++;
        int keep = memchr(in + i, '/', e - i) || (e - i > 4 && strncasecmp(in + e - 4, ".apk", 4) == 0);
        for (; i < e && i + 1 < n; i++) out[i] = keep ? in[i] : (char)toupper((unsigned char)in[i]);
        if (in[i] == ' ' && i + 1 < n) { out[i] = ' '; i++; }
    }
    out[i] = 0;
}

/* centered, shadowed, wrapped on spaces; returns the next free line */
/* Title font (resources/fonts/NewSuperMarioFontU.ttf), drawn at screen
   resolution on top of the scaled canvas; the 8x8 font is the fallback. */
#define TEXT_PX 11
#define CAP_PX 9
static stbtt_bakedchar ttf_ch[96];
static SDL_Texture *ttf_tex;
static float ttf_k = 1;            /* screen pixels per canvas pixel */
static float ttf_asc;              /* ascent, screen pixels */

static void ttf_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *data = malloc(n);
    if (!data || fread(data, 1, n, f) != (size_t)n) { fclose(f); free(data); return; }
    fclose(f);
    ttf_k = (float)H / LH;
    float px = TEXT_PX * ttf_k;
    {
        /* size the font by its capital height (fonts differ a lot) */
        stbtt_fontinfo fi;
        int x0, y0, x1, y1;
        if (stbtt_InitFont(&fi, data, stbtt_GetFontOffsetForIndex(data, 0)) &&
            stbtt_GetCodepointBox(&fi, 'H', &x0, &y0, &x1, &y1) && y1 > y0) {
            int asc, dsc, gap;
            stbtt_GetFontVMetrics(&fi, &asc, &dsc, &gap);
            float cap = (float)(y1 - y0) / (asc - dsc);   /* cap height / pixel height */
            px = CAP_PX * ttf_k / cap;
        }
    }
    int tw = 1024, th = 1024;
    unsigned char *a = calloc(tw, th);
    if (a && stbtt_BakeFontBitmap(data, 0, px, a, tw, th, 32, 96, ttf_ch) > 0) {
        stbtt_fontinfo fi;
        if (stbtt_InitFont(&fi, data, stbtt_GetFontOffsetForIndex(data, 0))) {
            int asc, dsc, gap;
            stbtt_GetFontVMetrics(&fi, &asc, &dsc, &gap);
            ttf_asc = asc * stbtt_ScaleForPixelHeight(&fi, px);
        }
        SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormat(0, tw, th, 32, SDL_PIXELFORMAT_RGBA8888);
        if (sf) {
            for (int y = 0; y < th; y++) {
                uint32_t *row = (uint32_t *)((uint8_t *)sf->pixels + y * sf->pitch);
                for (int x = 0; x < tw; x++) row[x] = 0xFFFFFF00u | a[y * tw + x];
            }
            SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
            ttf_tex = SDL_CreateTextureFromSurface(ren, sf);
            SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
            if (ttf_tex) SDL_SetTextureBlendMode(ttf_tex, SDL_BLENDMODE_BLEND);
            SDL_FreeSurface(sf);
        }
    }
    free(a);
    free(data);
}

static float ttf_width(const char *t)       /* canvas pixels */
{
    float w = 0;
    for (; *t; t++) {
        int c = (unsigned char)*t;
        if (c < 32 || c > 127) c = '?';
        w += ttf_ch[c - 32].xadvance;
    }
    return w / ttf_k;
}

static void ttf_draw(const char *t, float x, float y, uint32_t c)
{
    SDL_RenderSetLogicalSize(ren, 0, 0);
    SDL_SetTextureColorMod(ttf_tex, c >> 16, (c >> 8) & 255, c & 255);
    float sx = x * ttf_k, sy = y * ttf_k + ttf_asc;
    for (; *t; t++) {
        int ch = (unsigned char)*t;
        if (ch < 32 || ch > 127) ch = '?';
        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(ttf_ch, 1024, 1024, ch - 32, &sx, &sy, &q, 1);
        SDL_Rect src = { (int)(q.s0 * 1024 + .5f), (int)(q.t0 * 1024 + .5f),
                         (int)((q.s1 - q.s0) * 1024 + .5f), (int)((q.t1 - q.t0) * 1024 + .5f) };
        SDL_Rect dst = { (int)q.x0, (int)q.y0, (int)(q.x1 - q.x0), (int)(q.y1 - q.y0) };
        if (src.w > 0 && src.h > 0) SDL_RenderCopy(ren, ttf_tex, &src, &dst);
    }
    SDL_RenderSetLogicalSize(ren, LW, LH);
}

static int say_ttf(const char *buf, int y, uint32_t c)
{
    float maxw = LW - 16;
    const char *p = buf;
    char line[256];
    while (*p) {
        int len = (int)strlen(p), best = len;
        snprintf(line, sizeof(line), "%.*s", len, p);
        while (ttf_width(line) > maxw && len > 0) {
            len--;
            while (len > 0 && p[len] != ' ') len--;
            if (len == 0) { len = best; break; }
            snprintf(line, sizeof(line), "%.*s", len, p);
        }
        snprintf(line, sizeof(line), "%.*s", len, p);
        float x = (LW - ttf_width(line)) / 2;
        float o = 1.0f / ttf_k * (ttf_k >= 2 ? 2 : 1);
        ttf_draw(line, x + o, y + o, BLACK);
        ttf_draw(line, x, y, c);
        y += TEXT_PX + 4;
        p += len;
        while (*p == ' ') p++;
    }
    return y;
}

static int say(const char *t, int y, uint32_t c)
{
    char buf[256], line[64];
    caps(t, buf, sizeof(buf));
    if (ttf_tex) return say_ttf(buf, y, c);
    int maxc = LW / 8 - 2;
    const char *p = buf;
    while (*p) {
        int len = (int)strlen(p);
        if (len > maxc) {
            len = maxc;
            while (len > 0 && p[len] != ' ') len--;
            if (len == 0) len = maxc;
        }
        snprintf(line, sizeof(line), "%.*s", len, p);
        int x = (LW - (int)strlen(line) * 8) / 2;
        glyphs(line, x + 1, y + 1, BLACK);
        glyphs(line, x, y, c);
        y += 11;
        p += len;
        while (*p == ' ') p++;
    }
    return y;
}


static void underground(void)
{
    box(0, 0, LW, LH, BLACK);
    for (int x = 0; x < LW; x += 16) {
        int ys[3] = { 0, LH - 32, LH - 16 };
        for (int i = 0; i < 3; i++) {
            int y = ys[i];
            box(x, y, 16, 16, CAVE);
            hl(x, x + 15, y, CAVEL);
            hl(x, x + 15, y + 7, BLACK); vl(x + 7, y, y + 7, BLACK);
            vl(x + 3, y + 8, y + 15, BLACK); vl(x + 11, y + 8, y + 15, BLACK);
        }
    }
}

#ifdef SHOT
static void shot(void)
{
    static int n = 0;
    static double last = -1;
    if (progress >= 0 && progress < 1 && progress - last < 0.3 && last >= 0) return;
    last = progress;
    SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_RenderSetLogicalSize(ren, 0, 0);
    SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, sf->pixels, sf->pitch);
    SDL_RenderSetLogicalSize(ren, LW, LH);
    char p[256];
    snprintf(p, sizeof(p), "%s/shot%02d.bmp", getenv("SHOTDIR") ? getenv("SHOTDIR") : ".", n++);
    SDL_SaveBMP(sf, p);
    SDL_FreeSurface(sf);
}
#endif
static void ui_draw(void)
{
    if (!ui_ok) return;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) { }

    if (is_error) {
        underground();
        int y = say("Oops!", 44, WHITE);
        y += 12;
        for (int i = 0; i < n_lines; i++) {
            if (!lines[i][0]) continue;
            y = say(lines[i], y, strchr(lines[i], '/') ? YELLOW : WHITE) + (i == 0 ? 10 : 4);
        }
        if (status[0]) say(status, LH - 64, WHITE);
#ifdef SHOT
        shot();
#endif
        SDL_RenderPresent(ren);
        return;
    }

    underground();
    say(title, 26, WHITE);
    int done = progress >= 1;
    if (progress >= 0 && !done) say("Don't turn off the console", 40, YELLOW);

    int y = 64;
    for (int i = 0; i < n_lines; i++)
        y = say(lines[i], y, i == 0 ? WHITE : YELLOW) + 2;
    if (status[0] && !done) y = say(status, y + 4, WHITE);

    if (progress >= 0) {
        int bw = LW - 80;
        if (bw > 224) bw = 224;
        int bh = 12, x0 = (LW - bw) / 2, by = y + 8 > 112 ? y + 8 : 112;
        double p = progress > 1 ? 1 : progress;
        box(x0 - 1, by - 1, bw + 2, bh + 2, 0x46465a);
        box(x0, by, bw, bh, 0x191928);
        int fw = (int)(bw * p + 0.5);
        if (fw > 0) box(x0, by, fw, bh, 0x3cc850);
        if (!done) {
            char t[64];
            snprintf(t, sizeof(t), "%d%%", (int)(progress * 100 + 0.5));
            say(t, by + 20, WHITE);
            if (total_mb > 0) {
                snprintf(t, sizeof(t), "%.0f / %.0f MB", progress * total_mb, total_mb);
                say(t, by + 32, WHITE);
            }
        }
    }
#ifdef SHOT
    shot();
#endif
    SDL_RenderPresent(ren);
}

static void ui_lines(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    n_lines = 0;
    for (int i = 0; i < n && i < MAX_LINES; i++)
        snprintf(lines[n_lines++], sizeof(lines[0]), "%s", va_arg(ap, const char *));
    va_end(ap);
}

static void ui_status(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(status, sizeof(status), fmt, ap);
    va_end(ap);
    printf("[install] %s\n", status);
    fflush(stdout);
    ui_draw();
}

/* show the screen for `seconds`, or until a button/key is pressed */
static void ui_hold(int seconds)
{
    if (!ui_ok) return;
    Uint32 end = SDL_GetTicks() + seconds * 1000;
    Uint32 armed = SDL_GetTicks() + 800; /* ignore buttons still held */
    while (SDL_GetTicks() < end) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (SDL_GetTicks() < armed) continue;
            if (ev.type == SDL_CONTROLLERBUTTONDOWN || ev.type == SDL_KEYDOWN ||
                ev.type == SDL_JOYBUTTONDOWN || ev.type == SDL_QUIT)
                return;
        }
        ui_draw();
        SDL_Delay(50);
    }
}

static void ui_quit(void)
{
    if (font_tex) SDL_DestroyTexture(font_tex);
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    SDL_Quit();
}

static int fail(const char *l1, const char *l2, const char *st)
{
    is_error = 1;
    progress = -1;
    ui_lines(3, "INSTALLATION FAILED", l1, l2 ? l2 : "");
    ui_status("%s", st ? st : "Press any button");
    printf("[install] ERROR: %s %s\n", l1, l2 ? l2 : "");
    ui_hold(15);
    return 1;
}

/* ----------------------------------------------------------- install -- */
static int ends_with_apk(const char *n)
{
    size_t l = strlen(n);
    return l > 4 && strcasecmp(n + l - 4, ".apk") == 0;
}

static int find_apk(const char *dir, char *out, size_t outsz)
{
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    char best[512] = "";
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.' || !ends_with_apk(e->d_name)) continue;
        if (!best[0] || strcmp(e->d_name, best) < 0)
            snprintf(best, sizeof(best), "%s", e->d_name);
    }
    closedir(d);
    if (!best[0]) return 0;
    snprintf(out, outsz, "%s/%s", dir, best);
    return 1;
}

static int wanted(const char *n)
{
    return strncmp(n, "assets/", 7) == 0 || strncmp(n, "lib/arm64-v8a/", 14) == 0;
}

static int ends_with(const char *n, const char *ext)
{
    size_t l = strlen(n), e = strlen(ext);
    return l > e && strcasecmp(n + l - e, ext) == 0;
}

static int store_plain(const char *n)
{
    /* big files read at every launch: keep them uncompressed.
       Streamed sounds (.ogg) MUST be stored: the runner reads them from the
       APK at an offset, a compressed entry plays as silence. */
    return strcmp(n, "assets/game.droid") == 0 || strcmp(n, "lib/arm64-v8a/libyoyo.so") == 0 ||
           (strncmp(n, "assets/audiogroup", 17) == 0) ||
           ends_with(n, ".ogg") || ends_with(n, ".wav") || ends_with(n, ".mp3");
}

/* 1 if smmwe.apk was built by an older installer (compressed sounds) */
static int apk_outdated(const char *path)
{
    int err = 0;
    zip_t *z = zip_open(path, ZIP_RDONLY, &err);
    if (!z) return 1;
    int bad = 0;
    zip_int64_t n = zip_get_num_entries(z, 0);
    for (zip_int64_t i = 0; i < n && !bad; i++) {
        zip_stat_t st;
        const char *name = zip_get_name(z, i, 0);
        if (!name || !store_plain(name)) continue;
        if (zip_stat_index(z, i, 0, &st) == 0 && (st.valid & ZIP_STAT_COMP_METHOD) &&
            st.comp_method != ZIP_CM_STORE)
            bad = 1;
    }
    zip_close(z);
    return bad;
}

static void on_progress(zip_t *z, double p, void *ud)
{
    (void)z; (void)ud;
    progress = p;
    ui_draw();
}

/* ------------------------------------------------------------- mods -- */
/* Music / sound mods: .ogg files in "Import Levels/Mods" (also inside .zip
   files there) replace the game's sounds with the same name. They are
   built into data/smmwe.apk; data/mods.stamp remembers which mods are in. */
#define MODS_DIR "Import Levels/Mods and Textures"
#define MAX_MODS 4096
typedef struct {
    char key[128];
    char path[1024];
    zip_t *z;
    zip_uint64_t idx;
} ModFile;
static ModFile mods[MAX_MODS];
static int n_mods = 0;
static zip_t *mod_zips[256];
static int n_mod_zips = 0;
static uint64_t mods_hash = 1469598103934665603ULL;

static void hash_str(const char *t)
{
    for (; *t; t++) { mods_hash ^= (unsigned char)*t; mods_hash *= 1099511628211ULL; }
}

static void mod_key(const char *name, char *key, size_t n)
{
    const char *b = strrchr(name, '/');
    b = b ? b + 1 : name;
    size_t i = 0;
    for (; b[i] && i + 1 < n; i++) key[i] = (char)tolower((unsigned char)b[i]);
    key[i] = 0;
}

static ModFile *find_mod(const char *key)
{
    for (int i = 0; i < n_mods; i++)
        if (strcmp(mods[i].key, key) == 0) return &mods[i];
    return NULL;
}

static void add_mod(const char *name, const char *path, zip_t *z, zip_uint64_t idx)
{
    char key[128];
    mod_key(name, key, sizeof(key));
    if (n_mods >= MAX_MODS || find_mod(key)) return;
    ModFile *m = &mods[n_mods++];
    snprintf(m->key, sizeof(m->key), "%s", key);
    snprintf(m->path, sizeof(m->path), "%s", path);
    m->z = z;
    m->idx = idx;
}

static void scan_mods(const char *dir, int depth)
{
    DIR *d = opendir(dir);
    if (!d) return;
    char *names[1024];
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < 1024)
        if (e->d_name[0] != '.') names[n++] = strdup(e->d_name);
    closedir(d);
    for (int i = 0; i < n; i++)            /* sorted: same result every time */
        for (int j = i + 1; j < n; j++)
            if (strcmp(names[j], names[i]) < 0) { char *t = names[i]; names[i] = names[j]; names[j] = t; }
    for (int i = 0; i < n; i++) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        struct stat st;
        if (stat(path, &st) != 0) { free(names[i]); continue; }
        if (S_ISDIR(st.st_mode)) {
            if (depth < 4) scan_mods(path, depth + 1);
        } else if (ends_with(names[i], ".ogg") || ends_with(names[i], ".zip")) {
            char sig[1300];
            snprintf(sig, sizeof(sig), "%s|%lld|%lld\n", path, (long long)st.st_size, (long long)st.st_mtime);
            hash_str(sig);
            if (ends_with(names[i], ".ogg")) {
                add_mod(names[i], path, NULL, 0);
            } else if (n_mod_zips < 256) {
                int err = 0;
                zip_t *z = zip_open(path, ZIP_RDONLY, &err);
                if (z) {
                    int used = 0;
                    zip_int64_t ne = zip_get_num_entries(z, 0);
                    for (zip_int64_t k = 0; k < ne; k++) {
                        const char *en = zip_get_name(z, k, 0);
                        if (en && ends_with(en, ".ogg")) { add_mod(en, path, z, k); used = 1; }
                    }
                    if (used) mod_zips[n_mod_zips++] = z;
                    else zip_close(z);
                }
            }
        }
        free(names[i]);
    }
}

static void mods_stamp(char *out, size_t n)
{
    if (n_mods == 0) snprintf(out, n, "none");
    else snprintf(out, n, "%d-%016llx", n_mods, (unsigned long long)mods_hash);
}

static void read_stamp(const char *gamedir, char *out, size_t n)
{
    char p[1100];
    snprintf(p, sizeof(p), "%s/data/mods.stamp", gamedir);
    FILE *f = fopen(p, "r");
    snprintf(out, n, "none");
    if (!f) return;
    if (fgets(out, (int)n, f)) out[strcspn(out, "\r\n")] = 0;
    fclose(f);
}

static void write_stamp(const char *gamedir)
{
    char p[1100], st[64];
    snprintf(p, sizeof(p), "%s/data/mods.stamp", gamedir);
    mods_stamp(st, sizeof(st));
    FILE *f = fopen(p, "w");
    if (f) { fprintf(f, "%s\n", st); fclose(f); }
}

static int install(const char *gamedir, int from_original)
{
    char dataDir[900], apk[1024], out_apk[1024], tmp_apk[1100], orig_dir[1024], orig_dst[2048];
    snprintf(dataDir, sizeof(dataDir), "%s/" SETUP_DIR, gamedir);
    snprintf(out_apk, sizeof(out_apk), "%s/" OUT_APK, gamedir);
    snprintf(tmp_apk, sizeof(tmp_apk), "%s/" OUT_APK ".part", gamedir);
    snprintf(orig_dir, sizeof(orig_dir), "%s/" INSTALLED_DIR, dataDir);
    {
        char dd[1024];
        snprintf(dd, sizeof(dd), "%s/data", gamedir);
        mkdir(dd, 0755);
    }

    if (!find_apk(from_original ? orig_dir : dataDir, apk, sizeof(apk)))
        return 2;

    const char *base = strrchr(apk, '/') ? strrchr(apk, '/') + 1 : apk;
    char shown[64];
    snprintf(shown, sizeof(shown), "%.60s", base);
    if (n_mods) {
        char mline[64];
        snprintf(mline, sizeof(mline), "%d mod sound%s", n_mods, n_mods == 1 ? "" : "s");
        ui_lines(3, from_original ? "Applying mods" : "Installing game data", shown, mline);
    } else {
        ui_lines(2, from_original ? "Updating game data" : "Installing game data", shown);
    }
    progress = 0;
    ui_status("Checking the APK...");

    int err = 0;
    zip_t *src = zip_open(apk, ZIP_RDONLY, &err);
    if (!src) {
        int sys_errno = errno;
        zip_error_t ze;
        zip_error_init_with_code(&ze, err);
        char why[128];
        snprintf(why, sizeof(why), "%s", zip_error_strerror(&ze));
        zip_error_fini(&ze);
        struct stat st;
        long long mb = (stat(apk, &st) == 0) ? (long long)(st.st_size >> 20) : -1;
        fprintf(stderr, "[install] zip_open('%s') failed: code %d (%s), errno %d (%s), size %lld MB\n",
                apk, err, why, sys_errno, strerror(sys_errno), mb);
        char l2[128];
        if (err == ZIP_ER_OPEN || err == ZIP_ER_READ || err == ZIP_ER_SEEK) {
            snprintf(l2, sizeof(l2), "Read error: %s", why);
            return fail("Cannot read the APK file", l2, "Copy it again; check the SD card");
        }
        snprintf(l2, sizeof(l2), "%s (%lld MB)", why, mb);
        return fail("APK incomplete or damaged:", l2, "Download/copy the APK again");
    }

    int has_droid = 0, has_yoyo = 0, has_v7 = 0;
    zip_uint64_t total = 0;
    zip_int64_t n = zip_get_num_entries(src, 0);
    for (zip_int64_t i = 0; i < n; i++) {
        const char *name = zip_get_name(src, i, 0);
        if (!name) continue;
        if (strcmp(name, "assets/game.droid") == 0) has_droid = 1;
        if (strcmp(name, "lib/arm64-v8a/libyoyo.so") == 0) has_yoyo = 1;
        if (strcmp(name, "lib/armeabi-v7a/libyoyo.so") == 0) has_v7 = 1;
        zip_stat_t st;
        if (wanted(name) && zip_stat_index(src, i, 0, &st) == 0 && (st.valid & ZIP_STAT_SIZE))
            total += st.size;
    }
    total_mb = total / 1048576.0;
    if (!has_droid) {
        zip_close(src);
        return fail("This is not a GameMaker APK", "(assets/game.droid missing)", NULL);
    }
    if (!has_yoyo) {
        zip_close(src);
        return fail(has_v7 ? "This APK is 32-bit only" : "Wrong APK build",
                    "(lib/arm64-v8a/libyoyo.so missing)", NULL);
    }

    struct statvfs vfs;
    if (statvfs(gamedir, &vfs) == 0) {
        unsigned long long freeb = (unsigned long long)vfs.f_bavail * vfs.f_frsize;
        if (freeb < total + 32ull * 1024 * 1024) {
            char need[64];
            snprintf(need, sizeof(need), "Need %llu MB free, have %llu MB", (unsigned long long)(total >> 20) + 32, freeb >> 20);
            zip_close(src);
            return fail("Not enough free space", need, NULL);
        }
    }

    unlink(tmp_apk);
    zip_t *dst = zip_open(tmp_apk, ZIP_CREATE | ZIP_TRUNCATE, &err);
    if (!dst) {
        zip_close(src);
        return fail("Cannot create", "ports/smmwe/" OUT_APK, "Is the SD card read-only?");
    }

    ui_status("Getting ready...");
    int applied = 0;
    for (zip_int64_t i = 0; i < n; i++) {
        const char *name = zip_get_name(src, i, 0);
        if (!name || !wanted(name)) continue;
        size_t l = strlen(name);
        if (l && name[l - 1] == '/') continue;
        if (n_mods && strncmp(name, "assets/", 7) == 0 && ends_with(name, ".ogg")) {
            char key[128];
            mod_key(name, key, sizeof(key));
            ModFile *m = find_mod(key);
            if (m) {
                zip_source_t *ms = m->z ? zip_source_zip_file(dst, m->z, m->idx, 0, 0, -1, NULL)
                                        : zip_source_file(dst, m->path, 0, -1);
                if (ms) {
                    zip_int64_t mi = zip_file_add(dst, name, ms, ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE);
                    if (mi >= 0) {
                        zip_set_file_compression(dst, (zip_uint64_t)mi, ZIP_CM_STORE, 0);
                        applied++;
                        continue;
                    }
                    zip_source_free(ms);
                }
                fprintf(stderr, "[install] mod %s: cannot use %s\n", key, m->path);
            }
        }
        zip_stat_t est;
        int plain = store_plain(name) ||
                    (zip_stat_index(src, i, 0, &est) == 0 && (est.valid & ZIP_STAT_COMP_METHOD) &&
                     est.comp_method == ZIP_CM_STORE);
        /* plain: decompress and store; otherwise copy the compressed data as is */
        zip_source_t *s = zip_source_zip_file(dst, src, (zip_uint64_t)i, plain ? 0 : ZIP_FL_COMPRESSED, 0, -1, NULL);
        if (!s) goto fail_zip;
        zip_int64_t idx = zip_file_add(dst, name, s, ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE);
        if (idx < 0) { zip_source_free(s); goto fail_zip; }
        if (plain) zip_set_file_compression(dst, (zip_uint64_t)idx, ZIP_CM_STORE, 0);
    }

    ui_status("Copying game data...");
    zip_register_progress_callback_with_state(dst, 0.005, on_progress, NULL, NULL);
    if (zip_close(dst) != 0) {
        fprintf(stderr, "[install] zip_close: %s\n", zip_strerror(dst));
        zip_discard(dst);
        zip_close(src);
        unlink(tmp_apk);
        return fail("Writing smmwe.apk failed", "Check free space and the SD card", NULL);
    }
    zip_close(src);
    for (int i = 0; i < n_mod_zips; i++) zip_close(mod_zips[i]);
    n_mod_zips = 0;
    if (n_mods) printf("[install] mods: %d of %d sounds replaced\n", applied, n_mods);
    write_stamp(gamedir);
    sync();

    progress = 1;
    ui_status("Almost done...");
    if (rename(tmp_apk, out_apk) != 0) {
        unlink(tmp_apk);
        return fail("Cannot rename smmwe.apk", strerror(errno), NULL);
    }
    mkdir(orig_dir, 0755);
    snprintf(orig_dst, sizeof(orig_dst), "%s/%s", orig_dir, base);
    if (!from_original) {
        /* keep only the APK just installed in installed/ (saves space) */
        DIR *od = opendir(orig_dir);
        if (od) {
            struct dirent *e;
            while ((e = readdir(od))) {
                if (e->d_name[0] == '.' || !ends_with_apk(e->d_name) || strcmp(e->d_name, base) == 0) continue;
                char old[2048];
                snprintf(old, sizeof(old), "%s/%s", orig_dir, e->d_name);
                unlink(old);
            }
            closedir(od);
        }
    }
    if (!from_original && rename(apk, orig_dst) != 0) {
        /* keep going: only the next launch would rebuild */
        fprintf(stderr, "[install] cannot move the original APK: %s\n", strerror(errno));
        unlink(apk);
    }

    char marker[1100];
    snprintf(marker, sizeof(marker), "%s/.installed", dataDir);
    FILE *m = fopen(marker, "w");
    if (m) { fprintf(m, "%s\n", base); fclose(m); }
    sync();

    ui_lines(2, "All done!", "Starting the game...");
    ui_status("Done");
    if (ui_ok) SDL_Delay(1200);
    return 0;

fail_zip:
    fprintf(stderr, "[install] %s\n", zip_strerror(dst));
    zip_discard(dst);
    zip_close(src);
    unlink(tmp_apk);
    return fail("Reading the APK failed", shown, "File damaged?");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <gamedir> [--message TEXT]\n", argv[0]);
        return 1;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    snprintf(g_gamedir, sizeof(g_gamedir), "%s", argv[1]);

    if (argc >= 4 && strcmp(argv[2], "--message") == 0) {
        ui_init();
        is_error = 1;
        char l1[128] = "", l2[128] = "";
        const char *msg = argv[3];
        const char *split = strchr(msg, '\n');
        if (split) {
            snprintf(l1, sizeof(l1), "%.*s", (int)(split - msg), msg);
            snprintf(l2, sizeof(l2), "%s", split + 1);
        } else {
            snprintf(l1, sizeof(l1), "%s", msg);
        }
        ui_lines(3, "THE GAME CANNOT START", l1, l2);
        ui_status("Press any button");
        ui_hold(15);
        ui_quit();
        return 0;
    }

    char dataDir[1024], apk[1024], origDir[1100], outApk[1100];
    snprintf(dataDir, sizeof(dataDir), "%s/" SETUP_DIR, argv[1]);
    snprintf(origDir, sizeof(origDir), "%s/" INSTALLED_DIR, dataDir);
    snprintf(outApk, sizeof(outApk), "%s/" OUT_APK, argv[1]);
    int from_original = 0;
    char modsDir[1200], old_stamp[64], new_stamp[64];
    snprintf(modsDir, sizeof(modsDir), "%s/" MODS_DIR, argv[1]);
    scan_mods(modsDir, 0);
    mods_stamp(new_stamp, sizeof(new_stamp));
    read_stamp(argv[1], old_stamp, sizeof(old_stamp));
    struct stat ost;
    int mods_changed = strcmp(old_stamp, new_stamp) != 0 && stat(outApk, &ost) == 0;
    if (!find_apk(dataDir, apk, sizeof(apk)) && mods_changed) {
        if (!find_apk(origDir, apk, sizeof(apk))) {
            fprintf(stderr, "[install] mods changed but Setup/installed is empty\n");
            ui_init();
            is_error = 1;
            ui_lines(3, "CANNOT APPLY THE MODS", "Put the SMM:WE APK in ports/smmwe/Setup/",
                     "again, then start the port.");
            ui_status("Press any button to play anyway");
            ui_hold(10);
            ui_quit();
            return 2;
        }
        fprintf(stderr, "[install] mods changed (%s -> %s), rebuilding\n", old_stamp, new_stamp);
        ui_init();
        int rc = install(argv[1], 1);
        ui_quit();
        return rc;
    }
    if (!find_apk(dataDir, apk, sizeof(apk))) {
        /* smmwe.apk from an older installer: rebuild it from Setup/installed */
        struct stat st;
        if (stat(outApk, &st) != 0 || !apk_outdated(outApk))
            return 2;
        if (!find_apk(origDir, apk, sizeof(apk))) {
            fprintf(stderr, "[install] smmwe.apk is outdated (no sound) and Setup/installed is empty\n");
            ui_init();
            is_error = 1;
            ui_lines(3, "GAME DATA NEEDS AN UPDATE", "Put the SMM:WE APK in ports/smmwe/Setup/",
                     "again to get the game sounds.");
            ui_status("Press any button to play anyway");
            ui_hold(10);
            ui_quit();
            return 2;
        }
        fprintf(stderr, "[install] smmwe.apk is outdated, rebuilding from Setup/installed\n");
        from_original = 1;
    }

    ui_init();
    int rc = install(argv[1], from_original);
    ui_quit();
    return rc;
}
