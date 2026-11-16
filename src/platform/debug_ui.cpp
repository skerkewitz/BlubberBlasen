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
    if (pick_tabs && --pick_tabs == 0) {
        /* the tabs shown at first; the windows are docked by now */
        select_tab("Round");
        select_tab("Objects");
        ImGui::SetWindowFocus("Game");   /* else the last window made keeps the focus and its tab */
    }
    rlImGuiEnd();
}
