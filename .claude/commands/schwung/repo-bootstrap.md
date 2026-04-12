# Schwung Repo Bootstrap

## Purpose
Use this skill whenever you are about to design, modify, port, or debug a module for the Schwung project.

This skill establishes the minimum required reading, repo conventions, and architectural assumptions before any implementation work begins.

## Required Reading
Before changing code, read and align with:
- `.claude/CLAUDE.md`
- `.claude/BUILDING.md`
- `.claude/MODULES.md`
- `.claude/API.md`

Also inspect at least one real module — the proven reference implementations are:
- `Branchage` (full module: native DSP + ui.js + ui_chain.js + packaging)
- `Grilles` (same structure, simpler engine)

## Core Assumptions
Schwung modules are folder-based modules with a `module.json` manifest.
Depending on the module, a module may include:
- `module.json`
- `ui.js`
- `ui_chain.js`
- `dsp.so`
- native source files under `dsp/`

For modern module work:
- prefer `api_version: 2` where applicable
- respect the repo’s existing module layout and conventions
- do not invent capabilities or manifest fields that are not already documented or used in the repo

## Bootstrap Checklist
1. Read the required docs.
2. Inspect at least one existing MIDI FX module.
3. Inspect shared Move input and LED helpers.
4. Confirm whether the target should be:
   - built-in repo module
   - external drop-in module
5. Confirm whether the implementation should be:
   - native MIDI FX
   - JS UI only
   - hybrid (native engine + JS UI)
6. Write a short implementation brief before coding.

## Implementation Brief Template
Produce this brief before any code changes:

### Module Summary
- Module name:
- Module id:
- Module type:
- Built-in or external:
- Native DSP required:
- UI required:
- Chain UI required:

### Functional Goal
Describe exactly what the module should do to incoming and outgoing MIDI.

### User Controls
List every exposed parameter and its type:
- enum
- int
- float
- toggle
- trigger

### Move Mapping
Describe:
- knob mapping
- shift behavior
- pad behavior
- step button behavior
- menu/back behavior
- LED plan

### Technical Plan
Describe:
- manifest fields
- source files to create or edit
- state persistence plan
- test plan

## Deploy and Iterate on Move

For rapid iteration without a full release cycle, deploy directly via SCP:

```bash
# Deploy a UI file
scp -i ~/.ssh/move_key src/ui/ui_chain.js ableton@move.local:/data/UserData/schwung/modules/midi_fx/<id>/ui_chain.js

# Verify the file landed correctly (MD5 comparison)
md5 src/ui/ui_chain.js
ssh -i ~/.ssh/move_key ableton@move.local "md5sum /data/UserData/schwung/modules/midi_fx/<id>/ui_chain.js"
```

After SCP: navigate away from the module and back — Schwung re-evaluates JS on entry.
If the old UI still shows after re-entry: Move has cached the script in RAM → full power cycle required.

**Debug tip:** Temporarily change the title string (e.g. `'MODULE v2'`) before deploying to confirm the new JS loaded.

**Log files on Move:**
```bash
ssh -i ~/.ssh/move_key ableton@move.local "cat /tmp/schwung.log"
# DSP debug output goes to /tmp/<module>_debug.log
```

## Guardrails
- Do not start coding before identifying at least one reference module.
- Do not assume the module is audio if the goal is clearly MIDI FX.
- Do not design desktop-style UI patterns that do not fit Move.
- Prefer consistency with existing Schwung modules over theoretical elegance.
- Reuse repo helpers when possible instead of re-implementing infrastructure.
- `ui_chain.js` is the PRIMARY interaction surface for a MIDI FX in a signal chain — design it first.

## Output Contract
When this skill is used, output:
1. a brief repo-aware summary
2. the implementation brief
3. a proposed file tree
4. a list of files that should be inspected next