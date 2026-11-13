# Dune 2000 data formats

Status legend: **confirmed** = parses every file to EOF and renders correctly; **partial** = some fields unknown.

## .R16 / .R8 image archives — confirmed
Files: `DATA.R16/R8` (units, buildings, effects: 5105 frames), `BLOX*.R16/R8` (tilesets: 800 tiles),
`MOUSE.R16/R8` (cursors: 265 frames). `UI_*.R16` uses a variant (below); `UIBB*.R16` is raw RGB555 (below).

Sequence of frames, little-endian, no file header:

| off | type | field |
|----:|------|-------|
| 0 | u8 | type: 0 = **empty entry** (this single byte is the whole entry, but it still occupies a frame index), 1 = frame (+ palette if palette_ptr≠0 and bpp=8), 2 = frame reusing previous palette |
| 1 | i32 | width |
| 5 | i32 | height |
| 9 | i32 | x offset |
| 13 | i32 | y offset |
| 17 | u32 | image ptr (runtime, ignore) |
| 21 | u32 | palette ptr (non-zero → palette block follows pixels, for type 1) |
| 25 | u8 | bpp: 8 or 16 |
| 26 | u8 | frame height |
| 27 | u8 | frame width |
| 28 | u8 | alignment |
| 29 | … | pixels: w·h bytes (8bpp indices) or w·h u16 (RGB555) |
| … | 520 bytes | palette block (type 1 with palette ptr): 8-byte header + 256 × u16 RGB555 |

- Tilesets are 16bpp RGB555, 32×32. Sprites in DATA.R16 are mostly 8bpp with per-frame RGB555 palettes.
- 8bpp sprite pixels: 0 = transparent, 1 = shadow, 240–255 = house colour shades (COLOURS.BIN), else frame palette.
- DATA.R16 has 6555 entries incl. 1450 empty ones; the art tables in Templates.bin account for exactly all of them
  (first 206 are technical/UI entries, then unit art, building art, projectiles, animations, icons, build-up, building animations).
- UI_*.R16 is the same format but every 3rd entry among the first 45 is preceded by 8 extra bytes (per D2kEditor):
  15 sidebar button records `{i32 x, i32 y, shown, pressed, 16bpp background}`, then power bar x4, two banners,
  three radar house logos, radar background (55 frames; table in re/notes/ui.md).
- `.R16` is a build-time cache: Westwood's dev path loaded `%s.bmp`/`%s.tga` and wrote RES files (see `re/functions.md`, LoadTileset).

## .MAP — confirmed (tile layer); special values partial
`u16 width, u16 height`, then `width*height` cells row-major of `u16 tile, u16 special`.
- `tile` indexes the 800-tile BLOX tileset (40 tiles per row in the atlas sense).
- `special`: 0 = none; 1/2 observed often (likely spice / thick spice); 20, 23, 45… (likely spawn points / special objects) — TODO.
- Some community maps have trailing bytes after the grid.
- The tileset is not stored in the .MAP: it comes from the mission file. Campaign map `X.MAP` ↔ `maps/_X.MIS`
  (or `Missions/X.MIS`); skirmish/multiplayer maps all use `maps/_SPAWN.MIS` (→ BLOXBGBS / TILEATR3).

## .MIS — header/names confirmed (all 368 files parse); events/conditions/AI raw
Reference: D2kEditor `_mission.pas` / `_misai.pas` (MIT, github.com/jkoncick/D2kEditor, cloned to `ref/`).
Original size 68066 bytes; the 1.06p+ "extended" files (143014 B here: all Summers' Solstice and 11
Coalition: New Enemies missions) append u16 event count, u16 condition count, conditions 48..255
(28 B each) and events 64..count-1 (72 B each) — D2kEditor `_mission.pas`. We read the two counts
(`MisFile::extended*`); the extra events are not decoded (docs/community-campaigns.md §6).

| off | size | field |
|----:|-----:|-------|
| 0 | 8 × u8 | tech level per side |
| 8 | 8 × u32 | starting money per side |
| 40 | 40 | unknown |
| 80 | 8 × u8 | house id per side |
| 88 | 8 × 7608 | AI segment per side (see `_misai.pas`) |
| 60952 | 8 × 8 u8 | allegiance [side][side]: 0 ally, 1 enemy, 2 neutral |
| 61016 | 64 × 72 | events (TEvent) |
| 65624 | 48 × 28 | conditions (TCondition) |
| 66968 | 200 | tileset name (NUL-padded, e.g. "Bloxbgbs") |
| 67168 | 200 | tile attribute file name (e.g. "tileatr3") |
| 67368 | u8, u8 | num events, num conditions |
| 67370 | i32 | time limit |
| 67374 | 692 | message data |

## bin/Templates.bin — confirmed
133140 bytes (original layout; an extended variant appends 64 more explosions). Struct layout in
`src/formats/templates.hpp` (from D2kEditor `_structures.pas`): 60 × UnitTemplate (256 B), 100 × BuildingTemplate
(268 B), 64 × WeaponTemplate (28 B), 64 × ExplosionTemplate (8 B), art tables, counts, name strings, group ids.
In game memory the unit templates live at 0x77E250 (stride 0x100).
Original data: 30 units, 62 buildings. House versions: for each unit/building *group* and house, the template whose
OwnerHouse bit matches (fallback: first template of that group).

## bin/TILEDATA.BIN — confirmed
1000 × {u16 index, u8 side, u8 type}: meaning of map special values < 1000. type 0 = unit (index = unit group),
128 = building (index = building group), 1 = misc object, 2 = explosion, 255 = nothing.
Special values ≥ 0x1000 use an explicit encoding (0x8000 misc, 0x4000 unit: side=(v>>6)&7 type=v&63,
0x2000 building: side=(v>>7)&7 type=v&127).

## bin/COLOURS.BIN — confirmed
8 houses × 16 RGB555 shades; replaces palette indexes 240–255. House order: Atreides, Harkonnen, Ordos, Emperor,
Fremen, Smugglers, Mercenaries, Sandworm.

## Map tile word
Low 12 bits = tileset tile; bit 12 (0x1000) = concrete; bits 13–15 = spice amount (when not concrete).
Spice is drawn by replacing the tile: thin spice picks one of 256 tiles by its 8-neighbour mask, thick spice uses
a fixed 8×8 pattern over tiles 300/301/320/321 (tables in `src/game/scenario.cpp`).

## .AUD — confirmed (all 6617 files decode)
u16 rate, u32 compressed size, u32 output size, u8 flags (bit0 stereo, bit1 16-bit), u8 compression (99 = IMA ADPCM).
Chunks: u16 comp size, u16 out size, u32 0x0000DEAF, data. Standard IMA ADPCM, low nibble first, decoder state carried
across chunks. Dune 2000 uses 22050 Hz mono 16-bit throughout.

## UIBB*.R16 — confirmed
Raw headerless RGB555 full-screen backdrop of the in-game frame (top border, radar box, sidebar columns).
`UIBB.R16` = 640×400; `UIBB_<w>x<h>.R16` = Gruntmods high-res variants (size = w·h·2).

## FONTCOL.FPL — confirmed (first 16 entries used)
1024 bytes = 256 x u32; Graphlib_font_pal 0x42e1c0 converts the first 16 with GetColor16bit (byte 2 = R,
byte 1 = G, byte 0 = B, byte 3 ignored). Index 0 = transparent, 1 = black outline, 2..15 = red in the shipped file.

## .FNT fonts — confirmed
Dump of the 0x408-byte in-memory font record — [1] space width, [2] first char, [3] spacing, [4] height, then 256
runtime glyph pointers (ignored) — followed by glyphs {u32 w, u32 h, w·h bytes; 0 = transparent} from first char on.
`bin/FONT*.BIN` (256 bytes) maps character → glyph code (0x20 = space). FONTCOL*.FNT uses palette FONTCOL.FPL (above).
Source: BlitFontChars 0x42dc00, GetStringPixelWidth 0x42dfb0, Graphlib__LoadFontFile 0x42e450.

## bin/ARMOUR.BIN, bin/SPEED.BIN — confirmed
ARMOUR (2702 B; longer files — War of the Landsraad's 2750 B — are read for the first 2702 like
the game does): 30 × {12 × u8 damage% vs armour type, u32 radius, u32 infantry death}, 30 × 50-char warhead names,
12 × 50-char armour names, u8 warhead count (14), u8 armour count (10: NONE WALL BUILDING WOOD LIGHT HEAVY CONCRETE
INVULNERABLE CY HARVESTER).
SPEED (384 B): float[8 terrain rows][4 speed types: Infantry, Tracked, Wheeled, Flying], 4 × 32-char names, 128 zero.

## UI_DATA/*.uib — string tables confirmed (40 files, 54780 strings)
u32 count, then count × {u16 len, key (NUL incl.), u16 len, value (NUL incl.)}. Loader: `src/formats/uib.*`.
Exceptions with other record layouts: `colours.uib` (name + colour), `menus.uib` (menu name + .uid layout file + ids).
`*.uil` = front-end page scripts (u32 count + u16-length lines; block language BACKGROUND/GROUPBOX/TEXTBOX/BUTTON/
ANIMATION/MOVIE/LISTBOX), `menus.uib` = menu name -> page file, `colours.uib` = named 0x00RRGGBB colours, `campaign.uib` =
the campaign table (3 x 10 x 0x104). Loaders: `src/formats/uil.*`, `src/game/campaign.*`; layouts in re/notes/campaign-flow.md.

## NSIS installers (Gruntmods Edition) — confirmed
Reader: `src/formats/nsis.*` (own code from the NSIS `fileform.h` layout; LZMA via the public-domain
LZMA SDK in `third_party/lzma`). Exe stub, then at a 512-byte boundary the first header:
u32 flags, u32 0xDEADBEEF, "NullsoftInst", u32 header size, u32 archive size (from the first header to
the end, incl. a trailing CRC32 unless flags&4).
- **Gruntmods 1.6.7** ("Nullsoft Install System v18-Sep-2024.cvs", Unicode, flags 0x70, first header
  at 0x23400): a 64-bit variant ("chunked64"). After the first header: u64 size of the data that
  follows the header block; then chunks `{u24 compressed length; LZMA props(5) + stream with end
  marker}` until a zero length, then 4 unknown bytes and the CRC. Every chunk is an independent LZMA
  stream (props 5D, dict 0x233733); outputs concatenate to the solid stream: chunk 0 = u64 header
  length + header (737774 bytes), then 2 MiB per chunk (678.9 MB total). Inside the stream every
  file is `u64 length + data`. Script entries are 36 bytes (opcode + 8 params): EW_EXTRACTFILE (20)
  = {overwrite, name, offset lo, offset hi, FILETIME lo, hi, allow-ignore, ?}; EW_ASSIGNVAR is 27
  (25 in stock NSIS 3), EW_CREATEDIR 11 (param 1 ≠ 0 = SetOutPath). Identical files are stored once
  (14499 paths share 678 MB; 1.2 GB unpacked). No BCJ filter.
- **Stock NSIS 3** (the cutscene installers, NSIS v3.10; `Dune2000Cutscenes.exe`): 28-byte entries,
  non-solid deflate (blocks `u32 len | 0x80000000` + raw deflate) — read via zlib when available.
  Solid/non-solid LZMA and stored blocks follow the same documented layout (untested: no sample).
- Header block: u32 flags, 8 × {offset, count} (pages, sections, entries, strings, lang tables, …).
  Sections are 24 + 1024 (ANSI) or 24 + 2048 (Unicode) bytes. Strings: UTF-16 (offsets in WCHARs)
  or ANSI; codes 1 lang, 2 shell, 3 var, 4 skip (NSIS 2 ANSI: 255/254/253/252) followed by a
  number `(lo & 0x7F) | ((hi & 0x7F) << 7)`. Variables: $0–$9 = 0–9, $R0–$R9 = 10–19, $INSTDIR 21,
  $OUTDIR 22, $PLUGINSDIR 26, $_OUTDIR 31. Paths are resolved by a linear pass (StrCpy/SetOutPath),
  only files under $INSTDIR are extracted; `..`, drive letters and absolute paths are refused.

## .VQA movies (PC cutscenes) — confirmed (all 43 Gruntmods English movies decode)
Decoder: `src/formats/vqa.*` (own code from the public VQA descriptions: Gordan Ugarkovic's
VQA_INFO, MultimediaWiki). Checked frame by frame against ffmpeg used as a black box: identical
until the first codebook switch, where ffmpeg breaks (its Format80 output buffer overflows on the
larger VQFL codebooks) and ours stays clean; audio sample counts match exactly (sample values
differ by a few LSB: ffmpeg uses a different IMA rounding).
- IFF: `FORM` <u32 BE size> `WVQA`, then chunks `{char id[4], u32 BE size, data, pad to even}`.
- `VQHD` (42 bytes, LE): u16 version (2), u16 flags (0x1D: bit 0 audio, bit 4 hicolor), u16 frames,
  u16 width (640), u16 height (400), u8 blockW (4), u8 blockH (4; 2 in A_BR01/A_BR03/H_BR02),
  u8 fps (15), u8 cbParts (0), u16 colours (**0 = hicolor**), u16 maxBlocks (3000..6000), u32 0,
  u16 largest frame, u16 sample rate (22050), u8 channels (1), u8 bits (16), 14 more bytes.
- **Dune 2000 uses hicolor (15-bit) VQA** in every file: codebook entries are blockW*blockH
  RGB555 pixels (u16 LE), no palette.
- `LINF` {`LINH`, `LIND`} loop table, `CINF` {`CINH`, `CIND` = (u16 frame, u32 size) per codebook}
  — the frames where a new codebook starts; `FINF` frame offsets. Not needed for sequential play.
- Per frame, in this order: `SN2J` (6 bytes, ADPCM resync info, ignored), `SND2` (one frame of
  audio: 1470 samples = 735 bytes), optionally `VQFL` (a full codebook, `CBFZ` inside, that applies
  from this frame on — it sits right before the frame named in CIND), then `VQFR` with `CBFZ`
  (frame 0 only) and `VPRZ` (or `VPTR`: uncompressed).
- **Format80 (LCW)**: `0CCCPPPP PPPPPPPP` copy CCC+3 from (pos - P); `10CCCCCC` C literals (0 =
  end); `11CCCCCC PPPP` copy C+3 from P; `FE CCCC VV` fill; `FF CCCC PPPP` copy C from P. A
  chunk starting with a 0 byte is the relative variant (P of the two long copies counts back from
  the write position); Dune 2000's CBFZ chunks use both variants, VPRZ the absolute one.
- **Hicolor pointer codes** (`VPTR`/`VPRZ`, u16 LE each, type = code >> 13; blocks in raster order):
  0 skip (code & 0x1FFF) blocks (they keep the previous frame); 1 entry (code & 0xFF) repeated
  ((code >> 8 & 0x1F) + 1) * 2 times; 2 entry (code & 0xFF) once, then that many one-byte entries;
  3 entry (code & 0x1FFF) once; 5 entry (code & 0x1FFF) repeated <next byte> times.
- **Audio** `SND2`: IMA ADPCM (standard step/index tables), low nibble first, state carried from
  chunk to chunk (as in .AUD; stereo = first half left, second half right). `SND0` = raw PCM.
- Palettized VQA (other Westwood games; decoded but untested here): `CBF0/CBFZ` full and
  `CBP0/CBPZ` partial codebooks (cbParts chunks concatenated, then unpacked), `CPL0/CPLZ` palette
  (6-bit RGB), `VPT0/VPTZ` = two planes (low bytes, high bytes); high byte 0x0F (0xFF in version 1)
  = fill the block with colour <low byte>, else entry high*256+low.

## PS1 STR movies (DATA.XA on the PS1 disc) — confirmed (MDEC v2 + XA ADPCM)
Decoder: `src/formats/ps1_disc.*` (MODE2/2352 image, ISO9660 root, DATA.FAT; docs/ps1-assets.md)
and `src/formats/ps1_str.*` (own code from psx-spx "MDEC" / "CDROM XA" and the MPEG-1 VLC
table B.14). Against ffmpeg (black box): video within rounding (mean |diff| 0.4 of 255 per channel,
float IDCT), XA audio sample-identical.
- 40 STR entries, all bitstream **version 2**, 320x208 at 15 fps (house montages 208x112,
  copyright 320x240), mono XA ADPCM 18900 Hz on channel 1. 5 or 10 sectors per frame.
- Video sector (Form 1, submode 0x48): header `u16 0x0160, u16 0x8001, u16 chunk, u16 chunks,
  u32 frame, u32 frame bytes, u16 width, u16 height` + 12 bytes, then 2016 data bytes.
- Frame: `u16 MDEC code count, u16 0x3800, u16 qscale, u16 version`, bitstream of 16-bit LE words
  read MSB first. Macroblocks 16x16 column by column (top to bottom, then left to right), blocks
  Cr, Cb, Y0 (top left), Y1, Y2, Y3. Block: DC = signed 10 bits x 2 (quant[0]); AC: VLCs from
  MPEG-1 table B.14 ("11s" = run 0 level 1), `10` = end of block, `000001` + 6-bit run + 10-bit
  signed level = escape; value = (level * quant[zigzag pos] * qscale + 4) >> 3, clamped to
  -1024..1023; the PS1 quant matrix is MPEG-1's intra matrix with 2 at DC.
- Picture: 8x8 IDCT (flat block = DC/8), Y + 128, chroma 4:2:0, R = Y + 1.402 Cr,
  G = Y - 0.3437 Cb - 0.7143 Cr, B = Y + 1.772 Cb.
- XA audio sector (Form 2, submode bit 2; coding bit 0 stereo, bit 2 18900 Hz): 18 sound groups of
  128 bytes = 16-byte header (shift / filter per unit) + 28 x 4 data bytes; 8 units of 28 4-bit
  samples (stereo: even units left, odd right). sample = (nibble << 12 >> shift) +
  (old1 * K0[filter] + old2 * K1[filter] + 32) >> 6, K0 = 0, 60, 115, 98, 122, K1 = 0, 0, -52, -55, -60.

## Not yet decoded
`PALETTE.RES`, other `bin/*.BIN` (ARMOUR, SPEED, BUILEXP, TECHPOS layouts are in
D2kEditor `_structures.pas`; TILEATR in `_tileset.pas`).
