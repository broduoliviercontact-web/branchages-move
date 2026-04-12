# Package and Release External Schwung Module

## Purpose
Use this skill to package a Schwung module as an external drop-in module that can be installed without rebuilding the full host.

## Goal
Prepare a self-contained module folder and release artifact that can be copied into the appropriate Schwung modules location and recognized by the host.

## Packaging Rules
The final package should contain only the files required by the module.

Typical contents:
- `module.json`
- `ui.js` if used
- `ui_chain.js` if used
- `dsp.so` if used
- any required static assets supported by the module design

Do not include:
- temporary build products
- object files
- editor folders
- repo-local debug clutter
- unrelated source material unless intentionally shipping source

## Packaging Process

### 1. Verify Folder Integrity
Check:
- manifest is present
- ids and names are correct
- required binaries exist
- optional UI files are present if referenced
- no missing referenced files remain

### 2. Verify Runtime Assumptions
Check:
- parameter defaults match the implementation
- chain editing parameters are valid
- state restore keys are valid
- the module can initialize from a clean install

### 3. Build Release Artifact
Prepare:
- module folder
- compressed release artifact if requested
- short install instructions
- version note / changelog summary

### 4. Install Instructions
Provide exact install instructions for the user, including:
- where the module folder or archive should go
- whether a rescan or restart is needed
- what to verify after install

### 5. Smoke Test Checklist
Provide a minimal test checklist:
- module appears in browser or chain
- module loads
- parameters edit correctly
- MIDI behavior works
- state recalls correctly
- no stuck notes after bypass/remove/stop

## Required Output Format

### Release Contents
List every file in the release package.

### Install Instructions
Provide the exact install steps.

### Validation Checklist
Provide a short but concrete checklist.

### Known Limitations
List any current limitations.

### Version Note
Provide a concise release note.

## Install Paths by component_type

The module store extracts to a category subdirectory based on `component_type`:

| `component_type` | Install path |
|------------------|-------------|
| `midi_fx` | `modules/midi_fx/<id>/` |
| `sound_generator` | `modules/sound_generators/<id>/` |
| `audio_fx` | `modules/audio_fx/<id>/` |
| `utility` | `modules/utilities/<id>/` |
| `overtake` | `modules/overtake/<id>/` |
| `tool` | `modules/tools/<id>/` |

For manual drop-in, use the same path: `/data/UserData/schwung/modules/<category>/<id>/`.

After copying files, call `host_rescan_modules()` from any Schwung JS context, or restart Schwung. No host recompile is needed.

## Module Catalog Entry Format

To list a module in the official Schwung catalog (`module-catalog.json`), the entry must follow this format:

```json
{
    "id": "your-module-id",
    "name": "Your Module Name",
    "description": "One clear sentence describing what it does.",
    "component_type": "midi_fx",
    "version": "1.0.0",
    "download_url": "https://github.com/<org>/<repo>/releases/download/v1.0.0/<module-id>-module.tar.gz"
}
```

> **Critical:** Use `"version"`, not `"latest_version"`. The Schwung installer reads `"version"` to detect whether an update is available. `"latest_version"` is silently ignored.

The tarball must extract cleanly into a single folder named `<id>/` containing `module.json` and all required files. The module store uses `curl` to download and `tar -xzf` to extract.

## macOS Packaging Pitfalls

When packaging on macOS, these issues will silently break Linux installation:

### 1. GNUSparseFile.0/ on Linux extraction
`bsdtar` (macOS default) marks large binaries as GNUSparse. On Linux, `tar -xzf` creates `GNUSparseFile.0/dsp.so` instead of `dsp.so` — the DSP is never found.

**Fix:** Use `dd` to copy dsp.so and `tar --no-xattrs` (or `gtar`):
```bash
export COPYFILE_DISABLE=1
export COPY_EXTENDED_ATTRIBUTES_DISABLE=1
dd if=build/aarch64/dsp.so of=dist/MODULE_ID/dsp.so bs=1 2>/dev/null
tar --no-xattrs -C dist -czf archive.tar.gz MODULE_ID
# or: gtar -C dist -czf archive.tar.gz MODULE_ID
```
**Diagnose:** `tar -tzf dist/MODULE-module.tar.gz | grep -i sparse`

### 2. Wrong DSP entry point symbol
`chain-v2` requires `move_midi_fx_init`. If the binary exports `move_plugin_init_v2`, DSP silently fails.

**Diagnose:** `strings build/aarch64/dsp.so | grep 'move_midi_fx_init\|move_plugin_init'`

### 3. `raw_ui` vs `ui_hierarchy` — mutually exclusive

**Module WITH `ui.js`:** add `"raw_ui": true`. Do NOT also add `ui_hierarchy` — it conflicts and prevents the custom UI from loading.

**Module WITHOUT `ui.js`:** add `ui_hierarchy` to expose params in the Shadow UI slot editor. Without it, the module loads but shows "No presets" with no editable parameters. Do NOT add `raw_ui`.

```json
// With ui.js — raw_ui only:
"raw_ui": true,
"ui": "ui.js"

// Without ui.js — ui_hierarchy only:
"ui_hierarchy": {
  "levels": {
    "root": { "name": "My Module", "knobs": ["p1", "p2"], "params": ["p1", "p2"] }
  }
}
```

### 4. UI file must be named `ui.js`
The host always looks for `ui.js`. Naming it anything else (e.g. `module_ui.js`) causes silent failure. Copy it as `ui.js` in package.sh.

### 5. GitHub CDN caches assets by filename
If you delete and recreate a release with the same asset filename, GitHub serves the old cached file for ~60s. Use a versioned filename (`module-v1.0.0-module.tar.gz`) or upload with `--clobber` and wait.

### 6. Schwung installer expects `<id>-module.tar.gz` — no version in filename

The Schwung installer looks for a release asset named exactly `<id>-module.tar.gz` (without version number). If only a versioned asset exists (`<id>-v1.0.0-module.tar.gz`), the installer silently fails even though `release.json` looks correct.

**Fix:** Upload **both** files in the release workflow:

```yaml
- name: Create unversioned copy for Schwung installer
  run: cp dist/${{ steps.module.outputs.tarball }} dist/${{ steps.module.outputs.tarball_unversioned }}

- name: Create GitHub release
  uses: softprops/action-gh-release@v2
  with:
    files: |
      dist/${{ steps.module.outputs.tarball }}
      dist/${{ steps.module.outputs.tarball_unversioned }}
```

Where `tarball_unversioned` = `${ID}-module.tar.gz`.

The `download_url` in `release.json` must point to the **unversioned** filename:
```
https://github.com/<org>/<repo>/releases/download/v1.0.0/<id>-module.tar.gz
```

**Symptom:** "add custom module" works (user provides the full URL manually) but Schwung installer fails silently.

### 7. Move restart required after install
The Schwung shim scans modules once at boot. A full Move restart is required after installing a new module.

---

## Guardrails
- Do not package build junk.
- Do not reference files that are not included.
- Do not claim portability unless the release was structured for external installation.
- Always include a post-install test checklist.
- Match the install path to the module's `component_type`.
- Verify the tarball extracts into exactly one folder with no intermediate wrapper directories.