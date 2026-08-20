# CLAUDE.md

The guidance for working in this repository is in [`AGENTS.md`](AGENTS.md). **Read it before
changing anything**, and read [`docs/TOOLCHAIN.md`](docs/TOOLCHAIN.md) before changing the build:
the cross toolchain silently miscompiled this code in three separate ways, and every one of them
presented as an application bug.

This repository keeps one shared instruction set for coding assistants so that build rules,
measurement discipline and the hardware facts cannot drift between tools.
