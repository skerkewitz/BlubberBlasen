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
#include "game/game.h"

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
static Sound     sounds[SFX_COUNT];
static int       sound_loaded[SFX_COUNT];

static const char *const sfx_files[SFX_COUNT] = {
    "jump", "shoot", "pop", "trap", "kill", "gem", "death",
    "start", "clear", "hurry", "gameover", "menu", "pause", "extra"
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
    UnloadImage(img);

    for (i = 0; i < SFX_COUNT; i++) {
        const char *path = asset_path(TextFormat("sfx/%s.wav", sfx_files[i]));
        sound_loaded[i] = 0;
        if (IsAudioDeviceReady() && FileExists(path)) {
            sounds[i] = LoadSound(path);
            sound_loaded[i] = 1;
        }
    }
}

static void unload_assets(void)
{
    int i;
    for (i = 0; i < SFX_COUNT; i++) {
        if (sound_loaded[i]) {
            UnloadSound(sounds[i]);
        }
    }
    UnloadTexture(chr_sprites);
    UnloadTexture(chr_bg);
}

/* --- Save RAM ----------------------------------------------------------------- */
static const char *save_path(void)
{
    return TextFormat("%s%s", GetApplicationDirectory(), SAVE_FILE);
}

static void sram_load(void)
{
    int size = 0;
    unsigned char *data;
    if (!FileExists(save_path())) {
        return;   /* first start: the game creates a default table */
    }
    data = LoadFileData(save_path(), &size);
    if (data) {
        if (size == SRAM_SIZE) {
            memcpy(game_sram(), data, SRAM_SIZE);
        }
        UnloadFileData(data);
    }
}

static void sram_save(void)
{
    if (!SaveFileData(save_path(), game_sram(), SRAM_SIZE)) {
        TraceLog(LOG_WARNING, "SRAM: could not write %s", save_path());
    }
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
        if (IsKeyDown(KEY_V))            pad |= PAD_X;
        if (IsKeyDown(KEY_Q))            pad |= PAD_L;
        if (IsKeyDown(KEY_E))            pad |= PAD_R;
        if (IsKeyDown(KEY_ENTER))        pad |= PAD_START;
        if (IsKeyDown(KEY_RIGHT_SHIFT))  pad |= PAD_SELECT;
    } else {
        if (IsKeyDown(KEY_UP))           pad |= PAD_UP;
        if (IsKeyDown(KEY_DOWN))         pad |= PAD_DOWN;
        if (IsKeyDown(KEY_LEFT))         pad |= PAD_LEFT;
        if (IsKeyDown(KEY_RIGHT))        pad |= PAD_RIGHT;
        if (IsKeyDown(KEY_PERIOD))       pad |= PAD_B;   /* jump   */
        if (IsKeyDown(KEY_COMMA))        pad |= PAD_Y;   /* bubble */
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

static void draw_sprites(const Sprite *oam)
{
    int i;
    /* Lower index = higher priority on SNES, so draw back-to-front. */
    for (i = MAX_SPRITES - 1; i >= 0; i--) {
        const Sprite *s = &oam[i];
        if (s->y >= SCREEN_H) {
            continue;
        }
        draw_cell(chr_sprites, s->tile, SPRITE_SIZE, s->x, s->y,
                  (s->attr & SPR_ATTR_FLIP_X) != 0, (s->attr & SPR_ATTR_FLIP_Y) != 0,
                  palette[(s->attr & SPR_ATTR_PAL_MASK) >> SPR_ATTR_PAL_SHIFT]);
    }
}

static void play_sounds(uint16_t mask)
{
    int i;
    for (i = 0; i < SFX_COUNT; i++) {
        if ((mask & (1u << i)) && sound_loaded[i]) {
            PlaySound(sounds[i]);
        }
    }
}

/* --- Developer options ------------------------------------------------------
 *   --autoplay        a simple built-in bot plays instead of the keyboard (implies --mute)
 *   --autoplay2       like --autoplay, and a second bot plays player 2
 *   --mute            no sound
 *   --shot <tick>     save the framebuffer as shot_<tick>.png (repeatable)
 *   --quit <tick>     exit after that many game ticks
 *   --seed <n>        fixed random seed (same seed + same input = same game)
 *   --round <n>       new games start at round n
 *   --clear <tick>    remove all enemies at that tick (to see the round end)
 */
#define MAX_SHOTS 16
static int      opt_autoplay;
static int      opt_mute;
static int      opt_autoplay2;
static long     opt_shots[MAX_SHOTS];
static int      opt_shot_count;
static long     opt_quit = -1;
static long     opt_seed = -1;
static long     opt_round = 1;
static long     opt_clear = -1;

static void parse_args(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--autoplay") == 0) {
            opt_autoplay = 1;
            opt_mute = 1;
        } else if (strcmp(argv[i], "--autoplay2") == 0) {
            opt_autoplay = 1;
            opt_autoplay2 = 1;
            opt_mute = 1;
        } else if (strcmp(argv[i], "--mute") == 0) {
            opt_mute = 1;
        } else if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc && opt_shot_count < MAX_SHOTS) {
            opt_shots[opt_shot_count++] = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--quit") == 0 && i + 1 < argc) {
            opt_quit = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--clear") == 0 && i + 1 < argc) {
            opt_clear = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--round") == 0 && i + 1 < argc) {
            opt_round = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            opt_seed = strtol(argv[++i], NULL, 10);
        }
    }
}

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

/* --- Main ----------------------------------------------------------------- */
int main(int argc, char **argv)
{
    RenderTexture2D fb;
    double accumulator = 0.0;
    const double tick = 1.0 / TICK_HZ;
    long ticks_total = 0;

    parse_args(argc, argv);

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(SCREEN_W * WINDOW_SCALE, SCREEN_H * WINDOW_SCALE, "Blubber Blasen");
    SetWindowMinSize(SCREEN_W, SCREEN_H);
    SetTargetFPS(TICK_HZ);
    SetExitKey(KEY_ESCAPE);
    if (!opt_mute) {
        InitAudioDevice();
    }

    fb = LoadRenderTexture(SCREEN_W, SCREEN_H);
    SetTextureFilter(fb.texture, TEXTURE_FILTER_POINT);
    load_assets();

    sram_load();
    if (opt_seed >= 0) {
        SetRandomSeed((unsigned int)opt_seed);
    }
    game_init((uint16_t)(opt_seed >= 0 ? opt_seed : GetRandomValue(1, 0xFFFF)));
    game_debug_start_round((uint8_t)(opt_round > 0 && opt_round < 256 ? opt_round : 1));

    while (!WindowShouldClose()) {
        int ticks = 0;

        if (IsKeyPressed(KEY_F11)) {
            ToggleBorderlessWindowed();
        }

        /* Fixed 60 Hz game tick, independent of monitor refresh rate. */
        accumulator += GetFrameTime();
        while (accumulator >= tick && ticks < MAX_CATCHUP) {
            uint16_t pad[NUM_PLAYERS];
            pad[0] = (uint16_t)(read_keyboard(0) | read_gamepad(0));
            if (opt_autoplay) {
                pad[0] = (uint16_t)(pad[0] | autoplay_pad(ticks_total, 0));
            }
            pad[1] = (uint16_t)(read_keyboard(1) | read_gamepad(1));
            if (opt_autoplay2) {
                pad[1] = (uint16_t)(pad[1] | autoplay_pad(ticks_total + 60, 1));
            }
            if (ticks_total == opt_clear) {
                game_debug_clear_round();
            }
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

        /* Render into the 256x224 "SNES" framebuffer. */
        BeginTextureMode(fb);
        ClearBackground(BLACK);
        draw_bg(game_bg(0), BG0_ROWS, game_bg_scroll_y(), game_bg_window_top());
        draw_sprites(game_oam());
        draw_bg(game_bg(1), BG_H, 0, 0);
        EndTextureMode();
        if (opt_shot_count) {
            take_shots(fb, ticks_total);
        }
        if (opt_quit >= 0 && ticks_total >= opt_quit) {
            break;
        }

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
            DrawTexturePro(fb.texture, src, dst, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
        }
        EndDrawing();
    }

    TraceLog(LOG_INFO, "GAME: %ld ticks, %.1f s", ticks_total, GetTime());
    unload_assets();
    UnloadRenderTexture(fb);
    if (IsAudioDeviceReady()) {
        CloseAudioDevice();
    }
    CloseWindow();
    return 0;
}
