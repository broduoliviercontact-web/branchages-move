/**
 * ui_chain.js — Branchage compact chain UI (2 pages)
 *
 * Page 1: X, Y, Kick density, Snare density, Hat density, Chaos
 * Page 2: Steps, Sync, BPM
 *
 * Jog wheel navigates, jog click toggles edit mode.
 * Pads show X/Y glow. Cursor wraps between pages.
 */

'use strict';

import {
  decodeDelta,
  isCapacitiveTouchMessage,
  setLED as sharedSetLED,
} from '/data/UserData/schwung/shared/input_filter.mjs';

/* ── Constants ─────────────────────────────────────────────────────────── */

const PAD_BASE     = 68;
const CC_JOG_WHEEL = 14;
const CC_JOG_CLICK = 3;
const FLASH_TICKS  = 5;
const PAGE_MAIN    = 0;
const PAGE_PARAMS  = 1;

const PAD_BRIGHT_NEAR = 0.07;
const PAD_BRIGHT_MED  = 0.22;
const PAD_BRIGHT_FAR  = 0.45;

const KNOB_PARAMS = {
  71: 'map_x',
  72: 'map_y',
  73: 'density_kick',
  74: 'density_snare',
  75: 'density_hat',
  76: 'randomness',
};

const MAIN_PARAM_LIST = [
  'map_x', 'map_y',
  'density_kick', 'density_snare', 'density_hat',
  'randomness',
];

const PARAMS_PARAM_LIST = [
  'steps', 'sync', 'bpm',
];

const PARAM_DEFAULTS = {
  map_x:         0.5,
  map_y:         0.5,
  density_kick:  0.5,
  density_snare: 0.5,
  density_hat:   0.5,
  randomness:    0.0,
  steps:         16,
  sync:          0,
  bpm:           120,
  // read-only — for flash indicators
  kick_note:     36,
  snare_note:    38,
  hat_note:      42,
};

/* ── State ─────────────────────────────────────────────────────────────── */

const s = {
  params:        { ...PARAM_DEFAULTS },
  page:          PAGE_MAIN,
  step:          0,
  flash:         [0, 0, 0],
  focused:       'map_x',
  editing:       false,
  dirty:         true,
  padLEDCache:   new Uint8Array(32),
  padDirty:      true,
  padDirtyPhase: 0,
};

/* ── Param helpers ─────────────────────────────────────────────────────── */

function clamp01(v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

function clampParam(key, value) {
  if (key === 'steps') { const n = Math.round(value); return n < 1 ? 1 : n > 32 ? 32 : n; }
  if (key === 'bpm')   { const n = Math.round(value); return n < 40 ? 40 : n > 240 ? 240 : n; }
  if (key === 'sync')  { return Math.round(value) !== 0 ? 1 : 0; }
  return clamp01(value);
}

function formatParam(key, value) {
  if (key === 'steps' || key === 'bpm') return String(Math.round(value));
  if (key === 'sync') return value > 0 ? 'internal' : 'move';
  return value.toFixed(4);
}

function jogDelta(key, d) {
  if (key === 'steps' || key === 'bpm' || key === 'sync') return d > 0 ? 1 : -1;
  return d * 0.005;
}

function dispVal(key) {
  const v = s.params[key];
  if (key === 'steps' || key === 'bpm') return String(Math.round(v));
  if (key === 'sync') return Math.round(v) === 0 ? 'MOV' : 'INT';
  return v.toFixed(2);
}

function setParam(key, value) {
  const next = clampParam(key, value);
  s.params[key] = next;
  host_module_set_param(key, formatParam(key, next));
  s.dirty = true;
}

/* ── Navigation ─────────────────────────────────────────────────────────── */

function currentParamList() {
  return s.page === PAGE_PARAMS ? PARAMS_PARAM_LIST : MAIN_PARAM_LIST;
}

function cyclePage(delta) {
  s.page    = (s.page + delta + 2) % 2;
  s.editing = false;
}

function moveCursor(delta) {
  const list = currentParamList();
  const idx  = list.indexOf(s.focused);
  const raw  = idx < 0 ? 0 : idx + delta;
  if (raw < 0) {
    cyclePage(-1);
    const nl = currentParamList();
    s.focused = nl[nl.length - 1];
  } else if (raw >= list.length) {
    cyclePage(1);
    s.focused = currentParamList()[0];
  } else {
    s.focused = list[raw];
  }
  s.dirty = true;
}

function foc(key) {
  return s.focused === key ? (s.editing ? '[' : '>') : ' ';
}

/* ── Pad helpers ────────────────────────────────────────────────────────── */

function padIndexToXY(idx) {
  return { x: (idx % 8) / 7, y: Math.floor(idx / 8) / 3 };
}

function padGlow(idx, mx, my) {
  const { x, y } = padIndexToXY(idx);
  const d = Math.sqrt((x - mx) ** 2 + (y - my) ** 2);
  if (d < PAD_BRIGHT_NEAR) return 127;
  if (d < PAD_BRIGHT_MED)  return 50;
  if (d < PAD_BRIGHT_FAR)  return 12;
  return 0;
}

function setLED(note, vel) { sharedSetLED(note, vel); }

function refreshPlayhead() {
  const raw = host_module_get_param('play_step');
  if (raw === undefined || raw === null) return;
  const step = parseInt(raw, 10);
  if (Number.isFinite(step)) s.step = step & 31;
}

/* ── Render ─────────────────────────────────────────────────────────────── */

function drawBar(bx, by, bw, bh, value) {
  const filled = Math.round(clamp01(value) * bw);
  draw_rect(bx - 1, by - 1, bw + 2, bh + 2, 1);
  if (filled > 0)  fill_rect(bx, by, filled, bh, 1);
  if (filled < bw) fill_rect(bx + filled, by, bw - filled, bh, 0);
}

function renderMainPage() {
  const p       = s.params;
  const kDot    = s.flash[0] > 0 ? '*' : '.';
  const sDot    = s.flash[1] > 0 ? '*' : '.';
  const hDot    = s.flash[2] > 0 ? '*' : '.';
  const stepNum = String(s.step + 1).padStart(2, '0');

  print(0,   0, 'BRANCH 1/2', 1);
  print(76,  0, `K${kDot}S${sDot}H${hDot}`, 1);
  print(110, 0, stepNum, 1);

  print(0, 10, `X${foc('map_x')}`, 1);
  drawBar(16, 11, 108, 5, p.map_x);

  print(0, 18, `Y${foc('map_y')}`, 1);
  drawBar(16, 19, 108, 5, p.map_y);

  print(0,  27, `K${foc('density_kick')}`,  1); drawBar(16,  28, 26, 5, p.density_kick);
  print(44, 27, `S${foc('density_snare')}`, 1); drawBar(60,  28, 26, 5, p.density_snare);
  print(88, 27, `H${foc('density_hat')}`,   1); drawBar(104, 28, 20, 5, p.density_hat);

  print(0, 36, `~${foc('randomness')}`, 1);
  drawBar(16, 37, 108, 5, p.randomness);

  const mark = s.editing ? '[EDIT]' : '[ NAV]';
  print(0, 54, `${mark} ${s.focused}: ${dispVal(s.focused)}`, 1);
}

function renderParamsPage() {
  print(0, 0, 'BRANCH 2/2', 1);

  print(0, 18, `ST${foc('steps')}${dispVal('steps')}`, 1);

  print(0,  36, `SY${foc('sync')}${dispVal('sync')}`, 1);
  print(64, 36, `BP${foc('bpm')}${dispVal('bpm')}`, 1);

  const mark = s.editing ? '[EDIT]' : '[ NAV]';
  print(0, 54, `${mark} ${s.focused}: ${dispVal(s.focused)}`, 1);
}

function render() {
  clear_screen();
  if (s.page === PAGE_PARAMS) renderParamsPage();
  else                        renderMainPage();
}

/* ── Pad LEDs ───────────────────────────────────────────────────────────── */

function updatePadSlice() {
  const mx   = s.params.map_x;
  const my   = s.params.map_y;
  const base = s.padDirtyPhase * 8;
  for (let i = base; i < base + 8; i++) {
    const target = padGlow(i, mx, my);
    if (s.padLEDCache[i] !== target) {
      s.padLEDCache[i] = target;
      setLED(PAD_BASE + i, target);
    }
  }
  s.padDirtyPhase = (s.padDirtyPhase + 1) & 3;
  if (s.padDirtyPhase === 0) s.padDirty = false;
}

/* ── Lifecycle ──────────────────────────────────────────────────────────── */

function init() {
  for (const key of Object.keys(PARAM_DEFAULTS)) {
    const raw = host_module_get_param(key);
    if (raw === undefined || raw === null) continue;
    if (key === 'sync') {
      s.params[key] = raw === 'internal' ? 1 : 0;
    } else {
      s.params[key] = parseFloat(raw);
    }
  }
  s.page    = PAGE_MAIN;
  s.focused = 'map_x';
  s.editing = false;
  refreshPlayhead();
  s.padDirty = true;
  s.dirty    = true;
}

function tick() {
  for (let i = 0; i < 3; i++) {
    if (s.flash[i] > 0) { s.flash[i]--; s.dirty = true; }
  }
  const prev = s.step;
  refreshPlayhead();
  if (s.step !== prev) s.dirty = true;

  if (s.dirty) { render(); s.dirty = false; }
  if (s.padDirty) updatePadSlice();
}

/* ── Input ──────────────────────────────────────────────────────────────── */

function onMidiMessageInternal(data) {
  if (!data || data.length < 3) return;
  if (isCapacitiveTouchMessage(data)) return;

  const status = data[0];
  const b1     = data[1];
  const b2     = data.length > 2 ? data[2] : 0;
  const type   = status & 0xF0;

  if (type === 0xB0) {
    // Knobs 71-76: direct control of main params
    if (b1 >= 71 && b1 <= 76 && KNOB_PARAMS[b1]) {
      const key   = KNOB_PARAMS[b1];
      const delta = decodeDelta(b2) * 0.01;
      setParam(key, s.params[key] + delta);
      s.focused = key;
      s.editing = true;
      s.page    = PAGE_MAIN;
      if (key === 'map_x' || key === 'map_y') s.padDirty = true;
      return;
    }

    // Jog wheel: navigate or edit
    if (b1 === CC_JOG_WHEEL) {
      const d = decodeDelta(b2);
      if (s.editing) {
        setParam(s.focused, s.params[s.focused] + jogDelta(s.focused, d));
        if (s.focused === 'map_x' || s.focused === 'map_y') s.padDirty = true;
      } else {
        moveCursor(d > 0 ? 1 : -1);
      }
      return;
    }

    // Jog click: toggle edit mode
    if (b1 === CC_JOG_CLICK && b2 > 0) {
      s.editing = !s.editing;
      s.dirty   = true;
      return;
    }
  }

  // Pads: set map X/Y + trigger glow update
  if (type === 0x90 && b1 >= PAD_BASE && b1 < PAD_BASE + 32 && b2 > 0) {
    const idx      = b1 - PAD_BASE;
    const { x, y } = padIndexToXY(idx);
    setParam('map_x', x);
    setParam('map_y', y);
    s.padDirty = true;
  }
}

function onMidiMessageExternal(data) {
  if (!data || data.length < 2) return;
  const b2 = data.length > 2 ? data[2] : 0;
  if ((data[0] & 0xF0) === 0x90 && b2 > 0) {
    if (data[1] === s.params.kick_note)  s.flash[0] = FLASH_TICKS;
    if (data[1] === s.params.snare_note) s.flash[1] = FLASH_TICKS;
    if (data[1] === s.params.hat_note)   s.flash[2] = FLASH_TICKS;
    s.dirty = true;
  }
}

/* ── Export chain_ui ────────────────────────────────────────────────────── */

globalThis.chain_ui = {
  init,
  tick,
  onMidiMessageInternal,
  onMidiMessageExternal,
};
