---
applyTo: 'rcsc/common/**,rcsc/net/**'
---

# Client & Agent Network Framework

## TL;DR
`AbstractClient` + `SoccerAgent` form librcsc's transport/agent contract; `OnlineClient` (real UDP via `select()`) and `OfflineClient` (log replay) are the two concrete transports, and every concrete agent (player/coach/trainer) subclasses `SoccerAgent` and drives whichever client it's handed.
- `AbstractClient::run(SoccerAgent*)` is the mainloop entry point — pure virtual, implemented once per transport.
- `ServerParam` is a lazy singleton (`ServerParam::instance()`) populated ONLY by parsing the server's `(server_param ...)` message — it holds no independently configured values.
- Say/hear messages use a pluggable `SayMessageParser`/header-character registry, decoupled from the transport layer.
- **Open the full file when:** you need exact select()/timeout semantics ([online_client.cpp](../../rcsc/common/online_client.cpp)), the full ~500-field `ServerParam` param table ([server_param.cpp](../../rcsc/common/server_param.cpp)), or a specific say-message encoder ([say_message_parser.h](../../rcsc/common/say_message_parser.h)).

## Overview
librcsc's client framework is the direct counterpart to rcssserver's `RemoteClient` (`rcssserver/src/remoteclient.h`). Where the server owns one `RemoteClient` per connected UDP peer (non-blocking, optional gzip streambuf swap), librcsc owns one `AbstractClient` per agent process, wrapping a single UDP socket (or, for `OfflineClient`, a log file) and bridging raw datagrams to a concrete `SoccerAgent` subclass (`PlayerAgent`, `CoachAgent`, `TrainerAgent` — all outside `rcsc/common`, in `rcsc/player`, `rcsc/coach`, `rcsc/trainer`).

## Architecture
```
SoccerAgent (abstract)                AbstractClient (abstract)
  M_client: shared_ptr<AbstractClient>   M_compressor / M_decompressor (GZip)
  init(), createConsoleClient()          M_sent_message / M_received_message
  handleStart/Message/Timeout/Exit()     run(agent)=0, connectTo()=0
        ^  friend class AbstractClient        ^
        |                                     |
  PlayerAgent / CoachAgent / TrainerAgent   OnlineClient        OfflineClient
  (rcsc/player,coach,trainer)               (UDPSocket, select) (ifstream replay)
```
- [common/soccer_agent.h:54](../../rcsc/common/soccer_agent.h:54) `class SoccerAgent` — `friend class AbstractClient` (line 56) lets the client call the agent's protected `handleStart/handleMessage/handleTimeout/handleExit` hooks directly without exposing them publicly. `createConsoleClient()` (line 104) is pure virtual — each concrete agent decides whether to build an `OnlineClient` or `OfflineClient` from CLI args.
- [common/abstract_client.h:48](../../rcsc/common/abstract_client.h:48) `class AbstractClient` — pure virtuals `run()` (line 107), `connectTo()` (116), `sendMessage()` (125), `receiveMessage()` (133), `openOfflineLog()` (141), `printOfflineThink()` (147). Protected non-virtual helpers `handleStart/handleMessage/handleTimeout/handleExit` (228-261) simply forward to the matching `SoccerAgent` method — this indirection is how the transport stays agent-agnostic.
- [common/online_client.h:52](../../rcsc/common/online_client.h:52) `class OnlineClient : public AbstractClient` — holds `shared_ptr<UDPSocket> M_socket` and an `ofstream M_offline_out` (dual-purpose: can log live traffic to a file while playing).
- [common/offline_client.h:45](../../rcsc/common/offline_client.h:45) `class OfflineClient : public AbstractClient` — holds only an `ifstream M_offline_in`; `connectTo()`/`sendMessage()` are no-ops (see impl notes below).

### OnlineClient::run() mainloop — [common/online_client.cpp:80-137](../../rcsc/common/online_client.cpp:80)
1. `handleStart(agent)` → agent connects + sends init; if it fails or `!isServerAlive()`, calls `handleExit` and returns immediately.
2. Builds an `fd_set` once around `M_socket->fd()`.
3. Loops `while (isServerAlive())`: `select()` with timeout = `intervalMSec()` (settable via `AbstractClient::setIntervalMSec`).
   - `ret == 0` → timeout: increments `timeout_count`/`waited_msec`, calls `agent->handleTimeout(...)`.
   - `ret > 0` → resets counters, calls `agent->handleMessage()`.
   - `ret < 0` → `perror("select")`, breaks loop.
4. On loop exit, always calls `handleExit(agent)`.
- `connectTo()` (144) constructs `UDPSocket(hostname, port)`; failure sets `setServerAlive(false)`.
- `sendMessage()` (167) calls `compress(msg)` (gzip if `M_compression_level` set) then `M_socket->writeDatagram(...)`.
- `receiveMessage()` (194) reads into a static `MAX_MESG=8192` buffer, calls `decompress()`, and — if `openOfflineLog()` was called — echoes the decompressed text to `M_offline_out` for later replay.

### OfflineClient — [common/offline_client.h](../../rcsc/common/offline_client.h)
Same interface, degenerate transport: `connectTo()` always returns true (no real socket), `sendMessage()` is a no-op returning 1, `receiveMessage()` reads one line at a time from `M_offline_in` (opened via `openOfflineLog(filepath)`, which for this class is the *input* log rather than an output log as in `OnlineClient`). `run()` is a tight loop with no waiting — "consumes computational resources as much as possible" per its own doc comment. This is the debugging/replay tool: point it at a `.log`/offline dump captured by `OnlineClient::openOfflineLog()` from a prior live game.

## Patterns & Conventions
- **Non-copyable base classes**: both `AbstractClient` and `SoccerAgent` privately declare copy ctor/assign (no `= delete` yet on these two — older style; contrast with `SayMessageParser` which uses `= delete`, showing the codebase mid-migration to modern C++).
- **shared_ptr for lifetime, raw pointer for callbacks**: `SoccerAgent::M_client` is a `shared_ptr<AbstractClient>`, but `AbstractClient::run(SoccerAgent* agent)` takes a raw pointer — ownership flows one way (agent owns client), calls flow the other way (client drives agent).
- **Protocol version threading**: no version field lives on `AbstractClient` itself; version is a property of the *agent's config* (`agent_.config().version()`) and is passed explicitly into every version-sensitive parse call, e.g. `ServerParam::instance().parse(msg, agent_.config().version())`.
- **Compression is opt-in per-message-direction**: `AbstractClient::compress()`/`decompress()` (protected, common to both transports) use `GZCompressor`/`GZDecompressor`; `setCompressionLevel(level)` toggles it — mirrors rcssserver's `(compression <lvl>)` command which triggers `RemoteClient`'s gzip streambuf swap server-side.

## Key Abstractions
- **`ServerParam`** — [common/server_param.h:55](../../rcsc/common/server_param.h:55), singleton via `ServerParam::instance()` / `ServerParam::i()` ([server_param.h:691-703](../../rcsc/common/server_param.h:691)). Constructed with `M_param_map(new ParamMap("server_param"))` ([server_param.cpp:386-389](../../rcsc/common/server_param.cpp:386)); every field is registered into `M_param_map` via a long fluent chain of `.add()`/operator() calls (e.g. lines 980-1003) mapping server-side param name strings (`"foul_detect_probability"`, `"illegal_defense_duration"`, etc.) directly to `ServerParam` member pointers — **these names match rcssserver's `serverparam.cpp` param-map keys 1:1**, confirming this is a mirror, not an independent config.
  - `ServerParam::parse(const char* msg, const double& version)` ([server_param.cpp:1010-1032](../../rcsc/common/server_param.cpp:1010)): for `version >= 8.0` delegates to `RCSSParamParser` which fills `M_param_map` generically (name+value pairs, forward/backward compatible with new server params); for older protocol falls back to positional `parseV7(msg)` which has zero param names, just an ordered list of ~90 positional values (see comment block at 1041-1060) — a real "if server version < 8, params must be read positionally" gotcha.
  - **Never independently configured**: it is populated exclusively by this `parse()` call, invoked from the agent's own message dispatcher — e.g. [player/player_agent.cpp:2027](../../rcsc/player/player_agent.cpp:2027) `ServerParam::instance().parse(msg, agent_.config().version())`, and identically from `coach_agent.cpp:1396` and `trainer_agent.cpp`. `PlayerParam::instance().parse(...)` follows the exact same singleton pattern for player-type params.
- **Version handshake**: agent sends `(init <TeamName> (version N) ...)` via `PlayerInitCommand`/`PlayerReconnectCommand` in `PlayerAgent::Impl::sendInitCommand()` ([player/player_agent.cpp:1284-1318](../../rcsc/player/player_agent.cpp:1284)) — called from `AbstractClient::handleStart()`'s trigger path, NOT from `run()` directly (per `SoccerAgent::init()`'s doc comment: "(init) command is sent in run() method. Do not call it yourself!"). The server's reply `(init <side> <unum> <mode>)` is parsed by `PlayerAgent::Impl::analyzeInit()` ([player_agent.cpp:2060-2092](../../rcsc/player/player_agent.cpp:2060)) via `sscanf(msg, "(init %c %d %127[^)]", ...)`; only after this round-trip does the client become "fully connected" (matches rcssserver's `InitSenderPlayer`/`InitSenderCoach` handshake gate).
- **`SayMessageParser`** — [common/say_message_parser.h:50](../../rcsc/common/say_message_parser.h:50), abstract; concrete subclasses each own one `header()` char (a single-character tag prefixing the encoded audio payload) and a `parse(sender, dir, msg, current)` method. `Ptr = shared_ptr<SayMessageParser>`. Companion files: `audio_message.h` (message payload types), `audio_codec.h/.cpp` (bit-packing/encoding), `audio_memory.h/.cpp` (per-agent heard-message history, consumed by `WorldModel`/agent decision code — out of scope here, see `world-model-and-agent-core.instructions.md`).

## Integration Points
- **rcssserver (sibling repo) `RemoteClient`** (`rcssserver/src/remoteclient.h`) — server-side non-blocking UDP peer, the mirror image of `OnlineClient`; both honor the same `(compression <lvl>)` protocol toggle.
- **rcssserver `ServerParam`** (`rcssserver/src/serverparam.cpp`) — authoritative source of every constant `librcsc::ServerParam` mirrors; sent once at connection time via `InitSenderPlayer`/`InitSenderCoach` (see rcssserver's `monitor-protocol`/`config-params` instructions for the send side).
- **`rcsc/player`, `rcsc/coach`, `rcsc/trainer`** — concrete `SoccerAgent` subclasses that own the actual `(init ...)` handshake, message-type dispatch (`"(init "`, `"(server_param "`, `"(see "`, `"(hear "`, etc. string-prefix checks), and drive `ServerParam`/`PlayerParam` population. This instructions file covers the *framework*; those directories cover the *concrete agents*.
- **`rcsc/net`** — `abstract_socket.{h,cpp}` (common socket setup/errno handling), `udp_socket.{h,cpp}` (`class UDPSocket`, `writeDatagram()`/`readDatagram()` overloads used by `OnlineClient`), `host_address.{h,cpp}` (hostname/IP resolution used by `connectTo()`), `tcp_socket.{h,cpp}` (present but unused by the player/coach/trainer clients documented here — likely reserved for monitor-style tooling).

## Build & Test
Part of the standard librcsc CMake/Autotools build (`rcsc/common/CMakeLists.txt`, `rcsc/net/CMakeLists.txt`); no dedicated unit tests found for `AbstractClient`/`OnlineClient`/`OfflineClient` — the clang parser has `rcsc/clang/test_clang_parser.cpp` (CppUnit) as a model if tests were to be added for this layer.

## Logging
No structured logger in this layer — `OnlineClient::run()` uses `perror("select")` on syscall failure and `std::cerr`/`std::cout` diagnostics scattered in `connectTo()` and `PlayerAgent::Impl::sendInitCommand()`. The `OfflineClient`/`openOfflineLog()` mechanism doubles as a lightweight "traffic logger": `OnlineClient::openOfflineLog(filepath)` records every decompressed inbound message plus synthetic `(think)` markers (`printOfflineThink()`), producing a log file that `OfflineClient` can later replay byte-for-byte.

## Important Notes
- `ServerParam`/`PlayerParam` are process-wide singletons — do not assume per-agent isolation if multiple agents run in one process (uncommon here, but a real gotcha if ever attempted).
- v7 vs v8+ protocol branching in `ServerParam::parse()` is easy to miss when adding new params — new fields must go in the `M_param_map` chain (v8+ path); the `parseV7` positional path is effectively frozen/legacy.
- `AbstractClient::handleStart`/`handleMessage`/etc. being `protected` + `friend`ed rather than public is intentional encapsulation — do not bypass by making agent hooks public when subclassing.
- `OnlineClient`'s `M_offline_out` and `OfflineClient`'s `M_offline_in` are easy to confuse by name (`openOfflineLog` exists on both, with reversed direction) — check which class you're in before assuming read vs. write semantics.

## See Also
- `world-model-and-agent-core.instructions.md` — `WorldModel`, `AudioMemory` consumption of parsed say/hear data, decision-making loop.
- `coach-trainer-clang.instructions.md` — `CoachAgent`/`TrainerAgent` specifics, CLang advice messages layered on top of this same client framework.
- `geometry.instructions.md` — vector/angle primitives used throughout parsed sensory messages.
- Cross-repo: rcssserver's `networking-io.instructions.md` (RemoteClient, UDP transport) and `config-params.instructions.md` (ServerParam authoritative source) are the server-side counterparts to this document; `monitor-protocol.instructions.md` documents the init/handshake message formats from the server's sending side.

---
Part of: [`librcsc copilot-instructions.md`](../copilot-instructions.md)
