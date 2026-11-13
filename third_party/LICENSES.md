# Third-party components

OpenD2K itself is licensed under the GNU General Public License, version 3 or later (`LICENSE`).
The components below keep their own licences. All of them are compatible with GPL-3.0-or-later.

| Component | Where | Licence | Notice |
|---|---|---|---|
| SDL3 3.4.20 (Sam Lantinga and contributors) | downloaded at build time (`cmake/FetchSDL.cmake`) or the system's; shipped as `libSDL3` / `SDL3.dll` | zlib | `licenses/SDL3-LICENSE.txt` in the packages |
| LZMA SDK decoder (Igor Pavlov) | `third_party/lzma` | public domain | `third_party/lzma/README.md` |
| stb_truetype, stb_image (Sean Barrett) | `third_party/stb` | public domain or MIT (dual, at the user's choice) | end of each header; stb_image's zlib decoder also unpacks the downloaded art pack zip (`src/formats/zip_reader.cpp`) |
| cgltf (Johannes Kuhlmann) | `third_party/cgltf` | MIT | `third_party/cgltf/LICENSE` |
| Play font (Jonas Hecksher) | `third_party/fonts/play`, compiled into the program | SIL Open Font License 1.1 | `third_party/fonts/play/OFL.txt` |
| zlib 1.3.1 (Jean-loup Gailly, Mark Adler) | the system's (macOS, Linux, Android) or downloaded and linked statically (`cmake/FetchZLIB.cmake`, the Windows packages) | zlib | this table (the zlib licence needs no notice in binaries) |
| Terrain textures and rock scans (ambientCG, Poly Haven) | `assets/terrain` | CC0 1.0 | `assets/terrain/LICENSES.md` |

No part of the original *Dune 2000* (code, graphics, sounds, music, movies, text, missions) is included.
