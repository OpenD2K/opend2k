# Contributing to OpenD2K

Thanks for helping! Bug reports, ideas and pull requests are welcome.

## Reporting a bug
Open an issue with the **Bug report** template. The most useful things to include:
- OpenD2K version (Options → Updates shows it), platform and device (macOS / Windows / Linux / iOS / Android, GPU if 3D).
- Graphics mode (Original / Enhanced / 3D / Custom), art pack installed or not, game language.
- What you did, what you expected, what happened. A screenshot or short video helps a lot.
- For gameplay bugs: the mission or skirmish map and, if you can, a **save game** from just before the problem
  (saves are in the app data folder: macOS `~/Library/Application Support/d2k-mac/Dune 2000/saves/`,
  Windows `%APPDATA%\d2k-mac\Dune 2000\saves\`, Linux `~/.local/share/d2k-mac/Dune 2000/saves/`).
- If the original 1998 game behaves differently, say how — OpenD2K's Original mode aims to match it exactly.

**Never attach the original game files** (DATA.R16, .MIX, .BIN, maps from the install, cutscenes): they are
copyrighted. Save games and screenshots are fine.

## Pull requests
- Fork the repository, make your change on a branch, open a pull request against `main`.
- Keep the simulation (`src/game/`) deterministic and free of platform / SDL calls; every non-original feature
  goes behind a setting, and Graphics: Original must stay pixel-exact.
- Build and run the tests before you send it (see [docs/building.md](docs/building.md)):
  `cmake --build build && ./build/game_test && ./build/formats_test` (the data tests need your own game install).
- Describe what changed and how you tested it; screenshots for visual changes.
- Do not copy code from other projects with incompatible licences, and never include original game data.

This public repository is published from the maintainers' working repository. Accepted pull requests are
merged there and appear here with the next release, with credit to you in the commit and the changelog.

## Licence of contributions
By contributing you agree that your code is licensed under **GPL-3.0-or-later** (the project licence, see
[LICENSE](LICENSE)) and art for the redesign art pack under **CC BY-SA 4.0**.
