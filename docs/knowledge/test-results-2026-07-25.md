# Session Results — 2026-07-25 — Windows pass

Works the **Windows verification checklist** in
[FORK-GUIDE.md](../../FORK-GUIDE.md#windows-verification-checklist) (the
remaining queue item that gated the upstream PRs). Continues
[test-results-2026-07-24.md](test-results-2026-07-24.md). Branch: `modernize-te`.

**Verdict: Windows reaches functional parity with the verified macOS behaviour.**
Every checklist item passes; the one known gap (engine preference) is now
implemented rather than deferred. Three Windows-only defects were found and
fixed, and two environment traps were identified that are not code bugs but
will bite anyone repeating this on Windows.

## Environment

| | |
|---|---|
| Host | Windows 11 Home 10.0.26200, Resolume Arena 7.27.1 (rev 15990) |
| Toolchain | VS 2022 BuildTools, MSVC 14.42.34433, Windows SDK 10.0.22621, CMake 3.26.0-rc2 |
| Build | `cmake -B build-win -G "Visual Studio 17 2022" -A x64` + `--build --config Release` |
| Engine | TouchDesigner **2025.33070** at `C:\Program Files\Derivative\TouchDesigner` |
| TE client lib | `lib/TouchEngine/TouchEngine.dll` 2023.11780 (the redistributable — see below) |
| Installed to | `Documents\Resolume Arena\Extra Effects\FFGLTouchEngine\` (both DLLs + `TouchEngine.dll`) |
| Backups | old v1.1.3-h moved to `Documents\Resolume Arena\_plugin-backups\` (duplicate `TE01`/`TEFX` IDs) |

## Windows-only defects found and fixed

### 1. `windows.h` min/max macros broke the range-remap code (compile break)
`std::min` / `std::max` in the float-range work failed to compile at 6 sites
(`TouchEnginePluginBase.cpp` 383/384, 1373/1374, 1610/1611, 1632/1633) with
`C2589: '(' illegal token on right side of '::'` — the `min`/`max` macros from
`windows.h`. `WIN32_LEAN_AND_MEAN` was also being `#define`d *after* the
`windows.h` include, so it had never done anything.

Fixed by moving `WIN32_LEAN_AND_MEAN` above the include and adding `NOMINMAX`
alongside it, plus `add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)` in
CMake for translation units that reach `windows.h` through Spout first.
Also added the missing `#include <algorithm>` (it came in transitively on
libc++, not on MSVC).

**Belongs on `feat/slot-naming-ranges`** — that branch introduced the
`std::min`/`std::max` calls. FORK-GUIDE predicted any compile break would be
the framework bump; it was not.

### 2. Newest-engine preference was macOS-only (the known gap — now closed)
`FindNewestTouchDesignerApp()` was inside `#ifdef __APPLE__`, so Windows fell
back to TE's own resolution. Implemented the Windows equivalent and made the
call site cross-platform.

Windows cannot reuse the macOS approach of parsing the build out of the
directory name: the **newest install is normally the unsuffixed
`TouchDesigner` directory**, which carries no build number, while older ones
are `TouchDesigner.2025.32820`. The Windows implementation therefore reads
`bin\TouchDesigner.exe`'s version resource (`ProductVersion` =
`major.minor.year.build`) and orders installs by that, gathering candidates
from both `HKLM\SOFTWARE\Derivative\TouchDesigner` (`Path`/`Path_<n>`, which
covers non-default install locations) and `%ProgramFiles%\Derivative`.
`TEInstanceSetPreferredEnginePath` wants the installation **directory** on
Windows, not an app bundle.

Verified live — every load logs:
```
FFGL: FFGLTouchEngine: preferred engine: C:\Program Files\Derivative\TouchDesigner
```
which is the 33070 install. **Belongs on `feat/dynamic-params`.**

### 3. Engine-path failures were silent
Added logging for the `TEInstanceSetPreferredEnginePath` failure branch and for
"no TouchDesigner install found", so a mis-resolved engine is diagnosable from
the Arena log instead of presenting as a dead echo channel.

## Environment traps (not code bugs — but they cost the most time here)

### A. `TouchDesigner\bin\TouchEngine.dll` is NOT the redistributable
Shipping the `TouchEngine.dll` copied out of the 33070 install's `bin\` makes
**every** `TEInstanceConfigure` fail with `TEResultBadUsage`
("API client usage error"). It fails the same way when loaded in place from its
own `bin\` directory, so this is not a missing-sibling-DLL problem — that DLL
is an internal component of the TD install, not the client library.

Isolated outside Resolume with a standalone harness
(`TEInstanceCreate` → `SetPreferredEnginePath` → `Configure` → `Load`), which
also proved the failure was **not** caused by the new engine-preference call:

| TouchEngine.dll | Configure | Load |
|---|---|---|
| 33070 `bin\` copy, next to the exe | `BadUsage` | — |
| 33070 `bin\`, loaded in place via PATH | `BadUsage` | — |
| repo `lib/TouchEngine/` 2023.11780 | **Success** | **Success** |

**Ship the redistributable `lib/TouchEngine/TouchEngine.dll`.** The client
library version and the engine version are independent: the 2023.11780
redistributable resolves and hosts the **2025.33070** engine
(`TEInstanceGetConfiguredEnginePath` → `C:\Program Files\Derivative\TouchDesigner`),
and it exports everything this fork needs, including
`TEInstanceSetPreferredEnginePath` and `TEInstanceGetConfiguredEnginePath`
(168 TE exports).

### B. The macOS `TouchEngine` symlink pins break tox loads on Windows
`touchengine_tests/engine-*/TouchEngine` are git-checked-out macOS symlinks —
on Windows they materialise as 37-byte text files containing
`/Applications/TouchDesigner.33070.app`. TE honours a file-system link of that
name next to the tox as a deliberate engine pin, cannot use this one, and fails
the load with:
```
TEInstanceConfigure failed — A path to TouchEngine was specified but it could not be used
```
The tox loads fine from any directory without that file. Added
`touchengine_tests/win-unpinned/` (tox copies, no pin) for Windows work; a
Windows pin would need a real junction/`.lnk`, not the checked-out placeholder.

## Checklist results

| # | Item | Result |
|---|---|---|
| 1 | It compiles | **PASS** after defect 1. Remaining warnings (`C4018`, two `C4715` in `GlToDXFromat`/`GetGlType`) are pre-existing upstream defects, already cataloged in codebase-notes |
| 2 | Colour correct (no R/B swap) | **PASS (by construction)** — the `.bgra` removal is entirely inside `#ifdef __APPLE__` (`TouchEngine.cpp` 44–76); the Windows `fragmentShaderCode` is byte-identical to upstream, confirmed against `origin/master`. No colour anomaly observed live. *Not* independently confirmed with a passthrough tox — none of the available test toxes pass their input through |
| 3 | Reload gauntlet | **PASS** — 3× Reload on a *playing, cooking* FX clip (`probe2.tox`). Arena PID 22808 unchanged throughout (never restarted), full re-enumeration each time (`engine:` + echo CHOP/DAT re-registered at 16:24:10 / :31 / :52), **0** `Failed to set double value` and **0** `skipping parameter` in the whole log. This is the crash that motivated `fix/stability` |
| 4 | Tox path change mid-load | **PASS** — `load already in progress — queueing reload` fired, and the queued load landed on the **new** tox (final state = `probe2.tox` with probe2's parameter set) |
| 5 | Enumeration + unique slot names | **PASS** — `probe2.tox` exposes `Float1`–`Float10`, `Color1R/G/B/A`, `Pulse1`–`Pulse4`, and **both** `Menu1` and `Menu2` as independent slots (the Menu2 collision fix holds on Windows). `NoiseOutOnly5Param` exposes all five families (`Float1`/`Int1`/`Toggle1`/`Text1`/`Pulse1`) with real values |
| 6 | Float ranges | **PASS** — the wire carries 0–1 (`Float2` = 0.303922) while TD holds the real out-of-range values: the tox readout shows `Floatunranged 145.0`, `Resolutionwhw 1920.0`, `Resolutionwhh 1080.0`. A host set to 1.0 sticks past the settling window. The `GetParameterDisplay` half (real value in the Arena readout) is code-identical to macOS but was not separately read out of the UI |
| 7 | Echo channel | **PASS** — `par echo CHOP registered: op/out2` and `par echo DAT registered: op/out3`, including the late-registration path (`(late):`). TD-side preset recall reflects back into the host: setting a slot triggered the tox's Parameter-Execute and the readout + host slots moved together |
| 8 | Engine preference | **IMPLEMENTED** (was "not implemented"). See defect 2 |

## Not covered by this pass

- `Unload` / `Clear Instance` paths, multi-instance (several TE clips at once),
  and long-run stability were not exercised.
- Issue #12 (32-bit tox) and #17 (hardcoded 60 fps) are untouched carried issues;
  `NoiseOutOnly32Bit.tox` was not tested on Windows.
- Spout sender path (`EnableSpoutLogFile`, `SpoutSender`) was not exercised —
  the source plugin initialises it on Windows but no Spout receiver was attached.
- The three feature branches were **not** rebuilt individually on Windows; only
  `modernize-te`. The two fixes above are attributed to their branches in prose
  but have not been cherry-picked onto them.

---

# Second session, same day — defect pass + verification (v3.1.0)

After the parity pass above, an audit of the render loop turned up seven
long-standing upstream defects. All were fixed and then verified live on the
same rig (Arena 7.27.1, TouchDesigner 2025.33070 engine, x64 Release).

## What the audit found that symptom-chasing had not

Three of these ran **once per frame**, which is why they had never been
reported as bugs — they degrade slowly rather than failing outright:

- The **generator rebuilt its entire Spout interop every frame**. The resize
  test compared `GetGlType(RawTextureDesc.Format)` (a GL *type* enum) against
  `GLFormat`, which `FFGLTouchEngine` never assigns. It stayed `0`,
  `GetGlType()` never returns `0`, so `CleanupInterop()` + `CreateInterop()` +
  `CreateDX11Texture()` + `InitializeGlTexture()` ran on every frame.
  `FFGLTouchEngineFX` had the correct `DXFormat` comparison all along — the two
  plugins had simply drifted.
- **D3D immediate-context refcount underflow** in both plugins: an explicit
  `devContext->Release()` on a `ComPtr` that then released again in its
  destructor.
- `keyedMutex` was a **raw pointer declared uninitialised** — the null check
  read garbage whenever `QueryInterface` failed — and leaked on every early
  return.

Plus: six error paths returned `FF_FALSE`, which *is* `FF_SUCCESS` (both `0`);
two functions fell off their switches with no return (real UB, and the
compiler had been saying so via `C4715`); `Unload` left both ready flags set on
a still-live instance; and Spout sender names came from an unseeded/re-seeded
global `rand()` while the texture-access mutexes were the shared literals
`"mutex"` / `"mutex1"` / `"mutex2"`.

**Audit correction:** `codebase-notes.md` defect 12 claims the charset indexing
in `GenerateRandomString` "drops 'z'". That is wrong — the set is 62 characters
plus NUL, so `sizeof - 1` is 62 and picking in `[0,61]` covers all of them.
Only the *seeding* was broken.

## Verification results

| Check | Result |
|---|---|
| **Multi-instance** (never tested before) | **PASS** — three TouchEngine sources playing at once, each with a different tox, each rendering its own content. Setting `Text1` on the layer-3 instance to `LAYER-3-ONLY` changed **only** that layer; layer 5 kept rendering `Test`. No texture or parameter bleed |
| Reload gauntlet, 3× on a playing instance, with two others live | **PASS** — host PID unchanged, full re-enumeration each time |
| `Unload` | **PASS** — output stops cleanly (black), other instances unaffected, and the `TELinkEventRemoved` safety net logs "pausing output until parameters are re-enumerated" as designed |
| Engine-pin diagnostic, in-host | **PASS** — fires verbatim on the real G: drive folder, naming the file, quoting `/Applications/TouchDesigner.33070.app`, and warning that every tox in the folder is affected |
| Log hygiene | **PASS** — `Releasing texture` went from 702 occurrences to **0**; `Failed to set double value`, `skipping parameter`, and all interop errors at **0** |
| Build | Clean; both `C4715` warnings gone (fixed, not suppressed) |

## Still not covered

- **Long soak.** Everything above is minutes, not hours. The refcount and
  interop changes are exactly the kind that show up over a full set.
- Multi-instance was tested with three *sources*; a mixed source+FX+FX load was
  not exercised.
- Issue #12 (32-bit tox) and #17 (hardcoded 60 fps) remain untouched. #17 is
  more pressing on Windows, where 144Hz displays are common.
- macOS has not been rebuilt since this pass. The changes are shared-code and
  Windows-guarded, but that is inference, not verification.
