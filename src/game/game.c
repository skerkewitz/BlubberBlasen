/*
 * Blubber Blasen - game code: top-level state machine, screens and HUD.
 * Rules (see docs/adr/0002): no floating point, no heap allocation, static
 * state only, timers in frames, fixed-point math.
 */

#include "game/internal.h"

#define START_LIVES     3
#define INTRO_FRAMES    120
#define CLEAR_FRAMES    300      /* collect the gems, then the next round */
#define HURRY_TEXT      120
#define GAME_OVER_FRAMES 240
#define TITLE_FRAMES    600
#define HISCORE_FRAMES  420
#define ENTRY_TIMEOUT   1800
#define MAX_LIVES       9
#define CHASER_DELAY    600      /* frames after HURRY UP until the chaser comes */
#define CONTINUE_FROM   9        /* the continue countdown starts here, one per second */
#define SCROLL_SPEED    2        /* pixels per frame while the next round scrolls in */
#define STORY_PAGE_FRAMES 360    /* each intro page stays up to 6 s */
#define STORY_PAGES     3

/* --- Shared state ------------------------------------------------------------ */
GameState g_state;
uint16_t  g_frame;
uint16_t  g_level_frames;
uint8_t   g_level;
uint8_t   g_round;
uint8_t   g_hurry;
uint16_t  g_sfx;
uint8_t   g_skip_rounds;
uint16_t  g_freeze;
Counters  g_count;

Player    g_players[NUM_PLAYERS];
Enemy     g_enemies[MAX_ENEMIES];
Bubble    g_bubbles[MAX_BUBBLES];
Item      g_items[MAX_ITEMS];
uint16_t  g_bg0[BG_W * BG0_ROWS];
uint16_t  g_bg1[BG_W * BG_H];
uint16_t  g_scroll_y;
uint8_t   g_window_top;

/* --- Local state ------------------------------------------------------------- */
static Sprite   oam[MAX_SPRITES];
static uint8_t  oam_count;
static uint16_t state_timer;
static uint16_t rng;
static uint16_t prev_pad[NUM_PLAYERS];
static uint16_t clear_timer;
static uint8_t  hurry_text;
static uint8_t  first_level;     /* developer option, see game_debug_start_round() */
static uint8_t  played[NUM_PLAYERS];  /* took part in this game (for the high scores) */
static uint8_t  page;            /* layer 0 page showing the current round */
static uint8_t  boss_level;      /* index of the boss round in level_defs */
static uint8_t  secret_level;    /* index of the secret room, 0xFF if none */
static uint8_t  no_death;        /* nobody has lost a life this game (doors) */
static uint8_t  door_open;       /* a door is in this round */
static uint8_t  secret_part;     /* which clue the secret room shows (1..3) */
static uint8_t  return_level;    /* where the game goes on after the secret room */
static uint16_t secret_timer;
static uint8_t  ending_delay;
static uint8_t  ending_page;
static uint8_t  ending_happy;

#define DOOR_X        192        /* on the floor, right of the middle */
#define SECRET_FRAMES 600
#define ENDING_PAGE_FRAMES 480

/* Clues in the secret rooms (docs/story.md). */
static const char *const clue_text[3][2] = {
    { "CODE PART 1 OF 3:", "THE OFF SWITCH HAS TWO LEVERS." },
    { "CODE PART 2 OF 3:", "PULL BOTH LEVERS AT ONCE." },
    { "CODE PART 3 OF 3:", "ONE BLOB ALONE CANNOT DO IT." },
};

/* Endings (docs/story.md): two players get the happy one. */
#define ENDING_LINES 6
static const char *const ending_happy_text[3][ENDING_LINES] = {
    { "THE GRUMBLE BOILER", "IS BEATEN!", "", "BLUBBER AND BLASI PULL", "BOTH LEVERS AT ONCE...", "" },
    { "THE WORKERS WAKE UP AS", "THEMSELVES, AND THE", "FACTORY BLOWS BUBBLES", "AGAIN.", "", "" },
    { "BLASENHAUSEN SHINES IN", "A THOUSAND COLOURS.", "", "THANK YOU, BLUBBER", "AND BLASI!", "" },
};
static const char *const ending_sad_text[2][ENDING_LINES] = {
    { "THE GRUMBLE BOILER", "IS BEATEN...", "", "BUT ONE BLOB ALONE", "CANNOT PULL BOTH", "LEVERS." },
    { "THE FACTORY FALLS", "SILENT AND THE WORKERS", "SLEEP ON. BLASENHAUSEN", "STAYS GREY.", "", "COME BACK WITH A FRIEND!" },
};
static const char *const credits_text[ENDING_LINES] = {
    "BLUBBER BLASEN", "", "A HOBBY PROJECT", "", "THE END", "THANK YOU FOR PLAYING"
};
static uint8_t  story_page;
static uint8_t  story_starter;   /* who pressed Start on the title */
static uint8_t  skip_story;      /* developer option */

/* Intro pages (docs/story.md). Each line fits the 32 column screen. */
static const char *const story_text[STORY_PAGES][6] = {
    { "BLASENHAUSEN RUNS ON", "SOAP BUBBLES.", "", "THEY ALL COME FROM", "THE OLD BUBBLE FACTORY.", "" },
    { "ONE NIGHT THE GRUMBLE", "BOILER TOOK OVER.", "", "NOW IT MAKES SOOT, AND", "ITS WORKERS HAVE TURNED", "INTO MONSTERS." },
    { "ONLY TWO SOAP BLOBS GOT", "AWAY: BLUBBER AND BLASI.", "", "CLIMB THE FACTORY, SET", "THE WORKERS FREE AND", "STOP THE BOILER!" },
};

/* Factory floors, ten rounds each (docs/story.md). */
static const char *const floor_names[11] = {
    "THE CELLAR", "PIPE WORKS", "SPRING SHOP", "BOILER ROOM", "CONVEYOR HALLS", "DRYING LOFTS",
    "DYE WORKS", "PUMP STATION", "PRESSURE TANKS", "THE CHIMNEY", "TOP OF THE BOILER"
};
static uint8_t  continue_count;
static uint8_t  continue_frames;

/* Extra lives at 30,000 and 100,000 points. */
static const uint8_t extend_scores[2][8] = {
    { 0, 0, 0, 3, 0, 0, 0, 0 },
    { 0, 0, 1, 0, 0, 0, 0, 0 },
};

static uint8_t  entry_player;
static char     entry_name[4];
static uint8_t  entry_pos;
static uint8_t  entry_rank;

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
    uint8_t i;
    enemies_clear();
    bubbles_clear();
    items_clear();
    for (i = 0; i < NUM_PLAYERS; i++) {
        g_players[i].id = i;
        g_players[i].state = PS_OFF;
    }
}

static uint8_t any_alive(void)
{
    uint8_t i;
    for (i = 0; i < NUM_PLAYERS; i++) {
        if (g_players[i].state == PS_ALIVE) {
            return 1;
        }
    }
    return 0;
}

/* --- HUD ------------------------------------------------------------------------
 * Row 0: 1UP, HIGH SCORE, 2UP. Row 1: the three scores. The round number
 * sits on the left wall, the EXTEND letters on the side walls and the spare
 * lives (sprites) in the bottom corners. */
static void draw_hud_labels(void)
{
    text_put(2, 0, "1UP", PAL_GREEN);
    text_put(11, 0, "HIGH SCORE", PAL_RED);
    text_put(25, 0, "2UP", PAL_CYAN);
}

static const uint8_t *best_score(void)
{
    const uint8_t *best = hiscore_best();
    uint8_t i;
    for (i = 0; i < NUM_PLAYERS; i++) {
        if (score_cmp(g_players[i].score, best) > 0) {
            best = g_players[i].score;
        }
    }
    return best;
}

static void draw_hud_values(void)
{
    static const uint8_t score_col[NUM_PLAYERS] = { 0, 24 };
    uint8_t i;

    for (i = 0; i < NUM_PLAYERS; i++) {
        const Player *p = &g_players[i];
        if (p->state == PS_OFF && (g_frame & 32)) {
            text_put(score_col[i], 1, " START  ", PAL_YELLOW);
        } else {
            text_digits(score_col[i], 1, p->score, 8, PAL_NONE);
        }
    }
    text_digits(11, 1, best_score(), 8, PAL_NONE);
    text_number(0, HUD_ROWS, g_round, PAL_YELLOW);

    /* EXTEND letters on the side walls: player 1 left, player 2 right. */
    for (i = 0; i < NUM_PLAYERS; i++) {
        static const char letters[] = "EXTEND";
        uint8_t k;
        for (k = 0; k < 6; k++) {
            char s[2];
            uint8_t have = (uint8_t)((g_players[i].letters >> k) & 1u);
            s[0] = letters[k];
            s[1] = 0;
            text_put((uint8_t)(i == 0 ? 0 : BG_W - 1), (uint8_t)(HUD_ROWS + 4 + (k << 1)),
                     g_players[i].state == PS_OFF ? " " : s, have ? PAL_YELLOW : PAL_GREY);
        }
    }
}

/* --- Title ---------------------------------------------------------------------- */
static void enter_title(void)
{
    uint16_t r, c;

    clear_objects();
    bg_clear(0);
    bg_clear(1);
    g_scroll_y = 0;
    g_window_top = 0;
    page = 0;
    for (r = 0; r < CHR_TITLE_H; r++) {
        for (c = 0; c < CHR_TITLE_W; c++) {
            g_bg0[(r << 5) + c] = (uint16_t)(CHR_TITLE_BASE + (r << 5) + c);
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
    g_scroll_y = 0;
    g_window_top = 0;
    page = 0;
    text_put(9, 4, "BEST BLASTERS", PAL_YELLOW);
    hiscore_draw_table(9, highlight);
    set_state(GS_HISCORES);
}

/* --- Secret room ------------------------------------------------------------------ */
/* Gems on every few standing spots of the room, and the clue on the wall. */
static void start_secret_room(void)
{
    int16_t r, c;
    uint8_t n = 0;

    for (r = 3; r < LEVEL_H - 2 && n < 12; r = (int16_t)(r + 1)) {
        for (c = 3; c < LEVEL_W - 4 && n < 12; c = (int16_t)(c + 4)) {
            if (!level_solid_tile(c, r) && !level_solid_tile((int16_t)(c + 1), r) &&
                level_solid_tile(c, (int16_t)(r + 1)) && level_solid_tile((int16_t)(c + 1), (int16_t)(r + 1)) &&
                !(r == LEVEL_H - 2 && (c < 6 || c > 24))) {
                item_spawn(IT_FOOD, TO_POS(c << 3), TO_POS(FIELD_Y + (r << 3) - 8), GEM_TIER);
                n++;
            }
        }
    }
    text_put(9, 4, "SECRET ROOM", PAL_YELLOW);
    text_put(4, 6, clue_text[secret_part - 1][0], PAL_CYAN);
    text_put(1, 7, clue_text[secret_part - 1][1], PAL_NONE);
    secret_timer = SECRET_FRAMES;
    sfx(SFX_EXTRA);
    set_state(GS_PLAY);
}

static uint8_t gems_left(void)
{
    uint8_t i, n = 0;
    for (i = 0; i < MAX_ITEMS; i++) {
        n = (uint8_t)(n + (g_items[i].kind == IT_FOOD));
    }
    return n;
}

/* --- Rounds --------------------------------------------------------------------- */
static void begin_transition(void);
static void enter_ending(void);
/* The round's room is in place (page shown): enemies drop in, players
 * drift to their corners, "ROUND n / READY!". */
static void round_intro(void)
{
    uint8_t i;

    bubbles_clear();
    items_clear();
    effects_clear();
    enemies_clear();
    g_skip_rounds = 0;
    enemies_spawn_level();
    for (i = 0; i < NUM_PLAYERS; i++) {
        player_enter_round(&g_players[i]);
    }

    g_level_frames = 0;
    g_hurry = 0;
    g_freeze = 0;
    hurry_text = 0;
    clear_timer = 0;
    for (i = 0; i < NUM_PLAYERS; i++) {
        g_players[i].powers = (uint16_t)(g_players[i].powers & (0xFFFFu ^ POWER_BIT(POWER_GLOVE)));
    }
    if (level_defs[g_level].kind == LEVEL_BOSS) {
        boss_spawn();
    } else {
        boss_reset();
    }
    /* A door appears in rounds 20, 40, 50 and 60 if nobody has lost a life. */
    door_open = (uint8_t)(no_death && level_defs[g_level].kind == LEVEL_NORMAL &&
                          (g_round == 20 || g_round == 40 || g_round == 50 || g_round == 60));

    bg_clear(1);
    draw_hud_labels();
    draw_hud_values();
    if (level_defs[g_level].kind == LEVEL_SECRET) {
        start_secret_room();
        return;
    }
    text_put(12, 12, "ROUND", PAL_YELLOW);
    text_number(18, 12, g_round, PAL_YELLOW);
    {
        /* floor = (round - 1) / 10, without a division */
        uint8_t floor = 0, r = (uint8_t)(g_round - 1);
        const char *name;
        uint8_t len = 0;
        while (r >= 10 && floor < 10) {
            r = (uint8_t)(r - 10);
            floor++;
        }
        name = floor_names[floor];
        while (name[len]) {
            len++;
        }
        text_put((uint8_t)((BG_W - len) >> 1), 13, name, PAL_CYAN);
    }
    text_put(13, 15, "READY!", PAL_NONE);
    sfx(SFX_START);
    set_state(GS_ROUND_INTRO);
}

/* First round of a game: no scrolling. */
static void start_round(void)
{
    page = 0;
    g_scroll_y = 0;
    g_window_top = FIELD_Y;
    level_load(g_level, page);
    round_intro();
}

/* Later rounds: draw the next room into the other page and scroll it in.
 * Everything in the old room disappears; the players drift along. */
static void begin_transition(void)
{
    uint8_t i;

    level_load(g_level, (uint8_t)(page ^ 1));
    bubbles_clear();
    items_clear();
    effects_clear();
    enemies_clear();
    for (i = 0; i < NUM_PLAYERS; i++) {
        player_enter_round(&g_players[i]);
    }
    text_clear_row(12);
    text_clear_row(13);
    text_clear_row(15);
    set_state(GS_TRANSITION);
}

static void update_transition(void)
{
    uint16_t target = (uint16_t)((page ^ 1) ? PAGE_PX : 0);
    uint8_t i;

    for (i = 0; i < NUM_PLAYERS; i++) {
        player_update(&g_players[i], 0, 0);
    }
    g_scroll_y = (uint16_t)((g_scroll_y + SCROLL_SPEED) & (BG0_ROWS * 8 - 1));
    if (g_scroll_y == target) {
        page = (uint8_t)(page ^ 1);
        round_intro();
    }
}

/* A player starts or continues: fresh lives, the score is kept. */
static void join(Player *p)
{
    p->lives = START_LIVES;
    played[p->id] = 1;
    player_start(p);
    sfx(SFX_START);
}

static void new_game(uint8_t starter)
{
    uint8_t i, k;

    clear_objects();
    for (i = 0; i < NUM_PLAYERS; i++) {
        for (k = 0; k < 8; k++) {
            g_players[i].score[k] = 0;
        }
        g_players[i].lives = 0;
        g_players[i].extends_given = 0;
        g_players[i].letters = 0;
        g_players[i].powers = 0;
        played[i] = 0;
    }
    g_count.blown = 0;
    g_count.popped = 0;
    g_count.jumps = 0;
    g_count.water = 0;
    g_count.walked = 0;
    g_count.food = 0;
    g_count.water_kills = 0;
    g_count.defeated = 0;
    g_count.fire_kills = 0;
    g_count.candies = 0;
    g_count.queued = 0;
    no_death = 1;
    ending_delay = 0;
    g_level = first_level;
    g_round = (uint8_t)(first_level + 1);
    join(&g_players[starter]);
    start_round();
}

/* Rounds go up to the boss; skipping (umbrella, EXTEND, warp) stops there. */
static void next_round(uint8_t advance)
{
    while (advance--) {
        if (g_level < boss_level) {
            g_level++;
            g_round++;
        }
    }
    begin_transition();
}

/* A player at the door pressing Up: rounds 20, 40, 60 lead to the secret
 * room, round 50 warps to round 70. */
static void check_door(const uint16_t pressed[NUM_PLAYERS])
{
    uint8_t i;
    if (!door_open) {
        return;
    }
    for (i = 0; i < NUM_PLAYERS; i++) {
        const Player *p = &g_players[i];
        if (p->state == PS_ALIVE && (pressed[i] & PAD_UP) &&
            overlap(p->x, p->y, TO_POS(DOOR_X), TO_POS(PLAYER_SPAWN_Y), 10, 12)) {
            door_open = 0;
            sfx(SFX_EXTRA);
            if (g_round == 50) {
                next_round(20);
            } else if (secret_level != 0xFF) {
                secret_part = (uint8_t)(g_round == 20 ? 1 : g_round == 40 ? 2 : 3);
                return_level = g_level;
                g_level = secret_level;
                begin_transition();
            }
            return;
        }
    }
}

/* Called by the player module when a death animation has finished.
 * Losing a life calms the enemies down and restarts the round timer. */
void player_killed(Player *p)
{
    if (p->lives) {
        p->lives--;
    }
    p->powers = 0;   /* power-ups are lost with a life */
    no_death = 0;    /* no more doors this game */
    enemies_calm_down();
    chaser_reset();
    items_clear_rocks();
    g_level_frames = 0;
    g_hurry = 0;

    if (p->lives) {
        player_start(p);
        return;
    }
    p->state = PS_OFF;
    if (players_in_game() == 0) {
        /* Everybody is out: offer a continue before the game is over. */
        continue_count = CONTINUE_FROM;
        continue_frames = 0;
        text_put(11, 12, "CONTINUE?", PAL_YELLOW);
        text_number(14, 14, continue_count, PAL_NONE);
        set_state(GS_CONTINUE);
    }
}

static void update_continue(const uint16_t pressed[NUM_PLAYERS])
{
    uint8_t i;
    for (i = 0; i < NUM_PLAYERS; i++) {
        if (pressed[i] & PAD_START) {
            join(&g_players[i]);
            text_clear_row(12);
            text_clear_row(14);
            g_state = GS_PLAY;
            return;
        }
    }
    if (++continue_frames >= 60) {
        continue_frames = 0;
        if (continue_count == 0) {
            text_clear_row(12);
            text_clear_row(14);
            text_put(11, 13, "GAME OVER", PAL_RED);
            sfx(SFX_GAMEOVER);
            set_state(GS_GAME_OVER);
            return;
        }
        continue_count--;
        sfx(SFX_MENU);
    }
    text_number(14, 14, continue_count, PAL_NONE);
}

static void check_extra_lives(Player *p)
{
    if (p->state != PS_OFF && p->extends_given < 2 &&
        score_cmp(p->score, extend_scores[p->extends_given]) >= 0) {
        p->extends_given++;
        if (p->lives < MAX_LIVES) {
            p->lives++;
        }
        sfx(SFX_EXTRA);
    }
}

/* Start: a player who is not in the game joins (or continues); a player
 * who is, pauses. */
static uint8_t handle_start(const uint16_t pressed[NUM_PLAYERS])
{
    uint8_t i;
    for (i = 0; i < NUM_PLAYERS; i++) {
        if (!(pressed[i] & PAD_START)) {
            continue;
        }
        if (g_players[i].state == PS_OFF) {
            join(&g_players[i]);
        } else if (g_state == GS_PLAY) {
            text_put(13, 13, "PAUSE", PAL_NONE);
            sfx(SFX_PAUSE);
            set_state(GS_PAUSE);
            return 1;
        }
    }
    return 0;
}

static void update_play(const uint16_t pad[NUM_PLAYERS], const uint16_t pressed[NUM_PLAYERS])
{
    uint8_t i;

    if (handle_start(pressed)) {
        return;
    }

    g_level_frames++;
    if (g_freeze) {
        g_freeze--;
    }
    for (i = 0; i < NUM_PLAYERS; i++) {
        player_update(&g_players[i], pad[i], pressed[i]);
    }
    check_door(pressed);
    if (g_state != GS_PLAY) {
        return;   /* went through a door */
    }

    /* While nobody is alive (everyone dying), the room stands still. */
    if (!any_alive() || g_state != GS_PLAY) {
        return;
    }

    bubbles_update();
    enemies_update();
    chaser_update();
    boss_update();
    items_update();
    effects_update();
    counters_check();
    powerup_round_tick();
    bonus_round_tick();

    /* EXTEND completed or umbrella collected: the round ends at once. */
    if (g_skip_rounds && level_defs[g_level].kind == LEVEL_NORMAL) {
        next_round(g_skip_rounds);
        return;
    }
    g_skip_rounds = 0;

    switch (level_defs[g_level].kind) {
    case LEVEL_SECRET:
        /* Collect what you can, then on to the round after the door. */
        if (--secret_timer == 0 || gems_left() == 0) {
            g_level = return_level;
            next_round(1);
        }
        return;
    case LEVEL_BOSS:
        if (boss_fighting()) {
            uint8_t hp = boss_hp(), k;
            text_put(5, HUD_ROWS, "BOILER", PAL_RED);
            for (k = 0; k < 12; k++) {
                text_put((uint8_t)(12 + k), HUD_ROWS, (uint8_t)(k << 1) < hp ? "#" : "-", PAL_ORANGE);
            }
        } else {
            text_clear_row(HUD_ROWS);
            text_number(0, HUD_ROWS, g_round, PAL_YELLOW);
        }
        if (boss_defeated() && ++ending_delay >= 120) {
            enter_ending();
        }
        for (i = 0; i < NUM_PLAYERS; i++) {
            check_extra_lives(&g_players[i]);
        }
        return;
    default:
        break;
    }

    {
        uint16_t limit = level_defs[g_level].hurry_frames;
        uint8_t left = enemies_count();
        if (!g_hurry && limit && g_level_frames >= limit && left) {
            g_hurry = 1;
            hurry_text = HURRY_TEXT;
            enemies_make_angry();
            sfx(SFX_HURRY);
        }
        if (g_hurry && !chaser_active() && !clear_timer && left && g_level_frames >= limit + CHASER_DELAY) {
            chaser_spawn();
            sfx(SFX_HURRY);
        }
        /* The last enemy standing gets angry. */
        if (left == 1 && bubbles_trapped_count() == 0 && !enemies_entering()) {
            enemies_make_angry();
        }
    }
    if (hurry_text) {
        hurry_text--;
        if (hurry_text & 8) {
            text_put(11, 13, "HURRY UP!", PAL_ORANGE);
        } else {
            text_clear_row(13);
        }
    }

    for (i = 0; i < NUM_PLAYERS; i++) {
        check_extra_lives(&g_players[i]);
    }

    if (clear_timer) {
        if (--clear_timer == 0) {
            next_round(1);
        }
    } else if ((g_level_frames & 7) == 0 && enemies_count() == 0 && bubbles_trapped_count() == 0) {
        clear_timer = CLEAR_FRAMES;
        chaser_reset();
        items_clear_rocks();
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
        text_put((uint8_t)(14 + i), 16, i <= entry_pos ? s : ".",
                 (uint8_t)(i == entry_pos ? PAL_YELLOW : PAL_NONE));
    }
}

static void enter_name_entry(uint8_t player, uint8_t rank)
{
    clear_objects();
    bg_clear(0);
    bg_clear(1);
    g_scroll_y = 0;
    g_window_top = 0;
    page = 0;
    entry_player = player;
    entry_rank = rank;
    entry_pos = 0;
    entry_name[0] = 'A';
    entry_name[1] = 'A';
    entry_name[2] = 'A';
    entry_name[3] = 0;
    text_put(8, 4, "CONGRATULATIONS!", PAL_YELLOW);
    text_put(8, 7, player == 0 ? "PLAYER 1" : "PLAYER 2", player == 0 ? PAL_GREEN : PAL_CYAN);
    text_put(8, 8, "ENTER YOUR NAME", PAL_CYAN);
    text_put(8, 10, "SCORE", PAL_NONE);
    text_digits(15, 10, g_players[player].score, 8, PAL_NONE);
    text_put(8, 12, "ROUND", PAL_NONE);
    text_number(15, 12, g_round, PAL_NONE);
    text_put(5, 21, "UP/DOWN  CHOOSE LETTER", PAL_GREY);
    text_put(5, 23, "B OR Y   CONFIRM", PAL_GREY);
    text_put(5, 25, "LEFT     BACK", PAL_GREY);
    set_state(GS_NAME_ENTRY);
}

/* After game over: name entry for each player who made the table, in turn. */
static void next_name_entry(uint8_t from, uint8_t highlight)
{
    uint8_t i;
    for (i = from; i < NUM_PLAYERS; i++) {
        uint8_t rank = hiscore_rank(g_players[i].score);
        if (played[i] && rank < HISCORE_COUNT) {
            enter_name_entry(i, rank);
            return;
        }
    }
    enter_hiscores(highlight);
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
            uint8_t player = entry_player;
            hiscore_insert(entry_rank, entry_name, g_players[player].score, g_round);
            played[player] = 0;
            next_name_entry((uint8_t)(player + 1), entry_rank);
            return;
        }
    }
    draw_name_entry();
}

/* --- Ending ---------------------------------------------------------------------------- */
static uint8_t ending_pages(void)
{
    return (uint8_t)(ending_happy ? 3 : 2);
}

static void show_ending_page(void)
{
    const char *const *lines = ending_page < ending_pages()
        ? (ending_happy ? ending_happy_text[ending_page] : ending_sad_text[ending_page])
        : credits_text;
    uint8_t i;
    bg_clear(1);
    for (i = 0; i < ENDING_LINES; i++) {
        text_put(4, (uint8_t)(6 + (i << 1)), lines[i],
                 ending_page < ending_pages() ? PAL_NONE : (i == 0 ? PAL_YELLOW : PAL_CYAN));
    }
    state_timer = 0;
}

static void enter_ending(void)
{
    uint8_t i;
    ending_happy = (uint8_t)(players_in_game() == NUM_PLAYERS);
    enemies_clear();
    bubbles_clear();
    items_clear();
    effects_clear();
    boss_reset();
    bg_clear(0);
    for (i = 0; i < NUM_PLAYERS; i++) {
        if (g_players[i].state != PS_OFF) {
            g_players[i].state = PS_OFF;   /* the heroes appear as part of the ending pictures */
        }
    }
    g_scroll_y = 0;
    g_window_top = 0;
    ending_page = 0;
    set_state(GS_ENDING);
    show_ending_page();
    sfx(ending_happy ? SFX_CLEAR : SFX_GAMEOVER);
}

static void next_name_entry(uint8_t from, uint8_t highlight);

static void update_ending(uint16_t any_pressed)
{
    if ((any_pressed & (PAD_B | PAD_Y | PAD_START)) || state_timer >= ENDING_PAGE_FRAMES) {
        if (++ending_page > ending_pages()) {
            next_name_entry(0, HISCORE_COUNT);
            return;
        }
        sfx(SFX_MENU);
        show_ending_page();
    }
}

/* Happy: the workers dance under rising bubbles. Sad: they sleep, grey. */
static void draw_ending(void)
{
    static const uint8_t workers[7] = {
        SPR_E_WALK0, SPR_THROWER0, SPR_BOUNCER0, SPR_FLUTTER0, SPR_HOPPER0, SPR_SPITTER0, SPR_DROPPER0
    };
    uint8_t i, bob = (uint8_t)((g_frame >> 4) & 1);
    if (ending_page >= ending_pages()) {
        return;   /* credits: text only */
    }
    for (i = 0; i < 7; i++) {
        if (ending_happy) {
            oam_add((uint8_t)(40 + i * 26), (uint8_t)(176 - ((bob ^ (i & 1)) << 2)), (uint8_t)(workers[i] + bob), 0);
        } else {
            oam_add((uint8_t)(40 + i * 26), 180, workers[i], (uint8_t)(PAL_GREY << SPR_ATTR_PAL_SHIFT));
        }
    }
    if (ending_happy) {
        for (i = 0; i < 6; i++) {
            uint8_t y = (uint8_t)(200 - ((g_frame + i * 61) & 191));
            oam_add((uint8_t)((i & 1) ? SCREEN_W - 24 : 8), y, (uint8_t)(SPR_BUBBLE0 + (i % 3)), 0);
        }
        oam_add(104, 150, (uint8_t)(SPR_P_WALK0 + bob), 0);
        oam_add(136, 150, (uint8_t)(SPR_P_WALK0 + bob), (uint8_t)(SPR_ATTR_FLIP_X | (PAL_CYAN << SPR_ATTR_PAL_SHIFT)));
    } else {
        oam_add(120, 150, SPR_P_WALK0, 0);
    }
}

/* --- Story --------------------------------------------------------------------------- */
static void show_story_page(void)
{
    uint8_t i;
    bg_clear(1);
    for (i = 0; i < 6; i++) {
        text_put(4, (uint8_t)(8 + (i << 1)), story_text[story_page][i], PAL_NONE);
    }
    text_put(6, 25, "START: SKIP", PAL_GREY);
    state_timer = 0;
}

static void enter_story(uint8_t starter)
{
    clear_objects();
    bg_clear(0);
    bg_clear(1);
    g_scroll_y = 0;
    g_window_top = 0;
    page = 0;
    story_starter = starter;
    story_page = 0;
    set_state(GS_STORY);
    show_story_page();
}

static void update_story(uint16_t any_pressed)
{
    if (any_pressed & PAD_START) {
        new_game(story_starter);
        return;
    }
    if ((any_pressed & (PAD_B | PAD_Y)) || state_timer >= STORY_PAGE_FRAMES) {
        if (++story_page >= STORY_PAGES) {
            new_game(story_starter);
            return;
        }
        sfx(SFX_MENU);
        show_story_page();
    }
}

/* Sprites that go with the intro pages. */
static void draw_story(void)
{
    uint8_t i;
    uint8_t bob = (uint8_t)((g_frame >> 4) & 1);
    switch (story_page) {
    case 0:   /* bubbles rising along both sides of the text */
        for (i = 0; i < 6; i++) {
            uint8_t y = (uint8_t)(200 - ((g_frame + i * 61) & 191));
            oam_add((uint8_t)((i & 1) ? SCREEN_W - 24 : 8), y, (uint8_t)(SPR_BUBBLE0 + (i % 3)), 0);
        }
        break;
    case 1: { /* the bewitched workers */
        static const uint8_t workers[7] = {
            SPR_E_WALK0, SPR_THROWER0, SPR_BOUNCER0, SPR_FLUTTER0, SPR_HOPPER0, SPR_SPITTER0, SPR_DROPPER0
        };
        for (i = 0; i < 7; i++) {
            oam_add((uint8_t)(40 + i * 26), (uint8_t)(40 + (bob ^ (i & 1))), (uint8_t)(workers[i] + bob), 0);
        }
        oam_add(120, 172, (uint8_t)(SPR_CHASER0 + bob), 0);
        break;
    }
    default:  /* the two heroes */
        oam_add(96, 172, (uint8_t)(SPR_P_WALK0 + bob), 0);
        oam_add(144, 172, (uint8_t)(SPR_P_WALK0 + bob), (uint8_t)(SPR_ATTR_FLIP_X | (PAL_CYAN << SPR_ATTR_PAL_SHIFT)));
        break;
    }
}

/* --- Sprites ---------------------------------------------------------------------- */

/* Spare lives as small heads on the bottom wall: player 1 from the left,
 * player 2 (tinted) from the right. One sprite holds two heads. */
static void draw_lives(void)
{
    uint8_t i;
    for (i = 0; i < NUM_PLAYERS; i++) {
        const Player *p = &g_players[i];
        uint8_t spare, x;
        uint8_t attr = (uint8_t)(i == 0 ? 0u : (PAL_CYAN << SPR_ATTR_PAL_SHIFT));
        if (p->state == PS_OFF || p->lives < 2) {
            continue;
        }
        spare = (uint8_t)(p->lives - 1);
        x = (uint8_t)(i == 0 ? 0 : SCREEN_W - 16);
        while (spare) {
            uint8_t tile = (uint8_t)(spare >= 2 ? SPR_LIFE2 : SPR_LIFE1);
            /* player 2's single head sits at the right edge of its cell */
            oam_add(x, SCREEN_H - 16, tile, (uint8_t)(attr | (i && spare == 1 ? SPR_ATTR_FLIP_X : 0u)));
            spare = (uint8_t)(spare >= 2 ? spare - 2 : 0);
            x = (uint8_t)(i == 0 ? x + 16 : x - 16);
        }
    }
}

static void build_oam(void)
{
    uint8_t i;
    oam_count = 0;

    if (g_state == GS_TRANSITION || g_state == GS_ROUND_INTRO || g_state == GS_PLAY || g_state == GS_PAUSE ||
        g_state == GS_CONTINUE || g_state == GS_GAME_OVER) {
        /* Lower index = higher priority (drawn on top). */
        for (i = 0; i < NUM_PLAYERS; i++) {
            player_draw(&g_players[i]);
        }
        draw_lives();
        boss_draw();
        if (door_open && g_state != GS_TRANSITION) {
            oam_add(DOOR_X, PLAYER_SPAWN_Y, SPR_DOOR, 0);
        }
        chaser_draw();
        effects_draw();
        items_draw();
        enemies_draw();
        bubbles_draw();
    }

    if (g_state == GS_STORY) {
        draw_story();
    }
    if (g_state == GS_ENDING) {
        draw_ending();
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
    boss_level = (uint8_t)(level_count - 1);
    secret_level = 0xFF;
    for (i = 0; i < level_count; i++) {
        if (level_defs[i].kind == LEVEL_BOSS) {
            boss_level = i;
        } else if (level_defs[i].kind == LEVEL_SECRET) {
            secret_level = i;
        }
    }
    hiscore_init();
    clear_objects();
    boss_reset();
    door_open = 0;
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
            uint8_t starter = (uint8_t)((pressed[0] & PAD_START) ? 0 : 1);
            if (skip_story) {
                new_game(starter);
            } else {
                enter_story(starter);
            }
        } else if (state_timer >= TITLE_FRAMES) {
            enter_hiscores(HISCORE_COUNT);
        }
        break;

    case GS_STORY:
        update_story(any_pressed);
        break;

    case GS_ENDING:
        update_ending(any_pressed);
        break;

    case GS_HISCORES:
        if (any_pressed & PAD_START) {
            new_game((uint8_t)((pressed[0] & PAD_START) ? 0 : 1));
        } else if (state_timer >= HISCORE_FRAMES) {
            enter_title();
        }
        break;

    case GS_ROUND_INTRO:
        (void)handle_start(pressed);
        for (i = 0; i < NUM_PLAYERS; i++) {
            player_update(&g_players[i], 0, 0);
        }
        enemies_update();
        draw_hud_values();
        if (state_timer >= INTRO_FRAMES && !players_entering() && !enemies_entering()) {
            text_clear_row(12);
            text_clear_row(13);   /* floor name */
            text_clear_row(15);
            set_state(GS_PLAY);
        }
        break;

    case GS_PLAY:
        update_play(pad, pressed);
        draw_hud_values();
        break;

    case GS_PAUSE:
        if (any_pressed & PAD_START) {
            text_clear_row(13);
            g_state = GS_PLAY;
        }
        break;

    case GS_CONTINUE:
        update_continue(pressed);
        break;

    case GS_TRANSITION:
        update_transition();
        draw_hud_values();
        break;

    case GS_GAME_OVER:
        if (state_timer >= GAME_OVER_FRAMES) {
            next_name_entry(0, HISCORE_COUNT);
        }
        break;

    case GS_NAME_ENTRY:
        update_name_entry(pressed[entry_player]);
        break;
    }

    state_timer++;
    build_oam();
}

void game_debug_start_round(uint8_t round)
{
    first_level = (uint8_t)(round >= 1 && round - 1 <= boss_level ? round - 1 : 0);
}

void game_debug_show_ending(uint8_t happy)
{
    uint8_t i;
    for (i = 0; i < NUM_PLAYERS; i++) {
        g_players[i].state = (uint8_t)(i == 0 || happy ? PS_ALIVE : PS_OFF);
    }
    enter_ending();
}

void game_debug_skip_story(uint8_t skip)
{
    skip_story = skip;
}

void game_debug_clear_round(void)
{
    enemies_clear();
    bubbles_clear();
}

const Sprite *game_oam(void)
{
    return oam;
}

const uint16_t *game_bg(uint8_t layer)
{
    return layer == 1 ? g_bg1 : g_bg0;
}

uint16_t game_bg_scroll_y(void)
{
    return g_scroll_y;
}

uint8_t game_bg_window_top(void)
{
    return g_window_top;
}

uint16_t game_sfx(void)
{
    return g_sfx;
}
