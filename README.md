# GigaACE Windows Soundcard

Experimental Windows virtual sound-card stack for decoding Allen & Heath GigaACE/SLink Ethernet audio frames and exposing them to DAWs through an ASIO bridge.

## What is included

- Qt GUI for live GigaACE frame capture or demo-signal generation.
- GigaACE decoder pipeline and shared-memory audio bridge.
- 64-channel ASIO input driver bridge for hosts such as REAPER.
- Replay tool for feeding recorded `.pcap`, `.pcapng`, or Wireshark-style hex dumps into the ASIO bridge.
- Capture analyzer and controlled GX4816/SLink identity probe tools for reverse-engineering stagebox startup behavior.
- Windows setup helper and packaging script for building a single setup executable.

## Build

Requirements:

- Windows 10/11
- Visual Studio 2022 with C++ tools
- CMake
- Qt 6 for MSVC 2022 x64

Build a Release package:

```powershell
.\scripts\Package-App.ps1
```

The generated installer is written to:

```powershell
.\dist\app\GigaACEVirtualSoundCardSetup.exe
```

## Usage

1. Run `GigaACEVirtualSoundCardSetup.exe`.
2. Start `GigaACE Virtual Sound Card`.
3. Press `Start Audio`.
4. In your DAW, select `ASIO` and then `GigaACE ASIO Driver`.

For recorded captures:

```powershell
.\build\Release\GigaAceReplay.exe --input capture.pcapng --loop --channels 64
```

For capture analysis:

```powershell
.\build\Release\GigaAceAnalyze.exe --input capture.pcapng --tone 440 --slots 128
```

For controlled stagebox identity probing:

```powershell
.\build\Release\GigaAceIdentify.exe --list
.\build\Release\GigaAceIdentify.exe --interface "\Device\NPF_{YOUR-ADAPTER-GUID}" --send --variant 0 --seconds 10
```

See [Stagebox Identity Probing](docs/STAGEBOX_IDENTITY_PROBING.md) for the recommended test procedure.

## Status

This project is experimental reverse-engineering work. It is intended for testing and research, not production use.
