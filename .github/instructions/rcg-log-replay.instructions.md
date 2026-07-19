---
applyTo: 'rcsc/rcg/**,rcsc/gz/**'
---

# RCG Log Format & Compressed I/O

## TL;DR
librcsc ships its own RCG (game-log) parser/serializer library under `rcsc/rcg/` plus zlib-based stream wrappers under `rcsc/gz/`, sharing lineage (class names, factory pattern) with rcssmonitor's own `rcss/rcg/` library but built as `rcsc::rcg` (namespace `rcsc`, not `rcss`).
- Version dispatch is done by sniffing the 4-byte header (`'U','L','G',<ver>` or `'['` for JSON) in [`Parser::create`](../../rcsc/rcg/parser.cpp:67-122).
- `Handler` ([handler.h](../../rcsc/rcg/handler.h:49)) is the abstract sink interface — old-format handlers (v1-v3) auto-convert to intermediate types, v4+ handlers are pure-virtual (`handleShow`, `handleMsg`, etc.).
- `rcsc::rcg::ParserV4` covers rcg file-format versions 4, 5, **and** 6 — same "one class, several file versions" pattern documented in rcssmonitor's rcg library.
- **Vendored third-party**: `rcsc/rcg/simdjson/` (simdjson.h/.cpp, ~80k+38k lines) and `rcsc/rcg/nlohmann/json.hpp` are bundled JSON libraries used only by `ParserSimdJSON`/`ParserJSON` — not documented here, treat as opaque dependencies.
- **Open the full file when:** modifying version-dispatch logic in `parser.cpp`, adding a new rcg version, or debugging binary struct layout in `types.h` (1713 lines, all wire-format structs).

## Overview
`rcsc/rcg/` is librcsc's in-process library for reading and writing RoboCup Soccer Simulator game-log (`.rcg`) files — the same on-disk format written by rcssserver's `Logger`/`Recorder` classes and read by rcssmonitor for replay. Unlike rcssserver (which only *writes* logs) and rcssmonitor (which only *reads* them for a GUI), librcsc's copy supports **both** parsing (`Parser`/`ParserV1..V4`/`ParserSimdJSON`) and serialization (`Serializer`/`SerializerV1..V6`), because librcsc is a client-side agent library that may need to record its own logs or replay a log for offline analysis/training tools.

`rcsc/gz/` provides gzip-compressed stream classes (`gzifstream`/`gzofstream`, `gzifilterstream`/`gzofilterstream`) so callers can transparently read/write `.rcg.gz` files through standard `std::istream`/`std::ostream` interfaces, mirroring rcssserver's `rcss::gz::gzofstream` used by its `Logger`.

## Architecture

### Parsing (read path)
- [`Parser`](../../rcsc/rcg/parser.h:51) — abstract base, `parse(istream&, Handler&)` pure virtual, plus a convenience `parse(filepath, Handler&)` overload ([parser.cpp:126](../../rcsc/rcg/parser.cpp:126)) that opens an `ifstream` and delegates.
- `Parser::create(istream&)` ([parser.cpp:67](../../rcsc/rcg/parser.cpp:67)) reads the first 4 bytes, decides the version, then either looks it up in the `Creators` factory (`rcss::Factory<Creator,int>`, keyed by version int) or falls back to hardcoded `new ParserV4()`/`ParserV3()`/`ParserV2()`/`ParserV1()`/`ParserSimdJSON()`.
- Concrete parsers: `ParserV1` (rcg v1, `.rcg` original binary format), `ParserV2` (v2), `ParserV3` (v3), `ParserV4` (handles v4, v5, **and** v6 — see [parser_v4.cpp](../../rcsc/rcg/parser_v4.cpp)), `ParserJSON`/`ParserSimdJSON` (the `-1`/`REC_VERSION_JSON` text format, using the bundled JSON libs).
- Version constants live in [types.h:137-1708](../../rcsc/rcg/types.h): `REC_OLD_VERSION=1`, `REC_VERSION_2=2`, `REC_VERSION_3=3`, `REC_VERSION_4=4`, `REC_VERSION_5=5`, `REC_VERSION_6=6`, `REC_VERSION_JSON=-1`, `DEFAULT_LOG_VERSION=REC_VERSION_6`.

### Serialization (write path)
- [`Serializer`](../../rcsc/rcg/serializer.h:51) — mirror-image abstract base: `Serializer::creators()` / `Serializer::create(int version)` use the same `rcss::Factory<Creator,int>` pattern.
- Concrete serializers: `SerializerV1` .. `SerializerV6` (one per rcg file-format version) plus `SerializerJSON` — note there is **no** `SerializerV5`/`SerializerV6` merge like the parser side; each version gets its own serializer class/file (`serializer_v1.cpp` … `serializer_v6.cpp`).

### Handler interface
[`Handler`](../../rcsc/rcg/handler.h:49) is the visitor/sink object passed into `Parser::parse`. It carries shared state (`M_log_version`, `M_server_version`, `M_timestamp`, `M_read_time`) and exposes two API generations:
- **Legacy (v1-v3) handlers** — concrete (non-virtual) convenience methods like `handleDispInfo`, `handleShowInfo`, `handleShowInfo2`, `handleShortShowInfo2`, `handleMsgInfo`, `handleDrawInfo`, `handlePlayMode(char)`, `handleTeamInfo`, `handlePlayerType(player_type_t&)`, `handleServerParam(server_params_t&)` — these auto-convert raw binary structs into the intermediate `*T` types (see `util.h` converters) then dispatch to the modern handlers below.
- **Modern (v4+) handlers** — pure virtual: `handleShow(const ShowInfoT&)`, `handleMsg(int,int,const std::string&)`, `handleDraw(int,const drawinfo_t&)`, `handlePlayMode(int,PlayMode)`, `handleTeam(int,const TeamT&,const TeamT&)`, `handleServerParam(const ServerParamT&)`, `handlePlayerParam(const PlayerParamT&)`, `handlePlayerType(const PlayerTypeT&)`, `handleTeamGraphic(char,int,int,const std::vector<std::string>&)`, `handleEOF()`. Any concrete `Handler` subclass must implement all of these.

### Compressed I/O (`rcsc/gz/`)
- [`gzfstream.h`](../../rcsc/gz/gzfstream.h) — `gzfilebuf` (streambuf over zlib, Pimpl'd), `gzifstream`/`gzofstream` ([:302](../../rcsc/gz/gzfstream.h:302), [:369](../../rcsc/gz/gzfstream.h:369)) — drop-in `ifstream`/`ofstream` replacements for `.gz` files. No seek/putback support (documented in the class comment).
- [`gzfilterstream.h`](../../rcsc/gz/gzfilterstream.h) — `gzfilterstreambuf`, `gzfilterstream`, `gzifilterstream`/`gzofilterstream` ([:247](../../rcsc/gz/gzfilterstream.h:247), [:301](../../rcsc/gz/gzfilterstream.h:301)) — filters an *existing* stream (e.g. wrap a socket or another streambuf) rather than opening a named file directly; used where the underlying transport isn't a plain file.
- [`gzcompressor.h`](../../rcsc/gz/gzcompressor.h) — standalone `GZCompressor`/`GZDecompressor` ([:44](../../rcsc/gz/gzcompressor.h:44), [:93](../../rcsc/gz/gzcompressor.h:93)) buffer-to-buffer (de)compression helpers, independent of the stream classes — useful for one-shot in-memory compress/decompress rather than streaming file I/O.

## Patterns & Conventions
- **Factory-based version dispatch**: both `Parser` and `Serializer` use `rcss::Factory<Creator,int>` (shared template from [`rcsc/factory.h`](../../rcsc/factory.h) — note it lives in namespace `rcss`, reused from the server codebase's factory idiom even though this is the librcsc/agent-side library). Keyed by the integer rcg version. New versions register a `Creator` (a `Ptr(*)()` function pointer) into `Parser::creators()`/`Serializer::creators()`.
- **Header sniffing, not extension checking**: version is determined purely from the first 4 bytes of the stream content (`'U','L','G',<version-char>` or `'['` for JSON) — never from file extension. See `Parser::create` at [parser.cpp:70-90](../../rcsc/rcg/parser.cpp:70).
- **Binary struct + intermediate type duality**: raw wire-format structs (`showinfo_t`, `showinfo_t2`, `short_showinfo_t2`, `team_t`, `player_type_t`, `server_params_t`, `player_params_t`, `dispinfo_t`, `dispinfo_t2`) are always paired with a modern intermediate type (`ShowInfoT`, `TeamT`, `PlayerTypeT`, `ServerParamT`, `PlayerParamT`, `BallT`, `PlayerT`, `drawinfo_t`). All conversions funnel through overloaded free functions named `convert(...)` in [util.h](../../rcsc/rcg/util.h) / `util.cpp`.
- **Network byte order helpers**: `nstohi`, `hitons`, `nstohd`/`nstohf`, `hdtons`/`hftons`, `nltohd`/`nltohf`, `hdtonl`/`hftonl`, `nstonl`/`nltons` in `util.h` convert between network-order shorts/longs and host floats/ints/bools, scaled by `SHOWINFO_SCALE`/`SHOWINFO_SCALE2` — required because the binary rcg formats (v1-v3) store coordinates as fixed-point network-order integers.
- **PImpl for zlib details**: `gzfilebuf::Impl` hides zlib internals from the public header (`gzfstream.h`), keeping `<zlib.h>` out of consumers' include path.

## Key Abstractions
| Type | File | Role |
|---|---|---|
| `Parser` / `Parser::Creators` | [parser.h](../../rcsc/rcg/parser.h) | Abstract read dispatcher, version-keyed factory |
| `ParserV1`/`V2`/`V3`/`V4` | `parser_v{1,2,3,4}.{h,cpp}` | Concrete binary parsers (V4 handles rcg v4/5/6) |
| `ParserJSON` / `ParserSimdJSON` | `parser_json.*`, `parser_simdjson.*` | Text/JSON rcg parsers (nlohmann vs simdjson backends) |
| `Serializer` / `Serializer::Creators` | [serializer.h](../../rcsc/rcg/serializer.h) | Abstract write dispatcher |
| `SerializerV1`..`V6`, `SerializerJSON` | `serializer_v{1..6}.*`, `serializer_json.*` | One concrete writer class per format version |
| `Handler` | [handler.h](../../rcsc/rcg/handler.h) | Pull-parser callback/visitor sink, legacy + modern APIs |
| `types.h` structs/enums | [types.h](../../rcsc/rcg/types.h) (1713 lines) | Wire-format structs, `DispInfoMode`, `DrawMode`, `MsgInfoMode`, `PlayerStatus`, version constants |
| `gzifstream`/`gzofstream` | [gzfstream.h](../../rcsc/gz/gzfstream.h) | File-based gzip stream (drop-in `ifstream`/`ofstream`) |
| `gzifilterstream`/`gzofilterstream` | [gzfilterstream.h](../../rcsc/gz/gzfilterstream.h) | Wraps an existing stream/streambuf with gzip filtering |
| `GZCompressor`/`GZDecompressor` | [gzcompressor.h](../../rcsc/gz/gzcompressor.h) | One-shot in-memory buffer (de)compression |

## Integration Points
- Consumers construct a `Handler` subclass (e.g. an agent-side log analyzer or replay tool), open a stream (plain `ifstream` or `gzifstream`/`gzifilterstream` for `.rcg.gz`), and call `Parser::create(is)` then `parser->parse(is, handler)`.
- To write a log, call `Serializer::create(version)` and drive it directly (see `serializer.cpp` base class helpers around `M_playmode`/`M_teams[2]` temp state at [serializer.h:76-77](../../rcsc/rcg/serializer.h:76)).
- `rcsc/factory.h`'s `rcss::Factory` template is shared infrastructure — same factory idiom rcssserver uses for its own versioned registries (`rcss::Factory` there keys `Logger`/protocol savers).
- `util.h` `convert()` overloads are the seam between binary (`*_t`) and intermediate (`*T`) representations — any new consumer needing v1-v3 compatibility must go through these, not hand-roll conversions.

## Build & Test
- Both directories build as part of the librcsc static/shared library via `CMakeLists.txt`/`Makefile.am` in each folder (`rcsc/rcg/CMakeLists.txt`, `rcsc/gz/CMakeLists.txt`) — no standalone test binaries found in these folders; verify with the library's top-level test suite if present.
- `rcsc/rcg/simdjson/` and `rcsc/rcg/nlohmann/json.hpp` are vendored, unmodified upstream sources — do not edit; update by re-vendoring the upstream release.

## Logging
- Parser errors are reported to `std::cerr` directly (no logging framework) — e.g. `Parser::create` prints `"(rcsc::rcg::Parser::create) no header."` or the detected version string ([parser.cpp:77](../../rcsc/rcg/parser.cpp:77), [parser.cpp:92-104](../../rcsc/rcg/parser.cpp:92)). There is no structured/leveled logging in this subsystem.

## Important Notes
- **File-format version ≠ monitor-protocol version.** Exactly as documented in rcssmonitor's rcg-format-library instructions: `REC_VERSION_4/5/6` are on-disk **file** format numbers, not the live monitor wire-protocol version — don't assume a `ParserV4`-produced `ShowInfoT` corresponds 1:1 to "monitor protocol v4".
- **`ParserV4` silently covers 3 file versions** (4, 5, 6) via internal branching — when debugging a parse issue, check *which* sub-version is active inside `parser_v4.cpp`, not just that `ParserV4` was selected.
- **`rcsc::rcg::Serializer` has no combined v4/5/6 class** — unlike the parser side, each version gets its own serializer (`SerializerV4`, `SerializerV5`, `SerializerV6`) — do not assume read/write dispatch is symmetric.
- **Namespace is `rcsc::rcg`**, not `rcss::rcg` (rcssmonitor) or a bare top-level — watch for accidental cross-includes when working in a workspace that has both libraries checked out.
- **Do not document or modify `simdjson/`, `nlohmann/json.hpp`** — vendored third-party, huge (80k+38k lines), unrelated to librcsc's own commit history.
- **gz classes have no seek/putback support** — explicitly called out in `gzfilebuf`'s class comment ([gzfstream.h:46-49](../../rcsc/gz/gzfstream.h:46)); code that needs to rewind a `.rcg.gz` stream must re-open it instead.

## See Also
- Cross-repo: [`rcssserver/.github/instructions/logging-and-savers.instructions.md`](../../../rcssserver/.github/instructions/logging-and-savers.instructions.md) — the writer side of this same wire format (`Logger`, `rcss::Factory`-based versioned savers, `rcss::gz::gzofstream`). librcsc's `rcsc/gz/` is directly analogous to rcssserver's `rcss/gzip/` gzstream wrappers (same zlib-streambuf idiom, different namespace/class names).
- Cross-repo: [`rcssmonitor/.github/instructions/rcg-format-library.instructions.md`](../../../rcssmonitor/.github/instructions/rcg-format-library.instructions.md) — rcssmonitor's own `rcss/rcg/` parser library. Same lineage/shared history as librcsc's `rcsc/rcg/` (matching class names `Parser`/`ParserV1..V4`/`Handler`, matching version-merge pattern where one `ParserV4` class covers file versions 4/5/6, and the same documented file-format-vs-protocol-version distinction) — but librcsc's copy additionally includes a full `Serializer` hierarchy (write side) and a `ParserSimdJSON` backend, which rcssmonitor may not have identically.

---
Part of: [`librcsc copilot-instructions.md`](../copilot-instructions.md)
