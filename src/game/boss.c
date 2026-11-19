/*
 * The Grumble Boiler, boss of round 100 (docs/story.md).
 *
 * A 32x32 iron kettle (four sprites) that floats around the top of the
 * factory and spits soot balls at the players. Touching it is deadly.
 * Bubbles bounce off it; only lightning bolts hurt it. When its strength
 * is gone it is trapped in a giant bubble; a player who touches that
 * bubble pops it, and the ending begins.
 */

#include "game/internal.h"

#define BOSS_HP          24
#define BOSS_SIZE        32
#define BOSS_LEFT        16
#define BOSS_RIGHT       (SCREEN_W - 16 - BOSS_SIZE)
#define BOSS_TOP         (FIELD_Y + 8)
#define BOSS_BOTTOM      (FIELD_BOTTOM - 8 - BOSS_SIZE - 24)
#define BOSS_SPEED       FX(0, 128)
#define BOSS_SPEED_ANGRY FX(0, 220)   /* below half strength */
#define BOSS_SPEED_MAD   FX(1, 32)    /* nearly beaten */
#define SPIT_EVERY       100
#define SPIT_EVERY_ANGRY 64
#define SOOT_SPEED_X     FX(1, 64)
#define HURT_FRAMES      20
#define TRAPPED_Y        (FIELD_Y + 64)

enum { BOSS_OFF, BOSS_FIGHT, BOSS_TRAPPED, BOSS_POPPED };

static struct {
    uint8_t  state;
    uint8_t  hp;
    uint8_t  hurt;          /* frames left flashing */
    uint8_t  left, down;    /* movement direction */
    uint8_t  spit_timer;
    uint16_t x, y;          /* top-left of the 32x32 body, 8.8 */
} boss;

void boss_reset(void)
{
    boss.state = BOSS_OFF;
}

void boss_spawn(void)
{
    boss.state = BOSS_FIGHT;
    boss.hp = BOSS_HP;
    boss.hurt = 0;
    boss.left = 0;
    boss.down = 1;
    boss.spit_timer = SPIT_EVERY;
    boss.x = TO_POS(112);
    boss.y = TO_POS(BOSS_TOP);
}

uint8_t boss_fighting(void)
{
    return (uint8_t)(boss.state == BOSS_FIGHT);
}

uint8_t boss_hp(void)
{
    return boss.state == BOSS_FIGHT ? boss.hp : 0;
}

uint8_t boss_defeated(void)
{
    return (uint8_t)(boss.state == BOSS_POPPED);
}

uint16_t boss_center_x(void)
{
    return (uint16_t)(boss.x + TO_POS(BOSS_SIZE / 2));
}

uint16_t boss_center_y(void)
{
    return (uint16_t)(boss.y + TO_POS(BOSS_SIZE / 2));
}

/* Is an object (16x16 sprite at x, y) touching the boss body? */
static uint8_t touches(uint16_t x, uint16_t y, uint8_t reach)
{
    int16_t dx = (int16_t)((int16_t)(PX(x) + 8) - (int16_t)(PX(boss.x) + BOSS_SIZE / 2));
    int16_t dy = (int16_t)((int16_t)(PX(y) + 8) - (int16_t)(PX(boss.y) + BOSS_SIZE / 2));
    if (dx < 0) dx = (int16_t)-dx;
    if (dy < 0) dy = (int16_t)-dy;
    return (uint8_t)(dx < reach && dy < reach);
}

uint8_t boss_touch(uint16_t x, uint16_t y)
{
    return (uint8_t)(boss.state == BOSS_FIGHT && touches(x, y, 20));
}

/* A lightning bolt at (x, y): hurts the boss. Returns 1 if it hit. */
uint8_t boss_hit_by_bolt(uint16_t x, uint16_t y)
{
    if (boss.state != BOSS_FIGHT || !touches(x, y, 22)) {
        return 0;
    }
    if (!boss.hurt) {
        boss.hp--;
        boss.hurt = HURT_FRAMES;
        sfx(SFX_KILL);
        if (boss.hp == 0) {
            boss.state = BOSS_TRAPPED;
            items_clear_rocks();
            sfx(SFX_TRAP);
        }
    }
    return 1;
}

static void spit(void)
{
    const Player *p = player_nearest(boss.x, boss.y);
    int8_t dir = (int8_t)(PX(p->x) < PX(boss.x) + 8 ? -1 : 1);
    uint16_t mouth_x = (uint16_t)(boss.x + TO_POS(8));
    uint16_t mouth_y = (uint16_t)(boss.y + TO_POS(16));
    for (uint8_t k = 0; k < 3; k++) {
        item_spawn_soot(mouth_x, mouth_y, (fx8_8)(dir < 0 ? -SOOT_SPEED_X : SOOT_SPEED_X),
                        (fx8_8)(FX(0, 128) + (fx8_8)(k << 7)));
    }
    sfx(SFX_SHOOT);
}

void boss_update(void)
{
    fx8_8 speed;

    switch (boss.state) {
    case BOSS_FIGHT:
        speed = boss.hp <= BOSS_HP / 4 ? BOSS_SPEED_MAD : boss.hp <= BOSS_HP / 2 ? BOSS_SPEED_ANGRY : BOSS_SPEED;
        boss.x = (uint16_t)(boss.left ? boss.x - (uint16_t)speed : boss.x + (uint16_t)speed);
        boss.y = (uint16_t)(boss.down ? boss.y + (uint16_t)speed : boss.y - (uint16_t)speed);
        if (PX(boss.x) <= BOSS_LEFT) {
            boss.x = TO_POS(BOSS_LEFT);
            boss.left = 0;
        } else if (PX(boss.x) >= BOSS_RIGHT) {
            boss.x = TO_POS(BOSS_RIGHT);
            boss.left = 1;
        }
        if (PX(boss.y) <= BOSS_TOP) {
            boss.y = TO_POS(BOSS_TOP);
            boss.down = 1;
        } else if (PX(boss.y) >= BOSS_BOTTOM) {
            boss.y = TO_POS(BOSS_BOTTOM);
            boss.down = 0;
        }
        if (boss.hurt) {
            boss.hurt--;
        }
        if (--boss.spit_timer == 0) {
            spit();
            boss.spit_timer = (uint8_t)(boss.hp <= BOSS_HP / 2 ? SPIT_EVERY_ANGRY : SPIT_EVERY);
        }
        break;

    case BOSS_TRAPPED:
        /* The giant bubble drifts to a reachable height and waits. */
        if (PX(boss.y) < TRAPPED_Y) {
            boss.y = (uint16_t)(boss.y + FX(0, 64));
        } else if (PX(boss.y) > TRAPPED_Y) {
            boss.y = (uint16_t)(boss.y - FX(0, 64));
        }
        for (uint8_t i = 0; i < NUM_PLAYERS; i++) {
            const Player *p = &g_players[i];
            if (p->state == PS_ALIVE && touches(p->x, p->y, 24)) {
                boss.state = BOSS_POPPED;
                score_add(g_players[i].score, 5, 1);   /* +100,000 */
                item_spawn(IT_BURST, boss.x, boss.y, 0);
                item_spawn(IT_BURST, (uint16_t)(boss.x + TO_POS(16)), (uint16_t)(boss.y + TO_POS(16)), 0);
                sfx(SFX_CLEAR);
                return;
            }
        }
        break;

    default:
        break;
    }
}

void boss_draw(void)
{
    uint8_t base, attr = 0;
    uint8_t x = PX(boss.x), y = PX(boss.y);

    if (boss.state == BOSS_OFF || boss.state == BOSS_POPPED) {
        return;
    }
    if (boss.state == BOSS_TRAPPED) {
        for (uint8_t k = 0; k < 4; k++) {   /* the giant bubble in front */
            oam_add((uint8_t)(x + ((k & 1) << 4)), (uint8_t)(y + ((k >> 1) << 4)), (uint8_t)(SPR_GIANT_BUBBLE + k), 0);
        }
        attr = (uint8_t)(PAL_GREY << SPR_ATTR_PAL_SHIFT);
        base = SPR_BOSS0;
    } else {
        base = (uint8_t)(boss.spit_timer < 20 ? SPR_BOSS1 : SPR_BOSS0);   /* mouth opens before it spits */
        if (boss.hurt & 4) {
            attr = (uint8_t)(PAL_RED << SPR_ATTR_PAL_SHIFT);
        }
    }
    for (uint8_t k = 0; k < 4; k++) {
        oam_add((uint8_t)(x + ((k & 1) << 4)), (uint8_t)(y + ((k >> 1) << 4)), (uint8_t)(base + k), attr);
    }
}
