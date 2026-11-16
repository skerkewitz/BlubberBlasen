/*
 * Platform layer for stage 1 (PC). The ONLY module allowed to use raylib.
 * Responsibilities: window, input -> SNES joypad word, fixed 60 Hz tick,
 * loading the 24-bit PNGs into "VRAM", rendering two tilemaps and the
 * sprite table into a 256x224 framebuffer, integer scaling, sound effects,
 * and keeping the save RAM in a file.
 *
 * Graphics convention for stage 1: 24-bit PNG, colour (255,0,255) is transparent.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"
#include "rlgl.h"       /* rlGetShaderIdDefault: detect a failed shader compile */
#include "game/game.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif
#ifdef BB_REPLAY
#include <time.h>
#include "replay/replay.h"
#endif
#ifdef BB_DEBUG_UI
#include "platform/debug_ui.h"
#endif

#define WINDOW_SCALE   3
#define TICK_HZ        60
#define MAX_CATCHUP    4      /* max game ticks per rendered frame */
#define STICK_DEADZONE 0.5f
#define TILE           8
#define CHR_COLS       32
#define SAVE_FILE      "blubberblasen.sav"

#ifndef BB_ASSET_DIR
#define BB_ASSET_DIR "assets"
#endif

static char      asset_root[1024];
static Texture2D chr_bg;       /* 8x8 background tiles */
static Texture2D chr_sprites;  /* 16x16 sprite cells   */
static Texture2D chr_sprites_p2;  /* the same, player colours swapped (sprite palette PAL_P2) */
static Sound     sounds[SFX_COUNT];
static int       sound_loaded[SFX_COUNT];
static Music     music[MUSIC_COUNT];
static int       music_loaded[MUSIC_COUNT];
static int       music_playing = MUSIC_NONE;
static long      ticks_total;   /* game ticks run so far */

/* Temporary tracks (royalty free, see assets/README.md); swap the files here. */
static const char *const music_files[MUSIC_COUNT] = {
    NULL,
    "music/zephiramusic-lofi-gaming-606302.mp3",                         /* MUSIC_TITLE */
    "music/bouncyrunner-retro-games-flower-hill-stage-two-400495.mp3"     /* MUSIC_GAME */
};
#define MUSIC_VOLUME 0.5f

static const char *const sfx_files[SFX_COUNT] = {
    "jump", "shoot", "pop", "trap", "kill", "gem", "death",
    "start", "clear", "hurry", "gameover", "menu", "pause", "extra",
    "dash", "UhHelpMe", "vacuum", "multikill"
};

/* Tints for the 8 "palettes" of stage 1. */
static const Color palette[8] = {
    { 255, 255, 255, 255 },   /* PAL_NONE   */
    { 255,  80,  80, 255 },   /* PAL_RED    */
    { 110, 255, 110, 255 },   /* PAL_GREEN  */
    { 110, 220, 255, 255 },   /* PAL_CYAN   */
    { 255, 230,  80, 255 },   /* PAL_YELLOW */
    { 255, 130, 210, 255 },   /* PAL_PINK   */
    { 255, 160,  60, 255 },   /* PAL_ORANGE */
    { 150, 150, 150, 255 },   /* PAL_GREY   */
};

/* --- Assets --------------------------------------------------------------- */
static void find_asset_root(void)
{
    const char *app = TextFormat("%sassets", GetApplicationDirectory());
    if (DirectoryExists(app)) {
        snprintf(asset_root, sizeof asset_root, "%s", app);
    } else {
        snprintf(asset_root, sizeof asset_root, "%s", BB_ASSET_DIR);
    }
    TraceLog(LOG_INFO, "ASSETS: using %s", asset_root);
}

static const char *asset_path(const char *rel)
{
    return TextFormat("%s/%s", asset_root, rel);
}

/* Load a PNG as RGBA with the magenta colour key turned transparent.
 * A missing file gives a transparent image of the given size. */
static Image load_keyed_image(const char *rel, int fallback_w, int fallback_h)
{
    const char *path = asset_path(rel);
    Image img;

    if (!FileExists(path)) {
        TraceLog(LOG_WARNING, "ASSETS: missing %s", path);
        return GenImageColor(fallback_w, fallback_h, BLANK);
    }
    img = LoadImage(path);
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    ImageColorReplace(&img, (Color){ 255, 0, 255, 255 }, BLANK);
    return img;
}

/* Copy one 8x8 tile from src into the CHR image at tile index dst_tile. */
static void put_tile(Image *chr, const Image *src, int sx, int sy, int dst_tile)
{
    Rectangle s = { (float)sx, (float)sy, TILE, TILE };
    Rectangle d = { (float)((dst_tile % CHR_COLS) * TILE), (float)((dst_tile / CHR_COLS) * TILE), TILE, TILE };
    ImageDraw(chr, *src, s, d, WHITE);
}

/* Build the background "VRAM": font, tilesets and title logo in one texture,
 * at the tile numbers given by the CHR_* constants in game.h. */
static Texture2D build_bg_chr(void)
{
    Image chr = GenImageColor(CHR_COLS * TILE, CHR_COLS * TILE, BLANK);
    Image img;
    Texture2D tex;
    unsigned n;

    img = load_keyed_image("gfx/font.png", 256, 16);
    ImageDraw(&chr, img, (Rectangle){ 0, 0, (float)img.width, (float)img.height },
              (Rectangle){ 0, 0, (float)img.width, (float)img.height }, WHITE);
    UnloadImage(img);

    for (n = 0; n < CHR_TILESET_COUNT; n++) {
        int k;
        img = load_keyed_image(TextFormat("gfx/tileset%02u.png", n + 1), 48, 24);
        for (k = 0; k < 18 && k < (int)CHR_TILESET_STRIDE; k++) {
            put_tile(&chr, &img, (k % 6) * TILE, (k / 6) * TILE, (int)(CHR_TILESET_BASE + n * CHR_TILESET_STRIDE) + k);
        }
        UnloadImage(img);
    }

    img = load_keyed_image("gfx/title.png", 256, 176);
    ImageDraw(&chr, img, (Rectangle){ 0, 0, (float)img.width, (float)img.height },
              (Rectangle){ 0, (float)((CHR_TITLE_BASE / CHR_COLS) * TILE), (float)img.width, (float)img.height }, WHITE);
    UnloadImage(img);

    tex = LoadTextureFromImage(chr);
    UnloadImage(chr);
    return tex;
}

static void load_assets(void)
{
    Image img;
    int i;

    find_asset_root();
    chr_bg = build_bg_chr();
    img = load_keyed_image("gfx/sprites.png", 256, 256);
    chr_sprites = LoadTextureFromImage(img);
    /* Player 2: the player's green ramp becomes a blue one (a palette swap,
     * as on the SNES; only player cells are drawn with PAL_P2). */
    {
        static const Color from[4] = { { 16, 64, 24, 255 }, { 32, 140, 56, 255 }, { 64, 200, 80, 255 }, { 150, 240, 150, 255 } };
        static const Color to[4]   = { { 16, 32, 88, 255 }, { 40, 80, 190, 255 }, { 70, 130, 240, 255 }, { 160, 200, 255, 255 } };
        for (i = 0; i < 4; i++) {
            ImageColorReplace(&img, from[i], to[i]);
        }
        chr_sprites_p2 = LoadTextureFromImage(img);
    }
    UnloadImage(img);
    for (i = 0; i < SFX_COUNT; i++) {
        sound_loaded[i] = 0;
    }
    for (i = 0; i < MUSIC_COUNT; i++) {
        music_loaded[i] = 0;
    }
}

/* Opens the audio device and loads sounds and music. On the PC at start;
 * in the browser only after the first click or key press, because browsers
 * keep an audio device created earlier silent (autoplay rule). */
static int audio_started;

static void start_audio(void)
{
    int i;

    audio_started = 1;
    music_playing = MUSIC_NONE;   /* so the track the game wants starts now */
    InitAudioDevice();
    for (i = 0; i < SFX_COUNT; i++) {
        const char *path = asset_path(TextFormat("sfx/%s.wav", sfx_files[i]));
        sound_loaded[i] = 0;
        if (IsAudioDeviceReady() && FileExists(path)) {
            sounds[i] = LoadSound(path);
            sound_loaded[i] = 1;
        }
    }

    for (i = 0; i < MUSIC_COUNT; i++) {
        music_loaded[i] = 0;
        if (music_files[i] && IsAudioDeviceReady()) {
            const char *path = asset_path(music_files[i]);
            if (FileExists(path)) {
                music[i] = LoadMusicStream(path);
                music[i].looping = true;
                SetMusicVolume(music[i], MUSIC_VOLUME);
                music_loaded[i] = 1;
            } else {
                TraceLog(LOG_WARNING, "ASSETS: missing %s", path);
            }
        }
    }
}

/* Switch tracks when the game asks for another one, and keep the stream fed. */
static void update_music(int wanted)
{
    if (wanted != music_playing) {
        if (music_playing != MUSIC_NONE && music_loaded[music_playing]) {
            StopMusicStream(music[music_playing]);
        }
        music_playing = wanted;
        if (wanted != MUSIC_NONE && music_loaded[wanted]) {
            PlayMusicStream(music[wanted]);
        }
    }
    if (music_playing != MUSIC_NONE && music_loaded[music_playing]) {
        UpdateMusicStream(music[music_playing]);
    }
}

static void unload_assets(void)
{
    int i;
    for (i = 0; i < MUSIC_COUNT; i++) {
        if (music_loaded[i]) {
            UnloadMusicStream(music[i]);
        }
    }
    for (i = 0; i < SFX_COUNT; i++) {
        if (sound_loaded[i]) {
            UnloadSound(sounds[i]);
        }
    }
    UnloadTexture(chr_sprites);
    UnloadTexture(chr_sprites_p2);
    UnloadTexture(chr_bg);
}

/* --- Save RAM -----------------------------------------------------------------
 * PC: a file next to the executable. Browser: the same file in an IndexedDB
 * backed folder (/save), loaded once before the game starts and written
 * back after every change. */
#ifdef __EMSCRIPTEN__
static int save_ready;

EMSCRIPTEN_KEEPALIVE void bb_save_ready(void)
{
    save_ready = 1;
}

static void save_mount(void)
{
    EM_ASM({
        try {
            FS.mkdir('/save');
            FS.mount(IDBFS, {}, '/save');
            console.log('SAVE: loading from IndexedDB');
            FS.syncfs(true, function (err) {
                console.log(err ? 'SAVE: loaded with error ' + err : 'SAVE: loaded');
                _bb_save_ready();
            });
        } catch (e) {
            console.log('SAVE: not available (' + e + '), playing without saving');
            _bb_save_ready();
        }
    });
}

static void save_flush(void)
{
    EM_ASM({ FS.syncfs(false, function (err) {}); });
}

static const char *save_path(void)
{
    return "/save/" SAVE_FILE;
}
#else
static const char *save_path(void)
{
    return TextFormat("%s%s", GetApplicationDirectory(), SAVE_FILE);
}
#endif

static void sram_load(void)
{
    int size = 0;
    unsigned char *data;
    if (!FileExists(save_path())) {
        return;   /* first start: the game creates a default table */
    }
    data = LoadFileData(save_path(), &size);
    if (data) {
        /* older, shorter save files keep their high scores */
        if (size > 0 && size <= SRAM_SIZE) {
            memcpy(game_sram(), data, (size_t)size);
        }
        UnloadFileData(data);
    }
}

static void sram_save(void)
{
    if (!SaveFileData(save_path(), game_sram(), SRAM_SIZE)) {
        TraceLog(LOG_WARNING, "SRAM: could not write %s", save_path());
    }
#ifdef __EMSCRIPTEN__
    save_flush();
#endif
}

/* --- Input ---------------------------------------------------------------- */
/* Keyboard mapping as documented in README.md (Controls). */
static uint16_t read_keyboard(int player)
{
    uint16_t pad = 0;
    if (player == 0) {
        if (IsKeyDown(KEY_W))            pad |= PAD_UP;
        if (IsKeyDown(KEY_S))            pad |= PAD_DOWN;
        if (IsKeyDown(KEY_A))            pad |= PAD_LEFT;
        if (IsKeyDown(KEY_D))            pad |= PAD_RIGHT;
        if (IsKeyDown(KEY_K))            pad |= PAD_B;   /* jump   */
        if (IsKeyDown(KEY_J))            pad |= PAD_Y;   /* bubble */
        if (IsKeyDown(KEY_C))            pad |= PAD_A;
        if (IsKeyDown(KEY_U))            pad |= PAD_A;   /* vacuum (Plus) */
        if (IsKeyDown(KEY_H))            pad |= PAD_X;   /* push bubble (Plus) */
        if (IsKeyDown(KEY_V))            pad |= PAD_X;
        if (IsKeyDown(KEY_Q))            pad |= PAD_L;   /* dash left (Plus)  */
        if (IsKeyDown(KEY_E))            pad |= PAD_R;   /* dash right (Plus) */
        if (IsKeyDown(KEY_ENTER))        pad |= PAD_START;
        /* Either Shift: the browser build (Emscripten's GLFW) reports both as Left Shift. */
        if (IsKeyDown(KEY_RIGHT_SHIFT) || IsKeyDown(KEY_LEFT_SHIFT)) pad |= PAD_SELECT;
    } else {
        if (IsKeyDown(KEY_UP))           pad |= PAD_UP;
        if (IsKeyDown(KEY_DOWN))         pad |= PAD_DOWN;
        if (IsKeyDown(KEY_LEFT))         pad |= PAD_LEFT;
        if (IsKeyDown(KEY_RIGHT))        pad |= PAD_RIGHT;
        if (IsKeyDown(KEY_PERIOD))       pad |= PAD_B;   /* jump   */
        if (IsKeyDown(KEY_COMMA))        pad |= PAD_Y;   /* bubble */
        if (IsKeyDown(KEY_M))            pad |= PAD_X;   /* push bubble (Plus) */
        if (IsKeyDown(KEY_N))            pad |= PAD_A;   /* vacuum (Plus) */
        if (IsKeyDown(KEY_L))            pad |= PAD_L;   /* dash left (Plus)  */
        if (IsKeyDown(KEY_SLASH))        pad |= PAD_R;   /* dash right (Plus) */
        if (IsKeyDown(KEY_RIGHT_CONTROL)) pad |= PAD_START;
    }
    return pad;
}

static uint16_t read_gamepad(int index)
{
    uint16_t pad = 0;
    float ax, ay;

    if (!IsGamepadAvailable(index)) {
        return 0;
    }

    if (IsGamepadButtonDown(index, GAMEPAD_BUTTON_LEFT_FACE_UP))     pad |= PAD_UP;
    if (IsGamepadButtonDown(index, GAMEPAD_BUTTON_LEFT_FACE_DOWN))   pad |= PAD_DOWN;
    if (IsGamepadButtonDown(index, GAMEPAD_BUTTON_LEFT_FACE_LEFT))   pad |= PAD_LEFT;
    if (IsGamepadButtonDown(index, GAMEPAD_BUTTON_LEFT_FACE_RIGHT))  pad |= PAD_RIGHT;
    if (IsGamepadButtonDown(index, GAMEPAD_BUTTON_RIGHT_FACE_DOWN))  pad |= PAD_B;
    if (IsGamepadButtonDown(index, GAMEPAD_BUTTON_RIGHT_FACE_LEFT))  pad |= PAD_Y;
    if (IsGamepadButtonDown(index, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) pad |= PAD_A;
    if (IsGamepadButtonDown(index, GAMEPAD_BUTTON_RIGHT_FACE_UP))    pad |= PAD_X;
    /* The shoulder buttons are the SNES L and R: LB dashes left, RB right. */
    if (IsGamepadButtonDown(index, GAMEPAD_BUTTON_LEFT_TRIGGER_1))   pad |= PAD_L;
    if (IsGamepadButtonDown(index, GAMEPAD_BUTTON_RIGHT_TRIGGER_1))  pad |= PAD_R;
    if (IsGamepadButtonDown(index, GAMEPAD_BUTTON_MIDDLE_LEFT))      pad |= PAD_SELECT;
    if (IsGamepadButtonDown(index, GAMEPAD_BUTTON_MIDDLE_RIGHT))     pad |= PAD_START;

    ax = GetGamepadAxisMovement(index, GAMEPAD_AXIS_LEFT_X);
    ay = GetGamepadAxisMovement(index, GAMEPAD_AXIS_LEFT_Y);
    if (ax < -STICK_DEADZONE) pad |= PAD_LEFT;
    if (ax >  STICK_DEADZONE) pad |= PAD_RIGHT;
    if (ay < -STICK_DEADZONE) pad |= PAD_UP;
    if (ay >  STICK_DEADZONE) pad |= PAD_DOWN;

    return pad;
}

/* --- Rendering ------------------------------------------------------------ */
static void draw_cell(Texture2D tex, int index, int size, int x, int y, int flip_x, int flip_y, Color tint)
{
    int cols = tex.width / size;
    Rectangle src;
    src.x = (float)((index % cols) * size);
    src.y = (float)((index / cols) * size);
    src.width  = (float)(flip_x ? -size : size);
    src.height = (float)(flip_y ? -size : size);
    DrawTextureRec(tex, src, (Vector2){ (float)x, (float)y }, tint);
}

/* Draw a tilemap of `rows` rows, vertically scrolled by scroll_y pixels
 * (wrapping around), only below screen line window_top. */
static void draw_bg(const uint16_t *map, int rows, int scroll_y, int window_top)
{
    int r, c;
    int first_row = scroll_y / TILE;
    int fine = scroll_y % TILE;

    BeginScissorMode(0, window_top, SCREEN_W, SCREEN_H - window_top);
    for (r = 0; r <= BG_H; r++) {
        const uint16_t *line = &map[((first_row + r) % rows) * BG_W];
        int y = r * TILE - fine;
        for (c = 0; c < BG_W; c++) {
            uint16_t e = line[c];
            int tile = e & BG_TILE_MASK;
            if (tile == 0) {
                continue;
            }
            draw_cell(chr_bg, tile, TILE, c * TILE, y,
                      (e & BG_FLIP_X) != 0, (e & BG_FLIP_Y) != 0, palette[(e >> BG_PAL_SHIFT) & 7]);
        }
    }
    EndScissorMode();
}

/* Fill a window's span on every scanline with colour c in the current blend mode. */
static void draw_window_spans(const ColorWindow *w, Color c)
{
    int y;
    for (y = 0; y < SCREEN_H; y++) {
        if (w->left[y] <= w->right[y]) {
            DrawRectangle(w->left[y], y, w->right[y] - w->left[y] + 1, 1, c);
        }
    }
}

/* SNES colour math in windows: inside each window's span on every scanline
 * the window's colour is added to or subtracted from what layer 0 and the
 * backdrop drew (clamped), optionally halved (see ColorWindow in game.h).
 * Halving goes first and the colour is halved with it:
 * (bg +- c) / 2 = bg / 2 +- c / 2. Called between layer 0 and the sprites,
 * so sprites stay untouched. */
static void draw_windows(const ColorWindow *w)
{
    int i;
    for (i = 0; i < NUM_WINDOWS; i++) {
        uint8_t r = w[i].add_r, g = w[i].add_g, b = w[i].add_b;
        uint8_t half = (uint8_t)((w[i].math & CMATH_HALF) != 0);
        Color c;
        if (!r && !g && !b && !half) {
            continue;
        }
        if (half) {
            /* framebuffer * (1 - 128/255); alpha stays at 255, unlike BLEND_ALPHA */
            rlSetBlendFactorsSeparate(RL_ZERO, RL_ONE_MINUS_SRC_ALPHA, RL_ZERO, RL_ONE, RL_FUNC_ADD, RL_FUNC_ADD);
            BeginBlendMode(BLEND_CUSTOM_SEPARATE);
            draw_window_spans(&w[i], (Color){ 0, 0, 0, 128 });
            EndBlendMode();
            r >>= 1;
            g >>= 1;
            b >>= 1;
            if (!r && !g && !b) {
                continue;
            }
        }
        /* 5-bit to 8-bit like the SNES DAC: v << 3 | v >> 2 */
        c.r = (unsigned char)((r << 3) | (r >> 2));
        c.g = (unsigned char)((g << 3) | (g >> 2));
        c.b = (unsigned char)((b << 3) | (b >> 2));
        c.a = 255;
        if (w[i].math & CMATH_SUB) {
            /* framebuffer - colour (raylib's BLEND_SUBTRACT_COLORS is the other
             * way round); alpha stays, or the frame would turn transparent here */
            rlSetBlendFactorsSeparate(RL_ONE, RL_ONE, RL_ZERO, RL_ONE, RL_FUNC_REVERSE_SUBTRACT, RL_FUNC_ADD);
            BeginBlendMode(BLEND_CUSTOM_SEPARATE);
        } else {
            BeginBlendMode(BLEND_ADDITIVE);
        }
        draw_window_spans(&w[i], c);
        EndBlendMode();
    }
}

static void draw_sprites(const Sprite *oam)
{
    int i;
    /* Lower index = higher priority on SNES, so draw back-to-front. */
    for (i = MAX_SPRITES - 1; i >= 0; i--) {
        const Sprite *s = &oam[i];
        if (s->y >= SCREEN_H) {
            continue;
        }
        int pal = (s->attr & SPR_ATTR_PAL_MASK) >> SPR_ATTR_PAL_SHIFT;
        draw_cell(pal == PAL_P2 ? chr_sprites_p2 : chr_sprites, s->tile, SPRITE_SIZE, s->x, s->y,
                  (s->attr & SPR_ATTR_FLIP_X) != 0, (s->attr & SPR_ATTR_FLIP_Y) != 0,
                  pal == PAL_P2 ? WHITE : palette[pal]);
    }
}

static void play_sounds(uint32_t mask)
{
    int i;
    for (i = 0; i < SFX_COUNT; i++) {
        if ((mask & ((uint32_t)1u << i)) && sound_loaded[i]) {
            PlaySound(sounds[i]);
        }
    }
}

/* --- Command line -----------------------------------------------------------
 *   --mute            no sound (all builds)
 *   --no-crt          start without the retro TV filter (all builds; Tab toggles it)
 * Developer options, only in builds with BB_DEBUG (see README):
 *   --autoplay        a simple built-in bot plays instead of the keyboard (implies --mute)
 *   --autoplay2       like --autoplay, and a second bot plays player 2
 *   --shot <tick>     save the framebuffer as shot_<tick>.png (repeatable)
 *   --shot-crt <n>    also save each shot through the retro TV filter at n x
 *                     scale, as shot_<tick>_crt.png
 *   --quit <tick>     exit after that many game ticks
 *   --seed <n>        fixed random seed (same seed + same input = same game)
 *   --round <n>       new games start at round n
 *   --clear <tick>    remove all enemies at that tick (to see the round end)
 *   --skip-intro      no story pages after the title (also implied by --autoplay and --round)
 *   --intro           show the story pages anyway (for screenshots with --autoplay)
 *   --ending <happy|sad>  start with that ending (to look at it)
 *   --invincible, --infinite-lives, --hitboxes, --air, --cpu-view   switch those debug switches on
 *   --plus, --rescue, --cpu2   set those options for this run (not saved)
 *   --options         start on the options screen
 * Recordings (desktop development builds, see README "Recordings"):
 *   --replay <file>   play a recording instead of the keyboard
 *   --jump <tick>     with --replay: run fast to that tick, then pause
 *   --record          record even with --autoplay (normally off for bot runs)
 *   --no-record       don't record this run
 */
#define MAX_SHOTS 16
static int      opt_mute;
static int      opt_no_crt;
#ifdef BB_DEBUG
static int      opt_autoplay;
static int      opt_autoplay2;
static long     opt_shots[MAX_SHOTS];
static int      opt_shot_count;
static int      opt_shot_crt;
static long     opt_quit = -1;
static long     opt_seed = -1;
static long     opt_round = 1;
static long     opt_clear = -1;
static int      opt_skip_intro;
static int      opt_intro;
static int      opt_ending = -1;
static int      opt_switch[DBG_COUNT];
static int      opt_plus, opt_rescue, opt_cpu2, opt_options, opt_set_options;
static const char *opt_replay;
static long     opt_jump = -1;
static int      opt_record = -1;   /* -1: record unless it's a bot run */
#endif
#ifdef BB_DEBUG_UI
static int      opt_debug_ui;      /* --debug-ui: start with the debug view */
static long     opt_ui_shot = -1;  /* --ui-shot <tick>: save the whole window (debug view) as ui_shot.png */
#endif

static void parse_args(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-crt") == 0) {
            opt_no_crt = 1;
        } else if (strcmp(argv[i], "--mute") == 0) {
            opt_mute = 1;
#ifdef BB_DEBUG
        } else if (strcmp(argv[i], "--autoplay") == 0) {
            opt_autoplay = 1;
            opt_mute = 1;
        } else if (strcmp(argv[i], "--autoplay2") == 0) {
            opt_autoplay = 1;
            opt_autoplay2 = 1;
            opt_mute = 1;
        } else if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc && opt_shot_count < MAX_SHOTS) {
            opt_shots[opt_shot_count++] = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--shot-crt") == 0 && i + 1 < argc) {
            opt_shot_crt = (int)strtol(argv[++i], NULL, 10);
            if (opt_shot_crt < 1) opt_shot_crt = 1;
            if (opt_shot_crt > 16) opt_shot_crt = 16;
        } else if (strcmp(argv[i], "--quit") == 0 && i + 1 < argc) {
            opt_quit = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--ending") == 0 && i + 1 < argc) {
            opt_ending = strcmp(argv[++i], "happy") == 0;
        } else if (strcmp(argv[i], "--intro") == 0) {
            opt_intro = 1;
        } else if (strcmp(argv[i], "--skip-intro") == 0) {
            opt_skip_intro = 1;
        } else if (strcmp(argv[i], "--clear") == 0 && i + 1 < argc) {
            opt_clear = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--round") == 0 && i + 1 < argc) {
            opt_round = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            opt_seed = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--invincible") == 0) {
            opt_switch[DBG_INVINCIBLE] = 1;
        } else if (strcmp(argv[i], "--infinite-lives") == 0) {
            opt_switch[DBG_INFINITE_LIVES] = 1;
        } else if (strcmp(argv[i], "--hitboxes") == 0) {
            opt_switch[DBG_HITBOXES] = 1;
        } else if (strcmp(argv[i], "--air") == 0) {
            opt_switch[DBG_AIR] = 1;
        } else if (strcmp(argv[i], "--cpu-view") == 0) {
            opt_switch[DBG_CPU_VIEW] = 1;
        } else if (strcmp(argv[i], "--plus") == 0) {
            opt_plus = opt_set_options = 1;
        } else if (strcmp(argv[i], "--rescue") == 0) {
            opt_rescue = opt_set_options = 1;
        } else if (strcmp(argv[i], "--cpu2") == 0) {
            opt_cpu2 = opt_set_options = 1;
        } else if (strcmp(argv[i], "--options") == 0) {
            opt_options = 1;
        } else if (strcmp(argv[i], "--replay") == 0 && i + 1 < argc) {
            opt_replay = argv[++i];
        } else if (strcmp(argv[i], "--jump") == 0 && i + 1 < argc) {
            opt_jump = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--record") == 0) {
            opt_record = 1;
        } else if (strcmp(argv[i], "--no-record") == 0) {
            opt_record = 0;
#endif
#ifdef BB_DEBUG_UI
        } else if (strcmp(argv[i], "--debug-ui") == 0) {
            opt_debug_ui = 1;
        } else if (strcmp(argv[i], "--ui-shot") == 0 && i + 1 < argc) {
            opt_ui_shot = strtol(argv[++i], NULL, 10);
#endif
        }
    }
}

#ifdef BB_DEBUG
static uint16_t autoplay_pad(long t, int player)
{
    static uint16_t dirs[NUM_PLAYERS] = { PAD_RIGHT, PAD_LEFT };
    uint16_t pad = dirs[player];
    if ((t % 180) == 0) {
        dirs[player] = (uint16_t)(GetRandomValue(0, 1) ? PAD_LEFT : PAD_RIGHT);
    }
    if ((t % 20) < 2)  pad |= PAD_Y;
    if ((t % 90) < 10) pad |= PAD_B;
    if ((t % 1200) < 2) pad |= PAD_START;  /* title, name entry (rarely, so it seldom pauses) */
    return pad;
}

static void crt_shot(Texture2D frame, int scale, const char *path);

static void take_shots(RenderTexture2D fb, long t)
{
    int i;
    for (i = 0; i < opt_shot_count; i++) {
        if (opt_shots[i] >= 0 && t >= opt_shots[i]) {
            Image img = LoadImageFromTexture(fb.texture);
            ImageFlipVertical(&img);
            ExportImage(img, TextFormat("shot_%ld.png", opt_shots[i]));
            UnloadImage(img);
            if (opt_shot_crt) {
                crt_shot(fb.texture, opt_shot_crt, TextFormat("shot_%ld_crt.png", opt_shots[i]));
            }
            opt_shots[i] = -1;   /* taken */
        }
    }
}

/* --- Debug keys and overlays ------------------------------------------------
 *   F2 invincible   F3 infinite lives   F4 hitboxes   F5 air currents
 *   F7 pause / frame step (F8 = one frame)   F9 slow motion
 *   F10 spawn the next item or special bubble next to player 1
 *   F1 CPU partner view: predicted danger positions and the chosen action
 *   Backspace: mark the moment in the recording / jump to the next mark (see below) */
static int dbg_paused;
static int dbg_step;
static int dbg_slow;
static int dbg_spawn;
static double dbg_message_until;
static char dbg_message[64];
static char cpu_action[48];

static void dbg_say(const char *text)
{
    snprintf(dbg_message, sizeof dbg_message, "%s", text);
    dbg_message_until = GetTime() + 2.0;
}

/* --- Recordings ---------------------------------------------------------------
 * Every interactive run is recorded to recordings/<date>-<time>.bbr next to
 * the executable (src/replay). Backspace marks a moment ("that was weird!");
 * a recording with marks is kept as ...-marked.bbr, of the others only the
 * newest KEEP_RECORDINGS stay. --replay plays one back: the keyboard does
 * nothing then, Backspace jumps to the next mark, F7/F8/F9 pause, step and
 * slow down as usual, Shift+F8 steps one frame back, Ctrl+Shift+F8 one second. */
#ifdef BB_REPLAY
#define KEEP_RECORDINGS 20
#define MARK_LEAD_IN    180   /* a jump to a mark stops 3 s before it */

static ReplayWriter rec;
static int          recording;
static char         rec_path[1024];
static Replay       play;
static ReplayPlayer player;
static int          replaying;
static long         ff_target = -1;      /* run fast up to this tick, then pause */
static uint16_t     shown_pad[NUM_PLAYERS];
static int          desync_said;

static const char *recordings_dir(void)
{
    return TextFormat("%srecordings", GetApplicationDirectory());
}

/* Keeps the marked recordings and the newest KEEP_RECORDINGS others. Names
 * start with the date, so sorting them sorts by age. */
static int by_name(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static void prune_recordings(void)
{
    FilePathList list = LoadDirectoryFilesEx(recordings_dir(), ".bbr", false);
    unsigned int i, plain = 0, seen = 0;
    qsort(list.paths, list.count, sizeof list.paths[0], by_name);
    for (i = 0; i < list.count; i++) {
        plain += strstr(list.paths[i], "-marked") == NULL;
    }
    for (i = 0; i < list.count && plain - seen > KEEP_RECORDINGS; i++) {
        if (strstr(list.paths[i], "-marked") == NULL) {
            remove(list.paths[i]);
            seen++;
        }
    }
    UnloadDirectoryFiles(list);
}

static void record_start(const ReplaySetup *s)
{
    char stamp[32];
    time_t now = time(NULL);
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&now));
    MakeDirectory(recordings_dir());
    prune_recordings();
    snprintf(rec_path, sizeof rec_path, "%s/%s.bbr", recordings_dir(), stamp);
    recording = replay_write_begin(&rec, rec_path, s) == 0;
    if (recording) {
        TraceLog(LOG_INFO, "RECORD: %s", rec_path);
    } else {
        TraceLog(LOG_WARNING, "RECORD: cannot write %s", rec_path);
    }
}

static void record_stop(void)
{
    if (!recording) {
        return;
    }
    replay_write_end(&rec, (uint32_t)ticks_total);
    recording = 0;
    if (rec.marks) {
        char marked[1040];
        size_t n = strlen(rec_path) - 4;   /* without ".bbr" */
        snprintf(marked, sizeof marked, "%.*s-marked.bbr", (int)n, rec_path);
        if (rename(rec_path, marked) == 0) {
            snprintf(rec_path, sizeof rec_path, "%s", marked);
        }
    }
    TraceLog(LOG_INFO, "RECORD: %ld ticks, %lu mark(s) in %s", ticks_total, (unsigned long)rec.marks, rec_path);
}

/* A developer action that changes the game: recorded, or refused while a
 * recording plays (it would make the replay drift apart). */
static int dbg_action(uint8_t kind, uint16_t a, uint16_t b)
{
    if (replaying) {
        dbg_say("NOT WHILE REPLAYING");
        return 0;
    }
    if (recording) {
        replay_write_event(&rec, (uint32_t)ticks_total, kind, a, b);
    }
    return 1;
}

/* Backspace: mark this moment, or (replaying) jump to the next mark. */
static void mark_key(void)
{
    uint32_t i;
    if (!replaying) {
        if (recording && dbg_action(RE_MARK, 0, 0)) {
            snprintf(dbg_message, sizeof dbg_message, "MARKED AT TICK %ld", ticks_total);
            dbg_message_until = GetTime() + 2.0;
        }
        return;
    }
    for (i = 0; i < play.count; i++) {
        long t = (long)play.events[i].tick;
        if (play.events[i].kind == RE_MARK && t - MARK_LEAD_IN > ticks_total) {
            ff_target = t - MARK_LEAD_IN;
            snprintf(dbg_message, sizeof dbg_message, "TO THE MARK AT TICK %ld", t);
            dbg_message_until = GetTime() + 2.0;
            return;
        }
    }
    dbg_say("NO MORE MARKS");
}

/* Shift+F8 while paused: one frame back, Ctrl+Shift+F8: one second back.
 * There are no snapshots of the game: the replay starts again from its
 * first tick and runs fast up to the frame wanted (about 20000 ticks a
 * second). */
static void step_back(long frames)
{
    long target = ticks_total - frames;
    if (ticks_total == 0) {
        dbg_say("THIS IS THE FIRST FRAME");
        return;
    }
    if (target < 0) {
        target = 0;
    }
    replay_play_begin(&player, &play);
    ticks_total = 0;
    desync_said = 0;
    shown_pad[0] = shown_pad[1] = 0;
    ff_target = target;
}
#else
static int dbg_action(uint8_t kind, uint16_t a, uint16_t b)
{
    (void)kind; (void)a; (void)b;
    return 1;
}
#define RE_SWITCH 1
#define RE_SPAWN  2
#define RE_CLEAR  3
#endif

static void dbg_toggle(int sw, const char *name)
{
    uint8_t on = (uint8_t)!game_debug_get((uint8_t)sw);
    /* switches that only show something aren't part of a recording */
    int shows_only = sw == DBG_CPU_VIEW || sw == DBG_HITBOXES || sw == DBG_AIR;
    if (!shows_only && !dbg_action(RE_SWITCH, (uint16_t)sw, on)) {
        return;
    }
    game_debug_set((uint8_t)sw, on);
    snprintf(dbg_message, sizeof dbg_message, "%s %s", name, on ? "ON" : "OFF");
    dbg_message_until = GetTime() + 2.0;
}

static void debug_keys(void)
{
    if (IsKeyPressed(KEY_F1)) dbg_toggle(DBG_CPU_VIEW, "CPU VIEW");
    if (IsKeyPressed(KEY_F2)) dbg_toggle(DBG_INVINCIBLE, "INVINCIBLE");
    if (IsKeyPressed(KEY_F3)) dbg_toggle(DBG_INFINITE_LIVES, "INFINITE LIVES");
    if (IsKeyPressed(KEY_F4)) dbg_toggle(DBG_HITBOXES, "HITBOXES");
    if (IsKeyPressed(KEY_F5)) dbg_toggle(DBG_AIR, "AIR CURRENTS");
    if (IsKeyPressed(KEY_F12)) dbg_toggle(DBG_VACUUM_WALLS, "VACUUM WALLS");
    if (IsKeyPressed(KEY_F6)) dbg_toggle(DBG_NO_STICKY, "NO STICKY BUBBLES");
    if (IsKeyPressed(KEY_F7)) {
        dbg_paused = !dbg_paused;
        snprintf(dbg_message, sizeof dbg_message, "FRAME STEP %s (F8 = next frame)", dbg_paused ? "ON" : "OFF");
        dbg_message_until = GetTime() + 2.0;
    }
    if (IsKeyPressed(KEY_F8) && dbg_paused) {
        if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
#ifdef BB_REPLAY
            if (replaying) {
                step_back(IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ? TICK_HZ : 1);
            } else
#endif
            {
                dbg_say("GOING BACK: ONLY WHILE REPLAYING");
            }
        } else {
            dbg_step = 1;
        }
    }
    if (IsKeyPressed(KEY_F9)) {
        dbg_slow = !dbg_slow;
        snprintf(dbg_message, sizeof dbg_message, "SLOW MOTION %s", dbg_slow ? "ON" : "OFF");
        dbg_message_until = GetTime() + 2.0;
    }
#ifdef BB_REPLAY
    if (IsKeyPressed(KEY_BACKSPACE)) {
        mark_key();
    }
#endif
    if (IsKeyPressed(KEY_F10) && dbg_action(RE_SPAWN, (uint16_t)dbg_spawn, 0)) {
        game_debug_spawn((uint8_t)dbg_spawn);
        snprintf(dbg_message, sizeof dbg_message, "SPAWN %s", game_debug_spawn_name((uint8_t)dbg_spawn));
        dbg_message_until = GetTime() + 2.0;
        dbg_spawn = (dbg_spawn + 1) % DEBUG_SPAWN_COUNT;
    }
}

/* Drawn into the framebuffer, on top of the game picture. */
static void debug_overlays(void)
{
    static const Color box_colors[5] = {
        { 80, 255, 80, 255 }, { 255, 70, 70, 255 }, { 90, 200, 255, 255 }, { 255, 230, 60, 255 }, { 255, 120, 255, 255 }
    };
    DebugBox boxes[64];
    int i, n, tx, ty;

    if (game_debug_get(DBG_AIR) && game_debug_room_shown()) {
        for (ty = 0; ty < 26; ty++) {
            for (tx = 0; tx < 32; tx++) {
                uint8_t c = game_debug_cell((uint8_t)tx, (uint8_t)ty);
                int cx = tx * 8 + 4, cy = 16 + ty * 8 + 4;
                Color col = { 120, 200, 255, 160 };
                switch (c & 0x07) {
                case 1: DrawLine(cx, cy + 3, cx, cy - 3, col); DrawLine(cx - 2, cy - 1, cx, cy - 3, col); DrawLine(cx + 2, cy - 1, cx, cy - 3, col); break;
                case 2: DrawLine(cx, cy - 3, cx, cy + 3, col); DrawLine(cx - 2, cy + 1, cx, cy + 3, col); DrawLine(cx + 2, cy + 1, cx, cy + 3, col); break;
                case 3: DrawLine(cx + 3, cy, cx - 3, cy, col); DrawLine(cx - 1, cy - 2, cx - 3, cy, col); DrawLine(cx - 1, cy + 2, cx - 3, cy, col); break;
                case 4: DrawLine(cx - 3, cy, cx + 3, cy, col); DrawLine(cx + 1, cy - 2, cx + 3, cy, col); DrawLine(cx + 1, cy + 2, cx + 3, cy, col); break;
                default: break;
                }
            }
        }
    }
    if (game_debug_get(DBG_HITBOXES)) {
        n = game_debug_boxes(boxes, 64);
        for (i = 0; i < n; i++) {
            DrawRectangleLines(boxes[i].x, boxes[i].y, boxes[i].w, boxes[i].h, box_colors[boxes[i].kind % 5]);
        }
    }
    if (game_debug_get(DBG_CPU_VIEW)) {
        const char *action = "";
        n = game_debug_cpu_view(boxes, 64, &action);
        for (i = 0; i < n; i++) {
            /* brighter = sooner (kind = look-ahead step 0..4) */
            unsigned char a = (unsigned char)(255 - boxes[i].kind * 40);
            DrawRectangle(boxes[i].x, boxes[i].y, boxes[i].w, boxes[i].h, (Color){ 255, 60, 60, a });
        }
        snprintf(cpu_action, sizeof cpu_action, "CPU: %s", action);
    }
}

/* Drawn in the window: active switches and the last message. */
static void debug_status(void)
{
    char line[96];
    snprintf(line, sizeof line, "DEBUG %s%s%s%s%s%s%s%s%s",
             game_debug_get(DBG_CPU_VIEW) ? "CPU " : "", game_debug_get(DBG_INVINCIBLE) ? "INV " : "", game_debug_get(DBG_INFINITE_LIVES) ? "LIVES " : "",
             game_debug_get(DBG_HITBOXES) ? "BOXES " : "", game_debug_get(DBG_AIR) ? "AIR " : "",
             game_debug_get(DBG_VACUUM_WALLS) ? "VACWALLS " : "", game_debug_get(DBG_NO_STICKY) ? "NOSTICKY " : "", dbg_paused ? "PAUSED " : "", dbg_slow ? "SLOW" : "");
    if (strcmp(line, "DEBUG ") != 0) {
        DrawText(line, 8, 8, 20, YELLOW);
    }
    if (GetTime() < dbg_message_until) {
        DrawText(dbg_message, 8, 32, 20, WHITE);
    }
    if (game_debug_get(DBG_CPU_VIEW)) {
        DrawText(cpu_action, 8, 56, 20, (Color){ 255, 120, 120, 255 });
    }
#ifdef BB_REPLAY
    if (replaying) {
        /* what the players pressed: lit = held */
        static const struct { uint16_t bit; const char *name; } buttons[] = {
            { PAD_LEFT, "<" }, { PAD_UP, "^" }, { PAD_DOWN, "v" }, { PAD_RIGHT, ">" },
            { PAD_B, "JUMP" }, { PAD_Y, "BUBBLE" }, { PAD_X, "PUSH" }, { PAD_A, "VACUUM" },
            { PAD_L, "L" }, { PAD_R, "R" }, { PAD_START, "START" }, { PAD_SELECT, "SELECT" }
        };
        int p, b, y = GetScreenHeight() - 30 * NUM_PLAYERS - 34;
        snprintf(line, sizeof line, "REPLAY %ld / %lu%s", ticks_total, (unsigned long)play.end_tick,
                 player.desynced ? TextFormat("   DRIFTED APART AT %lu", (unsigned long)player.desync_tick) : "");
        DrawText(line, 8, y, 20, player.desynced ? RED : SKYBLUE);
        for (p = 0; p < NUM_PLAYERS; p++) {
            int x = 8;
            y += 30;
            DrawText(TextFormat("P%d", p + 1), x, y, 20, GRAY);
            x += 40;
            for (b = 0; b < (int)(sizeof buttons / sizeof buttons[0]); b++) {
                int held = (shown_pad[p] & buttons[b].bit) != 0;
                DrawText(buttons[b].name, x, y, 20, held ? YELLOW : (Color){ 70, 70, 70, 255 });
                x += MeasureText(buttons[b].name, 20) + 12;
            }
        }
    }
#endif
}

#ifdef BB_DEBUG_UI
/* --- Debug view (debug_ui.cpp) ------------------------------------------------
 * The ` key (^ on a German keyboard) switches it on and off; opening it
 * pauses the game. --debug-ui starts with it (without pausing, so that
 * scripted runs go on).
 * Its panels change the game only through these functions, which record
 * like the F-keys do. */
static int debug_ui_on;
static int debug_ui_ready;

static void debug_ui_set(int on)
{
    if (on && !debug_ui_ready) {
        debug_ui_init();
        debug_ui_ready = 1;
    }
    if (on && (GetScreenWidth() < 1280 || GetScreenHeight() < 800)) {
        MaximizeWindow();   /* room for the panels next to the game */
    }
    debug_ui_on = on;
}

void host_state(DebugHost *out)
{
    out->paused = dbg_paused;
    out->slow = dbg_slow;
    out->tick = ticks_total;
    out->replaying = out->recording = out->drifted = 0;
    out->end_tick = 0;
#ifdef BB_REPLAY
    out->replaying = replaying;
    out->recording = recording;
    out->end_tick = replaying ? (long)play.end_tick : 0;
    out->drifted = replaying && player.desynced;
#endif
    out->message = GetTime() < dbg_message_until ? dbg_message : "";
}

void host_set_paused(int on)
{
    dbg_paused = on;
}

void host_step(void)
{
    dbg_step = 1;
}

void host_set_slow(int on)
{
    dbg_slow = on;
}

void host_toggle_switch(int sw)
{
    dbg_toggle(sw, "SWITCH");
}

void host_spawn(int what)
{
    if (dbg_action(RE_SPAWN, (uint16_t)what, 0)) {
        game_debug_spawn((uint8_t)what);
        snprintf(dbg_message, sizeof dbg_message, "SPAWN %s", game_debug_spawn_name((uint8_t)what));
        dbg_message_until = GetTime() + 2.0;
    }
}

void host_clear_round(void)
{
    if (dbg_action(RE_CLEAR, 0, 0)) {
        game_debug_clear_round();
        dbg_say("ROUND CLEARED");
    }
}

void host_goto_round(int round)
{
#ifdef BB_REPLAY
    if (replaying) {
        dbg_say("NOT WHILE REPLAYING");
        return;
    }
#endif
    if (!game_debug_goto_round((uint8_t)round)) {
        dbg_say("ONLY WHILE A ROUND IS BEING PLAYED");
        return;
    }
#ifdef BB_REPLAY
    /* recorded after it happened, at the same tick: the replay applies it
     * before this tick's game_tick, just as here */
    if (recording) {
        replay_write_event(&rec, (uint32_t)ticks_total, RE_ROUND, (uint16_t)round, 0);
    }
#endif
    snprintf(dbg_message, sizeof dbg_message, "GO TO ROUND %d", round);
    dbg_message_until = GetTime() + 2.0;
}

void host_mark(void)
{
#ifdef BB_REPLAY
    mark_key();
#else
    dbg_say("NOT RECORDING");
#endif
}
#endif /* BB_DEBUG_UI */
#endif /* BB_DEBUG */

/* --- Retro TV look ------------------------------------------------------------
 * A post-processing chain applied only when the 256x224 framebuffer is scaled
 * into the window; the framebuffer itself (and --shot) stays clean.
 * It imitates a SNES on a consumer CRT over composite video, in the stages a
 * real set has (docs/RETRO-EFFECT.md explains each step and its constants):
 *   1. Signal (signal_fs, into `signal`, 1024x224, fixed size): 15-bit colour
 *      (5 bits per channel, like SNES CGRAM) and the composite blur: luma
 *      soft, chroma bleeds wider (YIQ, horizontal only). 4x oversampled
 *      horizontally so the final pass can read it smoothly between pixels;
 *      stored gamma-encoded so dark tones don't band in 8 bits.
 *   2. Glow (glow_fs, into glow_a then glow_b, 256x224): the light the glass
 *      scatters (halation), blurred from the signal in two passes; it also
 *      carries part of the light the mask clips off bright colours.
 *   3. Screen (crt_fs, at window resolution), everything finer than a game
 *      pixel:
 *      - curved glass: barrel distortion that bows the picture's edges
 *        outward (CURVE). The middle of each edge stays on the window edge
 *        and the rest curves inward, so no game pixel is ever cut off; the
 *        HUD reaches the edges, so there are no rounded corners either
 *      - a round beam spot: each pixel takes the line above, its own and the
 *        line below, each as a bell curve that widens on bright pixels; the
 *        curve keeps its energy, so bright lines fill the gaps (scanlines)
 *      - towards the edges the beam goes out of focus (DEFOCUS, DEFOCUS_H)
 *        and the red and blue beams drift apart from green (CONVERGE), so
 *        each colour channel is drawn by its own beam
 *      - DISTANCE (0 = at the desk, 1 = from the sofa) softens everything
 *        and fades scanlines and mask, the way the eye did from far away
 *      - an aperture-grille mask: R, G, B phosphor stripes with dark gaps.
 *        From MASK_ON_TUBE_FROM (6x) up they follow the curved picture
 *        (TRIADS per game pixel), each stripe box-filtered over the screen
 *        pixel. Below that they are straight, one screen pixel per stripe:
 *        1-pixel stripes on the curve drift against the pixel grid and
 *        show moire bands.
 *      - the glow added on top, then a light vignette.
 * Scanlines, mask and the clipped-light glow fade out below 3x scale, where
 * they would only alias. Tab toggles it; --no-crt starts with it off. */

#define CRT_SIGNAL_W (SCREEN_W * 4)

/* Shared by the screen and glow shaders: the glow pass needs the mask's gain
 * to work out how much light the mask clips. */
#define CRT_MASK_CONSTS \
    "const float MASK_FILL = 0.75;\n" \
    "const float MASK_DARK = 0.1;\n" \
    "const float MASK_GAIN = 1.0 / (MASK_DARK + (1.0 - MASK_DARK) * MASK_FILL / 3.0);\n"

/* Stage 1. texture0 is the framebuffer; fragTexCoord runs over the whole
 * picture, so x * 256 is the position in game pixels. */
#define SIGNAL_SHADER_BODY \
    "uniform sampler2D texture0;\n" \
    "uniform vec4 colDiffuse;\n" \
    "uniform vec2 sourceSize;\n" \
    "const float LUMA_SHARP = 1.4;\n" \
    "const float CHROMA_SHARP = 0.45;\n" \
    "const mat3 toYIQ = mat3(0.299, 0.596, 0.211, 0.587, -0.274, -0.523, 0.114, -0.322, 0.312);\n" \
    "const mat3 toRGB = mat3(1.0, 1.0, 1.0, 0.956, -0.272, -1.106, 0.621, -0.647, 1.703);\n" \
    "vec3 fetch(vec2 texel)\n" \
    "{\n" \
    "    vec3 c = TEX(texture0, texel / sourceSize).rgb;\n" \
    "    c = floor(c * 31.0 + 0.5) / 31.0;\n" \
    "    return toYIQ * pow(c, vec3(2.2));\n" \
    "}\n" \
    "void main()\n" \
    "{\n" \
    "    vec2 p = fragTexCoord * sourceSize;\n" \
    "    float row = floor(p.y) + 0.5;\n" \
    "    float cx = floor(p.x) + 0.5;\n" \
    "    vec3 yiq = vec3(0.0);\n" \
    "    vec3 wsum = vec3(0.0);\n" \
    "    for (int i = -2; i <= 2; i++) {\n" \
    "        float tx = cx + float(i);\n" \
    "        float d = tx - p.x;\n" \
    "        vec3 w = vec3(exp(-LUMA_SHARP * d * d), vec2(exp(-CHROMA_SHARP * d * d)));\n" \
    "        yiq += w * fetch(vec2(tx, row));\n" \
    "        wsum += w;\n" \
    "    }\n" \
    "    vec3 rgb = max(toRGB * (yiq / wsum), 0.0);\n" \
    "    OUT = vec4(pow(rgb, vec3(1.0 / 2.2)), 1.0) * colDiffuse * fragColor;\n" \
    "}\n"

/* Stage 2, one blur direction. The first pass (horizontal, reading the
 * signal) turns each pixel into glow light: a faint share of its colour
 * (GLOW) plus a part (OVERFLOW) of what the mask will clip off it, scaled
 * by `overflow`, which follows the mask's fade at small scales. The second
 * pass blurs that vertically. Output is linear light. */
#define GLOW_SHADER_BODY \
    "uniform sampler2D texture0;\n" \
    "uniform vec4 colDiffuse;\n" \
    "uniform vec2 sourceSize;\n" \
    "uniform vec2 dir;\n" \
    "uniform float first;\n" \
    "uniform float overflow;\n" \
    "const float GLOW = 0.03;\n" \
    "const float GLOW_SIZE = 2.0;\n" \
    "const float OVERFLOW = 0.1;\n" \
    CRT_MASK_CONSTS \
    "vec3 src(vec2 uv)\n" \
    "{\n" \
    "    vec3 c = TEX(texture0, uv).rgb;\n" \
    "    if (first > 0.5) {\n" \
    "        c = pow(c, vec3(2.2));\n" \
    "        c = GLOW * c + OVERFLOW * overflow * MASK_FILL / 3.0 * max(c * MASK_GAIN - 1.0, 0.0);\n" \
    "    }\n" \
    "    return c;\n" \
    "}\n" \
    "void main()\n" \
    "{\n" \
    "    vec3 sum = vec3(0.0);\n" \
    "    float wsum = 0.0;\n" \
    "    for (int i = -6; i <= 6; i++) {\n" \
    "        float w = exp(-float(i * i) / (2.0 * GLOW_SIZE * GLOW_SIZE));\n" \
    "        sum += w * src(fragTexCoord + dir * float(i) / sourceSize);\n" \
    "        wsum += w;\n" \
    "    }\n" \
    "    OUT = vec4(sum / wsum, 1.0) * colDiffuse * fragColor;\n" \
    "}\n"

/* Stage 3. texture0 is the signal; sourceSize is still the game picture
 * (256x224), since all positions are in game pixels. */
#define CRT_SHADER_BODY \
    "uniform sampler2D texture0;\n" \
    "uniform sampler2D glowTex;\n" \
    "uniform vec4 colDiffuse;\n" \
    "uniform vec2 sourceSize;\n" \
    "uniform float scale;\n" \
    "const float CURVE = 0.12;\n" \
    "const float SCANLINES = 0.85;\n" \
    "const float BEAM_DARK = 0.22;\n" \
    "const float BEAM_BRIGHT = 0.36;\n" \
    "const float TRIADS = 1.0;\n" \
    "const float MASK_ON_TUBE_FROM = 6.0;\n" \
    "const float DEFOCUS = 0.5;\n" \
    "const float DEFOCUS_H = 0.4;\n" \
    "const float CONVERGE = 0.3;\n" \
    "const float DISTANCE = 0.0;\n" \
    CRT_MASK_CONSTS \
    "vec3 line(float row, float x)\n" \
    "{\n" \
    "    return pow(TEX(texture0, vec2(x, row) / sourceSize).rgb, vec3(2.2));\n" \
    "}\n" \
    "vec3 wide(float row, float x, float h)\n" \
    "{\n" \
    "    return 0.5 * (line(row, x - h) + line(row, x + h));\n" \
    "}\n" \
    "vec3 row3(float row, float x, float ox, float h)\n" \
    "{\n" \
    "    return vec3(wide(row, x + ox, h).r, wide(row, x, h).g, wide(row, x - ox, h).b);\n" \
    "}\n" \
    "vec3 spot(vec3 col, vec3 dy, float grow)\n" \
    "{\n" \
    "    float sigma = mix(BEAM_DARK, BEAM_BRIGHT, sqrt(dot(col, vec3(0.299, 0.587, 0.114)))) * grow;\n" \
    "    return col * exp(-dy * dy / (2.0 * sigma * sigma)) / (sigma * 2.5066);\n" \
    "}\n" \
    "float lit(float a, float b, float s)\n" \
    "{\n" \
    "    float d = MASK_FILL / 3.0;\n" \
    "    float fa = floor(a - s) * d + min(fract(a - s), d);\n" \
    "    float fb = floor(b - s) * d + min(fract(b - s), d);\n" \
    "    return (fb - fa) / (b - a);\n" \
    "}\n" \
    "void main()\n" \
    "{\n" \
    "    vec2 c = fragTexCoord - 0.5;\n" \
    "    c *= 1.0 + CURVE * c.yx * c.yx;\n" \
    "    vec2 e = (0.5 - abs(c)) * sourceSize * scale;\n" \
    "    float inside = clamp(min(e.x, e.y) + 0.5, 0.0, 1.0);\n" \
    "    vec2 p = (c + 0.5) * sourceSize;\n" \
    "    float e2 = 2.0 * dot(c, c);\n" \
    "    float grow = (1.0 + DEFOCUS * e2) * (1.0 + DISTANCE);\n" \
    "    float h = DEFOCUS_H * e2 + 0.5 * DISTANCE;\n" \
    "    vec2 o = CONVERGE * 2.0 * c;\n" \
    "    float row = floor(p.y) + 0.5;\n" \
    "    vec3 dy = p.y - row + vec3(o.y, 0.0, -o.y);\n" \
    "    vec3 mid = row3(row, p.x, o.x, h);\n" \
    "    vec3 beam = spot(row3(row - 1.0, p.x, o.x, h), dy + 1.0, grow) + spot(mid, dy, grow) + spot(row3(row + 1.0, p.x, o.x, h), dy - 1.0, grow);\n" \
    "    float fx = clamp((scale - 1.5) / 1.5, 0.0, 1.0) * (1.0 - DISTANCE);\n" \
    "    vec3 rgb = mix(mid, beam, fx * SCANLINES);\n" \
    "    float t = p.x * TRIADS;\n" \
    "    float hw = 0.5 * TRIADS / scale;\n" \
    "    if (scale < MASK_ON_TUBE_FROM) {\n" \
    "        t = gl_FragCoord.x / 3.0;\n" \
    "        hw = 0.5 / 3.0;\n" \
    "    }\n" \
    "    t -= floor(t);\n" \
    "    float s0 = (1.0 - MASK_FILL) / 6.0;\n" \
    "    vec3 cover = vec3(lit(t - hw, t + hw, s0), lit(t - hw, t + hw, s0 + 1.0 / 3.0), lit(t - hw, t + hw, s0 + 2.0 / 3.0));\n" \
    "    vec3 mask = mix(vec3(MASK_DARK), vec3(1.0), cover) * MASK_GAIN;\n" \
    "    rgb *= mix(vec3(1.0), mask, fx);\n" \
    "    rgb += TEX(glowTex, c + 0.5).rgb;\n" \
    "    rgb *= (1.0 - 0.35 * dot(c, c)) * inside;\n" \
    "    OUT = vec4(pow(rgb, vec3(1.0 / 2.2)), 1.0) * colDiffuse * fragColor;\n" \
    "}\n"

#ifdef __EMSCRIPTEN__
#define CRT_FS_HEADER \
    "#version 100\n" \
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\n" \
    "varying vec2 fragTexCoord;\n" \
    "varying vec4 fragColor;\n" \
    "#define TEX texture2D\n" \
    "#define OUT gl_FragColor\n"
#else
#define CRT_FS_HEADER \
    "#version 330\n" \
    "in vec2 fragTexCoord;\n" \
    "in vec4 fragColor;\n" \
    "out vec4 finalColor;\n" \
    "#define TEX texture\n" \
    "#define OUT finalColor\n"
#endif
static const char signal_fs[] = CRT_FS_HEADER SIGNAL_SHADER_BODY;
static const char glow_fs[]   = CRT_FS_HEADER GLOW_SHADER_BODY;
static const char crt_fs[]    = CRT_FS_HEADER CRT_SHADER_BODY;

static Shader          signal_shader, glow_shader, crt_shader;
static RenderTexture2D signal;             /* stage 1, CRT_SIGNAL_W x 224 */
static RenderTexture2D glow_a, glow_b;     /* stage 2: horizontal, then vertical blur */
static int    crt_ready;
static int    crt_on = 1;
static int    crt_loc_scale, crt_loc_glow;
static int    glow_loc_dir, glow_loc_first, glow_loc_overflow;

static int shader_ok(Shader sh)
{
    return IsShaderValid(sh) && sh.id != rlGetShaderIdDefault();
}

static void crt_load(void)
{
    /* All three use raylib's default vertex shader. */
    signal_shader = LoadShaderFromMemory(NULL, signal_fs);
    glow_shader   = LoadShaderFromMemory(NULL, glow_fs);
    crt_shader    = LoadShaderFromMemory(NULL, crt_fs);
    crt_ready = shader_ok(signal_shader) && shader_ok(glow_shader) && shader_ok(crt_shader);
    if (crt_ready) {
        Vector2 size = { (float)SCREEN_W, (float)SCREEN_H };
        SetShaderValue(signal_shader, GetShaderLocation(signal_shader, "sourceSize"), &size, SHADER_UNIFORM_VEC2);
        SetShaderValue(glow_shader, GetShaderLocation(glow_shader, "sourceSize"), &size, SHADER_UNIFORM_VEC2);
        SetShaderValue(crt_shader, GetShaderLocation(crt_shader, "sourceSize"), &size, SHADER_UNIFORM_VEC2);
        crt_loc_scale     = GetShaderLocation(crt_shader, "scale");
        crt_loc_glow      = GetShaderLocation(crt_shader, "glowTex");
        glow_loc_dir      = GetShaderLocation(glow_shader, "dir");
        glow_loc_first    = GetShaderLocation(glow_shader, "first");
        glow_loc_overflow = GetShaderLocation(glow_shader, "overflow");
        signal = LoadRenderTexture(CRT_SIGNAL_W, SCREEN_H);
        glow_a = LoadRenderTexture(SCREEN_W, SCREEN_H);
        glow_b = LoadRenderTexture(SCREEN_W, SCREEN_H);
        SetTextureFilter(signal.texture, TEXTURE_FILTER_BILINEAR);
        SetTextureFilter(glow_a.texture, TEXTURE_FILTER_POINT);
        SetTextureFilter(glow_b.texture, TEXTURE_FILTER_BILINEAR);
    } else {
        TraceLog(LOG_WARNING, "CRT: shader failed, drawing without it");
    }
}

static void crt_unload(void)
{
    UnloadShader(signal_shader);
    UnloadShader(glow_shader);
    UnloadShader(crt_shader);
    if (crt_ready) {
        UnloadRenderTexture(signal);
        UnloadRenderTexture(glow_a);
        UnloadRenderTexture(glow_b);
    }
}

/* Draw all of `src` into `dst` through `sh`. Render textures are stored
 * flipped; drawing with a negative source height keeps every stage in the
 * same orientation as the framebuffer, so later stages read them all with
 * the same coordinates. */
static void crt_pass(Shader sh, Texture2D src, RenderTexture2D dst)
{
    BeginTextureMode(dst);
    ClearBackground(BLACK);
    BeginShaderMode(sh);
    DrawTexturePro(src, (Rectangle){ 0.0f, 0.0f, (float)src.width, (float)-src.height },
                   (Rectangle){ 0.0f, 0.0f, (float)dst.texture.width, (float)dst.texture.height },
                   (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
    EndShaderMode();
    EndTextureMode();
}

static void glow_pass(Texture2D src, RenderTexture2D dst, float dx, float dy, float first)
{
    Vector2 dir = { dx, dy };
    SetShaderValue(glow_shader, glow_loc_dir, &dir, SHADER_UNIFORM_VEC2);
    SetShaderValue(glow_shader, glow_loc_first, &first, SHADER_UNIFORM_FLOAT);
    crt_pass(glow_shader, src, dst);
}

/* Stages 1 and 2 for this frame: fixed size, independent of the window
 * except for the glow's overflow share, which fades with the mask. */
static void crt_prepare(Texture2D frame, int scale)
{
    float overflow = ((float)scale - 1.5f) / 1.5f;
    if (overflow < 0.0f) overflow = 0.0f;
    if (overflow > 1.0f) overflow = 1.0f;
    crt_pass(signal_shader, frame, signal);
    SetShaderValue(glow_shader, glow_loc_overflow, &overflow, SHADER_UNIFORM_FLOAT);
    glow_pass(signal.texture, glow_a, 1.0f, 0.0f, 1.0f);
    glow_pass(glow_a.texture, glow_b, 0.0f, 1.0f, 0.0f);
}

/* Stage 3: draw the prepared picture into `dst` at `scale`. */
static void crt_draw(Rectangle dst, int scale)
{
    float s = (float)scale;
    BeginShaderMode(crt_shader);
    SetShaderValue(crt_shader, crt_loc_scale, &s, SHADER_UNIFORM_FLOAT);
    SetShaderValueTexture(crt_shader, crt_loc_glow, glow_b.texture);
    DrawTexturePro(signal.texture, (Rectangle){ 0.0f, 0.0f, (float)CRT_SIGNAL_W, (float)-SCREEN_H },
                   dst, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
    EndShaderMode();
}

#ifdef BB_DEBUG
/* --shot-crt: save the frame through the filter at a fixed scale, so the
 * look can be compared between runs and machines. */
static void crt_shot(Texture2D frame, int scale, const char *path)
{
    RenderTexture2D rt;
    Image img;
    if (!crt_ready) {
        TraceLog(LOG_WARNING, "CRT: no filter, %s not written", path);
        return;
    }
    rt = LoadRenderTexture(SCREEN_W * scale, SCREEN_H * scale);
    crt_prepare(frame, scale);
    BeginTextureMode(rt);
    ClearBackground(BLACK);
    crt_draw((Rectangle){ 0.0f, 0.0f, (float)(SCREEN_W * scale), (float)(SCREEN_H * scale) }, scale);
    EndTextureMode();
    img = LoadImageFromTexture(rt.texture);
    ImageFlipVertical(&img);
    ExportImage(img, path);
    UnloadImage(img);
    UnloadRenderTexture(rt);
}
#endif

/* --- Main ----------------------------------------------------------------- */
static RenderTexture2D fb;
static double accumulator;
static int    game_started;

static void start_game(void)
{
    sram_load();
#ifdef BB_REPLAY
    if (opt_replay) {
        char err[256];
        if (replay_load(opt_replay, &play, err, sizeof err) != 0) {
            TraceLog(LOG_ERROR, "REPLAY: %s", err);
            exit(1);
        }
        if (strcmp(play.setup.version, GAME_VERSION) != 0) {
            TraceLog(LOG_WARNING, "REPLAY: recorded with %s, this is %s; it may drift apart",
                     play.setup.version, GAME_VERSION);
        }
        replay_play_begin(&player, &play);
        replaying = 1;
        if (opt_jump > 0) {
            ff_target = opt_jump;
        }
        {
            uint32_t i;
            for (i = 0; i < play.count; i++) {
                if (play.events[i].kind == RE_MARK) {
                    TraceLog(LOG_INFO, "REPLAY: mark at tick %lu", (unsigned long)play.events[i].tick);
                }
            }
        }
        TraceLog(LOG_INFO, "REPLAY: %s, %lu ticks", opt_replay, (unsigned long)play.end_tick);
        game_started = 1;
        return;
    }
#endif
#ifdef BB_DEBUG
    if (opt_seed >= 0) {
        SetRandomSeed((unsigned int)opt_seed);
    }
    {
        uint16_t seed = (uint16_t)(opt_seed >= 0 ? opt_seed : GetRandomValue(1, 0xFFFF));
        uint8_t round = (uint8_t)(opt_round > 0 && opt_round < 256 ? opt_round : 1);
        /* bot runs and direct round starts go straight into the game */
        uint8_t skip = (uint8_t)(!opt_intro && (opt_autoplay || opt_round != 1 || opt_skip_intro));
        int k;
#ifdef BB_REPLAY
        /* the start written down, then made from what was written: a
         * recording starts exactly like the run it recorded */
        ReplaySetup s;
        memset(&s, 0, sizeof s);
        snprintf(s.version, sizeof s.version, "%s", GAME_VERSION);
        s.seed = seed;
        memcpy(s.sram, game_sram(), SRAM_SIZE);
        s.round = round;
        s.skip_story = skip;
        s.set_options = (uint8_t)opt_set_options;
        s.plus = (uint8_t)opt_plus;
        s.rescue = (uint8_t)opt_rescue;
        s.cpu2 = (uint8_t)opt_cpu2;
        s.open_options = (uint8_t)opt_options;
        s.ending = (int8_t)opt_ending;
        for (k = 0; k < DBG_COUNT; k++) {
            s.switches[k] = (uint8_t)opt_switch[k];
        }
        replay_apply_setup(&s);
        if (opt_record == 1 || (opt_record < 0 && !opt_autoplay)) {
            record_start(&s);
        }
#else
        game_init(seed);
        game_debug_start_round(round);
        game_debug_skip_story(skip);
        for (k = 0; k < DBG_COUNT; k++) {
            game_debug_set((uint8_t)k, (uint8_t)opt_switch[k]);
        }
        if (opt_set_options) {
            game_debug_options((uint8_t)opt_plus, (uint8_t)opt_rescue, (uint8_t)opt_cpu2);
        }
        if (opt_options) {
            game_debug_open_options();
        }
        if (opt_ending >= 0) {
            game_debug_show_ending((uint8_t)opt_ending);
        }
#endif
    }
#else
    game_init((uint16_t)GetRandomValue(1, 0xFFFF));
#endif
    game_started = 1;
    TraceLog(LOG_INFO, "GAME: started");
}

/* One game tick with this frame's input. Returns the sounds it asked for. */
static uint32_t game_step(void)
{
    uint16_t pad[NUM_PLAYERS];
#ifdef BB_REPLAY
    if (replaying) {
        if (ticks_total >= (long)play.end_tick) {
            dbg_paused = opt_quit < 0;
            dbg_say("END OF THE RECORDING");
            return 0;
        }
        replay_before_tick(&player, (uint32_t)ticks_total, pad);
        /* scripted runs (--quit) never pause; a jump stops where it was told */
        if (player.marked && opt_quit < 0 && ff_target < 0) {
            dbg_paused = 1;
            snprintf(dbg_message, sizeof dbg_message, "MARK AT TICK %ld (F7 = go on)", ticks_total);
            dbg_message_until = GetTime() + 4.0;
        }
        shown_pad[0] = pad[0];
        shown_pad[1] = pad[1];
        game_tick(pad);
        if (replay_after_tick(&player, (uint32_t)ticks_total) && !desync_said) {
            desync_said = 1;
            TraceLog(LOG_WARNING, "REPLAY: drifted apart at tick %ld (the game changed since the recording)", ticks_total);
        }
        ticks_total++;
        return game_sfx();   /* the save RAM stays as it is */
    }
#endif
    pad[0] = (uint16_t)(read_keyboard(0) | read_gamepad(0));
    pad[1] = (uint16_t)(read_keyboard(1) | read_gamepad(1));
#ifdef BB_DEBUG_UI
    if (debug_ui_on && debug_ui_wants_keyboard()) {
        /* typing into a debug field: the keys are not the game's */
        pad[0] = (uint16_t)read_gamepad(0);
        pad[1] = (uint16_t)read_gamepad(1);
    }
#endif
#ifdef BB_DEBUG
    if (opt_autoplay) {
        pad[0] = (uint16_t)(pad[0] | autoplay_pad(ticks_total, 0));
    }
    if (opt_autoplay2) {
        pad[1] = (uint16_t)(pad[1] | autoplay_pad(ticks_total + 60, 1));
    }
    if (ticks_total == opt_clear && dbg_action(RE_CLEAR, 0, 0)) {
        game_debug_clear_round();
    }
#endif
    game_tick(pad);
#ifdef BB_REPLAY
    if (recording) {
        replay_write_tick(&rec, (uint32_t)ticks_total, pad);
    }
#endif
    if (game_sram_dirty()) {
        sram_save();
    }
    ticks_total++;
    return game_sfx();
}

/* One rendered frame: 0..MAX_CATCHUP game ticks, then the picture.
 * Returns 0 when the program should end. */
static int frame(void)
{
    const double tick = 1.0 / TICK_HZ;
    int ticks = 0;

#ifdef __EMSCRIPTEN__
    if (!audio_started && !opt_mute && EM_ASM_INT({ return Module.bbActivated ? 1 : 0; })) {
        start_audio();   /* the page saw a click or a key press */
    }
    if (!game_started) {
        /* wait for the saved high scores to come out of IndexedDB */
        if (!save_ready) {
            BeginDrawing();
            ClearBackground(BLACK);
            EndDrawing();
            return 1;
        }
        start_game();
    }
#else
    if (IsKeyPressed(KEY_F11)) {
        ToggleBorderlessWindowed();
    }
#endif
    if (IsKeyPressed(KEY_TAB)) {
        crt_on = !crt_on;
    }
#ifdef BB_DEBUG_UI
    if (IsKeyPressed(KEY_GRAVE)) {
        debug_ui_set(!debug_ui_on);
        if (debug_ui_on) {
            dbg_paused = 1;   /* opened to look at something: hold the moment */
        }
    }
#endif

    /* Fixed 60 Hz game tick, independent of monitor refresh rate. */
    accumulator += GetFrameTime();
#ifdef BB_DEBUG
    debug_keys();
    if (dbg_slow) {
        accumulator -= GetFrameTime() * 0.75;   /* a quarter of the speed */
    }
    if (dbg_paused) {
        accumulator = dbg_step ? tick : 0.0;   /* frame step: one tick per F8 */
        dbg_step = 0;
    }
#endif
#ifdef BB_REPLAY
    if (replaying && ff_target >= 0) {
        /* jumping: run without pictures or sound, a limited number per frame */
        long n = 0;
        while (ticks_total < ff_target && ticks_total < (long)play.end_tick && n++ < 20000) {
            game_step();
        }
        if (ticks_total >= ff_target || ticks_total >= (long)play.end_tick) {
            ff_target = -1;
            if (opt_quit < 0) {
                dbg_paused = 1;
                snprintf(dbg_message, sizeof dbg_message, "PAUSED AT TICK %ld (F7 GO ON, F8 NEXT, SHIFT+F8 BACK, +CTRL: 1 S)",
                         ticks_total);
                dbg_message_until = GetTime() + 3.0;
            }
        }
        accumulator = 0.0;
    }
#endif
    while (accumulator >= tick && ticks < MAX_CATCHUP) {
        play_sounds(game_step());
        accumulator -= tick;
        ticks++;
    }
    if (ticks == MAX_CATCHUP) {
        accumulator = 0.0;   /* drop time after a long stall */
    }
    update_music(game_music());

    /* Render into the 256x224 "SNES" framebuffer. */
    BeginTextureMode(fb);
    ClearBackground(BLACK);
    draw_bg(game_bg(0), BG0_ROWS, game_bg_scroll_y(), game_bg_window_top());
    draw_windows(game_windows());
    draw_sprites(game_oam());
    draw_bg(game_bg(1), BG_H, 0, 0);
#ifdef BB_DEBUG
    debug_overlays();
#endif
    EndTextureMode();
#ifdef BB_DEBUG
    if (opt_shot_count) {
        take_shots(fb, ticks_total);
    }
    if (opt_quit >= 0 && ticks_total >= opt_quit) {
        return 0;
    }
#ifdef BB_REPLAY
    if (opt_quit >= 0 && replaying && ticks_total >= (long)play.end_tick) {
        return 0;   /* a scripted replay ends with the recording */
    }
#endif
#endif

#ifdef BB_DEBUG_UI
    if (debug_ui_on) {
        BeginDrawing();
        ClearBackground((Color){ 30, 30, 34, 255 });
        debug_ui_draw(fb.texture);
        if (opt_ui_shot >= 0 && ticks_total >= opt_ui_shot) {
            /* Read before the buffers swap. Not TakeScreenshot: on a scaled
             * display it multiplies the size by the scale and reads past
             * the picture. */
            Image shot = { 0 };
            shot.data = rlReadScreenPixels(GetRenderWidth(), GetRenderHeight());
            shot.width = GetRenderWidth();
            shot.height = GetRenderHeight();
            shot.mipmaps = 1;
            shot.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
            ExportImage(shot, "ui_shot.png");
            UnloadImage(shot);
            opt_ui_shot = -1;
        }
        EndDrawing();
        return 1;
    }
#endif
    /* Integer-scale and center in the window. */
    {
        int sw = GetScreenWidth() / SCREEN_W;
        int sh = GetScreenHeight() / SCREEN_H;
        int scale = sw < sh ? sw : sh;
        Rectangle src, dst;
        if (scale < 1) scale = 1;
        if (crt_ready && crt_on) {
            crt_prepare(fb.texture, scale);
        }
        BeginDrawing();
        ClearBackground(BLACK);
        src.x = 0; src.y = 0;
        src.width = (float)SCREEN_W;
        src.height = (float)-SCREEN_H;   /* render textures are flipped */
        dst.width  = (float)(SCREEN_W * scale);
        dst.height = (float)(SCREEN_H * scale);
        dst.x = (float)((GetScreenWidth()  - (int)dst.width)  / 2);
        dst.y = (float)((GetScreenHeight() - (int)dst.height) / 2);
        if (crt_ready && crt_on) {
            crt_draw(dst, scale);
        } else {
            DrawTexturePro(fb.texture, src, dst, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
        }
    }
#ifdef BB_DEBUG
    debug_status();
#endif
    EndDrawing();
    return 1;
}

#ifdef __EMSCRIPTEN__
static void web_frame(void)
{
    (void)frame();
}
#endif

int main(int argc, char **argv)
{
    parse_args(argc, argv);

#ifdef __EMSCRIPTEN__
    InitWindow(SCREEN_W * WINDOW_SCALE, SCREEN_H * WINDOW_SCALE, "Blubber Blasen");
    SetExitKey(KEY_NULL);           /* the browser owns Esc */
#else
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(SCREEN_W * WINDOW_SCALE, SCREEN_H * WINDOW_SCALE, "Blubber Blasen");
    SetWindowMinSize(SCREEN_W, SCREEN_H);
    SetTargetFPS(TICK_HZ);
    SetExitKey(KEY_ESCAPE);
#endif
    fb = LoadRenderTexture(SCREEN_W, SCREEN_H);
    SetTextureFilter(fb.texture, TEXTURE_FILTER_POINT);
    crt_load();
    crt_on = !opt_no_crt;
    load_assets();
#ifndef __EMSCRIPTEN__
    if (!opt_mute) {
        start_audio();
    }
#endif
#ifdef BB_DEBUG_UI
    if (opt_debug_ui) {
        debug_ui_set(1);
    }
#endif

#ifdef __EMSCRIPTEN__
    save_mount();
    {
        /* Normally the browser's frame timer drives the loop. For automated
         * checks (#nostart in the address) a plain 60 Hz timer does, since
         * headless browsers hardly run the frame timer. */
        int timer = EM_ASM_INT({ return location.hash === "#nostart" ? 1 : 0; });
        emscripten_set_main_loop(web_frame, timer ? TICK_HZ : 0, 1);   /* never returns */
    }
#else
    start_game();
    while (!WindowShouldClose() && frame()) {
    }

    TraceLog(LOG_INFO, "GAME: %ld ticks, %.1f s", ticks_total, GetTime());
#ifdef BB_REPLAY
    record_stop();
    replay_free(&play);
#endif
#ifdef BB_DEBUG_UI
    if (debug_ui_ready) {
        debug_ui_shutdown();
    }
#endif
    unload_assets();
    crt_unload();
    UnloadRenderTexture(fb);
    if (IsAudioDeviceReady()) {
        CloseAudioDevice();
    }
    CloseWindow();
#endif
    return 0;
}
