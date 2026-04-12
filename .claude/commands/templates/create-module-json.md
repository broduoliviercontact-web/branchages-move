---
description: Create a Schwung module.json manifest for a MIDI FX module
argument-hint: [module-name-and-feature-summary]
---

Create a production-ready `module.json` for a Schwung MIDI FX module based on this request:

$ARGUMENTS

Follow project memory in @.claude/CLAUDE.md.

Before drafting, inspect:
- `.claude/MODULES.md`
- `.claude/API.md`
- at least one existing module in `src/` or the reference implementations (Branchage, Grilles)

Requirements:
- Match real Schwung repo conventions.
- Default to a Signal Chain MIDI FX design unless the request clearly says otherwise.
- Prefer a compact, Move-friendly parameter surface.
- Use `api_version: 2` — required for Signal Chain support.
- Do not invent unsupported manifest fields.
- Do not add parameters that the engine will not really support.
- Keep the first version intentionally small and stable.

## Critical: ui_hierarchy vs chain_params vs raw_ui

These three fields are mutually exclusive in specific combinations. Getting this wrong silently breaks the module.

### Module WITH `ui.js` (custom full-screen UI):
```json
"ui": "ui.js",
"ui_chain": "ui_chain.js",
"raw_ui": true,
"chain_params": [ ... ]
```
- `raw_ui: true` tells Schwung to load `ui.js` instead of rendering Shadow UI
- `chain_params` exposes editable params when in chain mode (used by `ui_chain.js`)
- Do NOT add `ui_hierarchy` — it conflicts with `raw_ui` and prevents the custom UI from loading

### Module WITHOUT `ui.js` (Shadow UI only):
```json
"ui_hierarchy": {
  "levels": {
    "root": { "name": "My Module", "knobs": ["p1", "p2"], "params": ["p1", "p2"] }
  }
}
```
- `ui_hierarchy` lets Schwung render parameters in the Shadow UI slot editor
- Without it, the module loads but shows "No presets" with no editable parameters
- Do NOT add `raw_ui` — there is no `ui.js` to load

### Never combine:
- `raw_ui: true` + `ui_hierarchy` at the same level → conflict, custom UI never loads
- `ui_hierarchy` inside `capabilities` → wrong location, ignored

Parameter rules:
- Prefer 2 to 8 primary parameters.
- Use realistic types such as:
  - `enum`
  - `int`
  - `float`
  - `toggle`
- Every parameter must include a sensible default.
- Knob assignments must match the most important controls.

Decision rules:
- If this should be an external module, do not mark it as built-in unless explicitly requested.
- If the request is underspecified, infer a minimal usable V1 design and state your assumptions.

Return exactly:
1. A short design summary
2. One fenced `json` block containing the full `module.json`
3. A short assumptions section

Do not generate any other files in this step.