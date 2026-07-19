# Contributing

## Branching model

Two long-lived branches:

| Branch | Role | Direct pushes |
|---|---|---|
| `main` | Released, tagged, archived to Zenodo. Always deployable. | ❌ never |
| `dev` | Integration branch. All features land here first. | ❌ never |

All work happens on short-lived branches cut from `dev`:

```
feat/<slug>      new capability          feat/harfbuzz-shaping
fix/<slug>       bug fix                 fix/crc-byte-order
docs/<slug>      documentation only      docs/wire-format
chore/<slug>     tooling, CI, deps       chore/ci-pytest
```

### Flow

```
dev ──┬─► feat/xyz ──► PR ──► dev ──► (when a release is ready) ──► PR ──► main ──► tag ──► Zenodo DOI
      └─► fix/abc  ──► PR ──► dev
```

```bash
git switch dev && git pull
git switch -c feat/harfbuzz-shaping
# ... work, commit ...
git push -u origin feat/harfbuzz-shaping
gh pr create --base dev --fill
```

Every change reaches `dev` through a pull request — no exceptions, including single-line
fixes. Squash-merge feature PRs into `dev`; merge `dev` into `main` with a merge commit so
release history stays legible.

## Issues, epics, and planning

Work is tracked in three tiers:

- **Epic** — a major subsystem, labelled `epic`. Body holds a task list of its child issues,
  which GitHub renders as a progress bar. Epics are not implemented directly.
- **Issue** — one deliverable unit of work, linked from its epic and labelled by area
  (`area:host`, `area:firmware`, `area:protocol`, `area:docs`).
- **Pull request** — implements one issue. Reference it with `Closes #N` so the issue
  closes on merge.

Labels in use:

| Label | Meaning |
|---|---|
| `epic` | Umbrella issue tracking a subsystem |
| `area:host` | Desktop/mobile app, shaping, rendering |
| `area:firmware` | ESP32, panel drivers |
| `area:protocol` | Wire format, framing, CRC |
| `area:docs` | Documentation, specification |
| `good first issue` | Self-contained, well-specified |

## Releases and the DOI

`main` is the archival branch. Publishing a GitHub Release from `main` triggers Zenodo to
archive the repository and mint a version DOI under the project's concept DOI.

Before tagging a release, bump `version` and `date-released` in `CITATION.cff`.

**Ordering matters:** the repository must be enabled at
[zenodo.org/account/settings/github](https://zenodo.org/account/settings/github/) *before* the
release is published. A release published first is never archived.

## Licence

Contributions are accepted under the [AGPL-3.0-or-later](LICENSE) licence of this project.
