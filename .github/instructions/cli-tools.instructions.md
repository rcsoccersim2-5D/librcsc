---
applyTo: 'src/**,example/**'
---

# CLI Tools & Example Usage

## TL;DR
`librcsc/src` builds a set of small standalone CLI binaries — mostly `.rcg` game-log inspectors/converters built on `rcsc/rcg::Parser`/`Handler` — plus `librcsc/example` which shows minimal client-API usage patterns for onboarding.
- Every rcg-file tool follows the same shape: subclass `rcsc::rcg::Handler`, feed it to `rcsc::rcg::Parser::create(stream)->parse(stream, handler)`, override only the callbacks you care about.
- Binary names in `bin_PROGRAMS` (Makefile.am) **do not always match the `.cpp` file name** — e.g. `resultprinter.cpp` → `rcgresultprinter`, `scheduler.cpp` → `rclmscheduler`, `tableprinter.cpp` → `rclmtableprinter`.
- `rcg3to4.in` / `rcg4to3.in` are **not compiled binaries** — they are `configure`-templated `sh` wrapper scripts that just shell out to `rcgverconv -v 4/3`.
- **Open the full file when:** you need to add/change a CLI option, a new output format, or understand exact `.rcg` field mapping (cross-check against `rcg-log-replay.instructions.md`).

## Overview
| File | Binary (Makefile.am) | Purpose |
|---|---|---|
| [rcg2csv.cpp](../../src/rcg2csv.cpp) | `rcg2csv` | Dumps an rcg game log to two CSV files: `<base>.tracking.csv` (per-cycle ball/player positions, velocities, stamina) and `<base>.player_types.csv`. Uses `rcsc::ParamMap`/`CmdLineParser` for `-p/--player-types`, `--help`. |
| [rcg2txt.cpp](../../src/rcg2txt.cpp) | `rcg2txt` | Dumps rcg to a human-readable S-expression-like text format (`(Info (state ...) (ball ...) (player ...))`, terminated by `(Result "L" "R" score score)`) — format documented in a comment block at the top of the file. |
| [rcgrenameteam.cpp](../../src/rcgrenameteam.cpp) | `rcgrenameteam` | Rewrites an rcg file, replacing left/right team names while re-serializing everything else unchanged (via `rcsc::rcg::Serializer`). |
| [rcgreverse.cpp](../../src/rcgreverse.cpp) | `rcgreverse` | Produces a left/right-mirrored rcg file (swaps side, negates coordinates/angles) — always re-serializes as **rcg v1**. Drops `handleDraw` (returns `true` without writing draw info). |
| [rcgvalidator.cpp](../../src/rcgvalidator.cpp) | `rcgvalidator` | Parses an rcg file end-to-end and reports whether it's structurally valid; `--help`/`-h` or missing arg prints usage `<RcgFile>[.gz] [outputFile]`. Populates `ServerParam::instance()` from `handleServerParam`. |
| [rcgverconv.cpp](../../src/rcgverconv.cpp) | `rcgverconv` | The **real converter**: `rcgverconv [Options] <RcgFile>[.gz] -o <OutputFile>`, options `--version/-v <N>` (target rcg version, default json) and `--output/-o <file>`. `rcg3to4`/`rcg4to3` are thin wrappers around this. |
| [rcgversion.cpp](../../src/rcgversion.cpp) | `rcgversion` | Prints just the rcg log-format version number of a file: `rcgversion <RcgFile>[.gz]`. |
| [rcg3to4.in](../../src/rcg3to4.in) | *(sh script, not compiled)* | `configure`-generated wrapper: `rcg3to4 <input.rcg> <output.rcg>` → `rcgverconv -v 4 -o <output> <input>`. Requires `rcgverconv` in `PATH`. |
| [rcg4to3.in](../../src/rcg4to3.in) | *(sh script, not compiled)* | Same pattern, targets v3 (`rcgverconv -v 3 ...`). Note: its internal `usage()` message still says "rcg3to4" (copy-paste artifact). |
| [resultprinter.cpp](../../src/resultprinter.cpp) | `rcgresultprinter` | Prints final game result / score info parsed from an rcg log; also computes pitch-geometry constants (`PITCH_LENGTH=105.0`, `PITCH_WIDTH=68.0`, `GOAL_POST_RADIUS=0.06`) for result annotations. |
| [scheduler.cpp](../../src/scheduler.cpp) | `rclmscheduler` | League/tournament match **scheduler** — standalone utility unrelated to rcg parsing; has its own `TeamNameLoader` and `Scheduler` classes to generate round-robin/pairing schedules from a team-name file. |
| [tableprinter.cpp](../../src/tableprinter.cpp) | `rclmtableprinter` | Builds a **league results table** (win/loss/draw standings) from a batch of game-result inputs; largest tool in this group (~1450 lines), has its own `parseCmdLine`. |
| [object_table_printer.cpp](../../src/object_table_printer.cpp) | *(noinst, not installed)* | Dev-only generator: brute-forces `quantize_dist()` over `ServerParam` movable-object quantization tables and prints C++ `emplace_back(...)` lines to hand-paste into `rcsc/common/server_param.cpp`'s move-quantization tables (e.g. `M_movable_table_v18_wide`). Has a bare `main()` — no CLI args. |

## Architecture
- **rcg tools** (`rcg2csv`, `rcg2txt`, `rcgrenameteam`, `rcgreverse`, `rcgvalidator`, `rcgverconv`, `rcgversion`, `rcgresultprinter`) all: open input via `rcsc::gzifstream` (transparently handles `.rcg` and `.rcg.gz`), call `rcsc::rcg::Parser::create(stream)`, then `parser->parse(stream, my_handler)`. Writers additionally hold an `rcsc::rcg::Serializer::Ptr` created with a target version number.
- `scheduler.cpp` / `tableprinter.cpp` are **not** rcg consumers — pure text/CSV processing tools for league management, independent of `rcsc/rcg`.
- All tools are single-`.cpp`, no shared headers between them (each redeclares its own small `CommandCount`-style local structs when needed — see duplicated struct in `rcg2csv.cpp` and `rcg2txt.cpp`).

## Patterns & Conventions
- CLI parsing is either raw `argv`/`strcmp` (older tools: `rcgvalidator`, `rcgverconv`, `rcgversion`) or `rcsc::ParamMap` + `rcsc::CmdLineParser` (`rcg2csv` — the more modern pattern, matching `example/param_main.cpp`).
- `--help`/`-h` convention is inconsistent: some print via `options.printHelp()` (ParamMap-based), others hand-roll a `usage()` function writing to `std::cerr` and `return 0`.
- Output file naming: `rcg2csv` derives base name by stripping `.gz` then `.rcg` suffixes (`get_base_name()`), then appends `.tracking.csv` / `.player_types.csv`.
- Copyright/license header block (GNU LGPL) is boilerplate-copied verbatim across every tool — do not treat as tool-specific documentation.

## Key Abstractions
- `rcsc::rcg::Handler` — base class every rcg-consuming tool subclasses (see `rcg-log-replay.instructions.md` for the full callback contract: `handleLogVersion`, `handleShow`, `handleMsg`, `handleDraw`, `handlePlayMode`, `handleTeam`, `handleServerParam`, `handlePlayerParam`, `handlePlayerType`, `handleTeamGraphic`, `handleEOF`).
- `rcsc::rcg::Parser::create(stream)` / `rcsc::rcg::Serializer::create(version)` — factory entry points used identically across all tools and in `example/loader_main.cpp`, `example/result_writer_main.cpp`.
- `rcsc::ParamMap` + `rcsc::CmdLineParser` / `rcsc::ConfFileParser` — generic option-declaration framework (`rcsc/param/`), demonstrated end-to-end in `example/param_main.cpp` (declares `iparam`/`bparam`/`switch_param`, parses a `.conf` file then overrides from `argv`).

## Integration Points
- `example/` is the **canonical onboarding sample set** for the low-level client API surfaces:
  - `gzifstream_main.cpp` / `gzofstream_main.cpp` — minimal transparent gzip read/write via `rcsc::gzifstream`/`gzofstream` (`rcsc/gz/gzfstream.h`).
  - `loader_main.cpp` — implements the **older** `rcsc::rcg::Holder` interface (`addShowInfo`, `addShowInfo2`, `addShortShowInfo2`, `addMsgInfo`, `addDrawInfo`, `addPlayMode`, `addTeamInfo`, ...) — legacy v1-v3 push-style API, distinct from the newer `Handler`-based tools in `src/`.
  - `result_writer_main.cpp` + `result_writer.cpp/.h` — shows `rcsc::rcg::make_parser(stream)` + a custom `ResultWriter` handler; minimal skeleton to copy when writing a new rcg consumer.
  - `param_main.cpp` — canonical `ParamMap`/`CmdLineParser`/`ConfFileParser` demo; reads `test.conf` (sample config alongside `rcssserver-player.conf`, `rcssserver-server.conf`) then overrides from `argv`.
  - `sample.out` — captured expected output for comparison/regression when modifying example programs.

## Build & Test
- Built via Automake: [Makefile.am](../../src/Makefile.am) — `bin_PROGRAMS` lists the 10 installed tools (`rclmscheduler`, `rclmtableprinter`, `rcg2csv`, `rcg2txt`, `rcgrenameteam`, `rcgresultprinter`, `rcgreverse`, `rcgvalidator`, `rcgverconv`, `rcgversion`); `noinst_PROGRAMS = object_table_printer` (built but not installed, dev utility only).
- All tools link `-lrcsc $(BOOST_SYSTEM_LIB)` against `$(top_builddir)/rcsc` — i.e. they depend on the library being built first.
- `.in` files (`rcg3to4.in`, `rcg4to3.in`) are processed by `configure` (substituting `@libdir@`) into runnable shell scripts at build time — not linked/compiled.
- No dedicated unit tests for these CLI tools; validate manually by round-tripping a sample `.rcg`/`.rcg.gz` through `rcgverconv`/`rcgvalidator` and diffing `rcg2txt` output.

## Logging
- No shared logging framework — all tools write directly to `std::cerr` (usage/errors/progress) and `std::cout`/file streams (data output). Compare with `world-model-and-agent-core.instructions.md` for the client agent's structured debug-logging conventions, which do **not** apply here.

## Important Notes
- `rcg4to3.in`'s `usage()` text is copy-pasted from `rcg3to4.in` and still prints "Usage: rcg3to4 ..." — cosmetic bug, does not affect behavior.
- `rcgversion_CXXFLAGS` in Makefile.am (line 74) is misnamed — it's actually set under `rcgverconv_SOURCES`, likely a copy-paste typo in the build file (harmless since flags are the same across tools).
- `object_table_printer` has no CLI arguments and is meant to be run manually then its stdout pasted into `server_param.cpp`; it's a code-generation aid, not a log tool.
- Always check `.rcg.gz` support — every rcg tool opens input through `rcsc::gzifstream`, so gzipped logs work transparently without a separate flag.

## See Also
- `rcg-log-replay.instructions.md` — full `rcsc::rcg::Handler`/`Parser`/`Serializer` API and `.rcg` binary format details referenced by every tool above.
- `client-framework.instructions.md` — how the same parsing/handler patterns are used inside the live agent client (contrast with these offline batch tools).

---
Part of: [`librcsc copilot-instructions.md`](../copilot-instructions.md)
