/*
 * CPU partner (design guide: "CPU partner").
 *
 * The CPU partner is only an input source: each frame it produces a 16-bit
 * joypad word, exactly like a controller. Its behaviour is a small priority
 * list, checked from the top:
 *   1. free a trapped partner (co-op rescue)
 *   2. avoid danger: checked every frame by looking ahead (danger check below)
 *   3. pop trapped enemies, preferably once two or more are trapped
 *      (in the boss round: pop lightning bubbles)
 *   4. line up with an enemy at the same height and blow bubbles at it
 *   5. pop a BLASEN letter bubble it still needs (never one it has: that
 *      one would be wasted, so its dashes also avoid those)
 *   6. collect food (only when no human is playing; otherwise it leaves food)
 *   7. otherwise stay near the human player
 * Navigation uses the segment graph map2c stores with every round (NavSeg).
 * Difficulty comes from a reaction delay and deliberate aiming errors; the
 * delay applies to choosing a goal, not to dodging.
 *
 * Danger check (design guide): every frame, the positions of enemies,
 * shots, chasers and the boss are predicted for the next 26 frames from
 * their velocity, a handful of own actions is simulated for the same
 * frames, and the goal's action is only taken if it keeps a safe distance;
 * otherwise the action with the largest minimum distance wins. A shooter
 * that winds up counts with the shot it is about to fire: from the frame
 * the shot leaves, along its line. With an enemy in bubble range the CPU
 * shoots back instead, which traps a shooter before its shot leaves.
 */

#include "game/internal.h"

#define THINK_FRAMES   6      /* reaction delay: a new decision every 6 frames */
#define DANGER_X       30
#define DANGER_Y       18
#define SHOOT_RANGE    40     /* a bubble flies about 40 px (8 frames x 4 px + 8 px offset) */
#define SHOOT_RANGE_FAR 60    /* with the range candy */
#define FOLLOW_DIST    48
#define STUCK_FRAMES   90
#define UNSTICK_FRAMES 30
#define MAX_NAV        255
#define KEEP_DIST      18     /* never closer to an enemy on the same level */
#define SAFE_DIST      4      /* px of room left beyond a touch (collision is about 10 px apart) */
#define TOUCH_DIST     10
#define NEAR_SAMPLES   2      /* t <= 8 frames: what must be safe while shooting */
#define MAX_DANGERS    32     /* 8 enemies + 8 coming shots + shots in flight, chasers, boss */
#define NOT_YET        (-1000) /* a coming shot before it leaves: far away */
#define SAMPLES        5
#define DASH_REACH     ((DASH_FRAMES * 1152) >> 8)   /* px a dash covers: 12 x 4.5 */

/* The look-ahead times in frames. Everything computed per sample comes from
 * these constants: on the 65816 a multiplication by a variable (even one that
 * is only -1, 0 or 1) is a runtime call of hundreds of cycles, one by a
 * constant a few shifts and adds, and the danger check runs every frame for
 * every danger, sample and action. */
#define SAMPLE_0 4
#define SAMPLE_1 8
#define SAMPLE_2 14
#define SAMPLE_3 20
#define SAMPLE_4 26
static const uint8_t sample_t[SAMPLES] = { SAMPLE_0, SAMPLE_1, SAMPLE_2, SAMPLE_3, SAMPLE_4 };

/* px walked in t frames (WALK_SPEED, 0.92 px a frame), and risen in t frames
 * of a jump (never below 0), per sample */
#define WALK_PX(t) ((235 * (t)) >> 8)
#define RISE_PX(t) (((657 * (t) - 10 * (t) * (t)) >> 8) > 0 ? ((657 * (t) - 10 * (t) * (t)) >> 8) : 0)
static const int16_t walk_px[SAMPLES] = {
    WALK_PX(SAMPLE_0), WALK_PX(SAMPLE_1), WALK_PX(SAMPLE_2), WALK_PX(SAMPLE_3), WALK_PX(SAMPLE_4)
};
static const int16_t rise_px[SAMPLES] = {
    RISE_PX(SAMPLE_0), RISE_PX(SAMPLE_1), RISE_PX(SAMPLE_2), RISE_PX(SAMPLE_3), RISE_PX(SAMPLE_4)
};

/* d * v for a direction d of -1, 0 or 1, without a multiplication */
static int16_t signed_by(int8_t d, int16_t v)
{
    return (int16_t)(d < 0 ? -v : d > 0 ? v : 0);
}

enum { ACT_STAY, ACT_LEFT, ACT_RIGHT, ACT_JUMP, ACT_JUMP_LEFT, ACT_JUMP_RIGHT, ACT_DASH_LEFT, ACT_DASH_RIGHT, ACT_COUNT };
static const uint16_t act_pad[ACT_COUNT] = {
    0, PAD_LEFT, PAD_RIGHT, PAD_B, PAD_B | PAD_LEFT, PAD_B | PAD_RIGHT, PAD_L, PAD_R
};
#ifdef BB_DEBUG
static const char *const act_name[ACT_COUNT] = {
    "STAY", "LEFT", "RIGHT", "JUMP", "JUMP LEFT", "JUMP RIGHT", "DASH LEFT", "DASH RIGHT"
};
#endif

typedef struct {
    int16_t x[SAMPLES], y[SAMPLES];   /* predicted top-left, pixels */
    uint8_t reach;                    /* extra size (the boss is big) */
} Danger;

static Danger   dangers[MAX_DANGERS];
static uint8_t  danger_count;
static uint16_t prev_ex[MAX_ENEMIES], prev_ey[MAX_ENEMIES];
static uint16_t prev_cx[NUM_PLAYERS], prev_cy[NUM_PLAYERS];
static uint16_t prev_bx, prev_by;
static uint16_t predicted_frame = 0xFFFFu;
#ifdef BB_DEBUG
static uint8_t  view_action;      /* for the debug view */
static uint8_t  view_goal;
#define VIEW_ACTION(a) (view_action = (uint8_t)(a))
#define VIEW_GOAL(g) (view_goal = (uint8_t)(g))
#else
#define VIEW_ACTION(a) ((void)0)
#define VIEW_GOAL(g) ((void)0)
#endif
/* The goals of the priority list (header comment), for the debug view. */
enum { GOAL_NONE, GOAL_RESCUE, GOAL_POP, GOAL_HUNT, GOAL_LETTER, GOAL_FOOD, GOAL_FOLLOW };

static struct {
    uint16_t pad;           /* buttons held until the next decision */
    uint8_t  think;
    uint8_t  stuck;
    uint8_t  unstick;       /* frames left of the "get unstuck" move */
    int8_t   unstick_dir;
    uint8_t  fire;          /* alternates so Y is pressed, released, pressed */
    uint16_t last_out;      /* the pad word given last frame */
    uint16_t last_x, last_y;
} cpu[NUM_PLAYERS];

void cpu_reset(void)
{
    for (uint8_t i = 0; i < NUM_PLAYERS; i++) {
        cpu[i].pad = 0;
        cpu[i].think = 0;
        cpu[i].stuck = 0;
        cpu[i].unstick = 0;
        cpu[i].fire = 0;
    }
}

/* --- Navigation ----------------------------------------------------------------- */
static int16_t col_of(uint16_t x)
{
    return (int16_t)((PX(x) + 4) >> 3);
}

static int16_t row_of(uint16_t y)
{
    int16_t r = (int16_t)(((int16_t)PX(y) + 15 - FIELD_Y) >> 3);
    return r < 0 ? 0 : r;
}

static int16_t dist16(int16_t a, int16_t b)
{
    return (int16_t)(a > b ? a - b : b - a);
}

static int16_t seg_find(const LevelDef *d, int16_t c, int16_t r)
{
    for (uint8_t i = 0; i < d->nav_count; i++) {
        const NavSeg *s = &d->nav[i];
        if (s->row == r && c >= (int16_t)s->x0 - 1 && c <= (int16_t)s->x1 + 1) {
            return i;
        }
    }
    return -1;
}

/* The segment to stand on to reach (c, r): at or below r, close in x. */
static int16_t seg_for_target(const LevelDef *d, int16_t c, int16_t r)
{
    int16_t best = -1, best_cost = 0x7FFF;
    for (uint8_t i = 0; i < d->nav_count; i++) {
        const NavSeg *s = &d->nav[i];
        int16_t dx = c < s->x0 ? (int16_t)(s->x0 - c) : c > s->x1 ? (int16_t)(c - s->x1) : 0;
        int16_t dy = (int16_t)(s->row - r);
        int16_t cost = (int16_t)(dy >= 0 ? dy + dx : 40 + dist16(dy, 0) + dx);
        if (cost < best_cost) {
            best_cost = cost;
            best = i;
        }
    }
    return best;
}

/* Breadth-first search; returns the first segment on the way, or -1. */
static int16_t first_hop(const LevelDef *d, uint8_t from, uint8_t to)
{
    static uint8_t prev[MAX_NAV];
    static uint8_t queue[MAX_NAV];
    static uint8_t seen[MAX_NAV];

    for (uint8_t i = 0; i < d->nav_count; i++) {
        seen[i] = 0;
    }
    seen[from] = 1;
    uint8_t tail = 0;
    queue[tail++] = from;
    uint8_t head = 0;
    while (head < tail) {
        uint8_t s = queue[head++];
        if (s == to) {
            while (prev[s] != from && s != from) {
                s = prev[s];
            }
            return s;
        }
        for (uint8_t k = 0; k < d->nav[s].edge_count; k++) {
            uint8_t t = d->nav_edges[d->nav[s].edge_first + k];
            if (!seen[t]) {
                seen[t] = 1;
                prev[t] = s;
                queue[tail++] = t;
            }
        }
    }
    return -1;
}

static uint16_t toward(int16_t from_px, int16_t to_px, uint8_t slack)
{
    if (to_px < from_px - slack) {
        return PAD_LEFT;
    }
    if (to_px > from_px + slack) {
        return PAD_RIGHT;
    }
    return 0;
}

/* Buttons that bring the player towards the sprite position (tx, ty). */
static uint16_t move_to(const Player *p, uint16_t tx, uint16_t ty)
{
    const LevelDef *d = &level_defs[g_level];
    int16_t c = col_of(p->x), r = row_of(p->y);
    int16_t tc = col_of(tx), tr = row_of(ty);
    int16_t px = PX(p->x), gx = PX(tx);

    if (!p->on_ground || d->nav_count == 0) {
        return toward(px, gx, 2);                 /* steer in the air */
    }
    int16_t s = seg_find(d, c, r);
    int16_t t = seg_for_target(d, tc, tr);
    if (s < 0 || t < 0 || s == t) {
        uint16_t pad = toward(px, gx, 3);
        /* the target hangs above us (a bubble): jump up into it */
        if ((int16_t)PX(ty) < (int16_t)PX(p->y) - 10 && dist16(px, gx) < 14) {
            pad |= PAD_B;
        }
        return pad;
    }
    int16_t n = first_hop(d, (uint8_t)s, (uint8_t)t);
    if (n < 0) {
        return (uint16_t)(toward(px, gx, 3) | ((g_frame & 63) == 0 ? PAD_B : 0));
    }
    {
        const NavSeg *ns = &d->nav[n];
        const NavSeg *cs = &d->nav[s];
        int16_t want = c < ns->x0 ? ns->x0 : c > ns->x1 ? ns->x1 : c;
        if (ns->row < cs->row) {
            /* up: get under the platform, then jump */
            uint16_t pad = toward((int16_t)(c << 3), (int16_t)(want << 3), 4);
            if (dist16(c, want) <= 2) {
                pad |= PAD_B;
            }
            return pad;
        }
        if (want == c) {
            /* down, and the lower platform is right below: walk off the nearer edge */
            int16_t mid = (int16_t)((ns->x0 + ns->x1) >> 1);
            return (uint16_t)((mid < c || cs->x1 >= LEVEL_W - 4) && cs->x0 > 2 ? PAD_LEFT : PAD_RIGHT);
        }
        return toward((int16_t)(c << 3), (int16_t)(want << 3), 2);
    }
}

/* --- Decisions -------------------------------------------------------------------- */
static const Enemy *nearest_enemy(const Player *p)
{
    const Enemy *best = 0;
    uint16_t best_d = 0xFFFFu;
    for (uint8_t i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *e = &g_enemies[i];
        if (!e->active || e->entering) {
            continue;
        }
        uint16_t dd = (uint16_t)(dist16(PX(e->x), PX(p->x)) + dist16(PX(e->y), PX(p->y)));
        if (dd < best_d) {
            best_d = dd;
            best = e;
        }
    }
    return best;
}

/* A bubble worth popping: trapped enemies once two wait, or one about to
 * escape; in the boss round any lightning bubble. */
static const Bubble *bubble_to_pop(const Player *p)
{
    const Bubble *best = 0, *urgent = 0;
    uint16_t best_d = 0xFFFFu;
    uint8_t trapped = 0;
    uint8_t boss = (uint8_t)(level_defs[g_level].kind == LEVEL_BOSS);
    for (uint8_t i = 0; i < MAX_BUBBLES; i++) {
        const Bubble *b = &g_bubbles[i];
        uint8_t wanted = (uint8_t)(b->state == BS_TRAPPED || (boss && b->state == BS_FLOAT && b->kind == BK_LIGHTNING));
        if (!wanted) {
            continue;
        }
        trapped++;
        if (b->state == BS_TRAPPED && b->age > 180) {
            urgent = b;
        }
        uint16_t dd = (uint16_t)(dist16(PX(b->x), PX(p->x)) + dist16(PX(b->y), PX(p->y)));
        if (dd < best_d) {
            best_d = dd;
            best = b;
        }
    }
    if (urgent) {
        return urgent;
    }
    return (trapped >= 2 || boss || enemies_count() == 0) ? best : 0;
}

/* The nearest floating letter bubble the player still misses. */
static const Bubble *letter_to_take(const Player *p)
{
    const Bubble *best = 0;
    uint16_t best_d = 0xFFFFu;
    for (uint8_t i = 0; i < MAX_BUBBLES; i++) {
        const Bubble *b = &g_bubbles[i];
        if (b->state != BS_FLOAT || b->kind != BK_LETTER || (p->letters & (1u << b->letter))) {
            continue;
        }
        uint16_t dd = (uint16_t)(dist16(PX(b->x), PX(p->x)) + dist16(PX(b->y), PX(p->y)));
        if (dd < best_d) {
            best_d = dd;
            best = b;
        }
    }
    return best;
}

/* A dash pops every bubble on its way: would one in direction dir pop a
 * letter the player already has (and waste it)? */
static uint8_t dash_wastes_letter(const Player *p, int8_t dir)
{
    int16_t x0 = (int16_t)PX(p->x), x1 = (int16_t)(x0 + dir * DASH_REACH);
    int16_t lo = (int16_t)((x0 < x1 ? x0 : x1) - DASH_POP_X), hi = (int16_t)((x0 < x1 ? x1 : x0) + DASH_POP_X);
    for (uint8_t i = 0; i < MAX_BUBBLES; i++) {
        const Bubble *b = &g_bubbles[i];
        int16_t bx = (int16_t)PX(b->x);
        if (b->state == BS_FLOAT && b->kind == BK_LETTER && (p->letters & (1u << b->letter)) &&
            bx > lo && bx < hi && dist16(PX(b->y), PX(p->y)) < DASH_POP_Y) {
            return 1;
        }
    }
    return 0;
}

static const Item *food_to_take(const Player *p)
{
    const Item *best = 0;
    uint16_t best_d = 0xFFFFu;
    for (uint8_t i = 0; i < MAX_ITEMS; i++) {
        const Item *it = &g_items[i];
        if (it->kind != IT_FOOD && it->kind != IT_BONUS && it->kind != IT_POWERUP) {
            continue;
        }
        uint16_t dd = (uint16_t)(dist16(PX(it->x), PX(p->x)) + dist16(PX(it->y), PX(p->y)));
        if (dd < best_d) {
            best_d = dd;
            best = it;
        }
    }
    return best;
}

static uint16_t decide(Player *p)
{
    const Player *o = &g_players[p->id ^ 1];
    /* 1. free the partner */
    if (o->state == PS_RESCUE) {
        VIEW_GOAL(GOAL_RESCUE);
        return move_to(p, o->x, o->y);
    }
    /* 2. danger: handled every frame in cpu_pad (look-ahead) */
    /* 3. pop trapped enemies */
    const Bubble *b = bubble_to_pop(p);
    if (b) {
        VIEW_GOAL(GOAL_POP);
        return move_to(p, b->x, b->y);
    }
    /* 4. hunt */
    const Enemy *e = nearest_enemy(p);
    if (e) {
        VIEW_GOAL(GOAL_HUNT);
        int16_t ex = PX(e->x), mx = PX(p->x);
        if (dist16(PX(e->y), PX(p->y)) < 12) {
            /* same level: approach only to bubble range; facing it, shoot
             * (point-blank traps at once); too close and facing away: back off */
            uint8_t left = (uint8_t)(ex < mx);
            if (dist16(ex, mx) < KEEP_DIST && left != p->facing_left) {
                return left ? PAD_RIGHT : PAD_LEFT;
            }
            if (dist16(ex, mx) < ((p->powers & POWER_BIT(POWER_RANGE)) ? SHOOT_RANGE_FAR : SHOOT_RANGE)) {
                if (left != p->facing_left) {
                    return left ? PAD_LEFT : PAD_RIGHT;   /* turn first */
                }
                cpu[p->id].fire ^= 1;
                /* about one shot in eight goes wrong on purpose */
                return (uint16_t)(cpu[p->id].fire && (rng_next() & 7) ? PAD_Y : 0);
            }
        }
        return move_to(p, e->x, e->y);
    }
    /* 5. a letter it still needs */
    b = letter_to_take(p);
    if (b) {
        VIEW_GOAL(GOAL_LETTER);
        return move_to(p, b->x, b->y);
    }
    /* 6. food, but leave it for a human partner */
    if (o->state == PS_OFF || o->cpu) {
        const Item *f = food_to_take(p);
        if (f) {
            VIEW_GOAL(GOAL_FOOD);
            return move_to(p, f->x, f->y);
        }
    }
    /* 7. stay near the human */
    if (o->state == PS_ALIVE && dist16(PX(o->x), PX(p->x)) + dist16(PX(o->y), PX(p->y)) > FOLLOW_DIST) {
        VIEW_GOAL(GOAL_FOLLOW);
        return move_to(p, o->x, o->y);
    }
    VIEW_GOAL(GOAL_NONE);
    return 0;
}


/* --- Danger check -------------------------------------------------------------------- */
typedef char samples_are_five[SAMPLES == 5 ? 1 : -1];   /* add_danger writes them out */

static void add_danger(uint16_t x, uint16_t y, fx8_8 vx, fx8_8 vy, uint8_t reach)
{
    if (danger_count >= MAX_DANGERS) {
        return;
    }
    /* a jump in position (new object in the slot, wrap-around) is no speed */
    if (vx > FX(3, 0) || vx < -FX(3, 0) || vy > FX(3, 0) || vy < -FX(3, 0)) {
        vx = 0;
        vy = 0;
    }
    Danger *d = &dangers[danger_count++];
    d->reach = reach;
    int16_t px = (int16_t)PX(x);
    int16_t py = (int16_t)PX(y);
    /* v * t fits 16 bits (|v| <= 3 px, t <= 26 frames): the same values as in 32 bits */
#define AHEAD(v, t) ((int16_t)((v) * (t)) >> 8)
    d->x[0] = (int16_t)(px + AHEAD(vx, SAMPLE_0));
    d->x[1] = (int16_t)(px + AHEAD(vx, SAMPLE_1));
    d->x[2] = (int16_t)(px + AHEAD(vx, SAMPLE_2));
    d->x[3] = (int16_t)(px + AHEAD(vx, SAMPLE_3));
    d->x[4] = (int16_t)(px + AHEAD(vx, SAMPLE_4));
    d->y[0] = (int16_t)(py + AHEAD(vy, SAMPLE_0));
    d->y[1] = (int16_t)(py + AHEAD(vy, SAMPLE_1));
    d->y[2] = (int16_t)(py + AHEAD(vy, SAMPLE_2));
    d->y[3] = (int16_t)(py + AHEAD(vy, SAMPLE_3));
    d->y[4] = (int16_t)(py + AHEAD(vy, SAMPLE_4));
#undef AHEAD
}

/* The shot an enemy winds up for: nowhere until it leaves in `in` frames,
 * then along its line. */
static void add_coming_shot(const Enemy *e)
{
    uint8_t in = enemy_shot_in(e);
    int8_t dir = (int8_t)(e->facing_left ? -1 : 1);
    int16_t x0 = (int16_t)PX(e->x), y0 = (int16_t)PX(e->y);

    if (!in || danger_count >= MAX_DANGERS) {
        return;
    }
    if (e->type == ENEMY_TYPE_SPITTER) {
        x0 = (int16_t)(x0 + signed_by(dir, SPIT_OFFSET));
    } else if (e->type != ENEMY_TYPE_THROWER) {
        y0 = (int16_t)(y0 + DOWN_SHOT_OFFSET);
    }
    Danger *d = &dangers[danger_count++];
    d->reach = 0;
    for (uint8_t k = 0; k < SAMPLES; k++) {
        int16_t t = (int16_t)sample_t[k] - in;
        if (t < 0) {
            d->x[k] = NOT_YET;
            d->y[k] = NOT_YET;
            continue;
        }
        /* speed * t by a constant speed (fits 16 bits: t <= 26), then the
         * direction as a sign: the same as (dir * speed * t) >> 8 */
        switch (e->type) {
        case ENEMY_TYPE_THROWER:
            d->x[k] = (int16_t)(x0 + (signed_by(dir, (int16_t)(t * ROCK_SPEED)) >> 8));
            d->y[k] = y0;
            break;
        case ENEMY_TYPE_SPITTER:
            d->x[k] = (int16_t)(x0 + (signed_by(dir, (int16_t)(t * SPIT_SPEED)) >> 8));
            d->y[k] = y0;
            break;
        default:
            d->x[k] = x0;
            d->y[k] = (int16_t)(y0 + ((int16_t)(t * DOWN_SHOT_SPEED) >> 8));
            break;
        }
    }
}

/* Once per frame: where everything dangerous will be. Velocities of enemies,
 * chasers and the boss come from their position one frame ago. */
static void predict_dangers(void)
{
    if (predicted_frame == g_frame) {
        return;
    }
    predicted_frame = g_frame;
    danger_count = 0;
    for (uint8_t i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *e = &g_enemies[i];
        if (e->active && !e->entering && !g_freeze) {
            add_danger(e->x, e->y, (fx8_8)(e->x - prev_ex[i]), (fx8_8)(e->y - prev_ey[i]), 0);
            add_coming_shot(e);
        }
        prev_ex[i] = e->x;
        prev_ey[i] = e->y;
    }
    for (uint8_t i = 0; i < MAX_ITEMS; i++) {
        const Item *it = &g_items[i];
        if (it->kind == IT_ROCK) {
            add_danger(it->x, it->y, it->vx, it->vy, 0);
        }
    }
    uint16_t x, y;
    for (uint8_t i = 0; i < NUM_PLAYERS; i++) {
        if (chaser_get(i, &x, &y)) {
            add_danger(x, y, (fx8_8)(x - prev_cx[i]), (fx8_8)(y - prev_cy[i]), 2);
            prev_cx[i] = x;
            prev_cy[i] = y;
        }
    }
    if (boss_fighting()) {
        x = (uint16_t)(boss_center_x() - TO_POS(8));
        y = (uint16_t)(boss_center_y() - TO_POS(8));
        add_danger(x, y, (fx8_8)(x - prev_bx), (fx8_8)(y - prev_by), 10);
        prev_bx = x;
        prev_by = y;
    }
}

/* Smallest room (px beyond a touch) to any danger over the first
 * `samples` look-ahead times, if the player did `act`. Later predictions
 * are less certain and get more slack (t / 2). Frames invulnerable from a
 * dash (the dash and DASH_GRACE after it) do not count. */
static int16_t evaluate(const Player *p, uint8_t act, uint8_t samples)
{
    int16_t best = 0x7FFF;
    int8_t dir = (int8_t)(act == ACT_LEFT || act == ACT_JUMP_LEFT || act == ACT_DASH_LEFT ? -1 :
                          act == ACT_RIGHT || act == ACT_JUMP_RIGHT || act == ACT_DASH_RIGHT ? 1 : 0);
    uint8_t jump = (uint8_t)(act >= ACT_JUMP && act <= ACT_JUMP_RIGHT && p->on_ground);
    uint8_t dash = (uint8_t)(act >= ACT_DASH_LEFT);

    for (uint8_t k = 0; k < samples; k++) {
        int16_t t = sample_t[k];
        int16_t ox, oy = (int16_t)PX(p->y);
        if (dash) {
            /* Invulnerable for the whole look-ahead (every sample is within
             * DASH_FRAMES + DASH_GRACE), so what counts is where the dash
             * ends: that spot against the furthest prediction. */
            if (k != samples - 1) {
                continue;
            }
            ox = (int16_t)(PX(p->x) + signed_by(dir, DASH_REACH));
        } else {
            ox = (int16_t)(PX(p->x) + signed_by(dir, walk_px[k]));
        }
        if (jump) {
            oy = (int16_t)(oy - rise_px[k]);
        }
        if (ox < BODY_MIN_CX - 8) ox = BODY_MIN_CX - 8;
        if (ox > BODY_MAX_CX - 8) ox = BODY_MAX_CX - 8;
        for (uint8_t i = 0; i < danger_count; i++) {
            int16_t ddx = dist16(ox, dangers[i].x[k]);
            int16_t ddy = dist16(oy, dangers[i].y[k]);
            int16_t dd = (int16_t)((ddx > ddy ? ddx : ddy) - TOUCH_DIST - dangers[i].reach + (t >> 1));
            if (dd < best) {
                best = dd;
            }
        }
    }
    return best;
}

static uint8_t act_of(uint16_t pad)
{
    uint8_t jump = (uint8_t)((pad & PAD_B) != 0);
    if (pad & PAD_L) return ACT_DASH_LEFT;
    if (pad & PAD_R) return ACT_DASH_RIGHT;
    if (pad & PAD_LEFT) return jump ? ACT_JUMP_LEFT : ACT_LEFT;
    if (pad & PAD_RIGHT) return jump ? ACT_JUMP_RIGHT : ACT_RIGHT;
    return jump ? ACT_JUMP : ACT_STAY;
}

/* An enemy on the same level within bubble range: shooting it (after
 * turning in the same frame) removes the danger. */
static uint16_t shoot_back(const Player *p)
{
    int16_t range = (p->powers & POWER_BIT(POWER_RANGE)) ? SHOOT_RANGE_FAR : SHOOT_RANGE;
    if (p->shoot_cooldown || (cpu[p->id].last_out & PAD_Y) || g_freeze) {
        return 0;
    }
    for (uint8_t i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *e = &g_enemies[i];
        if (e->active && !e->entering && dist16(PX(e->y), PX(p->y)) < 12 && dist16(PX(e->x), PX(p->x)) < range) {
            return (uint16_t)(PAD_Y | (PX(e->x) < PX(p->x) ? PAD_LEFT : PAD_RIGHT));
        }
    }
    return 0;
}

/* Keeps the goal's buttons if they are safe, else the safest action. */
static uint16_t dodge(const Player *p, uint16_t goal)
{
    uint8_t can_dash = player_can_dash(p);

    predict_dangers();
    uint8_t best_act = act_of(goal);
    /* still invulnerable after a respawn, or a shot about to go off: trust the goal */
    if (p->invulnerable > 20 || ((goal & PAD_Y) && p->shoot_cooldown == 0)) {
        VIEW_ACTION(best_act);
        return goal;
    }
    /* shooting takes care of the danger itself: only the next frames must be safe */
    int16_t best = evaluate(p, best_act, (uint8_t)((goal & PAD_Y) ? NEAR_SAMPLES : SAMPLES));
    if (best >= SAFE_DIST || danger_count == 0) {
        VIEW_ACTION(best_act);
        return goal;
    }
    {
        uint16_t shot = shoot_back(p);
        if (shot) {
            VIEW_ACTION(act_of(shot));
            return shot;
        }
    }
    best = evaluate(p, best_act, SAMPLES);
    for (uint8_t a = 0; a < ACT_COUNT; a++) {
        if ((a >= ACT_DASH_LEFT && (!can_dash || dash_wastes_letter(p, (int8_t)(a == ACT_DASH_LEFT ? -1 : 1)))) ||
            (a >= ACT_JUMP && a <= ACT_JUMP_RIGHT && !p->on_ground)) {
            continue;
        }
        int16_t v = evaluate(p, a, SAMPLES);
        if (v > best) {
            best = v;
            best_act = a;
        }
    }
    VIEW_ACTION(best_act);
    return (uint16_t)(act_pad[best_act] | (goal & PAD_Y));
}

#ifdef BB_DEBUG
uint8_t game_debug_cpu_view(DebugBox *out, uint8_t max, const char **action)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < danger_count; i++) {
        for (uint8_t k = 0; k < SAMPLES && n < max; k++) {
            int16_t x = dangers[i].x[k], y = dangers[i].y[k];
            if (x < 0 || x > SCREEN_W - 16 || y < 0 || y > SCREEN_H - 16) {
                continue;
            }
            out[n].x = (uint8_t)(x + 6);
            out[n].y = (uint8_t)(y + 6);
            out[n].w = 4;
            out[n].h = 4;
            out[n].kind = k;
            n++;
        }
    }
    *action = act_name[view_action];
    return n;
}

const char *game_debug_cpu_goal(void)
{
    static const char *const names[] = { "NOTHING", "FREE PARTNER", "POP TRAPPED", "HUNT", "LETTER", "FOOD", "FOLLOW" };
    return names[view_goal];
}
#endif

uint16_t cpu_pad(Player *p)
{
    uint8_t id = p->id;

    if (g_state != GS_PLAY || p->state != PS_ALIVE) {
        cpu[id].pad = 0;
        return 0;
    }
    /* not moving although it wants to: try something else for a moment */
    if (cpu[id].unstick) {
        cpu[id].unstick--;
        return (uint16_t)((cpu[id].unstick_dir < 0 ? PAD_LEFT : PAD_RIGHT) | (cpu[id].unstick > UNSTICK_FRAMES / 2 ? PAD_B : 0));
    }
    if (p->x == cpu[id].last_x && p->y == cpu[id].last_y && (cpu[id].pad & (PAD_LEFT | PAD_RIGHT))) {
        if (++cpu[id].stuck >= STUCK_FRAMES) {
            cpu[id].stuck = 0;
            cpu[id].unstick = UNSTICK_FRAMES;
            cpu[id].unstick_dir = (int8_t)((cpu[id].pad & PAD_LEFT) ? 1 : -1);
        }
    } else {
        cpu[id].stuck = 0;
    }
    cpu[id].last_x = p->x;
    cpu[id].last_y = p->y;

    if (cpu[id].think == 0) {
        cpu[id].pad = decide(p);
        cpu[id].think = THINK_FRAMES;
    } else {
        cpu[id].think--;
    }
    /* dodging is checked every frame, without the reaction delay */
    cpu[id].last_out = dodge(p, cpu[id].pad);
    /* Releasing B cuts a jump short (JUMP_CUT). Navigation and the danger
     * check both plan with full jumps, so B stays held while rising. */
    if (!p->on_ground && p->vy < 0) {
        cpu[id].last_out |= PAD_B;
    }
    return cpu[id].last_out;
}
