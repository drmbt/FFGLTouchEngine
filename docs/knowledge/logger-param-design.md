# Design note — a diagnostic string parameter

> **Built and shipped in v3.2.0 as the `Log` slot.** The design below is what
> was implemented, with two deviations recorded at the bottom. Verified live on
> Windows; not yet exercised on macOS.

Request: an empty string parameter the plugin writes into (severe warnings, load
times, engine path), so problems are visible in the Resolume UI and over
REST/OSC without tailing the Arena log.

**Verdict: feasible, worth doing, and it should ship in the normal build rather
than a separate one.** The costs are real but they are all avoidable by design.

## Why it is feasible

Everything needed already exists:

- The base already owns string slots (`Text1`–`Text40`, `ParameterMapString`)
  and `GetTextParameter` already serves them to the host.
- The #28 work added the mechanism for pushing values *up* to the host:
  `RaiseParamEvent(..., FF_EVENT_FLAG_VALUE)`, which makes Resolume re-query a
  parameter it has already cached. That is exactly what a log line needs.
- `FFGLLog::LogToHost` call sites already mark the interesting moments (load
  failures, engine path, pin diagnostic, link removal).

So this is mostly "tee the existing log calls into a ring buffer and expose the
newest entry", not new plumbing.

## The costs, and how to avoid each

**1. Do not put it on the render thread.** This is the one that matters. The
`Releasing texture` line removed in v3.1.0 is the cautionary example: a single
`LogToHost` in a per-frame callback produced 702 log lines in a short session and
buried everything else. The logger must only ever be written from lifecycle
events — load, enumeration, failure — never from `ProcessOpenGL`.

**2. Do not raise a param event per line.** `RaiseParamEvent` makes the host
re-query and repaint. Bursty events during enumeration would be visible as UI
churn. Coalesce: write into a small ring buffer under the existing
`TEStateMutex`, set a dirty flag, and raise **at most one** event per frame (or
per N frames) from a cheap check.

**3. Watch string allocation.** `GetTextParameter` returns `char*`; the existing
slots already deal with this. Keep a fixed-size buffer, truncate, do not
allocate per read.

**4. It is a static FFGL slot.** Slot names are read at scan time, so the logger
needs a reserved, permanently-named slot (e.g. `Log`) that exists whether or not
a tox is loaded. That costs one slot out of the pre-allocated budget and changes
the parameter layout — so it lands with the same care as any slot change, and it
should go in the same branch as the other naming work when the PR split happens.

## Why not a separate debug build

Tempting, but wrong here:

- The bugs worth catching are the ones that happen **in a show**, on the build
  actually installed. A debug-only logger is absent exactly when it is needed.
- Two binaries with the same FFGL plugin IDs (`TE01`/`TEFX`) cannot coexist in
  the scan tree — swapping builds mid-diagnosis means restarting Arena and
  losing the state that was being diagnosed.
- Selecting it via the engine-pin file next to the tox does not work either: the
  pin selects a *TouchDesigner install*, not a plugin build.

If the cost ever proves non-trivial, gate verbosity with a **parameter**
(off / errors / verbose) in the one build, defaulting to errors-only. That keeps
one binary and one plugin ID.

## Suggested shape

- One reserved string slot, `Log`, always present.
- Ring buffer of the last N lines (N ~ 16) with a monotonic counter, under
  `TEStateMutex`.
- `GetTextParameter` returns the newest line, or a compact `[n] message` so the
  counter makes repeats visible.
- Severity filter parameter, default errors-only.
- Written **only** from: load start/failure (with `TEResultGetDescription`),
  load completion plus elapsed ms, engine path actually configured, the
  engine-pin diagnostic, enumeration summary (`n` params, `m` skipped), and
  `TELinkEventRemoved`.
- Explicitly **not** written from `ProcessOpenGL` or any per-frame callback.

Load timing is worth capturing: TE load latency is the thing most worth knowing
before a set, and it is currently invisible. Stamp at `TEInstanceConfigure` and
report the delta at `TEEventInstanceDidLoad`.

---

## What was actually built (v3.2.0), and where it deviated

Implemented as designed: slot placed after every pre-allocated family (so no
existing index moves), always visible, written only from lifecycle events and
TE's statistics callback, host re-query raised only when the composed line
changes, host writes to the slot swallowed so they cannot be mistaken for a tox
parameter, and statistics cleared on unload so stale memory/fps figures do not
linger next to a stopped instance.

**Deviation 1 — no ring buffer.** The design called for the last N lines. Built
as a single status line instead: a Resolume parameter row shows one line, so a
scrollback would not be visible anyway. The status holds the last *significant*
event (an error, or the load result) and survives until the next one, so a
failure stays on screen rather than being scrolled away by routine stats.

**Deviation 2 — no verbosity parameter yet.** Deferred rather than dropped. The
content is already limited to errors, the load result, and ~1 Hz statistics,
which is about the right volume for one row. Add the parameter only if it proves
noisy in practice.

**Correction found during testing.** The first implementation derived fps from
`frameTimeCPU / frames`. That is wrong: `frameTimeCPU` is CPU time *spent* on
those frames, not elapsed wall time, so it yields throughput capacity — a frame
cooked in 1.1 ms reported as "655.6 fps" while the engine was actually running
at 59. Real rate now comes from wall-clock between statistics deliveries, and
the CPU cost is reported separately as `cook <n> ms`, which is the more useful
number anyway: it says how much headroom there is independent of how fast TE is
being driven.

**Observed output**, from a trivial tox on Windows:

```
loaded in 25.02s  |  GPU 90 MB  CPU 2335 MB  59.0 fps  cook 1.1 ms
```

The load time is the finding worth carrying forward — see
[backlog.md](backlog.md) item 3. 39.3 s cold, 25.0 s warm, for a tox with
almost nothing in it. That is engine spawn cost, not tox complexity.
