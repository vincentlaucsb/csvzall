---
name: dependency-remediation
description: Remediate a bug, advisory, API gap, or stale pin in a dependency we control (svg, svgplot, csv-parser, csvzall itself, obsidian-csvzall). Walks the controlled dependency tree from the deepest affected repo upward, fixing, version-bumping, merging green PRs, releasing, and re-pinning each consumer in turn.
---

# Controlled dependency remediation

Use this workflow when a problem surfaced in csvzall (or a downstream consumer)
actually belongs in, or must flow through, a repository owned by
[vincentlaucsb](https://github.com/vincentlaucsb). Third-party dependencies
(argparse, SQLiteCpp, simdjson, nlohmann/json, Catch2, ...) are out of scope:
for those, bump the pin in csvzall directly and update the README dependency
table.

## Controlled dependency tree

Deepest first. A repo may only be remediated after everything below it on its
path is merged and released.

```
vincentlaucsb/svg            (no controlled deps)
  └─ vincentlaucsb/svgplot   (pins svg; release.yml also checks out vincentlaucsb/single_header)
vincentlaucsb/csv-parser     (no controlled deps)
       └──┬──────────────────┘
          vincentlaucsb/csvzall      (pins csv-parser + svgplot)
            └─ vincentlaucsb/obsidian-csvzall  (pins csvzall + every transitive dep by commit)
```

Pin locations, by consumer:

| Consumer | Dependency | Where the pin lives |
|---|---|---|
| svgplot | svg | `CMakeLists.txt` `FetchContent_Declare(... GIT_TAG ...)` (currently tracks `master`; prefer a tag or commit when remediating) |
| csvzall | csv-parser | `external/csv-parser` submodule commit **and** `CSVZALL_CSV_PARSER_GIT_TAG` in `CMakeLists.txt` — keep both on the same commit |
| csvzall | svgplot | `CSVZALL_SVGPLOT_GIT_TAG` in `CMakeLists.txt`; README "Charts" table version |
| csvzall | csv-parser / svgplot API floors | `csvzall_validate_csv_parser()` / `csvzall_validate_svgplot_api()` in `CMakeLists.txt` — extend these when csvzall starts requiring a new upstream API |
| obsidian-csvzall | csvzall + all transitive deps | `native-binaries/build.json` (`sourceCommit`, `version`, `dependencies.*.commit`), regenerated through that repo's release-preparation workflow, never hand-copied binaries |

Re-verify this table before starting: run `git submodule status`, grep the
`GIT_TAG` lines, and read `native-binaries/build.json`. If the tree has grown
(a new controlled repo, a new pin location), update this skill in the same
change.

## Procedure

### 0. Scope the problem

1. Reproduce the issue at the top-level consumer where it was observed.
2. Identify the deepest controlled repo whose code must change. Everything on
   the path from that repo up to the observing consumer is the **remediation
   path**. Siblings off the path (e.g. csv-parser when the fix is in svg) are
   left alone.
3. Write down the path in order before editing anything, e.g.
   `svg → svgplot → csvzall → obsidian-csvzall`.
4. Use one worktree/branch per repo. Never commit one repo's change from another
   repo's checkout. Follow each repo's own `AGENTS.md`/`CLAUDE.md`, including its
   pull-before-edit rule on the default branch.

### 1. For each repo on the path, deepest first

Repeat this loop; do not start the next repo until the current one is released.

**a. Fix.** Make the minimal change that remediates the issue, plus a
regression test in that repo's own suite. If the deeper repo is the only place
the change is needed, its consumers still get a pin bump (step d) so they ship
the fix.

**b. Bump the version.** Read every version marker the repo documents before
choosing a number; never trust memory, tags, or issue text alone.

| Repo | Version markers (must agree with each other and the tag) |
|---|---|
| svg | `CMakeLists.txt` `project(... VERSION ...)` plus anything its `AGENTS.md`/README lists |
| svgplot | `node scripts/version.mjs bump X.Y.Z` / `check` (CMakeLists.txt, `svgplot.hpp` banner, package.json, package-lock.json) |
| csv-parser | `CMakeLists.txt` `project(... VERSION ...)`, Python package metadata validated by `python-publish.yml`, plus anything its `AGENTS.md` lists |
| csvzall | `node scripts/version.mjs bump X.Y.Z` / `check` (CMakeLists.txt, Doxyfile, `src/main.cpp`) |
| obsidian-csvzall | everything under "Version Locations" in its `AGENTS.md` (`manifest.json`, `versions.json`, `package.json`, `package-lock.json`, `native-binaries/*`, `mobile-src/manifest.json`) |

When a repo has `scripts/version.mjs`, bump only through it and run its
`check` before opening the PR; for repos without one, consider adding one
(markers list + `check` in CI + `check --tag` in the release workflow).

If the markers already disagree with each other or with the latest tag (as
svgplot's did before 0.4.1: tag `0.4.0`, sources `0.3.0`), stop and report the mismatch before picking a new number. Fix the drift as part
of the bump only once the intended baseline is confirmed.

Patch vs. minor (semver, pre-1.0 rules for 0.x repos):

- **Patch**: bug or security fix, behavior restored to what was documented, a
  pin bump that only pulls in a dependency's patch release, build/CI-only
  fixes. No new public API, flags, or file-format changes.
- **Minor**: any new public API, CLI flag, command, option, output field, or
  capability; a consumer pin bump that pulls in a dependency's **minor**
  release whose new API the consumer now uses or exposes; a raised API floor in
  the `csvzall_validate_*` checks; intentional behavior changes. On 0.x, a
  breaking change is also a minor bump; call it out in release notes.
- When in doubt between the two, choose minor and say why in the PR.

Bump only the repo being changed. Don't touch unrelated third-party version
references while bumping.

**c. PR, green, merge.**

1. Commit (fix + test + version bump can be one commit or separate commits),
   push the branch, open a PR against the repo's default branch. The PR body
   names the remediation path and links the upstream PR/release it depends on.
2. Wait for **all** required checks on the PR head to pass
   (`gh pr checks <n> --repo <owner/repo>`). Do not merge on pending or
   failing checks, and never bypass branch protection. If CI fails, fix on the
   same branch and re-verify.
3. Merge, then fast-forward the local default branch
   (`git pull --ff-only origin <default>`) and confirm the merge commit SHA.
   Merging is an outward-facing action: confirm with the user first unless they
   pre-authorized merging for this remediation run.

**d. Release.**

1. Tag the merge commit with the new version, matching that repo's existing tag
   style (csv-parser currently uses `vX.Y.Z`; svg, svgplot, csvzall, and
   obsidian-csvzall use bare `X.Y.Z`). Check `gh release list -R <repo>` first.
2. Check the repo's `.github/workflows/` for a release workflow and what
   triggers it:
   - **svgplot** `release.yml` runs on tag push and builds the single-header
     asset. Push the tag, wait for the workflow to succeed, and confirm a
     GitHub release with assets exists (create it with `gh release create` if
     the workflow only uploads assets).
   - **csvzall** `build-binaries.yml` runs on `release: published`. Create the
     release yourself with `gh release create <tag> --verify-tag --title ...
     --notes ...`, then wait for the binary/WASM asset jobs to finish green.
   - **obsidian-csvzall** `release.yml` has a two-phase flow: run it with
     `operation=prepare` (produces a release-prep PR with verified helpers),
     get that PR green and merged, then push the tag / run `operation=publish`.
     Do not build or copy native helpers by hand.
   - **csv-parser** `release-single-header.yml` runs on tag push and uploads
     `csv.hpp`; `python-publish.yml` runs on `release: published` and pushes
     the Python package. Push the tag, make sure a GitHub release exists
     (`gh release create` if the tag workflow didn't make one), and confirm
     both workflows succeed.
   - **No release workflow** (currently svg: only `cpp.yml`, `coverage.yml`,
     `docs.yml`): create the GitHub release yourself with `gh release create
     <tag> --verify-tag --title "<name> <version>" --notes "<summary>"` and
     attach any artifacts the repo has historically published.
   - Always re-list `.github/workflows/` rather than trusting this list; if a
     release workflow now exists, use it instead of creating the release by
     hand.
3. Release notes: what was fixed, why, and whether consumers must take action.
   Publishing a release is outward-facing; confirm with the user unless
   pre-authorized.

**e. Move up one level.** In the next consumer on the path, update every pin
location from the table above to the new tag/commit (submodule *and*
FetchContent fallback for csv-parser), update the README dependency table when
the version shown there changes, extend `csvzall_validate_*` if csvzall now
requires the new API, then repeat steps a–d for that consumer. For csvzall,
also record any csv-parser API friction in `CSV_PARSER_TODOS.md`.

### 2. Close out

- Report, per repo: PR link, merge SHA, version bump (patch/minor and why),
  release link, and the pin each consumer now uses.
- If a step was skipped or blocked (CI red, mismatched version markers,
  missing permissions), stop at that repo, say so plainly, and do not bump
  consumers to an unreleased commit.

## Guardrails

- Never pin a consumer to an unmerged branch or an untagged commit in a
  release; local sibling checkouts (`../csv-parser`, `../svgplot`,
  `SVGPLOT_ROOT`, `CSV_PARSER_ROOT`) are for development only.
- Never force-push a default branch, delete/move a published tag, or rewrite a
  release to "fix" a bad version. Cut a new patch instead.
- Keep the Obsidian helper artifact contract from `AGENTS.md` intact
  (`CSVZALL_OBSIDIAN_BUILD=ON`, static/in-tree deps, executable only).
- Don't change csvzall CLI behavior, file formats, or diagnostics as a side
  effect of a dependency bump; if upstream behavior changed, gate or adapt it
  explicitly and document it in `--help`.
