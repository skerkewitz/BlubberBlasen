/*
 * Blubber Blasen - game code: top-level state machine, screens and HUD.
 * Rules (see docs/adr/0002): no floating point, no heap allocation, static
 * state only, timers in frames, fixed-point math.
 */

#include "game/internal.h"

#define START_LIVES     3
#define INTRO_FRAMES    120
#define CLEAR_FRAMES    300      /* collect the gems, then the next round */
#define HURRY_FRAMES    1800     /* 30 s, then the enemies get angry */
#define HURRY_TEXT      120
#define GAME_OVER_FRAMES 240
#define TITLE_FRAMES    600
#define HISCORE_FRAMES  420
#define ENTRY_TIMEOUT   1800

/* --- Shared state ------------------------------------------------------------ */
GameState g_state;
uint16_t  g_frame;
uint16_t  g_level_frames;
uint8_t   g_level;
uint8_t   g_round;
uint8_t   g_hurry;
uint8_t   g_lives;
uint8_t   g_score[8];
uint16_t  g_sfx;

Player    g_player;
Enemy     g_enemies[MAX_ENEMIES];
Bubble    g_bubbles[MAX_BUBBLES];
Item      g_items[MAX_ITEMS];
uint16_t  g_bg[BG_LAYERS][BG_W * BG_H];

/* --- Local state ------------------------------------------------------------- */
static Sprite   oam[MAX_SPRITES];
static uint8_t  oam_count;
static uint16_t state_timer;
static uint16_t rng;
static uint16_t prev_pad[NUM_PLAYERS];
static uint16_t clear_timer;
static uint8_t  hurry_text;

static char     entry_name[4];
static uint8_t  entry_pos;
static uint8_t  entry_rank;
static uint8_t  table_highlight;

static const char entry_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ.!- ";
#define ENTRY_CHAR_COUNT ((uint8_t)(sizeof entry_chars - 1))

/* --- Helpers ----------------------------------------------------------------- */
uint16_t rng_next(void)
{
    /* 16-bit Galois LFSR, taps 0xB400 */
    uint16_t lsb = (uint16_t)(rng & 1u);
    rng >>= 1;
    if (lsb) {
        rng ^= 0xB400u;
    }
    return rng;
}

uint8_t chance(uint8_t p256)
{
    return (uint8_t)((rng_next() & 0xFFu) < p256);
}

void sfx(uint8_t id)
{
    g_sfx |= (uint16_t)(1u << id);
}

void oam_add(uint8_t x, uint8_t y, uint8_t tile, uint8_t attr)
{
    if (oam_count < MAX_SPRITES && y < SCREEN_H) {
        Sprite *s = &oam[oam_count++];
        s->x = x;
        s->y = y;
        s->tile = tile;
        s->attr = attr;
    }
}

uint8_t overlap(uint16_t ax, uint16_t ay, uint16_t bx, uint16_t by, uint8_t w, uint8_t h)
{
    int16_t dx = (int16_t)((int16_t)PX(ax) - (int16_t)PX(bx));
    int16_t dy = (int16_t)((int16_t)PX(ay) - (int16_t)PX(by));
    if (dx < 0) dx = (int16_t)-dx;
    if (dy < 0) dy = (int16_t)-dy;
    return (uint8_t)(dx < w && dy < h);
}

static void set_state(GameState s)
{
    g_state = s;
    state_timer = 0;
}

static void clear_objects(void)
{
    enemies_clear();
    bubbles_clear();
    items_clear();
    g_player.state = PS_OFF;
}

/* --- HUD ------------------------------------------------------------------------ */
static void draw_hud_labels(void)
{
    text_put(3, 0, "1UP", PAL_GREEN);
    text_put(10, 0, "HIGH SCORE", PAL_RED);
    text_put(21, 0, "ROUND", PAL_YELLOW);
    text_put(27, 0, "LIVES", PAL_CYAN);
}

static void draw_hud_values(void)
{
    const uint8_t *best = hiscore_best();
    text_digits(0, 1, g_score, 8, PAL_NONE);
    text_digits(11, 1, score_cmp(g_score, best) > 0 ? g_score : best, 8, PAL_NONE);
    text_number(22, 1, g_round, PAL_NONE);
    text_number(28, 1, g_lives, PAL_NONE);
}

/* --- Title ---------------------------------------------------------------------- */
static void enter_title(void)
{
    uint16_t r, c;

    clear_objects();
    bg_clear(0);
    bg_clear(1);
    for (r = 0; r < CHR_TITLE_H; r++) {
        for (c = 0; c < CHR_TITLE_W; c++) {
            g_bg[0][(r << 5) + c] = (uint16_t)(CHR_TITLE_BASE + (r << 5) + c);
        }
    }
    text_put(8, 25, "HI", PAL_RED);
    text_digits(11, 25, hiscore_best(), 8, PAL_NONE);
    text_put(7, 27, "STAGE 1 PROTOTYPE", PAL_GREY);
    set_state(GS_TITLE);
}

static void enter_hiscores(uint8_t highlight)
{
    clear_objects();
    bg_clear(0);
    bg_clear(1);
    table_highlight = highlight;
    text_put(9, 4, "BEST BLASTERS", PAL_YELLOW);
    hiscore_draw_table(9, highlight);
    set_state(GS_HISCORES);
}

/* --- Rounds --------------------------------------------------------------------- */
static void start_round(void)
{
    level_load(g_level);
    bubbles_clear();
    items_clear();
    enemies_clear();
    enemies_spawn_level();
    player_enter_round();

    g_level_frames = 0;
    g_hurry = 0;
    hurry_text = 0;
    clear_timer = 0;

    bg_clear(1);
    draw_hud_labels();
    draw_hud_values();
    text_put(12, 12, "ROUND", PAL_YELLOW);
    text_number(18, 12, g_round, PAL_YELLOW);
    text_put(13, 15, "READY!", PAL_NONE);
    sfx(SFX_START);
    set_state(GS_ROUND_INTRO);
}

static void new_game(void)
{
    uint8_t i;
    for (i = 0; i < 8; i++) {
        g_score[i] = 0;
    }
    g_lives = START_LIVES;
    g_level = 0;
    g_round = 1;
    clear_objects();
    player_start();
    start_round();
}

static void next_round(void)
{
    g_level++;
    if (g_level >= level_count) {
        g_level = 0;
    }
    if (g_round < 99) {
        g_round++;
    }
    start_round();
}

/* Called by the player module when the death animation has finished. */
void player_killed(void)
{
    if (g_lives) {
        g_lives--;
    }
    if (g_lives == 0) {
        g_player.state = PS_OFF;
        text_put(11, 13, "GAME OVER", PAL_RED);
        sfx(SFX_GAMEOVER);
        set_state(GS_GAME_OVER);
    } else {
        player_start();
    }
}

static void update_play(uint16_t pad, uint16_t pressed)
{
    if (pressed & PAD_START) {
        text_put(13, 13, "PAUSE", PAL_NONE);
        sfx(SFX_PAUSE);
        set_state(GS_PAUSE);
        return;
    }

    g_level_frames++;
    player_update(pad, pressed);

    /* While the player dies, the rest of the room stands still. */
    if (g_player.state == PS_DYING || g_state != GS_PLAY) {
        return;
    }

    bubbles_update();
    enemies_update();
    items_update();

    if (!g_hurry && g_level_frames >= HURRY_FRAMES && enemies_count()) {
        g_hurry = 1;
        hurry_text = HURRY_TEXT;
        enemies_make_angry();
        sfx(SFX_HURRY);
    }
    if (hurry_text) {
        hurry_text--;
        if (hurry_text & 8) {
            text_put(11, 13, "HURRY UP!", PAL_ORANGE);
        } else {
            text_clear_row(13);
        }
    }

    if (clear_timer) {
        if (--clear_timer == 0) {
            next_round();
        }
    } else if ((g_level_frames & 7) == 0 && enemies_count() == 0 && bubbles_trapped_count() == 0) {
        clear_timer = CLEAR_FRAMES;
        sfx(SFX_CLEAR);
    }
}

/* --- Name entry ------------------------------------------------------------------ */
static uint8_t entry_char_index(char c)
{
    uint8_t i;
    for (i = 0; i < ENTRY_CHAR_COUNT; i++) {
        if (entry_chars[i] == c) {
            return i;
        }
    }
    return 0;
}

static void draw_name_entry(void)
{
    uint8_t i;
    for (i = 0; i < 3; i++) {
        char s[2];
        s[0] = entry_name[i] == ' ' ? '_' : entry_name[i];
        s[1] = 0;
        if (i == entry_pos && (state_timer & 16)) {
            s[0] = ' ';
        }
        text_put((uint8_t)(14 + i), 16, i == entry_pos ? s : (i < entry_pos ? s : "."),
                 (uint8_t)(i == entry_pos ? PAL_YELLOW : PAL_NONE));
    }
}

static void enter_name_entry(uint8_t rank)
{
    clear_objects();
    bg_clear(0);
    bg_clear(1);
    entry_rank = rank;
    entry_pos = 0;
    entry_name[0] = 'A';
    entry_name[1] = 'A';
    entry_name[2] = 'A';
    entry_name[3] = 0;
    text_put(8, 4, "CONGRATULATIONS!", PAL_YELLOW);
    text_put(8, 7, "ENTER YOUR NAME", PAL_CYAN);
    text_put(8, 10, "SCORE", PAL_NONE);
    text_digits(15, 10, g_score, 8, PAL_NONE);
    text_put(8, 12, "ROUND", PAL_NONE);
    text_number(15, 12, g_round, PAL_NONE);
    text_put(5, 21, "UP/DOWN  CHOOSE LETTER", PAL_GREY);
    text_put(5, 23, "B OR Y   CONFIRM", PAL_GREY);
    text_put(5, 25, "LEFT     BACK", PAL_GREY);
    set_state(GS_NAME_ENTRY);
}

static void update_name_entry(uint16_t pressed)
{
    uint8_t ci = entry_char_index(entry_name[entry_pos]);

    if (pressed & PAD_UP) {
        ci = (uint8_t)(ci + 1 >= ENTRY_CHAR_COUNT ? 0 : ci + 1);
        sfx(SFX_MENU);
    } else if (pressed & PAD_DOWN) {
        ci = (uint8_t)(ci == 0 ? ENTRY_CHAR_COUNT - 1 : ci - 1);
        sfx(SFX_MENU);
    }
    entry_name[entry_pos] = entry_chars[ci];

    if ((pressed & PAD_LEFT) && entry_pos > 0) {
        entry_pos--;
        sfx(SFX_MENU);
    } else if ((pressed & (PAD_B | PAD_Y | PAD_START)) || state_timer >= ENTRY_TIMEOUT) {
        sfx(SFX_MENU);
        if (entry_pos < 2 && state_timer < ENTRY_TIMEOUT) {
            entry_pos++;
            entry_name[entry_pos] = entry_name[entry_pos - 1];
        } else {
            hiscore_insert(entry_rank, entry_name, g_score, g_round);
            enter_hiscores(entry_rank);
            return;
        }
    }
    draw_name_entry();
}

/* --- Sprites ---------------------------------------------------------------------- */
static void build_oam(void)
{
    uint8_t i;
    oam_count = 0;

    if (g_state == GS_ROUND_INTRO || g_state == GS_PLAY || g_state == GS_PAUSE || g_state == GS_GAME_OVER) {
        /* Lower index = higher priority (drawn on top). */
        player_draw();
        items_draw();
        enemies_draw();
        bubbles_draw();
    }

    for (i = oam_count; i < MAX_SPRITES; i++) {
        oam[i].x = 0;
        oam[i].y = SPRITE_HIDDEN_Y;
        oam[i].tile = 0;
        oam[i].attr = 0;
    }
}

/* --- API ------------------------------------------------------------------------- */
void game_init(uint16_t seed)
{
    uint8_t i;

    rng = seed ? seed : 0xACE1u;
    g_frame = 0;
    for (i = 0; i < NUM_PLAYERS; i++) {
        prev_pad[i] = 0;
    }
    hiscore_init();
    enter_title();
    build_oam();
}

void game_tick(const uint16_t pad[NUM_PLAYERS])
{
    uint16_t pressed[NUM_PLAYERS];
    uint16_t any_pressed;
    uint8_t i;

    for (i = 0; i < NUM_PLAYERS; i++) {
        pressed[i]  = (uint16_t)(pad[i] & (uint16_t)~prev_pad[i]);
        prev_pad[i] = pad[i];
    }
    any_pressed = (uint16_t)(pressed[0] | pressed[1]);

    g_sfx = 0;
    g_frame++;
    (void)rng_next();   /* keep the RNG ticking every frame */

    switch (g_state) {
    case GS_TITLE:
        if (state_timer & 32) {
            text_put(11, 23, "PUSH START", PAL_NONE);
        } else {
            text_clear_row(23);
        }
        if (any_pressed & PAD_START) {
            new_game();
        } else if (state_timer >= TITLE_FRAMES) {
            enter_hiscores(HISCORE_COUNT);
        }
        break;

    case GS_HISCORES:
        if (any_pressed & PAD_START) {
            new_game();
        } else if (state_timer >= HISCORE_FRAMES) {
            enter_title();
        }
        break;

    case GS_ROUND_INTRO:
        player_update(0, 0);
        enemies_update();
        draw_hud_values();
        if (state_timer >= INTRO_FRAMES && !player_entering() && !enemies_entering()) {
            text_clear_row(12);
            text_clear_row(15);
            set_state(GS_PLAY);
        }
        break;

    case GS_PLAY:
        update_play(pad[0], pressed[0]);
        draw_hud_values();
        break;

    case GS_PAUSE:
        if (pressed[0] & PAD_START) {
            text_clear_row(13);
            g_state = GS_PLAY;
        }
        break;

    case GS_GAME_OVER:
        if (state_timer >= GAME_OVER_FRAMES) {
            uint8_t rank = hiscore_rank(g_score);
            if (rank < HISCORE_COUNT) {
                enter_name_entry(rank);
            } else {
                enter_hiscores(HISCORE_COUNT);
            }
        }
        break;

    case GS_NAME_ENTRY:
        update_name_entry(any_pressed);
        break;
    }

    state_timer++;
    build_oam();
}

const Sprite *game_oam(void)
{
    return oam;
}

const uint16_t *game_bg(uint8_t layer)
{
    return g_bg[layer < BG_LAYERS ? layer : 0];
}

uint16_t game_sfx(void)
{
    return g_sfx;
}
