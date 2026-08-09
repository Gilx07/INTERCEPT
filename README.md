# INTERCEPT v0.1

Passive SA-MP RakNet packet/RPC monitor.

## Target

- Windows
- GTA San Andreas 1.0 US
- SA-MP 0.3.DL-R1
- 32-bit x86 build

## What v0.1 does

It does **not** modify, block, replay, or send traffic.

It only observes:

- outgoing packets
- incoming packets
- outgoing RPC
- incoming RPC

Each event is written to `INTERCEPT.log` next to the loaded game executable/module location.

The log includes:

- direction
- packet/RPC type
- ID where available
- payload byte count
- payload bit count for BitStream events
- RakNet priority
- reliability
- ordering channel
- timestamp flag for outgoing RPC
- a bounded hexadecimal payload dump (up to 256 bytes)

## Why RakHook

This project deliberately uses RakHook instead of hooking `WSASend`/`WSARecv`.

RakHook exposes client-side SA-MP events at the RakNet layer and explicitly lists
SA-MP 0.3DL-R1 as a supported version.

Pinned revision:

`109daae3031cd030d272fa35407412443f03dede`

## Build

Install:

- Visual Studio 2022 with Desktop development with C++
- CMake 3.20+
- Git

Generate a 32-bit Visual Studio build:

```bat
cmake -S . -B build -A Win32
cmake --build build --config Release
```

The first configure requires internet access because CMake fetches RakHook and
its Cyanide dependency.

Output:

```text
build\Release\INTERCEPT.dll
```

## Loading

Load `INTERCEPT.dll` into the GTA SA process after `samp.dll` is available.

This project intentionally does not include an injector.

## Expected log

Example:

```text
[12:34:56.123] [INFO] samp.dll detected.
[12:34:56.400] [INFO] RakHook initialized. attempt=2 samp_version=...
[12:34:57.010] [INFO] OUT PACKET id=... bytes=...
[12:34:57.020] [INFO] OUT RPC id=... bytes=...
[12:34:57.100] [INFO] IN PACKET id=... bytes=...
[12:34:57.150] [INFO] IN RPC id=... bytes=...
```

## Important limitation of v0.1

The first version is intentionally a monitor, not a packet editor/repeater.

Also, the packet event itself is a RakNet-level event. The first milestone is
to verify that the hook works reliably on the user's exact 0.3.DL-R1 client.

After that, the next layer should add packet/RPC name resolution and structured
BitStream decoders.
