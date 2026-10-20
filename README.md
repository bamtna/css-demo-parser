# css-demo-parser

Parser for Counter-Strike: Source demos (`HL2DEMO`, demo protocol 3, network protocol 24; the layout differences the engine has for protocols 14 to 23 are handled but untested). Usable as a C++20 library or through the `css-demo-parser` command line tool. Builds on Windows, Linux and macOS.

Decodes everything in the demo:

- demo frames (packets, console commands, user commands, data tables, string tables)
- every `net_*` / `svc_*` message
- send tables and server classes, string tables (including Snappy/LZSS compressed ones), instance baselines
- entities: full `svc_PacketEntities` decoding with entity state tracking
- game events, user messages (chat, hints, radar, damage, votes, stats, ...), temp entities, sounds, the recording player's user commands

It also writes all of it back: every message, frame and table can be serialised again, and decoded entities, temp entities, user messages, game events, sounds and string tables encode to the same bits the server sent.

Both client demos (recorded by a player) and SourceTV demos are supported. The differences that matter:

| | client | SourceTV |
| --- | --- | --- |
| recording player's origin | full float (`cslocaldata.m_vecOrigin`) | n/a |
| other players' origin | 1/32 unit (`csnonlocaldata.m_vecOrigin`) | 1/32 unit, for everyone |
| velocity | networked for the recording player | not networked, `PlayerTracker` estimates it from origin deltas |
| user commands (buttons, exact view angles) | yes | no |
| snapshot rate | every tick | `tv_snapshotrate` |

`demo_kind()` tells you which one you have, and `player_snapshots()` / `PlayerTracker` pick the right props for you.

## Building

```
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release
```

The tests parse and re-serialise every `.dem` in `demos/` if that folder exists (set `CSSDEMO_TEST_DEMO_DIR` to point them elsewhere, `;` separated).

## Command line

```
css-demo-parser <demo_file_path> [options]

  -o <path>         Output file (default: <demo>_dump.txt, - for stdout)
  --summary         Only the header, server, players and errors, no messages
  --entities        Include every entity property change
  --tempentities    Include temp entity events
  --sounds          Include sounds
  --usercmds        Include the recording player's user commands
  --datatables      Include server classes and their networked props
  --stringtables    Include the contents of every string table
  --all             Include all of the above
```

Exits with 2 if anything failed to decode; the errors are listed at the end of the dump.

## Library

Add it to a CMake project and link `cssdemo::cssdemo`:

```cmake
add_subdirectory(css-demo-parser)
# or
include(FetchContent)
FetchContent_Declare(cssdemo GIT_REPOSITORY https://github.com/bamtna/css-demo-parser.git GIT_TAG master)
FetchContent_MakeAvailable(cssdemo)

target_link_libraries(your_target PRIVATE cssdemo::cssdemo)
```

The CLI, examples and tests are only built when it's the top level project.

Everything is in the `cssdemo` namespace; `cssdemo/cssdemo.h` includes all of it. `DemoParser` reads the demo frame by frame, keeps the game state up to date and calls any `DemoListener`s you add:

```cpp
#include "cssdemo/cssdemo.h"

struct Kills : cssdemo::DemoListener {
    void on_game_event(const cssdemo::GameEvent& event) override {
        if (event.name == "player_death")
            std::cout << event.get_int("attacker") << " killed " << event.get_int("userid") << "\n";
    }
};

cssdemo::DemoFile demo("match.dem");
cssdemo::DemoParser parser(demo);
Kills kills;
parser.add_listener(kills);
parser.run();
```

Callbacks: `on_header`, `on_frame`, `on_net_message`, `on_packet_end`, `on_user_cmd`, `on_data_tables`, `on_string_table_change`, `on_game_event`, `on_user_message`, `on_entity_update`, `on_temp_entity`, `on_sound`, `on_error`, `on_finish`. `parser.state()` is the live `GameState` (server info, data tables, string tables, entities, players) and can be read from any callback. `on_packet_end` fires once all of a packet's messages have been applied, so it's the place to sample state per tick.

### Writing

`DemoWriter` writes frames, `update_header` fills in the playback ticks, time, frames and signon length. Packets are lists of `NetMessage`s, each with a `write` that mirrors the engine's `WriteToBuffer`:

```cpp
cssdemo::DemoWriter writer(header);
cssdemo::NetTick tick;
tick.tick = 1000;
std::vector<const cssdemo::NetMessage*> messages = { &tick, &entities };
writer.write_packet(cssdemo::DemoCommand::Packet, 1000, cmd_info, 0, 0, cssdemo::write_packet(messages, protocol));
writer.write_user_cmd(1000, 1, cssdemo::encode_user_cmd(cmd));
writer.write_stop(1001);
writer.update_header(0.015f);
writer.save("out.dem");
```

The payload encoders are the inverses of the decoders:

| | |
| --- | --- |
| `encode_prop`, `write_prop_changes`, `diff_props` | props and delta field lists |
| `write_entity_updates` | `svc_PacketEntities` data from enter/delta/leave/delete updates and explicit deletions |
| `write_string_table_update`, `make_create_string_table`, `make_update_string_table`, `write_string_table_snapshot` | string tables (uncompressed), `write_player_info` for `userinfo` |
| `encode_temp_entities`, `encode_user_message`, `GameEventList::encode`, `encode_sounds`, `encode_user_cmd` | the rest |
| `DataTables::write`, `GameEventList::write` | `dem_datatables` and `svc_GameEventList` |

`BitWriter` is the counterpart of `BitReader`.

For player movement (e.g. converting to another replay format) use `PlayerTracker`, which records one `PlayerSample` per player per tick with origin, eye angles, velocity, flags, team, life state, duck state and, in client demos, the recording player's user command. See `examples/player_csv.cpp`.

## Layout

| Path | |
| --- | --- |
| `src/cssdemo/Util` | bit readers and writers, Snappy and LZSS |
| `src/cssdemo/Protocol` | engine constants and basic types |
| `src/cssdemo/Demo` | demo header and frames |
| `src/cssdemo/Net` | net messages |
| `src/cssdemo/Game` | data tables, props, entities, string tables, game events, user messages, sounds, temp entities, user commands, players |
| `src/cssdemo/Parser` | `DemoParser` and `DemoListener` |
| `src/cssdemo/Analysis` | `PlayerTracker` |
| `src/cssdemo/Output` | `TextDumper`, the listener behind the CLI |
| `src/main.cpp` | CLI |
| `examples/` | example tools |
| `tests/` | tests |
