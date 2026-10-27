# Project agent memory

This file is the single source of truth for how coding agents work in this repository.
`CLAUDE.md` is a thin pointer to this file; put content here, never there.

This is BrainChip's fork of Neuromorphyx's BrainBoard1500 Arduino library for the BrainBoard1500 (BB15) rev 2.
For what the library does and how to build the examples, read `README.md`, `examples/README.md`, `tools/README.md`,
and `PATCHES.md`.

## Repository identity and the PR trap

Two remotes, deliberately asymmetric:

| remote | URL | direction |
| --- | --- | --- |
| `origin` | `Brainchip-Inc/BrainBoard1500` | fetch and push |
| `upstream` | `Neuromorphyx/BrainBoard1500` | fetch only (push URL is disabled: `no-pushing-to-upstream`) |

Because this is a GitHub fork, **a bare `gh pr create` defaults to the upstream parent**, so it will try to
open a pull request against Neuromorphyx. That is a public repository. Never do it.

- Never open a PR against Neuromorphyx without explicit approval from the human you are working for.
- Always pass the target explicitly:
  ```sh
  gh pr create --repo Brainchip-Inc/BrainBoard1500 --base main
  ```
- After creating a PR, check the returned URL starts with `https://github.com/Brainchip-Inc/`. If it does not,
  close it immediately and report the mistake.

Syncing from upstream is a fetch, a review, then a merge. Never a blind pull:

```sh
git fetch upstream
git log --oneline HEAD..upstream/main   # review what is coming
git merge upstream/main
```

### Basing a pull request that goes TO upstream

An approved upstream pull request must be branched from `upstream/main`, never from `main`. A pull request
carries every commit in the head branch that is not in the base, so a `main`-based branch offers Neuromorphyx
all of BrainChip's fork-only work along with the change: this file, `CLAUDE.md`, and whatever else has landed on
`main` since the last upstream sync. It only grows worse over time.

```sh
git checkout -b feat/my-example main            # WRONG
git fetch upstream
git checkout -b feat/my-example upstream/main   # RIGHT
```

Work still happens on `main`, where the conventions and tooling live. When a change is ready to go upstream,
cherry-pick just its commits onto a fresh `upstream/main`-based branch:

```sh
git fetch upstream
git checkout -b feat/my-example upstream/main
git cherry-pick <sha>...        # only the commits for this change
```

This is why the commit discipline below matters: a demo committed as one focused commit touching only its own
`examples/BrainChip/<name>/` folder lifts across in a single cherry-pick, while work bundled with unrelated edits
has to be dissected under pressure.

Verify before opening the pull request, so a mistake is caught while it is still private:

```sh
git log --oneline upstream/main..HEAD     # must show ONLY your intended commits
git diff --name-only upstream/main...HEAD # must show ONLY your intended files
```

Then push and open it cross-repo, with the target passed explicitly as above:

```sh
git push origin feat/my-example
gh pr create --repo Neuromorphyx/BrainBoard1500 \
  --base main --head Brainchip-Inc:feat/my-example
```

## Commit message format

BrainChip work uses [Conventional Commits](https://www.conventionalcommits.org/): `type(scope): description`.

- Allowed types: `feat`, `fix`, `docs`, `style`, `refactor`, `perf`, `test`, `build`, `ci`, `chore`, `revert`,
  `config`.
- Include a scope wherever a sensible one exists, for example `examples`, `src`, `license`, `docs`, `tools`.
- Imperative mood, lower-case description, no trailing period.

```
fix(src): correct BB15 reset and sleep pin mapping
feat(examples): add Nicla Vision human detection demo
docs: clarify example flashing steps
```

**Exception, and it matters:** upstream Neuromorphyx does not use Conventional Commits. Its history reads as
plain sentences, for example `Revise license and attribution details in README`. A commit that is intended for a
pull request back to Neuromorphyx must match that plain-sentence style instead, so the upstream history stays
consistent. Conventional Commits apply to everything that stays on the BrainChip fork.

## Arduino library layout

The Arduino IDE and the library index depend on this structure, so keep it intact:

- `library.properties` at the root is the library manifest.
- `src/` holds the library sources; `src/BrainBoard1500.h` is the public entry point declared by `includes=`, an
  umbrella over the `cbb15::` headers in `src/cbb15/`.
- `examples/` holds one folder per sketch, upstream's at the top level and BrainChip's under `examples/BrainChip/`,
  and the `.ino` inside must be named exactly like its folder (`examples/01_SpiLinkTest/01_SpiLinkTest.ino`).
  Renaming one without the other hides the sketch from the IDE.

Leave `library.properties` alone, especially `version=`. Upstream changes it with each release, so every local
change to it turns the next upstream sync into a merge conflict. Change it only when a human
explicitly asks for a release bump.

## Maintaining this file

Keep this file for knowledge useful to almost every future agent session in this project.
Do not repeat what the codebase already shows; point to the authoritative file or command instead.
Prefer rewriting or pruning existing entries over appending new ones.
When updating this file, preserve this bar for all agents and keep entries concise.
