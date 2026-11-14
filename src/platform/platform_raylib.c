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
    "ready", "full", "dash", "UhHelpMe", "vacuum"
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
        if (IsKeyDown(KEY_Q))            pad |= PAD_L;
        if (IsKeyDown(KEY_E))            pad |= PAD_R;
        if (IsKeyDown(KEY_L))            pad |= PAD_L | PAD_R;   /* dash: held direction or facing */
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
        if (IsKeyDown(KEY_SLASH))        pad |= PAD_L | PAD_R;   /* dash: held direction or facing */
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
    /* Both shoulder buttons (LB, RB) are the dash key: the held direction,
     * else the facing one (like L on the keyboard). */
    if (IsGamepadButtonDown(index, GAMEPAD_BUTTON_LEFT_TRIGGER_1))   pad |= PAD_L | PAD_R;
    if (IsGamepadButtonDown(index, GAMEPAD_BUTTON_RIGHT_TRIGGER_1))  pad |= PAD_L | PAD_R;
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

/* SNES colour math in windows: inside each window's span on every scanline
 * the window's colour is added (clamped) to what layer 0 and the backdrop
 * drew. Called between layer 0 and the sprites, so sprites stay untouched. */
static void draw_windows(const ColorWindow *w)
{
    int i, y;
    BeginBlendMode(BLEND_ADDITIVE);
    for (i = 0; i < NUM_WINDOWS; i++) {
        Color c;
        if (!w[i].add_r && !w[i].add_g && !w[i].add_b) {
            continue;
        }
        /* 5-bit to 8-bit like the SNES DAC: v << 3 | v >> 2 */
        c.r = (unsigned char)((w[i].add_r << 3) | (w[i].add_r >> 2));
        c.g = (unsigned char)((w[i].add_g << 3) | (w[i].add_g >> 2));
        c.b = (unsigned char)((w[i].add_b << 3) | (w[i].add_b >> 2));
        c.a = 255;
        for (y = 0; y < SCREEN_H; y++) {
            if (w[i].left[y] <= w[i].right[y]) {
                DrawRectangle(w[i].left[y], y, w[i].right[y] - w[i].left[y] + 1, 1, c);
            }
        }
    }
    EndBlendMode();
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
 *   --quit <tick>     exit after that many game ticks
 *   --seed <n>        fixed random seed (same seed + same input = same game)
 *   --round <n>       new games start at round n
 *   --clear <tick>    remove all enemies at that tick (to see the round end)
 *   --skip-intro      no story pages after the title (also implied by --autoplay and --round)
 *   --intro           show the story pages anyway (for screenshots with --autoplay)
 *   --ending <happy|sad>  start with that ending (to look at it)
 *   --invincible, --infinite-lives, --hitboxes, --air, --cpu-view, --full-energy   switch those debug switches on
 *   --plus, --rescue, --cpu2   set those options for this run (not saved)
 *   --options         start on the options screen
 */
#define MAX_SHOTS 16
static int      opt_mute;
static int      opt_no_crt;
#ifdef BB_DEBUG
static int      opt_autoplay;
static int      opt_autoplay2;
static long     opt_shots[MAX_SHOTS];
static int      opt_shot_count;
static long     opt_quit = -1;
static long     opt_seed = -1;
static long     opt_round = 1;
static long     opt_clear = -1;
static int      opt_skip_intro;
static int      opt_intro;
static int      opt_ending = -1;
static int      opt_switch[DBG_COUNT];
static int      opt_plus, opt_rescue, opt_cpu2, opt_options, opt_set_options;
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
        } else if (strcmp(argv[i], "--full-energy") == 0) {
            opt_switch[DBG_FULL_ENERGY] = 1;
        } else if (strcmp(argv[i], "--plus") == 0) {
            opt_plus = opt_set_options = 1;
        } else if (strcmp(argv[i], "--rescue") == 0) {
            opt_rescue = opt_set_options = 1;
        } else if (strcmp(argv[i], "--cpu2") == 0) {
            opt_cpu2 = opt_set_options = 1;
        } else if (strcmp(argv[i], "--options") == 0) {
            opt_options = 1;
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

static void take_shots(RenderTexture2D fb, long t)
{
    int i;
    for (i = 0; i < opt_shot_count; i++) {
        if (opt_shots[i] >= 0 && t >= opt_shots[i]) {
            Image img = LoadImageFromTexture(fb.texture);
            ImageFlipVertical(&img);
            ExportImage(img, TextFormat("shot_%ld.png", opt_shots[i]));
            UnloadImage(img);
            opt_shots[i] = -1;   /* taken */
        }
    }
}

/* --- Debug keys and overlays ------------------------------------------------
 *   F2 invincible   F3 infinite lives   F4 hitboxes   F5 air currents
 *   F6 full energy  F7 pause / frame step (F8 = one frame)   F9 slow motion
 *   F10 spawn the next item or special bubble next to player 1
 *   F1 CPU partner view: predicted danger positions and the chosen action */
static int dbg_paused;
static int dbg_step;
static int dbg_slow;
static int dbg_spawn;
static double dbg_message_until;
static char dbg_message[64];
static char cpu_action[48];

static void dbg_toggle(int sw, const char *name)
{
    game_debug_set((uint8_t)sw, (uint8_t)!game_debug_get((uint8_t)sw));
    snprintf(dbg_message, sizeof dbg_message, "%s %s", name, game_debug_get((uint8_t)sw) ? "ON" : "OFF");
    dbg_message_until = GetTime() + 2.0;
}

static void debug_keys(void)
{
    if (IsKeyPressed(KEY_F1)) dbg_toggle(DBG_CPU_VIEW, "CPU VIEW");
    if (IsKeyPressed(KEY_F2)) dbg_toggle(DBG_INVINCIBLE, "INVINCIBLE");
    if (IsKeyPressed(KEY_F3)) dbg_toggle(DBG_INFINITE_LIVES, "INFINITE LIVES");
    if (IsKeyPressed(KEY_F4)) dbg_toggle(DBG_HITBOXES, "HITBOXES");
    if (IsKeyPressed(KEY_F5)) dbg_toggle(DBG_AIR, "AIR CURRENTS");
    if (IsKeyPressed(KEY_F6)) dbg_toggle(DBG_FULL_ENERGY, "FULL ENERGY");
    if (IsKeyPressed(KEY_F12)) dbg_toggle(DBG_VACUUM_WALLS, "VACUUM WALLS");
    if (IsKeyPressed(KEY_F7)) {
        dbg_paused = !dbg_paused;
        snprintf(dbg_message, sizeof dbg_message, "FRAME STEP %s (F8 = next frame)", dbg_paused ? "ON" : "OFF");
        dbg_message_until = GetTime() + 2.0;
    }
    if (IsKeyPressed(KEY_F8) && dbg_paused) {
        dbg_step = 1;
    }
    if (IsKeyPressed(KEY_F9)) {
        dbg_slow = !dbg_slow;
        snprintf(dbg_message, sizeof dbg_message, "SLOW MOTION %s", dbg_slow ? "ON" : "OFF");
        dbg_message_until = GetTime() + 2.0;
    }
    if (IsKeyPressed(KEY_F10)) {
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
             game_debug_get(DBG_FULL_ENERGY) ? "ENERGY " : "", game_debug_get(DBG_VACUUM_WALLS) ? "VACWALLS " : "", dbg_paused ? "PAUSED " : "", dbg_slow ? "SLOW" : "");
    if (strcmp(line, "DEBUG ") != 0) {
        DrawText(line, 8, 8, 20, YELLOW);
    }
    if (GetTime() < dbg_message_until) {
        DrawText(dbg_message, 8, 32, 20, WHITE);
    }
    if (game_debug_get(DBG_CPU_VIEW)) {
        DrawText(cpu_action, 8, 56, 20, (Color){ 255, 120, 120, 255 });
    }
}
#endif /* BB_DEBUG */

/* --- Retro TV look ------------------------------------------------------------
 * A post-processing pass applied only when the 256x224 framebuffer is scaled
 * into the window; the framebuffer itself (and --shot) stays clean.
 * It imitates a SNES on a consumer CRT over composite video:
 *   - 15-bit colour (5 bits per channel, like SNES CGRAM)
 *   - composite blur: luma slightly soft, chroma bleeds wider (YIQ)
 *   - scanlines whose beam widens on bright pixels
 *   - an aperture-grille mask (RGB stripes) and a light vignette
 * Scanlines and mask fade out below 3x scale, where they would only alias.
 * Tab toggles it; --no-crt starts with it off. */
#define CRT_SHADER_BODY \
    "uniform sampler2D texture0;\n" \
    "uniform vec4 colDiffuse;\n" \
    "uniform vec2 sourceSize;\n" \
    "uniform float scale;\n" \
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
    "        vec3 w = vec3(exp(-2.2 * d * d), vec2(exp(-0.45 * d * d)));\n" \
    "        yiq += w * fetch(vec2(tx, row));\n" \
    "        wsum += w;\n" \
    "    }\n" \
    "    vec3 rgb = max(toRGB * (yiq / wsum), 0.0);\n" \
    "    float fx = clamp((scale - 1.5) / 1.5, 0.0, 1.0);\n" \
    "    float luma = dot(rgb, vec3(0.299, 0.587, 0.114));\n" \
    "    float dy = fract(p.y) - 0.5;\n" \
    "    float sigma = mix(0.26, 0.42, sqrt(luma));\n" \
    "    float scan = exp(-dy * dy / (2.0 * sigma * sigma));\n" \
    "    rgb *= mix(1.0, scan * 1.35, fx * 0.85);\n" \
    "    float m = mod(floor(gl_FragCoord.x), 3.0);\n" \
    "    vec3 mask = vec3(m < 0.5 ? 1.12 : 0.88, m > 0.5 && m < 1.5 ? 1.12 : 0.88, m > 1.5 ? 1.12 : 0.88);\n" \
    "    rgb *= mix(vec3(1.0), mask, fx);\n" \
    "    vec2 v = fragTexCoord - 0.5;\n" \
    "    rgb *= 1.0 - 0.35 * dot(v, v);\n" \
    "    OUT = vec4(pow(rgb, vec3(1.0 / 2.2)), 1.0) * colDiffuse * fragColor;\n" \
    "}\n"

#ifdef __EMSCRIPTEN__
static const char crt_fs[] =
    "#version 100\n"
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\n"
    "varying vec2 fragTexCoord;\n"
    "varying vec4 fragColor;\n"
    "#define TEX texture2D\n"
    "#define OUT gl_FragColor\n"
    CRT_SHADER_BODY;
#else
static const char crt_fs[] =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "out vec4 finalColor;\n"
    "#define TEX texture\n"
    "#define OUT finalColor\n"
    CRT_SHADER_BODY;
#endif

static Shader crt_shader;
static int    crt_ready;
static int    crt_on = 1;
static int    crt_loc_source, crt_loc_scale;

static void crt_load(void)
{
    crt_shader = LoadShaderFromMemory(NULL, crt_fs);   /* raylib's default vertex shader */
    crt_ready = IsShaderValid(crt_shader) && crt_shader.id != rlGetShaderIdDefault();
    if (crt_ready) {
        Vector2 size = { (float)SCREEN_W, (float)SCREEN_H };
        crt_loc_source = GetShaderLocation(crt_shader, "sourceSize");
        crt_loc_scale  = GetShaderLocation(crt_shader, "scale");
        SetShaderValue(crt_shader, crt_loc_source, &size, SHADER_UNIFORM_VEC2);
    } else {
        TraceLog(LOG_WARNING, "CRT: shader failed, drawing without it");
    }
}

/* --- Main ----------------------------------------------------------------- */
static RenderTexture2D fb;
static double accumulator;
static long   ticks_total;
static int    game_started;

static void start_game(void)
{
    sram_load();
#ifdef BB_DEBUG
    if (opt_seed >= 0) {
        SetRandomSeed((unsigned int)opt_seed);
    }
    game_init((uint16_t)(opt_seed >= 0 ? opt_seed : GetRandomValue(1, 0xFFFF)));
    game_debug_start_round((uint8_t)(opt_round > 0 && opt_round < 256 ? opt_round : 1));
    /* bot runs and direct round starts go straight into the game */
    game_debug_skip_story((uint8_t)(!opt_intro && (opt_autoplay || opt_round != 1 || opt_skip_intro)));
    {
        int k;
        for (k = 0; k < DBG_COUNT; k++) {
            game_debug_set((uint8_t)k, (uint8_t)opt_switch[k]);
        }
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
#else
    game_init((uint16_t)GetRandomValue(1, 0xFFFF));
#endif
    game_started = 1;
    TraceLog(LOG_INFO, "GAME: started");
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
    while (accumulator >= tick && ticks < MAX_CATCHUP) {
        uint16_t pad[NUM_PLAYERS];
        pad[0] = (uint16_t)(read_keyboard(0) | read_gamepad(0));
        pad[1] = (uint16_t)(read_keyboard(1) | read_gamepad(1));
#ifdef BB_DEBUG
        if (opt_autoplay) {
            pad[0] = (uint16_t)(pad[0] | autoplay_pad(ticks_total, 0));
        }
        if (opt_autoplay2) {
            pad[1] = (uint16_t)(pad[1] | autoplay_pad(ticks_total + 60, 1));
        }
        if (ticks_total == opt_clear) {
            game_debug_clear_round();
        }
#endif
        game_tick(pad);
        play_sounds(game_sfx());
        if (game_sram_dirty()) {
            sram_save();
        }
        accumulator -= tick;
        ticks++;
        ticks_total++;
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
#endif

    /* Integer-scale and center in the window. */
    BeginDrawing();
    ClearBackground(BLACK);
    {
        int sw = GetScreenWidth() / SCREEN_W;
        int sh = GetScreenHeight() / SCREEN_H;
        int scale = sw < sh ? sw : sh;
        Rectangle src, dst;
        if (scale < 1) scale = 1;
        src.x = 0; src.y = 0;
        src.width = (float)SCREEN_W;
        src.height = (float)-SCREEN_H;   /* render textures are flipped */
        dst.width  = (float)(SCREEN_W * scale);
        dst.height = (float)(SCREEN_H * scale);
        dst.x = (float)((GetScreenWidth()  - (int)dst.width)  / 2);
        dst.y = (float)((GetScreenHeight() - (int)dst.height) / 2);
        if (crt_ready && crt_on) {
            float s = (float)scale;
            SetShaderValue(crt_shader, crt_loc_scale, &s, SHADER_UNIFORM_FLOAT);
            BeginShaderMode(crt_shader);
        }
        DrawTexturePro(fb.texture, src, dst, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
        if (crt_ready && crt_on) {
            EndShaderMode();
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
    unload_assets();
    if (crt_ready) {
        UnloadShader(crt_shader);
    }
    UnloadRenderTexture(fb);
    if (IsAudioDeviceReady()) {
        CloseAudioDevice();
    }
    CloseWindow();
#endif
    return 0;
}
