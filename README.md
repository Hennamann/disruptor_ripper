# disruptor_ripper

An asset extractor for the PlayStation game **Disruptor** (Insomniac Games, 1996).
It reads the game's single data archive, `WAD.IN`, and writes the contents out as
PNG, WAV/VAG and OBJ files.

The PAL release (`SLES_005.35`), the North American NTSC release (`SLUS_002.24`), the Japanese
release (`SLPS_008.04`), the German (`SLES_005.65`) and French (`SLES_005.64`) releases, the PAL demo disc (`SLED_005.94`) and the US demo disc (`DEMO.EXE`,
September 1996) are supported; the version is detected from the archive header. The archive has no directory, so all
offsets come from the game's own loader code, and the few tables that live in the executable
(FMV audio sizes, music track sizes and loop points, sky colours) are built in for both regions.

## Building

The tool is plain C99 with no dependencies and builds on macOS, Linux and Windows:

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build

The executable ends up in `build/` (or `build/Release/` with Visual Studio).

## Usage

    disruptor_ripper [options] <path to WAD.IN>

With no selection options everything is extracted into a directory named
`Disruptor resources` next to `WAD.IN`. Options:

| option          | content                                                                 |
|-----------------|-------------------------------------------------------------------------|
| `-o DIR`        | write to `DIR` instead                                                  |
| `--enemies`     | enemy sprite frames, sprite sheets and the sprites cut from them, per level |
| `--weapons`     | weapon and psyonic sprites                                              |
| `--hud`         | HUD sheet with all palettes plus the 66 individual HUD sprites          |
| `--sounds`      | sound effects, weapon/psyonic sounds, loading screen speech             |
| `--music`       | streamed music, one directory per level                                 |
| `--fmv`         | voice-over audio of the FMV cutscenes                                   |
| `--textures`    | level textures (32x32 PNGs + atlas, animated/scripted texture frames), VRAM snapshot, item pickup images, fonts, mission text, entity/item/waypoint lists, the automap as SVG and PNG, the sky panorama, raw dumps of the remaining tables |
| `--geometry`    | level geometry as textured Wavefront OBJ/MTL                            |
| `--misc`        | boot splash screens, title/menu screens, loading screen hint strips     |

Audio is written both as decoded 16-bit `.wav` and as raw `.vag` (Sony SPU-ADPCM).
Music tracks come with a `tracks.txt` listing loop points.

## What is where in WAD.IN

The first 0x654 bytes are a table of absolute file offsets that the game copies to RAM;
`src/wad.h` documents every entry. Highlights:

* **Levels** (`0x288 + N*4`, N = 1..15, three slots unused): a 0x74800 byte block with an
  0x800 byte header, a 64x192 and a 704x320 16-bit VRAM image (CLUTs and 8bpp texture pages),
  followed by the level data. The level data starts with 8-byte texture entries
  (`u0 v0 clut u1 v1 tpage`, each a 32x32 texel texture) and ends with the SPU sound bank.
  The level's *areas* follow; each area holds up to 64 objects whose meshes are chains of
  packed-vertex quad lists (see the comment at the top of `src/level.c`). Enemy sprite sets
  ("pieces") are stored after each area.
* **Music** (`0x2C8 + N*0x20`): up to three songs per level, each with up to three "A" and
  three "B" variants, streamed as 22050 Hz mono ADPCM in 48 KB chunks. Track sizes and loop
  points live in tables inside the executable and are copied into `src/sounds.c`.
* **Weapons** (`0x008`) and **psyonics** (`0x148`) are repeated eight times (CD seek
  optimisation); only the first copy is extracted.
* **Title screens** (`0x4C8`): five 0x92000 byte variants with four 320x256 8bpp backgrounds, the
  menu string table (which also carries the mission names: THE GAUNTLET, CHEMICAL FACTORY, ROOFTOPS,
  JUPITER STATION, TRITON, MARS, ANTARCTICA, IO, REACTOR, ORBITING HABITAT, DREAM, PRISON, FORTRESS),
  the 12x14 and 8x8 menu fonts, icons and the menu sound bank; the menu music is streamed from
  the offsets at `0x4DC`.
* **NTSC differences:** the level block is 0x73800 bytes (its small VRAM image is the 320x32
  loading hint strip instead of PAL's 64x192 sprite page), splash screens and title backgrounds
  are 320x240, the title block contents sit 0x5000 bytes earlier, FMV audio sizes differ, and
  the sky has no flat background colour. Level data, music and speech are byte-identical.
* **Japanese release:** NTSC layout, but the level data has no 8x8 font table and its
  objective text is a string of indices into a per-level set of 13x13 1-bit glyphs, so each
  level gets `objective.png` (the rendered text) and `font_jp.png` instead of the Latin fonts;
  loading hint strips are 320x64; the title block has one extra menu glyph and no 8x8 font, so
  its palette and icons sit earlier. Executable tables match NTSC-U.
* **US demo disc:** the oldest build (September 1996). Its header is 0x5FC bytes (four title
  entries, speech table at 0x4EC, no FMV/hint/splash tables) and the level data tail is shorter.
  It holds levels 3, 6, 8, 9, 11 and 15 as earlier revisions of the retail levels: different
  item names ("BIG HEALTH BONUS", "MUTANT DATABANK"), a ZODIAC weapon pickup in level 8 that
  retail removed, objective text typos, extra textures in level 11, and four reload animation
  frames for the first weapon that no retail build has. Its executable carries the mission names
  (ROOFTOPS, ANTARCTICA, IO SULFURINE MINES, MARS DESERT, ORBITING HABITAT, FORTRESS); retail
  stores the names in the title block instead.
* **PAL demo disc:** the 43 MB archive uses the retail layout but holds only levels 3, 4, 6, 8, 9
  and 11 (one playable "sample mission" and five previews), each with one song (one A and one B
  track), only the weapon/psyonic groups of those levels, three title screen variants, no FMV
  audio, no splash screens and no hint strips. The warning screen is a separate `WARNING.IN`
  (one 320x256 16-bit image) which is extracted when it sits next to `WAD.IN`.
* Level ids are the game's internal numbering (slots 10 and 13 are empty); the play order is
  1 2 3 4 5 6 8 9 7 11 12 14 15.
* **Splash screens** (`0x644`): four 320x256 24-bit RGB images.

Per level, `entities.txt` (with `typedefs.txt`), `items.txt` and `waypoints.txt` list every enemy/object placement,
pickup (with its in-game name) and AI waypoint node with raw game coordinates, the same position
in the OBJ's coordinate space, type ids, area and difficulty mask (bit 0 easy, bit 1 normal,
bit 2 hard). `item_type_NN.png` is the pickup image for item type NN as referenced by
`items.txt`. `tables.txt` is a hex dump of the tables that are only partly understood
(door/lift controllers, switchable objects, effect emitters, texture scripts). Entities are not
named in the data; `typedefs.txt` explains the class byte (static, random turning, patrol, free
movement, rotating) and the sprite set tells what they look like.

Sound effects carry their SPU pitch in 16-byte descriptors (level banks, weapon blocks and the
title block), so sample rates are exact; only the psyonic banks have no table and are assumed
to be 22050 Hz.

`pause_menu.png` is the 128x96 4bpp pause screen graphic each level carries (its palette lives
inside the HUD sheet). `sky.png` is the level's sky, a 1280x240 cylindrical panorama (1280 texels = one full turn) that
eight of the levels have; `sky.txt` notes the flat colour the game draws behind it.
`map_NN.svg` / `map_NN.png` are the in-game automap (vector line art stored in the level data,
drawn fully revealed, doors in yellow, player start as a green cross).

**Viewing levels in a browser.** The extractor writes `Levels/viewer.html`, a self-contained Three.js
viewer with WASD/mouse-look controls and toggles for textures, vertex lighting, sky, enemies,
items, AI waypoints, toggleable meshes and the map overlay. Browsers refuse to load data from
`file://`, so serve the folder over HTTP:

    cd "Disruptor resources/Levels" && python3 -m http.server 8000

and open http://localhost:8000/viewer.html.

Geometry notes: the OBJ uses a right-handed, Y-up system with 16 game units per OBJ unit.
Objects are named `areaAA_objOO_idIII`; every mesh is a group named with its visibility id
(`vis ff` meshes are always drawn, others are toggled by game logic, e.g. door states).
Vertex colours are appended to the `v` lines.

## Credits

Original Windows extractor and sprite/sound format research by Yagotzirck.
The IDA database of the executable he used is kept in `misc/`.
