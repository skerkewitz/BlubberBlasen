/*
 * Debug view with Dear ImGui (see debug_ui.h, ADR-0005 point 7).
 *
 * One window, docked panels:
 *   Game      the 256 x 224 picture, scaled to fit (without the TV look)
 *   Run       pause, frame step, slow motion, replay position, mark
 *   Switches  the developer switches (the F-keys) as checkboxes
 *   Round     status of the game, go to a round, clear it, spawn things
 *   Objects   players, enemies, bubbles and items with their state
 *   CPU       the CPU partner's goal and chosen action
 *   Deaths    why lives were lost in this run
 *   Video     the pseudo SNES: sprites and sprite tiles per line (limits 32
 *             and 34), lines that lost sprites, DMA bytes per frame
 *   VRAM      the tiles in video memory, in a palette of your choice
 *   Palettes  the 256 colours of CGRAM
 *   OAM       the sprites as the SNES holds them
 * The layout is set up on the first start; Dear ImGui then keeps it in
 * blubberblasen-debug.ini next to the program.
 */

#include "platform/debug_ui.h"

#include "imgui.h"
#include "imgui_internal.h"   /* DockBuilder: the first layout */
#include "rlImGui.h"

#include <stdint.h>
#include <stdio.h>

extern "C" {
#include "game/game.h"
#include "snes/snes.h"
}

static char ini_path[1024];

/* Same order as the DBG_* enum in game.h. */
static const char *const switch_labels[DBG_COUNT] = {
    "Infinite lives (F3)", "Invincible (F2)", "Hitboxes (F4)", "Air currents (F5)",
    "CPU view (F1)", "Vacuum walls (F12)", "No sticky bubbles (F6)"
};

void debug_ui_init(void)
{
    rlImGuiSetup(true);
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    {
        /* readable on scaled displays (raylib's window is not high-DPI aware) */
        float dpi = GetWindowScaleDPI().x;
        if (dpi > 1.0f) {
            ImGui::GetStyle().ScaleAllSizes(dpi);
            ImGui::GetStyle().FontScaleDpi = dpi;
        }
    }
    snprintf(ini_path, sizeof ini_path, "%sblubberblasen-debug.ini", GetApplicationDirectory());
    io.IniFilename = ini_path;
}

void debug_ui_shutdown(void)
{
    rlImGuiShutdown();
}

int debug_ui_wants_keyboard(void)
{
    return ImGui::GetIO().WantTextInput ? 1 : 0;
}

/* The first layout: the game on the left, the panels on the right. */
static void first_layout(ImGuiID dock)
{
    ImGuiID left, right, right_top, right_bottom;
    ImGui::DockBuilderRemoveNode(dock);
    ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dock, ImGui::GetMainViewport()->Size);
    ImGui::DockBuilderSplitNode(dock, ImGuiDir_Right, 0.38f, &right, &left);
    ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.45f, &right_top, &right_bottom);
    ImGui::DockBuilderDockWindow("Game", left);
    ImGui::DockBuilderDockWindow("Round", right_top);
    ImGui::DockBuilderDockWindow("Run", right_top);
    ImGui::DockBuilderDockWindow("Switches", right_top);
    ImGui::DockBuilderDockWindow("Objects", right_bottom);
    ImGui::DockBuilderDockWindow("CPU", right_bottom);
    ImGui::DockBuilderDockWindow("Deaths", right_bottom);
    ImGui::DockBuilderDockWindow("Video", right_top);
    ImGui::DockBuilderDockWindow("VRAM", right_bottom);
    ImGui::DockBuilderDockWindow("Palettes", right_bottom);
    ImGui::DockBuilderDockWindow("OAM", right_bottom);
    ImGui::DockBuilderFinish(dock);
}

static void panel_game(Texture2D game)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::Begin("Game")) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float sx = avail.x / SCREEN_W, sy = avail.y / SCREEN_H;
        float scale = sx < sy ? sx : sy;
        if (scale >= 1.0f) {
            scale = (float)(int)scale;   /* whole pixels while it fits */
        }
        if (scale > 0.0f) {
            ImVec2 size(SCREEN_W * scale, SCREEN_H * scale);
            ImVec2 pos = ImGui::GetCursorPos();
            ImGui::SetCursorPos(ImVec2(pos.x + (avail.x - size.x) * 0.5f, pos.y + (avail.y - size.y) * 0.5f));
            /* render textures are upside down */
            ImGui::Image((ImTextureID)(uintptr_t)game.id, size, ImVec2(0, 1), ImVec2(1, 0));
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

static void panel_run(const DebugHost &h)
{
    if (ImGui::Begin("Run")) {
        bool paused = h.paused != 0, slow = h.slow != 0;
        if (ImGui::Checkbox("Paused (F7)", &paused)) {
            host_set_paused(paused);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!h.paused);
        if (ImGui::Button("Next frame (F8)")) {
            host_step();
        }
        ImGui::EndDisabled();
        if (ImGui::Checkbox("Slow motion (F9)", &slow)) {
            host_set_slow(slow);
        }
        ImGui::Separator();
        if (h.replaying) {
            ImGui::Text("Replay: tick %ld of %ld", h.tick, h.end_tick);
            if (h.drifted) {
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "The replay drifted apart from the game.");
            }
        } else {
            ImGui::Text("Tick %ld%s", h.tick, h.recording ? ", recording" : "");
        }
        if (ImGui::Button(h.replaying ? "Next mark (Backspace)" : "Mark this moment (Backspace)")) {
            host_mark();
        }
        if (h.message[0]) {
            ImGui::Separator();
            ImGui::TextWrapped("%s", h.message);
        }
    }
    ImGui::End();
}

static void panel_switches(void)
{
    if (ImGui::Begin("Switches")) {
        int i;
        for (i = 0; i < DBG_COUNT; i++) {
            bool on = game_debug_get((uint8_t)i) != 0;
            if (ImGui::Checkbox(switch_labels[i], &on)) {
                host_toggle_switch(i);
            }
        }
    }
    ImGui::End();
}

static void score_text(const uint8_t digits[8], char *out)
{
    int i, lead = 1, n = 0;
    for (i = 0; i < 8; i++) {
        if (lead && digits[i] == 0 && i < 7) {
            continue;
        }
        lead = 0;
        out[n++] = (char)('0' + digits[i]);
    }
    out[n] = 0;
}

static void panel_round(void)
{
    static int round_wanted = 1;
    static int spawn_what = 0;
    if (ImGui::Begin("Round")) {
        DebugStatus s;
        int i;
        game_debug_status(&s);
        ImGui::Text("%s, round %u, %s mode", s.state, (unsigned)s.round, s.plus ? "Plus" : "Arcade");
        ImGui::Text("Round time %u frames (%.1f s)%s", (unsigned)s.round_frames, s.round_frames / 60.0f,
                    s.hurry ? ", HURRY UP" : "");
        if (s.freeze) {
            ImGui::Text("Steam Whistle: %u frames left", (unsigned)s.freeze);
        }
        for (i = 0; i < NUM_PLAYERS; i++) {
            static const char letters[] = "BLASEN";
            char score[9], have[7];
            int k;
            score_text(s.score[i], score);
            for (k = 0; k < 6; k++) {
                have[k] = (s.letters[i] >> k) & 1 ? letters[k] : '.';
            }
            have[6] = 0;
            ImGui::Text("P%d: %u lives, score %s, letters %s", i + 1, (unsigned)s.lives[i], score, have);
        }
        ImGui::Separator();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7);
        ImGui::InputInt("##round", &round_wanted);
        if (round_wanted < 1) round_wanted = 1;
        if (round_wanted > 255) round_wanted = 255;
        ImGui::SameLine();
        if (ImGui::Button("Go to round")) {
            host_goto_round(round_wanted);
        }
        if (ImGui::Button("Clear the round")) {
            host_clear_round();
        }
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 13);
        if (ImGui::BeginCombo("##spawn", game_debug_spawn_name((uint8_t)spawn_what))) {
            for (i = 0; i < DEBUG_SPAWN_COUNT; i++) {
                if (ImGui::Selectable(game_debug_spawn_name((uint8_t)i), i == spawn_what)) {
                    spawn_what = i;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Spawn")) {
            host_spawn(spawn_what);
        }
    }
    ImGui::End();
}

static void flags_text(uint8_t f, char *out, size_t size)
{
    snprintf(out, size, "%s%s%s%s%s%s%s", f & DOBJ_ANGRY ? "angry " : "", f & DOBJ_TRAPPED ? "trapped " : "",
             f & DOBJ_ON_GROUND ? "ground " : "", f & DOBJ_FACING_LEFT ? "left " : "", f & DOBJ_CPU ? "cpu " : "",
             f & DOBJ_STUNNED ? "stunned " : "", f & DOBJ_ENTERING ? "entering " : "");
}

static void object_table(const char *title, uint8_t kind, const DebugObject *objs, int n)
{
    static const char *const timer_names[DOBJ_KINDS] = { "invuln.", "throw", "age", "timer" };
    int i, count = 0;
    for (i = 0; i < n; i++) {
        count += objs[i].kind == kind;
    }
    if (!ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    if (count == 0) {
        ImGui::TextDisabled("none");
        return;
    }
    if (ImGui::BeginTable(title, 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("#");
        ImGui::TableSetupColumn(kind == DOBJ_PLAYER ? "lives" : "type");
        ImGui::TableSetupColumn("state");
        ImGui::TableSetupColumn("x, y px");
        ImGui::TableSetupColumn("vx, vy");
        ImGui::TableSetupColumn(timer_names[kind]);
        ImGui::TableSetupColumn("flags");
        ImGui::TableHeadersRow();
        for (i = 0; i < n; i++) {
            const DebugObject &o = objs[i];
            char flags[96];
            if (o.kind != kind) {
                continue;
            }
            flags_text(o.flags, flags, sizeof flags);
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%u", (unsigned)o.slot);
            ImGui::TableNextColumn();
            if (kind == DOBJ_PLAYER) {
                ImGui::Text("%u", (unsigned)o.type);
            } else {
                ImGui::TextUnformatted(game_debug_type_name(kind, o.type));
            }
            ImGui::TableNextColumn(); ImGui::TextUnformatted(game_debug_state_name(kind, o.state));
            ImGui::TableNextColumn(); ImGui::Text("%.1f, %.1f", o.x / 256.0f, o.y / 256.0f);
            ImGui::TableNextColumn(); ImGui::Text("%.2f, %.2f", o.vx / 256.0f, o.vy / 256.0f);
            ImGui::TableNextColumn(); ImGui::Text("%u", (unsigned)o.timer);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(flags);
        }
        ImGui::EndTable();
    }
}

static void panel_objects(void)
{
    if (ImGui::Begin("Objects")) {
        DebugObject objs[64];
        int n = game_debug_objects(objs, 64);
        object_table("Players", DOBJ_PLAYER, objs, n);
        object_table("Enemies", DOBJ_ENEMY, objs, n);
        object_table("Bubbles", DOBJ_BUBBLE, objs, n);
        object_table("Items", DOBJ_ITEM, objs, n);
    }
    ImGui::End();
}

static void panel_cpu(void)
{
    if (ImGui::Begin("CPU")) {
        DebugBox boxes[64];
        const char *action = "";
        bool view = game_debug_get(DBG_CPU_VIEW) != 0;
        int n = game_debug_cpu_view(boxes, 64, &action);
        ImGui::Text("Goal:   %s", game_debug_cpu_goal());
        ImGui::Text("Action: %s", action);
        ImGui::Text("Predicted danger positions: %d", n);
        if (ImGui::Checkbox("Show them in the game (F1)", &view)) {
            host_toggle_switch(DBG_CPU_VIEW);
        }
    }
    ImGui::End();
}

static void panel_deaths(void)
{
    static const char *const causes[DEBUG_DEATH_CAUSES] = {
        "Shield", "Walker", "Bouncer", "Thrower", "Flutter", "Hopper", "Spitter", "Dropper",
        "Shot", "Storm cloud", "Boss", "Rescue bubble ran out"
    };
    if (ImGui::Begin("Deaths")) {
        const uint16_t *d = game_debug_death_causes();
        int i, total = 0;
        for (i = 0; i < DEBUG_DEATH_CAUSES; i++) {
            if (d[i]) {
                ImGui::Text("%-24s %u", causes[i], (unsigned)d[i]);
                total += d[i];
            }
        }
        if (total == 0) {
            ImGui::TextDisabled("No lives lost yet.");
        }
    }
    ImGui::End();
}

/* --- The pseudo SNES ------------------------------------------------------------ */
static uint32_t rgba(uint16_t c)
{
    uint32_t r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    return 0xFF000000u | ((b << 3 | b >> 2) << 16) | ((g << 3 | g >> 2) << 8) | (r << 3 | r >> 2);
}

static ImVec4 imcolor(uint16_t c)
{
    return ImVec4((c & 31) / 31.0f, ((c >> 5) & 31) / 31.0f, ((c >> 10) & 31) / 31.0f, 1.0f);
}

static void panel_video(void)
{
    if (ImGui::Begin("Video")) {
        SnesStats st = snes_stats();
        ImGui::Text("Busiest line: %u sprites (limit 32), %u sprite tiles (limit 34)", (unsigned)st.most_sprites,
                    (unsigned)st.most_tiles);
        if (st.range_over_lines || st.time_over_lines) {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Lines that lost sprites: %u (too many sprites), %u (too many tiles)",
                               (unsigned)st.range_over_lines, (unsigned)st.time_over_lines);
        } else {
            ImGui::TextDisabled("No line lost sprites in this frame.");
        }
        ImGui::Text("DMA last frame: %u bytes (the SNES manages about 5,000 to 6,000 per vertical blank)",
                    (unsigned)st.dma_bytes);
        ImGui::Text("Brightness %u%s, BG mode %u", snes_reg(PPU_INIDISP) & 15, snes_reg(PPU_INIDISP) & 0x80 ? " (forced blank)" : "",
                    snes_reg(PPU_BGMODE) & 7);
    }
    ImGui::End();
}

/* VRAM tiles as a picture: 16 tiles per row, in one palette. */
static void panel_vram(void)
{
    static const struct { const char *name; uint16_t word; int bpp; int tiles; int base_pal; } regions[] = {
        { "BG1 tiles (room, title) at 0x1000", 0x1000, 4, 512, 0 },
        { "BG3 tiles (font) at 0x3000", 0x3000, 2, 64, 0 },
        { "Sprite tiles at 0x4000", 0x4000, 4, 512, 128 },
    };
    static int region = 2, pal = 0, zoom = 2;
    static Texture2D tex;
    static uint32_t pixels[128 * 256];
    if (ImGui::Begin("VRAM")) {
        const uint16_t *vram = snes_vram(), *cg = snes_cgram();
        int t, x, y, rows;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18);
        if (ImGui::BeginCombo("##region", regions[region].name)) {
            for (t = 0; t < 3; t++) {
                if (ImGui::Selectable(regions[t].name, t == region)) region = t;
            }
            ImGui::EndCombo();
        }
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
        ImGui::SliderInt("palette", &pal, 0, 7);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6);
        ImGui::SliderInt("zoom", &zoom, 1, 4);
        rows = regions[region].tiles / 16;
        for (t = 0; t < regions[region].tiles; t++) {
            int bpp = regions[region].bpp;
            uint16_t addr = (uint16_t)(regions[region].word + t * bpp * 4);
            for (y = 0; y < 8; y++) {
                uint16_t w0 = vram[(addr + y) & 0x7FFF], w1 = bpp == 4 ? vram[(addr + 8 + y) & 0x7FFF] : 0;
                for (x = 0; x < 8; x++) {
                    int bit = 7 - x, c = ((w0 >> bit) & 1) | (((w0 >> (8 + bit)) & 1) << 1) | (((w1 >> bit) & 1) << 2) |
                                         (((w1 >> (8 + bit)) & 1) << 3);
                    uint16_t col = cg[(regions[region].base_pal + pal * (bpp == 4 ? 16 : 4) + c) & 255];
                    pixels[((t / 16) * 8 + y) * 128 + (t % 16) * 8 + x] = c ? rgba(col) : 0xFF302830u;
                }
            }
        }
        if (tex.id == 0) {
            Image img = GenImageColor(128, 256, BLACK);
            tex = LoadTextureFromImage(img);
            UnloadImage(img);
            SetTextureFilter(tex, TEXTURE_FILTER_POINT);
        }
        UpdateTexture(tex, pixels);
        {
            ImVec2 pos = ImGui::GetCursorScreenPos();
            float h = (float)(rows * 8) / 256.0f;
            ImGui::Image((ImTextureID)(uintptr_t)tex.id, ImVec2(128.0f * zoom, rows * 8.0f * zoom), ImVec2(0, 0), ImVec2(1, h));
            if (ImGui::IsItemHovered()) {
                ImVec2 m = ImGui::GetIO().MousePos;
                int tx = (int)((m.x - pos.x) / (8 * zoom)), ty = (int)((m.y - pos.y) / (8 * zoom));
                ImGui::SetTooltip("tile %d (VRAM 0x%04X)", ty * 16 + tx,
                                  regions[region].word + (ty * 16 + tx) * regions[region].bpp * 4);
            }
        }
    }
    ImGui::End();
}

static void panel_palettes(void)
{
    if (ImGui::Begin("Palettes")) {
        const uint16_t *cg = snes_cgram();
        int i;
        ImGui::TextDisabled("Rows 0..7: background (BG3 text uses 0..1 in fours), rows 8..15: sprite palettes 0..7");
        for (i = 0; i < 256; i++) {
            char id[16];
            if (i % 16) ImGui::SameLine(0, 2);
            else ImGui::Text("%2d", i / 16), ImGui::SameLine();
            snprintf(id, sizeof id, "##c%d", i);
            ImGui::ColorButton(id, imcolor(cg[i]), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
                               ImVec2(ImGui::GetFontSize(), ImGui::GetFontSize()));
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("CGRAM %d (row %d, colour %d): $%04X = %d, %d, %d", i, i / 16, i % 16, cg[i], cg[i] & 31,
                                  (cg[i] >> 5) & 31, (cg[i] >> 10) & 31);
            }
        }
    }
    ImGui::End();
}

static void panel_oam(void)
{
    if (ImGui::Begin("OAM")) {
        const uint8_t *o = snes_oam();
        int i, shown = 0;
        static bool hidden;
        ImGui::Checkbox("Also hidden sprites", &hidden);
        if (ImGui::BeginTable("oam", 8, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit |
                                            ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("#");
            ImGui::TableSetupColumn("x");
            ImGui::TableSetupColumn("y");
            ImGui::TableSetupColumn("tile");
            ImGui::TableSetupColumn("palette");
            ImGui::TableSetupColumn("priority");
            ImGui::TableSetupColumn("flip");
            ImGui::TableSetupColumn("size");
            ImGui::TableHeadersRow();
            for (i = 0; i < 128; i++) {
                const uint8_t *e = o + i * 4;
                int hi = (o[512 + i / 4] >> ((i % 4) * 2)) & 3;
                int x = e[0] | ((hi & 1) << 8);
                if (x >= 256) x -= 512;
                if (!hidden && e[1] >= 224 && e[1] < 240) continue;
                shown++;
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::Text("%d", i);
                ImGui::TableNextColumn(); ImGui::Text("%d", x);
                ImGui::TableNextColumn(); ImGui::Text("%d", e[1]);
                ImGui::TableNextColumn(); ImGui::Text("%d", e[2] | ((e[3] & 1) << 8));
                ImGui::TableNextColumn(); ImGui::Text("%d", (e[3] >> 1) & 7);
                ImGui::TableNextColumn(); ImGui::Text("%d", (e[3] >> 4) & 3);
                ImGui::TableNextColumn(); ImGui::Text("%s%s", e[3] & 0x40 ? "x" : "", e[3] & 0x80 ? "y" : "");
                ImGui::TableNextColumn(); ImGui::Text("%s", hi & 2 ? "large" : "small");
            }
            ImGui::EndTable();
        }
        (void)shown;
    }
    ImGui::End();
}

/* Shows this window's tab in its dock node. */
static void select_tab(const char *name)
{
    ImGuiWindow *w = ImGui::FindWindowByName(name);
    if (w && w->DockNode && w->DockNode->TabBar) {
        w->DockNode->TabBar->NextSelectedTabId = w->TabId;
    }
}

void debug_ui_draw(Texture2D game)
{
    static bool laid_out;
    static int pick_tabs;   /* frames until the first layout's tabs are picked */
    DebugHost h;
    host_state(&h);

    rlImGuiBegin();
    {
        ImGuiID dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
        if (!laid_out) {
            ImGuiDockNode *node = ImGui::DockBuilderGetNode(dock);
            laid_out = true;
            if (node == NULL || node->IsLeafNode()) {   /* nothing saved yet */
                first_layout(dock);
                pick_tabs = 3;
            }
        }
    }
    panel_game(game);
    panel_run(h);
    panel_switches();
    panel_round();
    panel_objects();
    panel_cpu();
    panel_deaths();
    panel_video();
    panel_vram();
    panel_palettes();
    panel_oam();
    if (pick_tabs && --pick_tabs == 0) {
        /* the tabs shown at first; the windows are docked by now */
        select_tab("Round");
        select_tab("Objects");
        ImGui::SetWindowFocus("Game");   /* else the last window made keeps the focus and its tab */
    }
    rlImGuiEnd();
}
