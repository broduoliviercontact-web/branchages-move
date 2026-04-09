'use strict';

import {
  decodeDelta
} from '/data/UserData/schwung/shared/input_filter.mjs';

const PAD_BASE = 68;
const STEP_BASE = 16;

const CC_JOG_WHEEL = 14;
const CC_JOG_CLICK = 3;

const PAGE_MAIN = 0;
const PAGE_PARAMS = 1;
const GRID_VIEW_STEPS = 16;
const FLASH_TICKS = 5;

const PAGE_LABELS = ['MAIN', 'PRMS'];
const BRANCH_LABELS = ['K', 'S', 'H'];

const BRANCH_PROB_KEYS = [
  'kick_branch_prob',
  'snare_branch_prob',
  'hat_branch_prob',
];

const BRANCH_NOTE_KEYS = [
  'kick_branch_note',
  'snare_branch_note',
  'hat_branch_note',
];

const BRANCH_ENABLED_KEYS = [
  'kick_branch_enabled',
  'snare_branch_enabled',
  'hat_branch_enabled',
];

const MAIN_KNOB_PARAMS = {
  71: 'map_x',
  72: 'map_y',
  73: 'density_kick',
  74: 'density_snare',
  75: 'density_hat',
  76: 'randomness',
};

const PARAMS_KNOB_PARAMS = {
  71: 'steps',
  72: 'bpm',
  73: 'sync',
  74: 'kick_branch_prob',
  75: 'snare_branch_prob',
  76: 'hat_branch_prob',
};

const MAIN_PARAM_LIST = [
  'map_x', 'map_y',
  'density_kick', 'density_snare', 'density_hat',
  'randomness',
  'kick_note', 'snare_note', 'hat_note',
];

const PARAMS_PARAM_LIST = [
  'steps', 'bpm', 'sync',
  'kick_branch_prob',   'kick_branch_note',   'kick_branch_enabled',
  'snare_branch_prob',  'snare_branch_note',  'snare_branch_enabled',
  'hat_branch_prob',    'hat_branch_note',    'hat_branch_enabled',
  'kick_branch_rand_low',  'kick_branch_rand_high',
  'snare_branch_rand_low', 'snare_branch_rand_high',
  'hat_branch_rand_low',   'hat_branch_rand_high',
];

const PARAM_DEFAULTS = {
  map_x: 0.5,
  map_y: 0.5,
  density_kick: 0.5,
  density_snare: 0.5,
  density_hat: 0.5,
  randomness: 0.0,
  steps: 16,
  bpm: 120,
  sync: 0,
  kick_note: 36,
  snare_note: 38,
  hat_note: 42,
  kick_branch_enabled: 1,
  snare_branch_enabled: 1,
  hat_branch_enabled: 1,
  kick_branch_prob: 0.15,
  snare_branch_prob: 0.20,
  hat_branch_prob: 0.30,
  kick_branch_note: 35,
  snare_branch_note: 40,
  hat_branch_note: 46,
  kick_branch_rand_low: 36,
  kick_branch_rand_high: 84,
  snare_branch_rand_low: 36,
  snare_branch_rand_high: 84,
  hat_branch_rand_low: 36,
  hat_branch_rand_high: 84,
  grid_view: 0,
};

const PAD_BRIGHT_NEAR = 0.07;
const PAD_BRIGHT_MED = 0.22;
const PAD_BRIGHT_FAR = 0.45;

const BRANCH_NOTE_PARAMS = {
  kick_branch_note: true,
  snare_branch_note: true,
  hat_branch_note: true,
};

const BRANCH_RAND_LOW_KEYS = [
  'kick_branch_rand_low',
  'snare_branch_rand_low',
  'hat_branch_rand_low',
];

const BRANCH_RAND_HIGH_KEYS = [
  'kick_branch_rand_high',
  'snare_branch_rand_high',
  'hat_branch_rand_high',
];

/* All note-style int params (main notes + per-lane branch range bounds). */
const NOTE_PARAMS = {
  kick_note: true,
  snare_note: true,
  hat_note: true,
  kick_branch_rand_low: true,
  kick_branch_rand_high: true,
  snare_branch_rand_low: true,
  snare_branch_rand_high: true,
  hat_branch_rand_low: true,
  hat_branch_rand_high: true,
};

const TOGGLE_PARAMS = {
  kick_branch_enabled: true,
  snare_branch_enabled: true,
  hat_branch_enabled: true,
};

const g = {
  params: { ...PARAM_DEFAULTS },
  step: 0,
  flash: [0, 0, 0],
  branchFlash: [0, 0, 0],
  previewGrid: [new Uint8Array(32), new Uint8Array(32), new Uint8Array(32)],
  previewRev: -1,
  focused: null,
  editing: false,
  padLEDCache: new Uint8Array(32),
  padDirty: true,
  padDirtyPhase: 0,
};

function clamp01(v) {
  return v < 0 ? 0 : v > 1 ? 1 : v;
}

function clampPage(v) {
  const page = Math.round(v);
  if (page < PAGE_MAIN) return PAGE_MAIN;
  if (page > PAGE_PARAMS) return PAGE_PARAMS;
  return page;
}

function clampNote(v) {
  const note = Math.round(v);
  if (note < 0) return 0;
  if (note > 127) return 127;
  return note;
}

function clampBranchNote(v) {
  const note = Math.round(v);
  if (note < -1) return -1;
  if (note > 127) return 127;
  return note;
}

function pageName() {
  return PAGE_LABELS[clampPage(g.params.grid_view)] || 'MAIN';
}

function padIndexToXY(idx) {
  return { x: (idx % 8) / 7, y: Math.floor(idx / 8) / 3 };
}

function padGlow(idx, mx, my) {
  const { x, y } = padIndexToXY(idx);
  const d = Math.sqrt((x - mx) ** 2 + (y - my) ** 2);
  if (d < PAD_BRIGHT_NEAR) return 127;
  if (d < PAD_BRIGHT_MED) return 50;
  if (d < PAD_BRIGHT_FAR) return 12;
  return 0;
}

function setLED(note, vel) {
  move_midi_internal_send([0, 0x90, note, vel]);
}

function isNoteParam(key) {
  return NOTE_PARAMS[key] === true;
}

function isToggleParam(key) {
  return TOGGLE_PARAMS[key] === true;
}


function clampParam(key, value) {
  if (BRANCH_NOTE_PARAMS[key]) return clampBranchNote(value);
  if (isNoteParam(key)) return clampNote(value);
  if (key === 'grid_view') return clampPage(value);
  if (isToggleParam(key)) return value > 0 ? 1 : 0;
  if (key === 'steps') {
    const steps = Math.round(value);
    if (steps < 1) return 1;
    if (steps > 32) return 32;
    return steps;
  }
  if (key === 'bpm') {
    const bpm = Math.round(value);
    if (bpm < 40) return 40;
    if (bpm > 240) return 240;
    return bpm;
  }
  if (key === 'sync') return value > 0 ? 1 : 0;
  return clamp01(value);
}

function formatParamValue(key, value) {
  if (isNoteParam(key) || BRANCH_NOTE_PARAMS[key] || key === 'grid_view' || isToggleParam(key) || key === 'steps' || key === 'bpm') {
    return String(Math.round(value));
  }
  if (key === 'sync') return value > 0 ? 'internal' : 'move';
  return value.toFixed(4);
}

function paramDelta(key, delta) {
  if (isNoteParam(key) || BRANCH_NOTE_PARAMS[key]) return delta;
  if (key === 'grid_view' || isToggleParam(key) || key === 'sync') return delta > 0 ? 1 : -1;
  if (key === 'steps' || key === 'bpm') return delta > 0 ? 1 : -1;
  return delta * 0.005;
}

function knobDelta(key, delta) {
  if (isNoteParam(key) || BRANCH_NOTE_PARAMS[key]) return delta;
  if (key === 'grid_view' || isToggleParam(key) || key === 'sync') return delta > 0 ? 1 : -1;
  if (key === 'steps' || key === 'bpm') return delta > 0 ? 1 : -1;
  return delta * 0.01;
}

function laneCharToCell(ch) {
  if (ch === 'A') return 2;
  if (ch === 'X') return 1;
  return 0;
}

function loadPreviewLane(raw, lane) {
  const target = g.previewGrid[lane];
  for (let i = 0; i < 32; i++) {
    target[i] = laneCharToCell(raw && i < raw.length ? raw[i] : '.');
  }
}

function refreshPreview(force = false) {
  const rawRev = host_module_get_param('preview_rev');
  const rev = rawRev !== undefined && rawRev !== null ? parseInt(rawRev, 10) : NaN;
  if (!force && Number.isFinite(rev) && rev === g.previewRev) return;

  loadPreviewLane(host_module_get_param('preview_kick') || '', 0);
  loadPreviewLane(host_module_get_param('preview_snare') || '', 1);
  loadPreviewLane(host_module_get_param('preview_hat') || '', 2);
  if (Number.isFinite(rev)) g.previewRev = rev;
}

function refreshPlayhead() {
  const raw = host_module_get_param('play_step');
  if (raw === undefined || raw === null) return;
  const step = parseInt(raw, 10);
  if (Number.isFinite(step)) g.step = step & 31;
}

function setParam(key, value) {
  const next = clampParam(key, value);
  g.params[key] = next;
  host_module_set_param(key, formatParamValue(key, next));
}

function cyclePage(delta = 1, resetFocus = true) {
  const next = (clampPage(g.params.grid_view) + delta + 2) % 2;
  setParam('grid_view', next);
  if (resetFocus) { g.focused = null; g.editing = false; }
  else { g.editing = false; }
}

function currentParamList() {
  return clampPage(g.params.grid_view) === PAGE_PARAMS ? PARAMS_PARAM_LIST : MAIN_PARAM_LIST;
}

function moveCursor(delta) {
  const list = currentParamList();
  const idx = list.indexOf(g.focused);
  const raw = idx < 0 ? 0 : idx + delta;
  if (raw < 0) {
    // début de liste → page précédente, dernier param
    cyclePage(-1, false);
    const newList = currentParamList();
    g.focused = newList[newList.length - 1];
  } else if (raw >= list.length) {
    // fin de liste → page suivante, premier param
    cyclePage(1, false);
    g.focused = currentParamList()[0];
  } else {
    g.focused = list[raw];
  }
}

function drawBar(bx, by, bw, bh, value) {
  const filled = Math.round(clamp01(value) * bw);
  draw_rect(bx - 1, by - 1, bw + 2, bh + 2, 1);
  if (filled > 0) fill_rect(bx, by, filled, bh, 1);
  if (filled < bw) fill_rect(bx + filled, by, bw - filled, bh, 0);
}

function renderMainPage() {
  const p = g.params;
  const kDot = g.flash[0] > 0 ? '*' : '.';
  const sDot = g.flash[1] > 0 ? '*' : '.';
  const hDot = g.flash[2] > 0 ? '*' : '.';
  const stepNum = String(g.step + 1).padStart(2, '0');

  print(0, 0, 'BRANCHAGE', 1);
  print(62, 0, `K${kDot}S${sDot}H${hDot}`, 1);
  print(110, 0, stepNum, 1);

  print(0, 10, 'X', 1);
  drawBar(10, 11, 114, 5, p.map_x);

  print(0, 18, 'Y', 1);
  drawBar(10, 19, 114, 5, p.map_y);

  const focK = g.focused === 'density_kick' ? '>' : ' ';
  const focS = g.focused === 'density_snare' ? '>' : ' ';
  const focH = g.focused === 'density_hat' ? '>' : ' ';
  print(0, 26, `K${focK}`, 1);
  drawBar(13, 27, 32, 5, p.density_kick);
  print(48, 26, `S${focS}`, 1);
  drawBar(61, 27, 32, 5, p.density_snare);
  print(96, 26, `H${focH}`, 1);
  drawBar(109, 27, 15, 5, p.density_hat);

  const focC = g.focused === 'randomness' ? '>' : ' ';
  print(0, 34, `~${focC}`, 1);
  drawBar(13, 35, 111, 5, p.randomness);

  const focKN = g.focused === 'kick_note' ? '>' : ' ';
  const focSN = g.focused === 'snare_note' ? '>' : ' ';
  const focHN = g.focused === 'hat_note' ? '>' : ' ';
  print(0, 42, `K${focKN}${p.kick_note} S${focSN}${p.snare_note} H${focHN}${p.hat_note}`, 1);

  const syncWarn = host_module_get_param('sync_warn') || '';
  if (syncWarn.length > 0) {
    print(0, 54, `!${syncWarn}`, 1);
  } else if (g.focused) {
    print(0, 54, `> ${g.focused}`, 1);
  } else {
    print(0, 54, `PAGE ${pageName()}`, 1);
  }
}

function randLaneFromFocused() {
  for (let lane = 0; lane < 3; lane++) {
    if (g.focused === BRANCH_RAND_LOW_KEYS[lane]) return lane;
    if (g.focused === BRANCH_RAND_HIGH_KEYS[lane]) return lane;
  }
  return -1;
}

function renderParamsPage() {
  const p = g.params;

  // Header: timing params
  const syncStr = p.sync > 0 ? 'INT' : 'MOV';
  const focSteps = g.focused === 'steps' ? '>' : ' ';
  const focBpm   = g.focused === 'bpm'   ? '>' : ' ';
  const focSync  = g.focused === 'sync'  ? '>' : ' ';
  print(0, 0, `ST${focSteps}${p.steps} BPM${focBpm}${p.bpm} ${focSync}${syncStr}`, 1);

  // Branch lanes
  const rows = [10, 24, 38];
  const focusedRandLane = randLaneFromFocused();

  for (let lane = 0; lane < 3; lane++) {
    const probKey    = BRANCH_PROB_KEYS[lane];
    const noteKey    = BRANCH_NOTE_KEYS[lane];
    const enabledKey = BRANCH_ENABLED_KEYS[lane];
    const dot        = g.branchFlash[lane] > 0 ? '*' : '.';
    const isRandFoc  = focusedRandLane === lane;
    const focLane    = isRandFoc || g.focused === probKey ? '>' : ' ';
    const focNote    = g.focused === noteKey ? '>' : ' ';
    const focEnabled = g.focused === enabledKey ? '>' : ' ';
    const y          = rows[lane];
    const noteVal    = p[noteKey];
    const noteStr    = noteVal === -1 ? 'RND' : String(noteVal).padStart(3, ' ');

    print(0, y, `${BRANCH_LABELS[lane]}${dot}${focLane}`, 1);
    drawBar(20, y + 1, 58, 5, p[probKey]);
    print(82, y, `${focNote}${noteStr}`, 1);
    print(106, y, `${focEnabled}${p[enabledKey] ? 'ON' : 'OF'}`, 1);
  }

  // Footer
  if (focusedRandLane >= 0) {
    const lo    = p[BRANCH_RAND_LOW_KEYS[focusedRandLane]];
    const hi    = p[BRANCH_RAND_HIGH_KEYS[focusedRandLane]];
    const lbl   = BRANCH_LABELS[focusedRandLane];
    const focLo = g.focused === BRANCH_RAND_LOW_KEYS[focusedRandLane] ? '>' : ' ';
    const focHi = g.focused === BRANCH_RAND_HIGH_KEYS[focusedRandLane] ? '>' : ' ';
    print(0, 54, `${lbl}RND ${focLo}${lo}-${focHi}${hi}`, 1);
  } else if (g.focused) {
    print(0, 54, `> ${g.focused}`, 1);
  } else {
    print(0, 54, 'PAGE PRMS', 1);
  }
}

function render() {
  clear_screen();
  const page = clampPage(g.params.grid_view);
  if (page === PAGE_PARAMS) {
    renderParamsPage();
  } else {
    renderMainPage();
  }
}

function updatePadSlice() {
  const mx = g.params.map_x;
  const my = g.params.map_y;
  const base = g.padDirtyPhase * 8;

  for (let i = base; i < base + 8; i++) {
    const target = padGlow(i, mx, my);
    if (g.padLEDCache[i] !== target) {
      g.padLEDCache[i] = target;
      setLED(PAD_BASE + i, target);
    }
  }

  g.padDirtyPhase = (g.padDirtyPhase + 1) & 3;
  if (g.padDirtyPhase === 0) g.padDirty = false;
}



globalThis.init = function () {
  for (const key of Object.keys(PARAM_DEFAULTS)) {
    const raw = host_module_get_param(key);
    if (raw === undefined || raw === null) continue;
    if (key === 'sync') {
      g.params[key] = raw === 'internal' ? 1 : 0;
    } else {
      g.params[key] = parseFloat(raw);
    }
  }
  refreshPreview(true);
  refreshPlayhead();
  g.padDirty = true;
};

globalThis.tick = function () {
  for (let i = 0; i < 3; i++) {
    if (g.flash[i] > 0) g.flash[i]--;
    if (g.branchFlash[i] > 0) g.branchFlash[i]--;
  }

  refreshPlayhead();
  refreshPreview();
  render();

  if (g.padDirty) updatePadSlice();
};

globalThis.onMidiMessageInternal = function (data) {
  if (!data || data.length < 3) return;
  // ignorer seulement les notes capacitives (note-on, notes 0-9), pas les CC
  if (data[0] === 0x90 && data[1] < 10) return;

  const status = data[0];
  const b1 = data[1];
  const b2 = data.length > 2 ? data[2] : 0;
  const type = status & 0xF0;
  const page = clampPage(g.params.grid_view);

  if (type === 0xB0) {
    const knobMap = page === PAGE_PARAMS ? PARAMS_KNOB_PARAMS : MAIN_KNOB_PARAMS;
    const key = knobMap[b1];

    // Knob direct control — focuses the param, stays in edit mode
    if (key) {
      const delta = decodeDelta(b2);
      setParam(key, g.params[key] + knobDelta(key, delta));
      g.focused = key;
      g.editing = true;
      if (key === 'map_x' || key === 'map_y') g.padDirty = true;
      return;
    }

    // Jog wheel: navigation ou changement de valeur si editing
    if (b1 === CC_JOG_WHEEL) {
      const d = decodeDelta(b2);
      if (g.editing && g.focused) {
        setParam(g.focused, g.params[g.focused] + paramDelta(g.focused, d));
        if (g.focused === 'map_x' || g.focused === 'map_y') g.padDirty = true;
      } else {
        moveCursor(d > 0 ? 1 : -1);
      }
      return;
    }

    // Jog click: focus premier param si vide, sinon bascule mode edit
    if (b1 === CC_JOG_CLICK && b2 > 0) {
      if (!g.focused) {
        g.focused = currentParamList()[0];
        g.editing = false;
      } else {
        g.editing = !g.editing;
      }
      return;
    }
    return;
  }

  if (type === 0x90 && b2 > 0) {
    // Step 4 (note 19): change page
    if (b1 === STEP_BASE + 3) {
      cyclePage(1);
      return;
    }

    // Pads: set map position (page MAIN only)
    if (page === PAGE_MAIN && b1 >= PAD_BASE && b1 < PAD_BASE + 32) {
      const idx = b1 - PAD_BASE;
      const { x, y } = padIndexToXY(idx);
      setParam('map_x', x);
      setParam('map_y', y);
      g.padDirty = true;
    }
  }
};

globalThis.onMidiMessageExternal = function (data) {
  if (!data || data.length < 2) return;

  const status = data[0] & 0xF0;
  const note = data[1];
  const velocity = data.length > 2 ? data[2] : 0;

  if (status !== 0x90 || velocity <= 0) return;

  if (note === g.params.kick_note) g.flash[0] = FLASH_TICKS;
  if (note === g.params.snare_note) g.flash[1] = FLASH_TICKS;
  if (note === g.params.hat_note) g.flash[2] = FLASH_TICKS;

  for (let lane = 0; lane < 3; lane++) {
    if (note === g.params[BRANCH_NOTE_KEYS[lane]]) {
      g.branchFlash[lane] = FLASH_TICKS;
    }
  }
};
