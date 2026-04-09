# Branchage

Per-lane branching drum sequencer for Schwung on Ableton Move.

`Branchage` is a chainable `midi_fx` module built around two ideas:

- `Grids` drives the drum rhythm
- each lane can probabilistically switch to its own alternate note, in the spirit of `Branches`

## How it works

```
            ┌───────────────┐
            │    GRIDS      │
            │  (rhythm map) │
            └──────┬────────┘
                   │
     ┌─────────────┼─────────────┐
     │             │             │
     ▼             ▼             ▼
  KICK          SNARE          HAT
   lane          lane          lane
     │             │             │
     ▼             ▼             ▼
 ┌────────┐   ┌────────┐   ┌────────┐
 │BRANCHES│   │BRANCHES│   │BRANCHES│
 │  prob  │   │  prob  │   │  prob  │
 └────┬───┘   └────┬───┘   └────┬───┘
      │            │            │
      ▼            ▼            ▼
  note A/B     note A/B     note A/B
  or RANDOM    or RANDOM    or RANDOM
```

1. **GRIDS** creates the rhythm skeleton (kick / snare / hat)
2. Each lane goes through a **branch** gate
3. Each trigger can:
   - stay the same note
   - switch to an alternate note
   - become a random note from a per-lane range

→ Result: evolving drum patterns with controlled randomness

## Features

- Grids-style topographic drum pattern generation
- 3 independent lanes: kick, snare, hat
- Per-lane branch enable, probability, and branch note
- Per-lane `RND` branch mode with independent random note ranges
- `move` sync from Move transport or `internal` sync from module BPM
- Custom Move UI with dedicated `MAIN`, `GRID`, and `BRCH` pages

## Prerequisites

- [Schwung](https://github.com/charlesvestal/move-anything) installed on your Ableton Move
- SSH access enabled if you want to install manually: `http://move.local/development/ssh`

Important for `sync=move`:

- the required fix is: `Move -> Settings -> MIDI -> MIDI Clock Out -> On`
- if `MIDI Clock Out` is disabled, `get_clock_status()` returns `UNAVAILABLE`
- in that case the module correctly stays silent in `move` sync mode
- once `MIDI Clock Out` is enabled, start and stop follow Move transport as expected

## Installation

### Via Module Store

If `Branchage` is published in Schwung Module Store:

1. Launch Schwung on Move
2. Open `Module Store`
3. Navigate to `Installed` or the relevant `MIDI FX` entry
4. Load `Branchage`

### Manual Install

```bash
./scripts/build.sh
./scripts/install.sh
```

Install path on Move:

```text
/data/UserData/schwung/modules/midi_fx/branchage
```

## Usage

`Branchage` is a MIDI FX module:

1. Insert `Branchage` in a chain MIDI FX slot
2. Route its output to a drum or sampler module
3. Choose `sync=move` to follow Move transport, or `sync=internal` to run from the module BPM
4. Shape the base groove on `MAIN`
5. Inspect the 16-step pattern preview on `GRID`
6. Configure per-lane branching on `BRCH`

When a Grids lane fires, `Branchage` outputs either:

- the lane base note
- the lane branch note
- a random note inside that lane's branch range when branch note is set to `RND`

Each Grids lane still emits at most one note per step.

## Quick Start Tutorial

This is the fastest way to get `Branchage` working on Move.

1. Insert `Branchage` in a chain MIDI FX slot.
2. Insert a drum instrument or sampler after it in the same chain.
3. Open the `MAIN` page.
4. Set `sync` to `internal` first if you want to confirm the module is producing notes immediately.
5. Adjust `map_x`, `map_y`, and the three density controls until the base groove feels right.
6. Go to `BRCH`.
7. Set a branch probability for kick, snare, or hat.
8. Set a fixed branch note for that lane, or turn the branch note below `0` to switch it to `RND`.
9. If you use `RND`, set that lane's random range with step buttons `20-25`.
10. Return to `MAIN` or `GRID` and listen to the result.

Important:

- `0` is still a valid MIDI note value
- random mode is activated by setting branch note to `-1`
- on the Move UI, you reach that by turning the branch note below `0`, which snaps to `RND`

To use Move transport sync:

1. On Move, go to `Settings -> MIDI -> MIDI Clock Out`.
2. Set `MIDI Clock Out` to `On`.
3. Back in `Branchage`, set `sync` to `move`.
4. Press play on Move.
5. The module should start with transport and stop when Move stops.

Recommended first patch:

1. Keep kick on a fixed branch note with low probability.
2. Put snare on `RND` with a tight range.
3. Put hat on `RND` with a wider range.
4. Use `GRID` to watch the base rhythm and `BRCH` to shape the note variation.

## UI

The custom Move UI has 3 pages:

- `MAIN`: map, density, randomness, timing
- `GRID`: 16-step rhythm preview
- `BRCH`: per-lane branch controls

On the `BRCH` page:

- knobs `71-73` edit branch probability for kick, snare, hat
- knobs `74-76` edit branch note for kick, snare, hat
- track buttons `40-42` focus branch probability
- step buttons `16-18` focus branch note
- turning a branch note below `0` snaps it to `RND` (`-1` internally, not `0`)
- step buttons `20-21` focus kick random low/high
- step buttons `22-23` focus snare random low/high
- step buttons `24-25` focus hat random low/high
- jog click toggles the focused lane on/off
- step button `19` changes page

When a lane is in `RND` mode:

- the note column shows `RND`
- the bottom line shows the focused lane random range
- if no range parameter is focused, the UI shows `STP[20-25]=range`

On the `MAIN` page in `sync=move`:

- `!Enable MIDI Clock Out` means Move clock status is unavailable, and it replaces the normal page label on the bottom line
- `!stopped` means Move sync is available but transport is stopped, and it also replaces the normal page label
- when Move clock is available and transport is running normally, the usual page label is shown

## Parameters

### Groove

| Parameter | What it does |
|---------|--------|
| `map_x`, `map_y` | Select the Grids pattern area. |
| `density_kick`, `density_snare`, `density_hat` | Set lane density. |
| `randomness` | Adds Grids variation. |
| `steps` | Sequence length. |
| `kick_note`, `snare_note`, `hat_note` | Base MIDI note for each lane. |

### Per-Lane Branch

Each lane has the same branch control structure.

| Parameter | What it does |
|---------|--------|
| `kick_branch_enabled`, `snare_branch_enabled`, `hat_branch_enabled` | Enables or disables branching on that lane. |
| `kick_branch_prob`, `snare_branch_prob`, `hat_branch_prob` | Branch probability for that lane. |
| `kick_branch_note`, `snare_branch_note`, `hat_branch_note` | Fixed branch note, or `-1` for `RND`. `0` remains a valid MIDI note. |
| `kick_branch_rand_low`, `snare_branch_rand_low`, `hat_branch_rand_low` | Low bound for lane random note selection. |
| `kick_branch_rand_high`, `snare_branch_rand_high`, `hat_branch_rand_high` | High bound for lane random note selection. |

Random branch behavior:

- set a `*_branch_note` to `-1` to enable `RND`
- `0` does not enable random mode
- in `RND`, the lane picks a random MIDI note from its own `[rand_low, rand_high]` range
- if low and high are inverted, the bounds are swapped internally
- setting a fixed note again disables `RND` without changing the stored range

### Timing

| Parameter | What it does |
|---------|--------|
| `sync` | Selects `move` or `internal`. |
| `bpm` | Internal tempo when `sync=internal`. |

## Troubleshooting

**No output in `move` sync mode**

- Enable `Settings -> MIDI -> MIDI Clock Out` on Move
- This is the actual fix if Move sync does not work
- Verify Move transport is running
- Check the `MAIN` page warning line
- Confirm the target instrument is loaded after `Branchage`

**No branch note changes**

- Check the lane branch `enabled` setting
- Increase the lane branch probability
- Confirm the lane branch note is set to a fixed note or `RND`

**Random mode does not sound right**

- Check that the lane branch note is actually `RND`
- Review that lane's `rand_low` and `rand_high`
- Remember each lane has its own random range now

**Preview does not match played branch notes**

- The `GRID` preview shows the base Grids rhythm only
- It does not attempt to predict probabilistic branch outcomes

## Build

```bash
make native
make test
make test-midi-fx
./scripts/build.sh
```

## Publish As Custom Module

To publish `Branchage` through the Schwung Module Store, build the release tarball and publish a tagged GitHub release.

Create the package locally:

```bash
./scripts/package.sh
```

This produces the exact structure Schwung expects:

```text
dist/branchage-module.tar.gz
└── branchage/
    ├── module.json
    ├── dsp.so
    └── branchage_ui.js
```

Release flow:

```bash
git add .
git commit -m "Release v0.2.0"
git tag v0.2.0
git push origin main
git push origin v0.2.0
```

After the tag is pushed, GitHub Actions will:

- build `branchage-module.tar.gz`
- create the GitHub release
- update `release.json` on `main`

Suggested Module Store catalog entry:

```json
{
  "id": "branchage",
  "name": "Branchage",
  "description": "Grids drum maps with per-lane Branches-style note branching",
  "author": "Mutable Instruments ideas / port for Schwung Move",
  "component_type": "midi_fx",
  "github_repo": "broduoliviercontact-web/branchages-move",
  "default_branch": "main",
  "asset_name": "branchage-module.tar.gz",
  "min_host_version": "0.3.0"
}
```

## Inspiration

`Branchage` is inspired by Mutable Instruments modules and ideas, especially:

- `Grids` for topographic drum pattern generation
- `Branches` for probabilistic per-lane switching

Reference:

- Mutable Instruments `Grids` documentation: https://pichenettes.github.io/mutable-instruments-documentation/modules/grids/

## Credits

- Schwung framework and host APIs: Charles Vestal and contributors
- Branchage implementation: project contributors
