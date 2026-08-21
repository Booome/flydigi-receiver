# Flydigi K5 (Apex 5) Firmware Files

Official firmware images for the Flydigi Apex 5 controller and its 2.4G receiver
(dongle), downloaded by Flydigi Space Station during a firmware upgrade and
recovered from the Windows temp directory.

- Source: `C:\Users\bodon\AppData\Local\Temp` (Windows C: mounted at
  `/home/bodong/windows_c`), from a Flydigi Space Station firmware upgrade.
- Purpose: reverse-engineering the controller↔dongle SLE (NearLink) protocol.

## Files

| File | Target | Version | Notes |
|---|---|---|---|
| `K5_BS20_Dongle_V2131_DFU.fwpkg` | **2.4G receiver (dongle)** | 2.1.3.1 | BS20 chip, DFU package, payload encrypted/packed |
| `K5_BS20_Gamepad_V1131_DFU.fwpkg` | controller | 1.1.3.1 | BS20 chip, DFU package, payload encrypted/packed |
| `K5_MH2113_V7045_0409_DFU.bin` | controller main | 7.0.4.5 | raw ARM Cortex-M firmware (SP 0x20006c80, reset 0x08014b10) |
| `OTA_K5_FR8008GP_V0128_0731.bin` | OTA image | 1.2.8 | full OTA image (655360 B) |

## Findings

- The dongle is a **BS20** chip (NearLink SLE), same family as our BS21 boards.
- The `.fwpkg` payload is encrypted/packed (no ARM vectors, no readable
  strings), so direct string extraction does not work on it.
- `K5_MH2113_V7045_0409_DFU.bin` is the raw controller firmware but contains no
  obvious protocol strings (likely code-only image).

The exact pairing (GID/DID) and RF framing logic live in these images but are
encrypted; extraction requires reverse-engineering the fwpkg format or
decompiling the controller firmware.