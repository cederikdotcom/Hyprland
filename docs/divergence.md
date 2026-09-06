# Hyprland CPU fork divergence

This is the compositor fork used by [Omarchy32 CPU](https://github.com/cederikdotcom/omarchy32cpu). Work lives on [`pixman-renderer`](https://github.com/cederikdotcom/Hyprland/tree/pixman-renderer), not the repository's default `main`. It supplies the CPU renderer; [aquamarine's `cpu-backend`](https://github.com/cederikdotcom/aquamarine/blob/cpu-backend/docs/divergence.md) supplies allocation and output support. Omarchy's [own registry](https://github.com/cederikdotcom/omarchy32cpu/blob/main/docs/divergence.md) counts neither repository's implementation patches.

## Two different measurements

Measured on 2026-09-06 before adding this accounting documentation:

| Measure | Result | Meaning |
|---|---|---|
| Custom patch baseline | upstream `v0.56.2-b`, `efb50993780079460b0cbed1363e2166a2de1d9f` | Exact parent of the first CPU patch; not a guessed merge base |
| Runtime/source snapshot | fork `63417a3015b1ee296b5f61a1281657b7a65c13e2` | Five custom commits beyond that baseline |
| Custom patch delta | **19 files, +1,383 / −40 lines (1,423 churn)** | CPU changes plus two build-toolchain adaptations |
| Current upstream main | `214bb0e5b8efd65869f597a16e34bfa57183ffee` | Development branch, distinct from the release branch |
| Direct current-main tree difference | **664 files, +17,310 / −32,751 lines** | Custom work mixed with upstream development changes |
| Commit-graph distance to main | **175 upstream-only / 41 fork-side commits** | Of those 41, 36 are already in the upstream release baseline and five are custom |

The fetched `v0.56.2-b` tip matched our pinned baseline: zero release-branch commits were missing at measurement time. That does **not** mean current upstream main is integrated. A three-dot comparison with main would also misleadingly count release-branch work as custom changes. Use the explicit baseline for custom ownership and the separate two-tree/main graph report for integration distance.

The new README, documentation, registry, reporting scripts and workflow add their own files/lines under `fork-accounting`. The historical 19-file number intentionally excludes them. Do not add this snapshot to Omarchy's current-main total and call the result one comparable percentage.

## Custom patch ownership

| Entry | Source scope | Files / churn at runtime snapshot | Tracking |
|---|---|---:|---|
| CPU renderer/integration | `src/render/PixmanRenderer.*`, `src/render/pixman/`, renderer interface, compositor startup, config and system info | 16 / 1,351 | [#1](https://github.com/cederikdotcom/Hyprland/issues/1) |
| SHM capture constraints | `src/protocols/ImageCopyCapture.cpp` | 1 / 49 | [#2](https://github.com/cederikdotcom/Hyprland/issues/2) |
| Python 3.11 compatibility | `meta/generateLuaStubs.py` | 1 / 10 | [#3](https://github.com/cederikdotcom/Hyprland/issues/3) |
| Older libstdc++ compatibility | `src/helpers/MiscFunctions.cpp` | 1 / 13 | [#4](https://github.com/cederikdotcom/Hyprland/issues/4) |
| Documentation/accounting | README, this document, `.github/divergence/`, reporting workflow | Measured separately | [#5](https://github.com/cederikdotcom/Hyprland/issues/5) |

The CPU renderer adds pixman textures, framebuffer/renderbuffer wrappers, element rendering and `RT_PIXMAN`. `HYPRLAND_RENDERER=pixman` selects it; when unset, `render:renderer` is consulted, with `auto` using GL. Startup does not construct the EGL-dependent GL renderer in CPU mode, and cleanup guards its absence. The SHM capture change avoids dereferencing an absent linux-dmabuf protocol, rather than claiming general dmabuf support.

The Python fix replaces PEP 695 type statements with assignment aliases. The C++ fix replaces unavailable `std::ranges::starts_with` with a lowercase string and `string_view::starts_with`; review its allocation/performance tradeoff as well as truthy semantics before upstreaming.

## Limitations and convergence order

Flat mode is deliberate: blur, shadows, glow, rounding, screen shaders and color-management LUTs are not supplied by this renderer. The implementation warns about non-normal output transforms; transformed output is not a supported acceptance baseline. It rejects dmabuf textures and uses CPU-mappable buffers. Full damage tracking is the established baseline. These are limitations of this implementation, not statements that software rendering can never support these features.

1. Preserve the validated release baseline while reviewing current-main API, renderer, configuration and dependency changes. No main merge or binary rebuild was performed by this documentation task.
2. Review SHM capture guards and the two toolchain adaptations as small independent upstream candidates. "Upstreamable" means a candidate, not submitted or accepted.
3. Port/test the renderer separately, keeping the GL path working. Exercise failed begin/end, damage, resize, texture format/stride, buffer lifetime, screenshots, cursor composition and normal-orientation output on both x86_64 and i686.
4. Package and pin the matching Hyprland/aquamarine/dependency set; a branch tip alone does not identify the binary installed on the Mac.

Historical headless, nested SHM, VM DRM and physical Mac evidence is in Omarchy's [renderer progress](https://github.com/cederikdotcom/omarchy32cpu/blob/main/docs/pixman-renderer/PROGRESS.md) and [2026-09-05 hardware report](https://github.com/cederikdotcom/omarchy32cpu/blob/main/docs/history/macbook-convergence-20260905.md). The latter records a long-lived session, successful config reload and desktop/menu/lock-screen inspection, but leaves physical click confirmation, manual authentication, cold boots and suspend/resume open. Accounting tests do not rerun those hardware tests.

## Reproduce and maintain the accounting

The source of truth is [the registry](../.github/divergence/registry.json). Every custom path belongs to exactly one entry. Unknown or overlapping ownership fails the report; unmatched pathspecs are surfaced for review. Each entry has a local GitHub issue with handling, rationale and exit conditions.

```bash
git fetch upstream main
python3 .github/divergence/test_report.py
python3 .github/divergence/snapshot.py
# Before committing, include tracked edits and untracked files:
python3 .github/divergence/snapshot.py --worktree
```

Run these on the documented working branch. The pinned baseline is intentionally not advanced automatically: change it only after integrating and validating upstream, then review every ownership change. The current-main distance is unclassified raw tree distance; it is never silently folded into the classified custom-patch table. Commit counts describe ancestry, not patch equivalence. Binary files count as files but contribute no textual line count.

The [divergence workflow](../.github/workflows/divergence.yml) still runs on work-branch pushes and manual dispatch. The [daily scheduler](https://github.com/cederikdotcom/Hyprland/blob/main/.github/workflows/upstream-sync.yml) runs from default `main` at 06:27 UTC (GitHub can delay it), explicitly checks out the CPU work branch, and runs both the merge queue and divergence accounting. Its work-branch copy is retained for review; edits must also reach default main to affect scheduling.

### One issue per pending merge batch

The daily monitor checks upstream `main` and `v0.56.2-b` independently. When commits are missing, it creates one open issue for that channel with exact source/target SHAs, up to 60 incoming commit subjects and a `git merge-tree` conflict check. Subsequent runs extend/update that pending batch instead of creating duplicates. Generated blocks preserve human notes outside the markers. Once the recorded target is an ancestor of the work branch, the batch closes; a subsequent batch gets a new issue. A manually closed issue for the same target stays closed.

The merge issue is published before classification, so accounting failures cannot hide incoming work. Failed issue writes fail the job. The monitor does not merge, build or install anything, and automatic closure proves source ancestry only—not runtime or hardware acceptance. Use comments for validation evidence. The branch SHA must match its configured remote work branch before the monitor can publish.

`python3 .github/divergence/test_monitor.py` tests the queue lifecycle. `python3 .github/divergence/monitor.py` inspects without issue writes; add `--publish` only to update the queue. Configuration is in `monitor.json`. Both scheduler and push workflow refresh the semantic divergence issue blocks separately from merge tasks.

The accounting engine is copied from [Omarchy32 CPU at 4b3278af](https://github.com/cederikdotcom/omarchy32cpu/blob/4b3278af/.github/divergence/report.py), with descriptive text adjusted for this workflow. Keep those copies aligned when fixing accounting behavior. The small local regression suite covers unique ownership, missing/duplicate ownership failures, binary weights and preservation of human issue text.
