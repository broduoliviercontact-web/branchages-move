/*
 * branchage_plugin.c — Grids + per-lane Branches hybrid MIDI FX wrapper.
 *
 * API: midi_fx_api_v1_t  (entry point: move_midi_fx_init)
 *
 * Clock modes:
 *   - sync=move: follows Move transport + Move BPM
 *   - sync=internal: free-running at module BPM
 *   0xFA resets Grids + all branch RNGs; 0xFC flushes active notes.
 *
 * Output:
 *   - Grids generates the musical rhythm for kick, snare, and hat.
 *   - Each lane can probabilistically replace its normal output note with
 *     an alternate "branch" note.
 *   - All notes are emitted as note-on + deferred note-off pairs.
 */

#include "midi_fx_api_v1.h"
#include "plugin_api_v1.h"
#include "../dsp/grids_engine.h"
#include "../dsp/branches_engine.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MIDI_NOTE_ON    0x90u
#define MIDI_NOTE_OFF   0x80u

#define DEFAULT_BPM          120.0f
#define DEFAULT_GATE_DIVISOR 3u
#define MIN_INTERNAL_BPM     40u
#define MAX_INTERNAL_BPM     240u
#define DEFAULT_STEP_LENGTH  16u
#define DEFAULT_UI_PAGE      0u
#define MAX_UI_PAGE          2u

#define DEFAULT_NOTE_KICK    36u
#define DEFAULT_NOTE_SNARE   38u
#define DEFAULT_NOTE_HAT     42u

#define DEFAULT_BRANCH_ENABLED 1u
#define DEFAULT_BRANCH_KICK_PROB  0.15f
#define DEFAULT_BRANCH_SNARE_PROB 0.20f
#define DEFAULT_BRANCH_HAT_PROB   0.30f
#define DEFAULT_BRANCH_KICK_NOTE  35u
#define DEFAULT_BRANCH_SNARE_NOTE 40u
#define DEFAULT_BRANCH_HAT_NOTE   46u

#define DEFAULT_BRANCH_RAND_LOW   36u
#define DEFAULT_BRANCH_RAND_HIGH  84u
#define BRANCH_RAND_SENTINEL      (-1)

#define VEL_NORMAL  80u
#define VEL_ACCENT  127u

static const host_api_v1_t *g_host = NULL;

static const char *kBranchProbKeys[GRIDS_NUM_LANES] = {
    "kick_branch_prob",
    "snare_branch_prob",
    "hat_branch_prob"
};

static const char *kBranchNoteKeys[GRIDS_NUM_LANES] = {
    "kick_branch_note",
    "snare_branch_note",
    "hat_branch_note"
};

static const char *kBranchEnabledKeys[GRIDS_NUM_LANES] = {
    "kick_branch_enabled",
    "snare_branch_enabled",
    "hat_branch_enabled"
};

static const char *kBranchRandLowKeys[GRIDS_NUM_LANES] = {
    "kick_branch_rand_low",
    "snare_branch_rand_low",
    "hat_branch_rand_low"
};

static const char *kBranchRandHighKeys[GRIDS_NUM_LANES] = {
    "kick_branch_rand_high",
    "snare_branch_rand_high",
    "hat_branch_rand_high"
};

static const float kDefaultBranchProb[GRIDS_NUM_LANES] = {
    DEFAULT_BRANCH_KICK_PROB,
    DEFAULT_BRANCH_SNARE_PROB,
    DEFAULT_BRANCH_HAT_PROB
};

static const uint8_t kDefaultBranchNote[GRIDS_NUM_LANES] = {
    DEFAULT_BRANCH_KICK_NOTE,
    DEFAULT_BRANCH_SNARE_NOTE,
    DEFAULT_BRANCH_HAT_NOTE
};

typedef struct {
    uint8_t active;
    uint8_t note;
    uint8_t channel;
    uint32_t frames_left;
    uint8_t clocks_left;
} PendingNoteOff;

typedef struct {
    BranchesEngine engine;
    uint8_t enabled;
    uint8_t note;
    uint8_t rand_mode;  /* 1 = pick random note from [rand_low, rand_high] */
    uint8_t rand_low;
    uint8_t rand_high;
} LaneBranch;

typedef struct {
    GridsEngine grids;
    LaneBranch branch[GRIDS_NUM_LANES];

    uint32_t frames_until_tick;
    uint8_t midi_clocks_until_tick;
    uint8_t clock_running;
    uint8_t clock_message_mode;
    uint8_t sync_mode;
    uint8_t step_length;
    uint16_t internal_bpm;

    uint8_t note[GRIDS_NUM_LANES];
    uint32_t rand_rng;
    uint8_t grid_view;

    char preview[GRIDS_NUM_LANES][GRIDS_NUM_STEPS + 1];
    uint32_t preview_revision;
    uint8_t preview_dirty;

    PendingNoteOff pending[GRIDS_NUM_LANES];
} BranchageInstance;

/* ---------------------------------------------------------------------------
 * Parsers
 * ------------------------------------------------------------------------- */

static uint8_t parse_norm(const char *s)
{
    if (!s) return 0;
    float v = (float)atof(s);
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    return (uint8_t)(v * 255.0f + 0.5f);
}

static float parse_prob(const char *s)
{
    if (!s) return 0.0f;
    float v = (float)atof(s);
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    return v;
}

static uint8_t parse_note(const char *s)
{
    if (!s) return 0;
    int v = atoi(s);
    if (v < 0) v = 0;
    if (v > 127) v = 127;
    return (uint8_t)v;
}

/* Returns -1 for random-mode sentinel, 0..127 for a fixed note. */
static int parse_branch_note(const char *s)
{
    if (!s) return 0;
    int v = atoi(s);
    if (v < BRANCH_RAND_SENTINEL) v = BRANCH_RAND_SENTINEL;
    if (v > 127) v = 127;
    return v;
}

static uint8_t parse_rand_bound(const char *s)
{
    if (!s) return 0;
    int v = atoi(s);
    if (v < 0) v = 0;
    if (v > 127) v = 127;
    return (uint8_t)v;
}

static uint16_t parse_bpm(const char *s)
{
    if (!s) return (uint16_t)DEFAULT_BPM;
    int v = atoi(s);
    if (v < (int)MIN_INTERNAL_BPM) v = MIN_INTERNAL_BPM;
    if (v > (int)MAX_INTERNAL_BPM) v = MAX_INTERNAL_BPM;
    return (uint16_t)v;
}

static uint8_t parse_steps(const char *s)
{
    if (!s) return DEFAULT_STEP_LENGTH;
    int v = atoi(s);
    if (v < 1) v = 1;
    if (v > 32) v = 32;
    return (uint8_t)v;
}

static uint8_t parse_toggle(const char *s)
{
    if (!s) return 0;
    if (strcmp(s, "on") == 0) return 1;
    if (strcmp(s, "off") == 0) return 0;
    return (uint8_t)(atoi(s) != 0);
}

static uint8_t parse_page(const char *s)
{
    int v = s ? atoi(s) : 0;
    if (v < 0) v = 0;
    if (v > (int)MAX_UI_PAGE) v = MAX_UI_PAGE;
    return (uint8_t)v;
}

static uint8_t parse_sync_mode(const char *s)
{
    if (!s) return 0;
    if (strcmp(s, "move") == 0) return 0;
    if (strcmp(s, "internal") == 0) return 1;
    return (uint8_t)(atoi(s) != 0);
}

/* ---------------------------------------------------------------------------
 * Timing helpers
 * ------------------------------------------------------------------------- */

static float current_bpm(const BranchageInstance *bi)
{
    if (bi && bi->sync_mode != 0) {
        return (float)bi->internal_bpm;
    }
    if (g_host && g_host->get_bpm) {
        float bpm = g_host->get_bpm();
        if (bpm >= 20.0f && bpm <= 400.0f) return bpm;
    }
    return DEFAULT_BPM;
}

static int current_sample_rate(void)
{
    if (g_host && g_host->sample_rate > 0) return g_host->sample_rate;
    return 44100;
}

static uint32_t frames_per_step(int sample_rate, float bpm)
{
    float sr = (sample_rate > 0) ? (float)sample_rate : 44100.0f;
    float use_bpm = (bpm > 0.0f) ? bpm : DEFAULT_BPM;
    uint32_t fps = (uint32_t)(sr * 60.0f / (use_bpm * 4.0f));
    return fps > 0 ? fps : 1u;
}

static uint32_t frames_per_gate(int sample_rate, float bpm)
{
    uint32_t fps = frames_per_step(sample_rate, bpm);
    uint32_t gate = fps / DEFAULT_GATE_DIVISOR;
    return gate > 0 ? gate : 1u;
}

static uint8_t midi_clocks_per_step(void)
{
    return 6u;
}

static uint8_t midi_clocks_per_gate(void)
{
    uint8_t gate = (uint8_t)(midi_clocks_per_step() / DEFAULT_GATE_DIVISOR);
    return gate > 0u ? gate : 1u;
}

static int emit_note_message(uint8_t status, uint8_t note, uint8_t vel,
                             uint8_t out_msgs[][3], int out_lens[],
                             int max_out, int count)
{
    if (count >= max_out) return count;
    out_msgs[count][0] = status;
    out_msgs[count][1] = note;
    out_msgs[count][2] = vel;
    out_lens[count] = 3;
    return count + 1;
}

static int flush_pending_note(PendingNoteOff *pending,
                              uint8_t out_msgs[][3], int out_lens[],
                              int max_out, int count)
{
    if (!pending->active) return count;
    count = emit_note_message((uint8_t)(MIDI_NOTE_OFF | (pending->channel & 0x0Fu)),
                              pending->note, 0, out_msgs, out_lens, max_out, count);
    pending->active = 0;
    pending->frames_left = 0;
    pending->clocks_left = 0u;
    return count;
}

static int flush_all_notes(BranchageInstance *bi,
                           uint8_t out_msgs[][3], int out_lens[],
                           int max_out, int count)
{
    for (int lane = 0; lane < GRIDS_NUM_LANES; lane++) {
        count = flush_pending_note(&bi->pending[lane], out_msgs, out_lens, max_out, count);
        if (count >= max_out) break;
    }
    return count;
}

static int advance_pending_clock(PendingNoteOff *pending,
                                 uint8_t out_msgs[][3], int out_lens[],
                                 int max_out, int count)
{
    if (!pending->active) return count;

    if (pending->clocks_left > 0u) pending->clocks_left--;
    if (pending->clocks_left == 0u) {
        count = emit_note_message((uint8_t)(MIDI_NOTE_OFF | (pending->channel & 0x0Fu)),
                                  pending->note, 0, out_msgs, out_lens, max_out, count);
        pending->active = 0;
        pending->frames_left = 0;
    }

    return count;
}

static int advance_pending_clocks(BranchageInstance *bi,
                                  uint8_t out_msgs[][3], int out_lens[],
                                  int max_out, int count)
{
    for (int lane = 0; lane < GRIDS_NUM_LANES; lane++) {
        count = advance_pending_clock(&bi->pending[lane], out_msgs, out_lens, max_out, count);
        if (count >= max_out) break;
    }
    return count;
}

static int advance_pending_note(PendingNoteOff *pending, uint32_t frames,
                                uint8_t out_msgs[][3], int out_lens[],
                                int max_out, int count)
{
    if (!pending->active) return count;

    if (frames >= pending->frames_left) {
        count = emit_note_message((uint8_t)(MIDI_NOTE_OFF | (pending->channel & 0x0Fu)),
                                  pending->note, 0, out_msgs, out_lens, max_out, count);
        pending->active = 0;
        pending->frames_left = 0;
    } else {
        pending->frames_left -= frames;
    }

    return count;
}

static int advance_pending_notes(BranchageInstance *bi, uint32_t frames,
                                 uint8_t out_msgs[][3], int out_lens[],
                                 int max_out, int count)
{
    for (int lane = 0; lane < GRIDS_NUM_LANES; lane++) {
        count = advance_pending_note(&bi->pending[lane], frames,
                                     out_msgs, out_lens, max_out, count);
        if (count >= max_out) break;
    }
    return count;
}

/* ---------------------------------------------------------------------------
 * Grid / branch helpers
 * ------------------------------------------------------------------------- */

static void mark_preview_dirty(BranchageInstance *bi)
{
    if (bi) bi->preview_dirty = 1;
}

static void reset_branch_engines(BranchageInstance *bi)
{
    for (int lane = 0; lane < GRIDS_NUM_LANES; lane++) {
        branches_engine_reset(&bi->branch[lane].engine);
    }
}

static uint8_t resolve_lane_note(BranchageInstance *bi, int lane)
{
    LaneBranch *branch = &bi->branch[lane];

    if (branch->enabled && branches_engine_should_branch(&branch->engine)) {
        if (branch->rand_mode) {
            uint8_t lo = branch->rand_low;
            uint8_t hi = branch->rand_high;
            if (lo > hi) { uint8_t tmp = lo; lo = hi; hi = tmp; }
            uint8_t range = (uint8_t)(hi - lo + 1u);
            bi->rand_rng = bi->rand_rng * 1664525u + 1013904223u;
            return lo + (uint8_t)((bi->rand_rng >> 16) % range);
        }
        return branch->note;
    }
    return bi->note[lane];
}

static int do_step(BranchageInstance *bi,
                   uint32_t gate_frames,
                   uint8_t gate_clocks,
                   uint8_t out_msgs[][3], int out_lens[],
                   int max_out)
{
    int count = 0;

    grids_tick(&bi->grids);
    if (bi->grids.step >= bi->step_length) {
        bi->grids.step = 0;
    }

    for (int lane = 0; lane < GRIDS_NUM_LANES; lane++) {
        PendingNoteOff *pending = &bi->pending[lane];

        if (pending->active) {
            count = flush_pending_note(pending, out_msgs, out_lens, max_out, count);
            if (count >= max_out) return count;
        }

        if (!grids_get_trigger(&bi->grids, lane)) continue;

        {
            uint8_t vel = grids_get_accent(&bi->grids, lane) ? VEL_ACCENT : VEL_NORMAL;
            uint8_t note = resolve_lane_note(bi, lane);
            count = emit_note_message(MIDI_NOTE_ON, note, vel,
                                      out_msgs, out_lens, max_out, count);
            if (count > max_out) return max_out;

            pending->active = 1;
            pending->note = note;
            pending->channel = 0;
            pending->frames_left = gate_frames;
            pending->clocks_left = gate_clocks;
        }
    }

    return count;
}

static void refresh_preview_cache(BranchageInstance *bi)
{
    if (!bi || !bi->preview_dirty) return;

    GridsEngine preview = bi->grids;
    preview.step = 0;
    preview.rng_state = 0xDEADBEEFu;
    for (int lane = 0; lane < GRIDS_NUM_LANES; lane++) {
        preview.trigger[lane] = false;
        preview.accent[lane] = false;
    }

    for (int s = 0; s < GRIDS_NUM_STEPS; s++) {
        grids_tick(&preview);

        for (int lane = 0; lane < GRIDS_NUM_LANES; lane++) {
            bool fire = grids_get_trigger(&preview, lane);
            bool acc = grids_get_accent(&preview, lane);
            bi->preview[lane][s] = fire ? (acc ? 'A' : 'X') : '.';
        }

        if (preview.step >= bi->step_length) {
            preview.step = 0;
        }
    }

    for (int lane = 0; lane < GRIDS_NUM_LANES; lane++) {
        bi->preview[lane][GRIDS_NUM_STEPS] = '\0';
    }

    bi->preview_dirty = 0;
    bi->preview_revision++;
}

static int write_preview_chunk(BranchageInstance *bi, int lane, int offset,
                               char *buf, int buf_len)
{
    char chunk[5];

    if (!bi || !buf || buf_len <= 0) return -1;
    if (lane < 0 || lane >= GRIDS_NUM_LANES) return -1;
    if (offset < 0 || offset > (GRIDS_NUM_STEPS - 4)) return -1;

    refresh_preview_cache(bi);

    for (int i = 0; i < 4; i++) {
        chunk[i] = bi->preview[lane][offset + i];
    }
    chunk[4] = '\0';

    return snprintf(buf, buf_len, "%s", chunk);
}

/* ---------------------------------------------------------------------------
 * State JSON helpers
 * ------------------------------------------------------------------------- */

/* Extract a quoted string value for a given key from a JSON object string.
 * Returns length written (excluding NUL), or -1 on failure.
 * Example: json_extract_value("{\"foo\":\"bar\"}", "foo", buf, 64) -> "bar" */
static int json_extract_value(const char *json, const char *key,
                              char *out, int out_len)
{
    char needle[128];
    const char *p, *start, *end;
    int len;

    if (!json || !key || !out || out_len <= 0) return -1;

    len = snprintf(needle, sizeof(needle), "\"%s\":\"", key);
    if (len < 0 || len >= (int)sizeof(needle)) return -1;

    p = strstr(json, needle);
    if (!p) return -1;

    start = p + len;
    end = strchr(start, '"');
    if (!end) return -1;

    len = (int)(end - start);
    if (len >= out_len) len = out_len - 1;
    memcpy(out, start, (size_t)len);
    out[len] = '\0';
    return len;
}

/* Apply all known parameters from a JSON state string. */
static void branchage_restore_state(void *instance, const char *json)
{
    char val[128];
    static const char *keys[] = {
        "map_x", "map_y",
        "density_kick", "density_snare", "density_hat",
        "randomness", "steps", "bpm", "sync",
        "kick_note", "snare_note", "hat_note",
        "grid_view",
        "kick_branch_prob", "snare_branch_prob", "hat_branch_prob",
        "kick_branch_note", "snare_branch_note", "hat_branch_note",
        "kick_branch_enabled", "snare_branch_enabled", "hat_branch_enabled",
        "kick_branch_rand_low", "snare_branch_rand_low", "hat_branch_rand_low",
        "kick_branch_rand_high", "snare_branch_rand_high", "hat_branch_rand_high",
        NULL
    };

    if (!instance || !json) return;

    for (int i = 0; keys[i]; i++) {
        if (json_extract_value(json, keys[i], val, sizeof(val)) > 0) {
            branchage_set_param(instance, keys[i], val);
        }
    }
}

/* ---------------------------------------------------------------------------
 * Instance lifecycle
 * ------------------------------------------------------------------------- */

static void *branchage_create_instance(const char *module_dir,
                                       const char *config_json)
{
    BranchageInstance *bi;

    (void)module_dir;
    (void)config_json;

    bi = (BranchageInstance *)calloc(1, sizeof(BranchageInstance));
    if (!bi) return NULL;

    grids_init(&bi->grids);
    grids_set_map_xy(&bi->grids, 128, 128);
    grids_set_density(&bi->grids, 0, 128);
    grids_set_density(&bi->grids, 1, 128);
    grids_set_density(&bi->grids, 2, 128);
    grids_set_randomness(&bi->grids, 0);

    bi->frames_until_tick = frames_per_step(44100, DEFAULT_BPM);
    bi->midi_clocks_until_tick = midi_clocks_per_step();
    bi->clock_running = 0;
    bi->clock_message_mode = 0;
    bi->sync_mode = 0;
    bi->step_length = DEFAULT_STEP_LENGTH;
    bi->internal_bpm = (uint16_t)DEFAULT_BPM;

    bi->note[0] = DEFAULT_NOTE_KICK;
    bi->note[1] = DEFAULT_NOTE_SNARE;
    bi->note[2] = DEFAULT_NOTE_HAT;

    for (int lane = 0; lane < GRIDS_NUM_LANES; lane++) {
        branches_engine_init(&bi->branch[lane].engine, 0xB001u + (uint32_t)(lane * 0x101u));
        branches_engine_set_probability(&bi->branch[lane].engine, kDefaultBranchProb[lane]);
        bi->branch[lane].enabled   = DEFAULT_BRANCH_ENABLED;
        bi->branch[lane].note      = kDefaultBranchNote[lane];
        bi->branch[lane].rand_mode = 0;
        bi->branch[lane].rand_low  = DEFAULT_BRANCH_RAND_LOW;
        bi->branch[lane].rand_high = DEFAULT_BRANCH_RAND_HIGH;
    }

    bi->rand_rng = 0xCAFEBABEu;

    bi->grid_view = DEFAULT_UI_PAGE;
    bi->preview_dirty = 1;
    return bi;
}

static void branchage_destroy_instance(void *instance)
{
    free(instance);
}

static int branchage_process_midi(void *instance,
                                  const uint8_t *in_msg, int in_len,
                                  uint8_t out_msgs[][3], int out_lens[],
                                  int max_out)
{
    BranchageInstance *bi = (BranchageInstance *)instance;

    if (!bi || in_len == 0) return 0;

    if (in_msg[0] == 0xFAu) {  /* Start */
        grids_engine_reset(&bi->grids);
        reset_branch_engines(bi);
        bi->frames_until_tick = frames_per_step(current_sample_rate(), current_bpm(bi));
        bi->midi_clocks_until_tick = midi_clocks_per_step();
        bi->clock_running = 1;
        {
            int count = flush_all_notes(bi, out_msgs, out_lens, max_out, 0);
            if (bi->sync_mode == 0 && count < max_out) {
                count += do_step(bi,
                                 frames_per_gate(current_sample_rate(), current_bpm(bi)),
                                 midi_clocks_per_gate(),
                                 out_msgs + count, out_lens + count, max_out - count);
                bi->midi_clocks_until_tick = midi_clocks_per_step();
            }
            return count;
        }
    }
    if (in_msg[0] == 0xFBu) {  /* Continue */
        bi->clock_running = 1;
        return flush_all_notes(bi, out_msgs, out_lens, max_out, 0);
    }
    if (in_msg[0] == 0xF8u) {  /* Clock tick */
        int count = 0;
        if (bi->sync_mode != 0 || !bi->clock_running) return 0;
        bi->clock_message_mode = 1;

        count = advance_pending_clocks(bi, out_msgs, out_lens, max_out, count);
        if (count >= max_out) return count;

        if (bi->midi_clocks_until_tick > 0u) bi->midi_clocks_until_tick--;
        if (bi->midi_clocks_until_tick == 0u) {
            count += do_step(bi,
                             frames_per_gate(current_sample_rate(), current_bpm(bi)),
                             midi_clocks_per_gate(),
                             out_msgs + count, out_lens + count, max_out - count);
            bi->midi_clocks_until_tick = midi_clocks_per_step();
        }
        return count;
    }
    if (in_msg[0] == 0xFCu) {  /* Stop */
        bi->clock_running = 0;
        return flush_all_notes(bi, out_msgs, out_lens, max_out, 0);
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * Parameter I/O
 * ------------------------------------------------------------------------- */

static void branchage_set_param(void *instance, const char *key, const char *val)
{
    BranchageInstance *bi = (BranchageInstance *)instance;

    if (!bi || !key || !val) return;

    if (strcmp(key, "state") == 0) {
        branchage_restore_state(instance, val);
        return;
    }

    if (strcmp(key, "map_x") == 0) {
        grids_set_map_xy(&bi->grids, parse_norm(val), bi->grids.map_y);
        mark_preview_dirty(bi);
        return;
    }
    if (strcmp(key, "map_y") == 0) {
        grids_set_map_xy(&bi->grids, bi->grids.map_x, parse_norm(val));
        mark_preview_dirty(bi);
        return;
    }
    if (strcmp(key, "density_kick") == 0) {
        grids_set_density(&bi->grids, 0, parse_norm(val));
        mark_preview_dirty(bi);
        return;
    }
    if (strcmp(key, "density_snare") == 0) {
        grids_set_density(&bi->grids, 1, parse_norm(val));
        mark_preview_dirty(bi);
        return;
    }
    if (strcmp(key, "density_hat") == 0) {
        grids_set_density(&bi->grids, 2, parse_norm(val));
        mark_preview_dirty(bi);
        return;
    }
    if (strcmp(key, "randomness") == 0) {
        grids_set_randomness(&bi->grids, parse_norm(val));
        mark_preview_dirty(bi);
        return;
    }
    if (strcmp(key, "steps") == 0) {
        bi->step_length = parse_steps(val);
        if (bi->grids.step >= bi->step_length) {
            bi->grids.step = 0;
        }
        mark_preview_dirty(bi);
        return;
    }
    if (strcmp(key, "sync") == 0) {
        bi->sync_mode = parse_sync_mode(val);
        bi->frames_until_tick = frames_per_step(44100, current_bpm(bi));
        bi->midi_clocks_until_tick = midi_clocks_per_step();
        bi->clock_message_mode = 0;
        if (bi->sync_mode != 0) {
            bi->clock_running = 1;
        } else if (g_host && g_host->get_clock_status) {
            bi->clock_running = (uint8_t)(g_host->get_clock_status() == MOVE_CLOCK_STATUS_RUNNING);
        }
        return;
    }
    if (strcmp(key, "bpm") == 0) {
        bi->internal_bpm = parse_bpm(val);
        if (bi->sync_mode != 0) {
            bi->frames_until_tick = frames_per_step(44100, current_bpm(bi));
        }
        return;
    }
    if (strcmp(key, "kick_note") == 0) {
        bi->note[0] = parse_note(val);
        return;
    }
    if (strcmp(key, "snare_note") == 0) {
        bi->note[1] = parse_note(val);
        return;
    }
    if (strcmp(key, "hat_note") == 0) {
        bi->note[2] = parse_note(val);
        return;
    }
    if (strcmp(key, "grid_view") == 0) {
        bi->grid_view = parse_page(val);
        return;
    }

    for (int lane = 0; lane < GRIDS_NUM_LANES; lane++) {
        if (strcmp(key, kBranchProbKeys[lane]) == 0) {
            branches_engine_set_probability(&bi->branch[lane].engine, parse_prob(val));
            return;
        }
        if (strcmp(key, kBranchNoteKeys[lane]) == 0) {
            int v = parse_branch_note(val);
            if (v == BRANCH_RAND_SENTINEL) {
                bi->branch[lane].rand_mode = 1;
            } else {
                bi->branch[lane].rand_mode = 0;
                bi->branch[lane].note = (uint8_t)v;
            }
            return;
        }
        if (strcmp(key, kBranchEnabledKeys[lane]) == 0) {
            bi->branch[lane].enabled = parse_toggle(val);
            return;
        }
    }

    for (int lane = 0; lane < GRIDS_NUM_LANES; lane++) {
        if (strcmp(key, kBranchRandLowKeys[lane]) == 0) {
            bi->branch[lane].rand_low = parse_rand_bound(val);
            return;
        }
        if (strcmp(key, kBranchRandHighKeys[lane]) == 0) {
            bi->branch[lane].rand_high = parse_rand_bound(val);
            return;
        }
    }
}

static int branchage_get_param(void *instance, const char *key,
                               char *buf, int buf_len)
{
    BranchageInstance *bi = (BranchageInstance *)instance;
    float v = -1.0f;

    if (!bi || !key || !buf || buf_len <= 0) return -1;

    if (strcmp(key, "state") == 0) {
        return snprintf(buf, buf_len,
            "{"
            "\"map_x\":\"%.4f\","
            "\"map_y\":\"%.4f\","
            "\"density_kick\":\"%.4f\","
            "\"density_snare\":\"%.4f\","
            "\"density_hat\":\"%.4f\","
            "\"randomness\":\"%.4f\","
            "\"steps\":\"%u\","
            "\"bpm\":\"%u\","
            "\"sync\":\"%s\","
            "\"kick_note\":\"%u\","
            "\"snare_note\":\"%u\","
            "\"hat_note\":\"%u\","
            "\"grid_view\":\"%u\","
            "\"kick_branch_prob\":\"%.4f\","
            "\"snare_branch_prob\":\"%.4f\","
            "\"hat_branch_prob\":\"%.4f\","
            "\"kick_branch_note\":\"%d\","
            "\"snare_branch_note\":\"%d\","
            "\"hat_branch_note\":\"%d\","
            "\"kick_branch_enabled\":\"%u\","
            "\"snare_branch_enabled\":\"%u\","
            "\"hat_branch_enabled\":\"%u\","
            "\"kick_branch_rand_low\":\"%u\","
            "\"snare_branch_rand_low\":\"%u\","
            "\"hat_branch_rand_low\":\"%u\","
            "\"kick_branch_rand_high\":\"%u\","
            "\"snare_branch_rand_high\":\"%u\","
            "\"hat_branch_rand_high\":\"%u\""
            "}",
            bi->grids.map_x / 255.0f,
            bi->grids.map_y / 255.0f,
            bi->grids.density[0] / 255.0f,
            bi->grids.density[1] / 255.0f,
            bi->grids.density[2] / 255.0f,
            bi->grids.randomness / 255.0f,
            bi->step_length,
            bi->internal_bpm,
            bi->sync_mode ? "internal" : "move",
            bi->note[0],
            bi->note[1],
            bi->note[2],
            bi->grid_view,
            bi->branch[0].engine.probability,
            bi->branch[1].engine.probability,
            bi->branch[2].engine.probability,
            bi->branch[0].rand_mode ? -1 : (int)bi->branch[0].note,
            bi->branch[1].rand_mode ? -1 : (int)bi->branch[1].note,
            bi->branch[2].rand_mode ? -1 : (int)bi->branch[2].note,
            bi->branch[0].enabled,
            bi->branch[1].enabled,
            bi->branch[2].enabled,
            bi->branch[0].rand_low,
            bi->branch[1].rand_low,
            bi->branch[2].rand_low,
            bi->branch[0].rand_high,
            bi->branch[1].rand_high,
            bi->branch[2].rand_high);
    }

    if (strcmp(key, "steps") == 0)
        return snprintf(buf, buf_len, "%u", bi->step_length);
    if (strcmp(key, "sync") == 0)
        return snprintf(buf, buf_len, "%s", bi->sync_mode ? "internal" : "move");
    if (strcmp(key, "bpm") == 0)
        return snprintf(buf, buf_len, "%u", bi->internal_bpm);
    if (strcmp(key, "kick_note") == 0)
        return snprintf(buf, buf_len, "%u", bi->note[0]);
    if (strcmp(key, "snare_note") == 0)
        return snprintf(buf, buf_len, "%u", bi->note[1]);
    if (strcmp(key, "hat_note") == 0)
        return snprintf(buf, buf_len, "%u", bi->note[2]);
    if (strcmp(key, "grid_view") == 0)
        return snprintf(buf, buf_len, "%u", bi->grid_view);
    if (strcmp(key, "play_step") == 0)
        return snprintf(buf, buf_len, "%u", bi->grids.step);
    if (strcmp(key, "preview_rev") == 0) {
        refresh_preview_cache(bi);
        return snprintf(buf, buf_len, "%u", bi->preview_revision);
    }
    if (strcmp(key, "preview_kick") == 0) {
        refresh_preview_cache(bi);
        return snprintf(buf, buf_len, "%s", bi->preview[0]);
    }
    if (strcmp(key, "preview_snare") == 0) {
        refresh_preview_cache(bi);
        return snprintf(buf, buf_len, "%s", bi->preview[1]);
    }
    if (strcmp(key, "preview_hat") == 0) {
        refresh_preview_cache(bi);
        return snprintf(buf, buf_len, "%s", bi->preview[2]);
    }
    if (strcmp(key, "preview_kick_1") == 0)  return write_preview_chunk(bi, 0,  0, buf, buf_len);
    if (strcmp(key, "preview_kick_2") == 0)  return write_preview_chunk(bi, 0,  4, buf, buf_len);
    if (strcmp(key, "preview_kick_3") == 0)  return write_preview_chunk(bi, 0,  8, buf, buf_len);
    if (strcmp(key, "preview_kick_4") == 0)  return write_preview_chunk(bi, 0, 12, buf, buf_len);
    if (strcmp(key, "preview_snare_1") == 0) return write_preview_chunk(bi, 1,  0, buf, buf_len);
    if (strcmp(key, "preview_snare_2") == 0) return write_preview_chunk(bi, 1,  4, buf, buf_len);
    if (strcmp(key, "preview_snare_3") == 0) return write_preview_chunk(bi, 1,  8, buf, buf_len);
    if (strcmp(key, "preview_snare_4") == 0) return write_preview_chunk(bi, 1, 12, buf, buf_len);
    if (strcmp(key, "preview_hat_1") == 0)   return write_preview_chunk(bi, 2,  0, buf, buf_len);
    if (strcmp(key, "preview_hat_2") == 0)   return write_preview_chunk(bi, 2,  4, buf, buf_len);
    if (strcmp(key, "preview_hat_3") == 0)   return write_preview_chunk(bi, 2,  8, buf, buf_len);
    if (strcmp(key, "preview_hat_4") == 0)   return write_preview_chunk(bi, 2, 12, buf, buf_len);

    for (int lane = 0; lane < GRIDS_NUM_LANES; lane++) {
        if (strcmp(key, kBranchProbKeys[lane]) == 0) {
            return snprintf(buf, buf_len, "%.4f", bi->branch[lane].engine.probability);
        }
        if (strcmp(key, kBranchNoteKeys[lane]) == 0) {
            if (bi->branch[lane].rand_mode)
                return snprintf(buf, buf_len, "-1");
            return snprintf(buf, buf_len, "%u", bi->branch[lane].note);
        }
        if (strcmp(key, kBranchEnabledKeys[lane]) == 0) {
            return snprintf(buf, buf_len, "%u", bi->branch[lane].enabled);
        }
    }

    for (int lane = 0; lane < GRIDS_NUM_LANES; lane++) {
        if (strcmp(key, kBranchRandLowKeys[lane]) == 0)
            return snprintf(buf, buf_len, "%u", bi->branch[lane].rand_low);
        if (strcmp(key, kBranchRandHighKeys[lane]) == 0)
            return snprintf(buf, buf_len, "%u", bi->branch[lane].rand_high);
    }

    if (strcmp(key, "sync_warn") == 0) {
        if (bi->sync_mode == 0 && g_host && g_host->get_clock_status) {
            int status = g_host->get_clock_status();
            if (status == MOVE_CLOCK_STATUS_UNAVAILABLE)
                return snprintf(buf, buf_len, "Enable MIDI Clock Out");
            if (status == MOVE_CLOCK_STATUS_STOPPED)
                return snprintf(buf, buf_len, "stopped");
        }
        return snprintf(buf, buf_len, "%s", "");
    }

    if (strcmp(key, "map_x") == 0) v = bi->grids.map_x / 255.0f;
    else if (strcmp(key, "map_y") == 0) v = bi->grids.map_y / 255.0f;
    else if (strcmp(key, "density_kick") == 0) v = bi->grids.density[0] / 255.0f;
    else if (strcmp(key, "density_snare") == 0) v = bi->grids.density[1] / 255.0f;
    else if (strcmp(key, "density_hat") == 0) v = bi->grids.density[2] / 255.0f;
    else if (strcmp(key, "randomness") == 0) v = bi->grids.randomness / 255.0f;

    if (v < 0.0f) return -1;
    return snprintf(buf, buf_len, "%.4f", v);
}

static int branchage_plugin_tick(void *instance,
                                 int frames, int sample_rate,
                                 uint8_t out_msgs[][3], int out_lens[],
                                 int max_out)
{
    BranchageInstance *bi = (BranchageInstance *)instance;
    uint32_t nf;
    float bpm;
    uint32_t fps;
    uint32_t gate;
    int count;

    if (!bi) return 0;

    if (bi->sync_mode == 0 && g_host && g_host->get_clock_status) {
        int status = g_host->get_clock_status();
        if (status == MOVE_CLOCK_STATUS_STOPPED ||
            status == MOVE_CLOCK_STATUS_UNAVAILABLE) {
            bi->clock_running = 0;
            return flush_all_notes(bi, out_msgs, out_lens, max_out, 0);
        } else if (status == MOVE_CLOCK_STATUS_RUNNING) {
            bi->clock_running = 1;
        }
    } else if (bi->sync_mode != 0) {
        bi->clock_running = 1;
    }

    if (bi->sync_mode == 0 && bi->clock_message_mode) return 0;

    bpm = current_bpm(bi);
    fps = frames_per_step(sample_rate, bpm);
    gate = frames_per_gate(sample_rate, bpm);
    nf = (uint32_t)frames;

    count = advance_pending_notes(bi, nf, out_msgs, out_lens, max_out, 0);
    if (count >= max_out) return count;

    if (!bi->clock_running) return count;

    if (bi->frames_until_tick <= nf) {
        uint32_t carry = nf - bi->frames_until_tick;
        bi->frames_until_tick = fps > carry ? fps - carry : 1u;
        return count + do_step(bi, gate, 0u, out_msgs + count, out_lens + count, max_out - count);
    }

    bi->frames_until_tick -= nf;
    return count;
}

static midi_fx_api_v1_t g_api = {
    .api_version      = MIDI_FX_API_VERSION,
    .create_instance  = branchage_create_instance,
    .destroy_instance = branchage_destroy_instance,
    .process_midi     = branchage_process_midi,
    .tick             = branchage_plugin_tick,
    .set_param        = branchage_set_param,
    .get_param        = branchage_get_param,
};

midi_fx_api_v1_t *move_midi_fx_init(const host_api_v1_t *host)
{
    g_host = host;
    return &g_api;
}
