# Contributing

Thank you for your interest in this project.

## Pull requests

This repository is maintained by BrainChip, and changes are made by the maintainer team.
Pull requests opened from outside that team will be closed.

The honest reason is that this repository is a fork. Upstream keeps moving, and every change
made here has to stay reconcilable with it, so each one is weighed against the next upstream
sync rather than judged on its own. Keeping that call with the maintainers is what stops this
fork from quietly drifting away from the library it tracks.

## Contributing to the library itself

The BrainBoard1500 library is developed by Neuromorphyx at
[Neuromorphyx/BrainBoard1500](https://github.com/Neuromorphyx/BrainBoard1500).

If you have a fix or an improvement to the **library**, please send it there. That is where it
belongs, and it reaches every BrainBoard1500 user rather than only BrainChip's fork. A change
landed upstream also arrives here on the next sync, so nothing is lost by taking the longer road.

What lives here is BrainChip's own examples and demos built on top of the library, under
`examples/BrainChip/`.

## Reporting a problem

If a demo does not work for you, please open an issue. That is the most useful thing you can
send us, and we read every one.

If the problem is in the library rather than in a BrainChip demo, please report it upstream
instead, as described above. If you are not sure which it is, open it here and we will route it.

Please search the existing issues first, then include:

- what you ran, and what happened instead of what you expected
- which host board you are on (Nicla Vision or Nicla Voice)
- your Arduino IDE or `arduino-cli` version
- which example sketch, and whether it is a BrainChip one or an upstream one
- the serial output around the failure, the smallest excerpt that shows it

You do not need to diagnose the cause or propose a fix. A clear description of what broke is
enough for us to work from.

## Questions and ideas

The [BrainChip Developer Hub](https://developer.brainchip.com/signup/) has the tools, model
zoo and documentation for the wider Akida platform, and the
[BrainChip Discord](https://discord.com/invite/9bmd9g52vn) is the place for questions, ideas,
and showing us what you have built.

## Building on this work

Please do. This repository is licensed under the Apache License 2.0. The library is copyright
NEX NOVUS d.o.o. (Neuromorphyx), and BrainChip's additions are copyright BrainChip Holdings
Ltd., under the same licence, so you can fork it and take it in your own direction without
asking us first. See [LICENSE](LICENSE) for the terms and [NOTICE](NOTICE) for the
attributions that must travel with the code.

---

## For maintainers

Every change reaches `main` of
[Brainchip-Inc/BrainBoard1500](https://github.com/Brainchip-Inc/BrainBoard1500) through a pull
request. Because this repository is a GitHub fork, a bare `gh pr create` targets Neuromorphyx,
so always pass the target explicitly:

```sh
gh pr create --repo Brainchip-Inc/BrainBoard1500 --base main
```

Commits on this fork follow Conventional Commits. The allowed types, the scope convention and
the worked examples live in [AGENTS.md](AGENTS.md), which is the single source of truth for
them; they are deliberately not repeated here.

One nuance from that file is worth knowing before you write the commit rather than after:
upstream Neuromorphyx does not use Conventional Commits. A commit destined for a pull request
back to Neuromorphyx should match upstream's plain-sentence style instead, so their history
stays consistent.
