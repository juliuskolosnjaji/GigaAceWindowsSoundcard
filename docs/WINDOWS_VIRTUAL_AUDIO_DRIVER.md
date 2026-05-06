# Windows Virtual Audio Driver Plan

This project needs two separate pieces to behave like Dante Virtual Soundcard on
Windows:

1. A user-mode bridge that captures and decodes GigaACE frames.
2. A signed Windows audio driver that exposes a WDM/WaveRT capture endpoint to
   DAWs and reads samples from the bridge.

The Qt application already implements the first piece:

- Npcap capture for raw Ethernet frames.
- GigaACE frame decoding.
- A shared-memory float ring buffer named `Global\GigaACEVirtualDevice`.
- WASAPI monitoring for diagnostics.

The remaining work is the kernel audio driver.

## ASIO Bridge Alternative

For DAWs, the project now also contains a user-mode ASIO bridge. This avoids
Secure Boot and kernel test-signing during development.

Build:

```powershell
cmake --build build --config Release
```

Register from elevated PowerShell:

```powershell
.\scripts\Register-GigaAceASIO.ps1
```

Then start `GigaAceVirtualSoundCard.exe` so it creates
`Local\GigaACEVirtualDevice`, open REAPER, and choose:

`Preferences > Audio > Device > Audio system: ASIO > GigaACE ASIO Driver`

The first implementation exposes 64 float32 input channels at 48 kHz. It reads
from the shared-memory ring written by the Qt capture app and outputs silence
when the app is not running.

## Recorded Frame Replay

Recorded GigaACE traffic can be replayed without the GUI:

```powershell
.\scripts\Build-Release.ps1 -Target GigaAceReplay
.\build\Release\GigaAceReplay.exe --input .\captures\sq5.pcapng --loop --channels 64
```

Supported input formats:

- classic `.pcap`
- `.pcapng`
- Wireshark-style text hex dumps

Run either the GUI or `GigaAceReplay.exe`, not both at the same time, because
both create the same `Local\GigaACEVirtualDevice` shared-memory ring for the
ASIO driver.

## Driver Architecture

Use Microsoft SysVAD as the base driver model, not the current hand-written
miniport skeleton. SysVAD is the official WDM virtual audio device sample and
already contains the correct PortCls/WaveRT topology, pin descriptors,
Power/PnP handling, INF package shape, and signing flow.

Target endpoint:

- Name: `GigaACE Virtual Sound Card`
- Type: capture endpoint, visible to WASAPI/MME/DirectSound and DAWs.
- Format: 48 kHz, 32-bit float PCM, stereo first; expand to 64 channels after
  basic stability.
- Data source: shared-memory ring buffer written by the Qt bridge.

## Milestones

1. Build unmodified SysVAD
   - Install Visual Studio WDK integration.
   - Build `audio\sysvad\sysvad.sln` from the Microsoft sample.
   - Install the sample driver in test-signing mode and confirm that a virtual
     audio endpoint appears in REAPER.

2. Create GigaACE-specific SysVAD fork
   - Keep only the capture endpoint needed for this project.
   - Rename INF strings, device names, and hardware IDs.
   - Remove unrelated APO, Bluetooth, USB, keyword detector, and render paths.

3. Replace tone generation with shared-memory capture
   - Open `\BaseNamedObjects\Global\GigaACEVirtualDevice` from kernel mode.
   - Validate ring header magic/version/channel/sample-rate fields.
   - Copy interleaved float samples into WaveRT capture packets.
   - Return silence on underrun.

4. Timing and clocking
   - Report position from a monotonic 48 kHz frame clock.
   - Track bridge write index independently from audio-engine read index.
   - Expose drop/underrun counters in trace logs.

5. Packaging
   - Create a test certificate.
   - Sign `.sys`, `.cat`, and package files.
   - Install using `pnputil` or `devcon`.

## Required Development Machine Setup

Visual Studio currently lacks the WDK MSBuild platform toolset:

`WindowsKernelModeDriver10.0`

The WDK toolset is now installed and the unmodified SysVAD package builds in
this workspace.

Install:

- Visual Studio 2022 with Desktop development with C++.
- Windows SDK.
- Windows Driver Kit matching the installed SDK.
- WDK Visual Studio extension / build tools integration.

Build the SysVAD baseline with:

```powershell
.\scripts\Build-SysVAD.ps1
```

This script deliberately disables Spectre mitigation and signing during the
compile step because the local VS install is missing Spectre kernel libraries
and signing needs an elevated certificate store. The package is signed later.

## Test-Signing Setup

Run these commands in an elevated terminal on the test machine:

```powershell
bcdedit /set testsigning on
shutdown /r /t 0
```

Secure Boot may need to be disabled while test-signing is enabled.

## Install Flow

Create the GigaACE-named package:

```powershell
.\scripts\Prepare-GigaAceDriverPackage.ps1
```

Enable test mode once, then reboot:

```powershell
.\scripts\Enable-TestSigning.ps1
shutdown /r /t 0
```

After reboot, sign and install from an elevated PowerShell:

```powershell
.\scripts\Sign-GigaAceDriverPackage.ps1
.\scripts\Install-GigaAceDriver.ps1
```

Then verify:

```powershell
Get-PnpDevice -Class MEDIA | Where-Object FriendlyName -like '*GigaACE*'
```

The endpoint should appear in Windows Sound settings and in DAWs such as REAPER.
