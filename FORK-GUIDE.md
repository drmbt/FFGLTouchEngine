# FORK-GUIDE — drmbt/FFGLTouchEngine

This fork exists to make [medcelerate/FFGLTouchEngine](https://github.com/medcelerate/FFGLTouchEngine)
usable as a live performance instrument on macOS: it fixes the crashes and
rendering faults that made the v2.0.4 plugins unreliable under Resolume Arena,
gives every pre-allocated FFGL slot a unique address so OSC/MIDI/REST control
actually reaches it, and adds the missing TouchDesigner→host direction so
parameter state changed inside a tox (preset recall, randomize, interpolation)
shows up in the Resolume UI. The work is split across three stacked branches so
the upstream maintainer can take the uncontroversial fixes without also taking
the breaking rename or the new feature surface. [CHANGELOG.md](CHANGELOG.md) is
the authoritative per-change record; this file is the decoder for *which branch
holds what, and who should use it*.

## Branch map

| Branch | Base | Audience | Contents |
|---|---|---|---|
| `master` | — | upstream tracking | Mirrors upstream v2.0.4. Never commit fork work here. |
| `fix/stability` | `master` | **upstream PR #1** — anyone on v2.0.4 | Crash, race, rendering and enumeration fixes. No slot names, ranges or wire semantics change: saved compositions and OSC maps keep working. |
| `feat/slot-naming-ranges` | `fix/stability` | **upstream PR #2** — needs a migration note in release notes | Unique static slot names, per-family ParamID counters, float range remapping. **Breaking for saved compositions and OSC/MIDI maps.** |
| `feat/dynamic-params` | `feat/slot-naming-ranges` | **upstream PR #3** — the #28 feature | Dirty-only parameter push and the par-state echo channel (TD→host reflection). Purely additive on top of PR #2. |
| `modernize-te` | = `feat/dynamic-params` + docs | Vincent's working branch | Everything above, plus `CHANGELOG.md`, `CLAUDE.md` and `docs/knowledge/`. `git diff feat/dynamic-params modernize-te -- src/` is empty by construction. |

The three feature branches carry **source and framework only** — no CHANGELOG,
no working notes. That keeps each PR diff about the code; the PR description
carries the prose.

Each branch builds and links on its own (`cmake --build build-modern --config
Release`, macOS/arm64), so the maintainer can merge PR #1 and stop there.

## Change map

Categories: **fix** = bug fix, no behaviour anyone depends on changes ·
**breaking** = existing compositions/OSC maps must be updated ·
**additive** = new capability, opt-in by tox authoring.

| Change | Category | Branch | CHANGELOG | Upstream issue |
|---|---|---|---|---|
| TouchEngine.framework → TouchEngine-macOS `c3ceb1a` (2025-06-13) | fix (prereq) | `fix/stability` | [Changed](CHANGELOG.md#changed) | — (unblocks #32-era headers) |
| Reload segfaulted the host — `TEEventInstanceReady` treated as render-readiness | fix | `fix/stability` | [Fixed](CHANGELOG.md#fixed) | #34-class instability |
| Load debounce (`isLoadPending`), superseding loads queued not dropped | fix | `fix/stability` | [Fixed](CHANGELOG.md#fixed) / [Added](CHANGELOG.md#added) | #34 |
| `TELinkEventRemoved` safety net (hold last frame instead of crashing) | fix | `fix/stability` | [Fixed](CHANGELOG.md#fixed) | #34 |
| Failed loads no longer resume/enumerate a dead instance (kills the `Failed to set double value` spam) | fix | `fix/stability` | [Fixed](CHANGELOG.md#fixed) | #34 |
| `TEStateMutex` — TE lifecycle / parameter maps / render frame serialized; FrameDidFinish atomic fast path | fix | `fix/stability` | [Fixed](CHANGELOG.md#fixed) | #34, prerequisite for #28 |
| macOS FX transparent-frame flicker (busy path redraws cached frame) + input base-pass bindings | fix | `fix/stability` | [Fixed](CHANGELOG.md#fixed) | — |
| macOS red/blue channel swap (removed the `.bgra` swizzle) | fix | `fix/stability` | [Fixed](CHANGELOG.md#fixed) | — |
| Enumeration robustness — log+skip, read-before-register, `default:` case, load-error surfacing, empty-path guard | fix | `fix/stability` | [Fixed](CHANGELOG.md#fixed) | #27, #32 |
| Post-enumeration value-event re-raise sweep (stale display strings) | fix | `fix/stability` | [Fixed](CHANGELOG.md#fixed) | #6-adjacent |
| Unique static slot names (`Float1-40`, `Int`, `Toggle`, `Text`, `Pulse1-40`, `Menu1-40`, `Color1R..A`) | **breaking** | `feat/slot-naming-ranges` | [Changed](CHANGELOG.md#changed) | #33 (shared OSC addresses) |
| Per-family ParamID counters — **fixes the Menu2 collision** | fix, but **shipped in the breaking branch** | `feat/slot-naming-ranges` | [Fixed](CHANGELOG.md#fixed) | #27 |
| Menus store their initial value; `GetFloatParameter` handles `FF_TYPE_OPTION` | fix | `feat/slot-naming-ranges` | [Fixed](CHANGELOG.md#fixed) | #27 |
| Float range remap (normalized 0–1 wire, `ParameterRanges`, `EffectiveRange`, `GetParameterDisplay` override) | **breaking** | `feat/slot-naming-ranges` | [Fixed](CHANGELOG.md#fixed) | #27, #8 |
| Dirty-only parameter push (+ pulse falling-edge re-dirty) | additive | `feat/dynamic-params` | [Added](CHANGELOG.md#added) | **#28** |
| `TELinkEventValueChange` handler (echo absorb) | additive | `feat/dynamic-params` | [Added](CHANGELOG.md#added) | **#28** |
| Par-state echo channel (`EchoNameToParamID`, `MenuTokens`, `HandleEchoChop/Dat`, `ApplyEchoValue`) | additive | `feat/dynamic-params` | [Added](CHANGELOG.md#added) | **#28** |
| Echo settling window (`LastPushFrame` / `EchoSettleFrames`) | additive | `feat/dynamic-params` | [Added](CHANGELOG.md#added) | **#28** |
| Newest-engine preference (macOS `/Applications` scan + `TEInstanceSetPreferredEnginePath`) + configured-engine logging | additive | `feat/dynamic-params` | [Added](CHANGELOG.md#added) | **#28** (enables the echo channel) |

### Hunks that resisted clean separation

**The Menu2 collision fix lives in the breaking branch, not in `fix/stability`.**
It is a genuine bug fix — a second menu parameter collapsed onto the first, so
one FFGL slot drove both TE menus and the second menu never appeared — but the
fix *is* the per-family ParamID counters, and those reallocate slots for every
family. Shipping them in `fix/stability` would silently move parameters between
slots in saved compositions, which is exactly the breakage that branch promises
not to cause. `fix/stability` therefore keeps the upstream
`ParameterMapInt.size()` arithmetic verbatim, with a comment at the site
pointing at `feat/slot-naming-ranges`. **PR #1's description should say the
menu collision is knowingly left unfixed there.**

Two smaller pieces moved up for the same reason:

- The output-scope enumeration walk still `break`s after the first texture link
  on `fix/stability` (upstream behaviour). It has to keep scanning to find the
  echo CHOP/DAT, so that restructure ships with `feat/dynamic-params`.
- `SetParamRange(...)` calls for float slots are removed only in
  `feat/slot-naming-ranges`, since removing them is what moves the wire to a
  normalized 0–1 position.

## Migration notes — `feat/slot-naming-ranges` (breaking)

Anyone upgrading past this branch must expect two changes:

**1. Slot renames.** Pre-allocated slots were named `Parameter<N>` (N derived
from the absolute index, so families overlapped), every event slot was literally
`Pulse`, and every colour component was `Color`. They are now:

| Family | Old | New |
|---|---|---|
| Float | `Parameter4`…`Parameter43` | `Float1`…`Float40` |
| Int | `Parameter44`… | `Int1`…`Int40` |
| Toggle | `Parameter84`… | `Toggle1`…`Toggle40` |
| Text | `Parameter124`… | `Text1`…`Text40` |
| Event | `Pulse` (×40, identical) | `Pulse1`…`Pulse40` |
| Menu | `Parameter204`… | `Menu1`…`Menu40` |
| Colour | `Color` (×40, identical) | `Color1R/G/B/A`…`Color10R/G/B/A` |

- **Saved compositions will not restore parameter values** across this change —
  the host matches by name. Re-set parameters and re-save.
- **OSC/REST addresses move.** This is the point: identically-named slots
  collapse to a single API entry, so before this change only the *first* event
  slot and the *first* colour were reachable at all. Rebuild OSC/MIDI maps
  against the new addresses.
- Event-button **captions** now read `Pulse2` rather than the TD label. FFGL has
  no runtime rename event and the host reads the caption from the static name;
  the parameter *row* still shows the TD label via the display-name mechanism.

**2. Normalized float wire.** Float slots now carry a 0–1 *position* on the FFGL
wire; the plugin remaps it against the TD-side range in both directions and
shows the real TD value in the host readout.

- **OSC/MIDI/REST senders must send 0–1.** The full TD range is reachable, but
  absolute TD values need sender-side scaling.
- Colour slots keep the native 0–1 wire (no remap). Ints are unscaled within the
  ±10000 prototype range.
- The effective range is captured at tox load and widened to include the initial
  value, so an unranged TD float sitting at 145 survives the round trip — but it
  **cannot be driven above its load-time value** from the host, and the range is
  frozen until the next load.

## Echo-channel authoring convention (`feat/dynamic-params`)

TouchEngine treats **input** link values as host-authoritative: comp-internal
writes to root custom pars emit no `ValueChange` and are invisible to
`TEInstanceLinkGet*Value` (verified empirically with instrumented builds). TE
*does* fire per-cook `ValueChange` for **output** links, so a tox reflects its
own parameter state by publishing it on an output.

**Preferred: Par DAT → Out DAT** (`TELinkTypeStringData`).

- Par DAT pointed at the component whose pars you want reflected → Out DAT.
- Layout is the stock Par DAT layout: a `name` column and a `value` column,
  header row optional (detected when cell (0,0) is literally `name`). Extra
  columns are ignored.
- The DAT carries menu **tokens** (`red`), which the plugin maps back to the
  option index through the menu's choice values — so menus round-trip correctly.
  Strings round-trip only through the DAT.

**Optional: Par CHOP → Out CHOP** (`TELinkTypeFloatBuffer`).

- Numeric pars only. Menus arrive as a raw **index**, not a token.
- Useful when you want per-frame numeric reflection without string parsing.

Both may be present; they are read independently.

**Naming rules — this is what the mapping keys on.**

- Channel names / DAT `name` cells must be the **TD par names**, exactly as a Par
  CHOP/DAT emits them.
- Scalar pars: the par name (`Float`, `Speed`).
- Vector pars: par name + lowercase component suffix — an RGBA par named `Rgba`
  publishes `Rgbar`, `Rgbag`, `Rgbab`, `Rgbaa`; XYZW uses `x/y/z/w`, size uses
  `w/h`. That is TD's own convention, so a Par CHOP/DAT already produces it.
- Names that don't match a registered parameter are ignored silently.

**Registration.** The plugin adopts the **first** `TELinkTypeFloatBuffer` output
link as the echo CHOP and the **first** `TELinkTypeStringData` output link as the
echo DAT — at enumeration, or via `TELinkEventAdded` if the engine registers
them late. If your tox has other CHOP/DAT outputs, order matters: put the echo
outputs first, or don't expose the others.

**Settling-window semantics.** For 30 frames after the host pushes a slot, echoes
for *that slot* are ignored. Out-link values cooked *before* a push arrive
*after* it and would otherwise revert the user's change — this was observed live
as a menu selection snapping back when the menu was part of its own echo set. So:

- A host change always wins for ~half a second, then TD's reported state takes
  over again.
- A pending (not yet pushed) host set beats the echo unconditionally.
- Echo values are stored **without** marking the parameter dirty, so they are
  never pushed back to TE — no feedback loop.
- Practical consequence: don't drive the same par from the host and from a
  free-running TD animation and expect the host value to stick.

**Engine version requirement.** The echo channel needs a TouchEngine whose
engine exposes CHOP/DAT output links. Old TouchDesigner installs expose *only*
texture outputs — the symptom is `GetLinkGroups(output)` returning a single
texture child and the echo silently never firing. On macOS the plugin now scans
`/Applications` and prefers the newest `TouchDesigner.<build>.app`, which covers
toxes loaded from unpinned folders; the configured engine path is logged at
every load, so check the Arena log if echo isn't working. A `TouchEngine`
file-system link next to the tox still overrides the preference — that is the
deliberate pinning mechanism, and pinning to an old build disables the echo
channel. **No equivalent scan exists on Windows yet** (see below).

## Verification status

| Area | macOS (arm64, Arena 7.x) | Windows |
|---|---|---|
| Reload gauntlet (3× Reload on a playing FX clip) | **verified live** — host alive, full re-enumeration each time, zero error spam | **untested** |
| macOS FX flicker fix | **verified live** — no flicker under continuous cooking | n/a (macOS-only path) |
| Red/blue swap fix | **verified live** | n/a (macOS-only shader) |
| Enumeration robustness | **verified live** — probe tox exposes the expected slots, bad links skip individually | **untested** |
| Unique slot names / OSC reachability | **verified live** via Resolume REST | **untested** |
| Menu collision fix | **verified live** — both menus appear and drive independently | **untested** |
| Float range remap | **verified live** — out-of-range defaults survive, real values in the readout | **untested** |
| Dirty-only push (#28 first half) | **verified live** — Parameter-Execute preset recall sticks | **untested** |
| Par-state echo channel (CHOP + DAT) | **verified live** — preset recall reflects colours, floats and menu selections into the Resolume UI/REST | **untested** |
| Newest-engine preference | **verified live** on an unpinned tox | **not implemented** — the `/Applications` scan is macOS-only; Windows relies on TE's own engine resolution |
| Build | **clean** (`cmake --build build-modern --config Release`, arm64) on all three branches | **not built** |

**Nothing here has been run on Windows.** Both plugins still compile the D3D11 /
Spout paths, and none of the changed code is macOS-only except the flicker fix,
the shader swizzle and the engine-path scan — but "should be fine" is not
verification. A Windows pass gates opening the upstream PRs.

## Known issues (carried, not introduced)

- RGBA parameters render as four separate faders rather than Resolume's native
  colour picker.
- Hardcoded 60 fps (#17) — `TEInstanceSetFrameRate(instance, 60, 1)`.
- 32-bit tox renders corrupted on macOS (#12).

See [CHANGELOG.md](CHANGELOG.md#known-issues-tracked-not-yet-fixed) and
[docs/knowledge/issues-audit.md](docs/knowledge/issues-audit.md).
