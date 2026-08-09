# INTERCEPT v0.2

Realtime SA-MP RakNet packet/RPC monitor for the user's tested client target.

## Target

- Windows
- GTA San Andreas 1.0 US
- SA-MP 0.3.DL-R1
- 32-bit x86
- RakHook pinned to `109daae3031cd030d272fa35407412443f03dede`

## Current status

### Implemented

- RakHook packet/RPC observation
- Incoming/outgoing event capture
- Thread-safe bounded event queue
- Native Win32 realtime GUI
- `All`, `Packets`, and `RPC` tabs
- Text filtering across ID/name/details/HEX
- `Hide Sync` filter for high-frequency sync traffic
- Pause display updates
- Event counters and queue-drop counter
- Per-event inspector with raw hexadecimal payload
- Bounded in-memory history (20,000 displayed events)
- Reduced `INTERCEPT.log` noise: packet/RPC traffic is routed to the realtime event queue instead of logging every event

### Decoder status

The decoder layer currently provides conservative packet/RPC naming and metadata.
It should not be treated as a complete SA-MP protocol schema yet. Field-level
layouts will be added only after they are verified against the exact 0.3.DL-R1
traffic and RakHook interfaces used by this project.

## GUI

The GUI is intentionally native Win32 for the first realtime-monitor milestone.
It is created only after RakHook initializes successfully.

The main view contains:

```text
+---------------------------------------------------------------+
| Filter                         [Hide Sync] [Pause]             |
+---------------------------------------------------------------+
| [All] [Packets] [RPC]                                         |
|                                                               |
| #    Dir  Type    ID   Name          Bytes  Details           |
| ...                                                           |
|                                                               |
+---------------------------------------------------------------+
| Selected event / status / raw HEX                             |
+---------------------------------------------------------------+
```

The GUI is a monitor/inspection surface. It does not currently modify, block,
replay, or transmit captured traffic.

## Build

Use a 32-bit Visual Studio generator because SA-MP/RakHook is x86:

```bat
cmake -S . -B build -A Win32
cmake --build build --config Release
```

Output:

```text
build\Release\INTERCEPT.dll
```

## Loading

Load `INTERCEPT.dll` into the GTA SA process after `samp.dll` is available.
This repository intentionally does not include an injector.

## Runtime flow

```text
RakHook callback
      |
      v
bounded event queue
      |
      +----> realtime GUI
      |
      +----> counters
```

The queue is capped at 4096 pending events. If the GUI cannot consume events
fast enough, old pending events are discarded and the `Queue drops` counter
records the loss. This prevents high-frequency synchronization traffic from
causing unbounded memory growth.

The GUI keeps up to 20,000 consumed events for inspection. Filters affect only
display; they do not change the underlying counters.

## Logging

`INTERCEPT.log` is now intended primarily for lifecycle and diagnostic messages:

```text
[INFO] INTERCEPT v0.2 starting
[INFO] samp.dll detected.
[INFO] INTERCEPT monitor callbacks registered.
[INFO] RakHook initialized. attempt=1 samp_version=3
[INFO] Realtime monitor is active.
```

High-frequency packet/RPC payloads should be inspected through the GUI instead
of the text log.

## Safety / scope

This milestone is deliberately passive. The packet editor/repeater idea is not
implemented in v0.2. The immediate objective is reliable capture, filtering,
inspection, and protocol identification before adding any more invasive
functionality.

## Development roadmap

- [x] Passive RakHook capture
- [x] Realtime event queue
- [x] Packet/RPC GUI separation
- [x] Sync filtering
- [x] Raw payload inspector
- [ ] Verified field-level SA-MP decoders
- [ ] Better event classification and search
- [ ] Dashboard/rate graphs
- [ ] Persistent capture/export format

README status is updated as milestones actually land in the source tree; planned
features are kept separate from implemented features.
