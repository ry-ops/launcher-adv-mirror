# ADR 0005 — Dependency management: CardputerMirror as a submodule, Launcher as a tracked fork

**Status:** Proposed
**Deciders:** firmware owner
**Related:** ADR 0001 (adapter architecture, states the intent this ADR makes
operational); ADR 0003, 0004 (both name specific things this ADR's merge
process must re-check).

## Context

This project depends on two upstreams that evolve independently, for
different reasons:

- **`ry-ops/cardputer-adv-mirror`** — the library, meant to stay unmodified
  here (ADR 0001). Its own ADR sequence (cardputer-adv-mirror ADR 0038
  onward) is where its API grows.
- **`bmorcelli/Launcher`** — the host firmware. This project adds a small,
  named set of changes on top of it (`adapters/launcher/`, plus the
  extraction in ADR 0003) and otherwise wants to keep receiving upstream's
  own fixes and features.

Those are two different relationships — "consume a stable, versioned
dependency" versus "maintain a small patch set against a moving firmware" —
and they need two different mechanisms, not one.

## Decision

**`cardputer-adv-mirror` → git submodule, pinned to a commit, bumped
explicitly.** Added at `lib_modules/CardputerMirror` (matching Launcher's
existing `lib_modules/ArduinoJson`, `lib_modules/Arduino_GFX` convention).
Pinned rather than tracking a moving branch: `git submodule update --remote`
plus a commit here is the only way the mirror version changes, so a mirror
change never silently alters this project's behavior mid-review. Bumping the
submodule is a normal, reviewable commit — diffed as "which commit does this
pointer move to," not as file content.

**`bmorcelli/Launcher` → real fork, `upstream` remote, periodic merge.**

```
git remote add upstream https://github.com/bmorcelli/Launcher.git
git fetch upstream
git merge upstream/main
```

merged (not rebased) into this fork's `main` on a cadence tied to upstream
activity, not a fixed schedule — check in after upstream tags a release, or
before making non-trivial adapter changes here, whichever comes first. Merge
over rebase because this is a single-maintainer project where the two
"real" edits against Launcher (ADR 0003's extraction, and whatever else
accumulates) are small enough that occasional merge commits are cheaper than
maintaining a rebasing patch stack.

**Every merge from `upstream` gets two manual re-checks**, because they're
exactly the kind of thing a large upstream diff can silently invalidate and
neither fails loudly on its own:

1. **ADR 0004's premise** — re-grep `src/main.cpp` (and anywhere else
   `xTaskCreate` shows up) for a new task that reaches a `tft->...` call.
   If one exists now, `busLock()` needs a real mutex, not `nullptr`.
2. **ADR 0003's extraction** — confirm `applyMatrixKeyEvent()` (or wherever
   the merge resolves it) still reflects `InputHandler()`'s current special-
   key/nav-key logic. A merge conflict here is the expected, useful case —
   it's Launcher's own key semantics changing colliding with the extraction
   marker, which is exactly the moment to re-verify by hand.

## Consequences

**Positive**

- The mirror can never drift into this project by surprise — every version
  change is an explicit, reviewable commit.
- Launcher's own bug fixes and features keep arriving via normal `git merge`,
  instead of this project slowly forking away from a firmware it depends on
  for everything except the mirror integration itself.
- The two re-checks turn ADR 0003 and 0004's "silent trap" negative
  consequences into a named step in an actual process, instead of relying on
  memory.

**Negative**

- Two upstreams means two update cadences to actually stay on top of; if
  either goes unmerged for a long stretch, the eventual merge gets bigger and
  the re-checks get harder to do carefully.
- Merge commits (vs. rebase) mean this fork's history has upstream's commits
  interleaved with local ones — fine for a single maintainer, noisier if
  this project ever gets more contributors and wants a cleaner log.

**Neutral**

- Nothing here prevents switching the mirror to a `git subtree` later (as
  ADR 0001 already flagged as a fallback) if this project ever needs
  Launcher-specific patches to the mirror's own files — that's a mechanism
  change, not an architecture change.

## Alternatives considered

- **Track the mirror's `main` branch directly (`git submodule update
  --remote` on every pull, no explicit pin).** Rejected: makes "what version
  of the mirror am I running" depend on when you last pulled, not on a
  commit in this project's own history — the opposite of reviewable.
- **Vendor Launcher's source instead of forking it (copy, don't track
  upstream).** Rejected: guarantees divergence from day one and forfeits
  every future upstream fix; the entire point of this ADR is to keep both
  dependencies current, not freeze one.
- **Rebase this fork's local commits onto `upstream/main` instead of
  merging.** Considered for a cleaner linear history. Rejected for now:
  rebasing after ADR 0003's extraction has landed means replaying a real
  code change across every upstream refactor of `interface.cpp`, which is
  more manual conflict resolution than an occasional merge commit for a
  single-maintainer project.
