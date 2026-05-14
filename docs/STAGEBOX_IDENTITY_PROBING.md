# Stagebox Identity Probing

This project includes an experimental identity probe tool:

```powershell
GigaAceIdentify.exe
```

The goal is to test which transmit pattern makes an Allen & Heath console treat
the computer as a GX4816-like SLink stagebox. This is separate from normal ASIO
recording and separate from sending audio or test tones into the console.

## Safety Model

Use this tool only on an isolated test link between the PC, the console, and an
optional mirror/tap switch. Do not run it on a normal office network.

Run either `GigaAceIdentify.exe` or the main GUI transmit feature, not both at
the same time. Two processes sending GX4816-like traffic on the same adapter can
make the test result meaningless.

The tool is deliberately conservative. It sends a small set of named variants
instead of random fuzzing. Each variant is printed before it is sent so the
console state and packet capture can be matched to the exact test.

## Variants

List the available probe variants:

```powershell
.\build\Release\GigaAceIdentify.exe --list
```

Current variants:

- `0 gx4816-slink-48k`  
  Observed GX4816/SLink shape: EtherType `0x04EE`, header
  `00 0A 04 EA 00`, stream type `0x02`, 48k packets/s.

- `1 gx4816-slink-96k`  
  Same GX4816 header, but free-running at 96k packets/s.

- `2 avantis-header-gx-mac`  
  Console-style `00 00 04 EA 00` header, but using the GX4816 source MAC.

- `3 gx4816-stream01`  
  GX4816 header with stream type `0x01` instead of `0x02`.

- `4 gx4816-zero-prefix`  
  GX4816 MAC and stream `0x02`, but bytes 14-18 zeroed.

- `5 short-gace-identity`  
  Short experimental identity frame with ASCII `GACE` / `GX4816` payload.

## Recommended Test Procedure

1. Connect the console SLink/GigaACE port to the PC through a test switch or
   tap.
2. Start a port-mirror capture before sending anything.
3. Keep the main GigaACE GUI stopped, or at least disable `Advertise GX4816` and
   `Send to console`.
4. Run one variant for a fixed duration.
5. Note exactly what the console shows during that variant.
6. Save the packet capture with the variant number in the filename.

Start with variant `0`:

```powershell
.\build\Release\GigaAceIdentify.exe --interface "\Device\NPF_{YOUR-ADAPTER-GUID}" --send --variant 0 --seconds 10
```

You can also match by adapter description fragment:

```powershell
.\build\Release\GigaAceIdentify.exe --interface "Intel(R) Ethernet" --send --variant 0 --seconds 10
```

Run all variants sequentially:

```powershell
.\build\Release\GigaAceIdentify.exe --interface "\Device\NPF_{YOUR-ADAPTER-GUID}" --send --all --seconds 5 --pause-ms 1000
```

## What To Record

For each test, write down:

- console model and firmware version
- selected physical port
- adapter/interface used on the PC
- exact `GigaAceIdentify` command
- whether the console shows a stagebox/device
- whether the console begins sending frames back
- whether any audio meters move
- packet capture filename

Good capture filenames are intentionally boring and explicit, for example:

```text
variant0_gx4816-slink-48k_avantis-starts-sending.pcapng
variant2_avantis-header-gx-mac_no-device-detected.pcapng
```

## Interpreting Results

If one variant causes the console to recognize a device, use the capture analyzer
to compare the before/after traffic:

```powershell
.\build\Release\GigaAceAnalyze.exe --input capture.pcapng --tone 440 --rate 96000 --slots 128 --top 30
```

The important question is not only whether the console displays a device. We
also need to know whether the console starts sending its normal audio/control
stream back to the PC. That response is the strongest sign that the identity
pattern is close to the real GX4816 startup behavior.

Once a working identity variant is found, the next step is to fold that exact
pattern into the main GUI's `Advertise GX4816` mode and then test audio TX
separately.
