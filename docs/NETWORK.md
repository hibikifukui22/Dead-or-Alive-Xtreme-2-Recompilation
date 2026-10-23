# Network access

Since v1.1.2, DOAX2 Recomp blocks all network access by default. The game
never opens a network socket, never starts Windows networking (Winsock) and
never looks anything up. This document explains what was there before, what
the block does and how to turn networking back on for development.

## Background

The original Xbox 360 game contains Microsoft's Xbox networking library
(System Link / Xbox Live support). At startup it:

1. starts networking (`XNetStartup`, `WSAStartup`),
2. creates a UDP socket using the Xbox VDP protocol (protocol 254) and
3. binds it to `0.0.0.0`, UDP port 1000, the standard Xbox 360 System Link port.

The game code that does this is `sub_82525AF0`, called from `sub_8252A9F8`
(port 1000 is passed in). The game has no online play that works on PC (and
its Xbox Live service is long gone), and it never connects anywhere. However, ReXGlue passes
guest socket calls straight to the Windows network stack, so step 3 opened
a real listening port. Windows Firewall asks about any program that listens
on the network, which is why players saw an "allow doax2.exe" prompt.

Even before v1.1.2 the runtime could not reach the internet. DNS lookups
(`XNetDnsLookup`) always reported failure, the Xbox HTTP functions
(`XHttp*`) are empty stubs, and the game does not import TCP connect or
download functions.

## What is blocked

The block lives in the ReXGlue SDK, in
`sdk/rexglue-sdk/src/kernel/xam/xam_net.cpp`. It is shipped as part of
`patches/rexglue-sdk/0001-doax2-runtime-changes.patch` and controlled by one
setting:

| Setting | Default | Meaning |
| --- | --- | --- |
| `guest_network` | `false` | `true` lets the game use the PC's network again |

With `guest_network` off, the functions below return before they reach
Windows. They fail with `WSAENETDOWN` (10050), as a console with no network
cable would:

| Xbox function (`NetDll_…`) | Behaviour while blocked |
| --- | --- |
| `WSAStartup` | succeeds with placeholder data; Winsock is **not** started |
| `socket` | fails, so no socket is ever created |
| `bind`, `connect`, `listen`, `accept` | fail |
| `send`, `sendto`, `WSASendTo` | fail, nothing is sent |
| `recv`, `recvfrom`, `WSARecvFrom` | fail, nothing is received |
| `select`, `setsockopt`, `ioctlsocket`, `shutdown` | fail |
| `XNetGetTitleXnAddr` | reports no address (`XNET_GET_XNADDR_NONE`) |

These never used the network, before or after the block:

| Xbox function | Behaviour |
| --- | --- |
| `XNetDnsLookup` | always "not found"; no lookup is made |
| `XNetQosServiceLookup` / `XNetQosListen` | no results / failure |
| `XNetGetEthernetLinkStatus` | link down |
| `XHttp*`, `Upnp*` | empty stubs |
| `inet_addr` | converts text to a number locally |

`closesocket` is left as it is, since there is never a socket to close.

The runtime logs each blocked function once, for example:

```
[krnl] Network call NetDll_WSAStartup_entry answered without Winsock (guest_network is off)
[krnl] Network call NetDll_socket_entry blocked (guest_network is off)
```

A normal play session logs exactly these two lines: the game starts
networking, tries its System Link socket, gets "network down" and carries on.

`rexruntime.dll` is the only shipped binary that links the Windows network
library (`WS2_32.dll`). The game executable, the graphics DLL and the
launcher link no network libraries.

## Local communication

Nothing else talks to other programs, either on the network or on the same
PC:

- **Debug output.** Log lines and the game's debug prints go to Windows
  debug output (`OutputDebugString`) only while a debugger is attached to
  the game. Debug output viewers such as DebugView receive nothing.
  See `sdk/rexglue-sdk/src/core/dbg_win.cpp` and spdlog's `msvc_sink`,
  which checks for a debugger by default.
- **Debug symbols.** The diagnostic tools (Diagnostic Logging in 60 FPS
  mode, and the write watch) read symbols only from local folders: the game
  folder, or `out\game` and `out\native60-build`. They never read
  `_NT_SYMBOL_PATH`, so they cannot reach a symbol server.
- **Profiler.** Release builds do not include the Tracy profiler client,
  which would otherwise listen on a TCP port.
- **Launcher.** The launcher starts the game, waits for it to close and can
  open the logs folder in Explorer. It has no network code.

These local connections are needed and left in place: Direct3D 12 to the
graphics driver, audio output, controller input (XInput, SDL/HID), and file
access to the game, save and log folders.

## Turning networking back on

Use this only for development, for example to work on System Link.

### For one run

Add the setting to the game's command line:

```
out\game\doax2.exe --guest_network=true <other options>
```

The release launcher does not pass this option, so a normal launch from the
launcher always keeps networking blocked. To check that networking is
working again, look for UDP port 1000:

```powershell
Get-NetUDPEndpoint -OwningProcess (Get-Process doax2).Id
```

Windows Firewall will ask about `doax2.exe` again, as before v1.1.2.

### As the default in a build

Change the default in `sdk/rexglue-sdk/src/kernel/xam/xam_net.cpp`:

```cpp
REXCVAR_DEFINE_BOOL(guest_network, false, "Net", ...);  // false -> true
```

Then regenerate the SDK patch (see [BUILDING.md](BUILDING.md)) and rebuild
the SDK and the game. To remove the block completely, delete the
`REX_NET_BLOCK(...)` lines and the `!REXCVAR_GET(guest_network)` branches in
`NetDll_WSAStartup_entry` and `NetDll_XNetGetTitleXnAddr_entry`. The code
after each of them is the original ReXGlue behaviour.

### If you enable it

- Keep the socket local where you can. Binding to `127.0.0.1` instead of
  `0.0.0.0` does not trigger the Windows Firewall prompt.
- Document any new network feature, and keep it off by default in public
  releases.
