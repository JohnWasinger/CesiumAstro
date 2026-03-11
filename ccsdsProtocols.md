# CCSDS Protocols for Lunar and Deep Space Communications

This document covers the CCSDS (Consultative Committee for Space Data Systems)
protocols used for lunar missions like NASA's LunaNet and deep space
communications - directly relevant to CesiumAstro's work on phased array
payloads for satellite communications.

## Protocol Overview

| Protocol                        | Standard       | Use Case                   | Key Features                          |
| ---                             | ---            | ---                        | ---                                   |
| Space Packet Protocol (SPP)     | CCSDS 133.0-B-2| Primary data encapsulation | Application-layer packet format       |
| Proximity-1                     | CCSDS 211.0-B-6| Orbiter↔Lander/Rover       | Short-range, autonomous link          |
| TM/TC Transfer Frames           | CCSDS 132.0-B-2| Ground↔Spacecraft          | Long-haul telemetry/telecommand       |
| Unified Space Data Link (USLP)  | CCSDS 732.1-B-2| Modern missions            | Flexible framing for all link types   |
| Delay-Tolerant Network (DTN)    | CCSDS 734.1-B-1| Inter-planetary routing    | Store-and-forward for high latency    |

## Packet and Frame Sizes

### Space Packet (CCSDS 133.0-B-2)

- **Primary header:** exactly 48 bits (6 bytes) — fixed, always present
- **Secondary header:** mission-defined, typically 48 bits (6 bytes) for a
  simple coarse/fine timecode
- **User data:** variable, 1 to 65536 bytes
- **CRC-16:** 16 bits (2 bytes)

A minimal packet is **8 bytes** (primary header + 1 byte data + CRC). A typical
telemetry packet like the one in [ccsds_crc.c](ccsds_crc.c) is around
**30-40 bytes**.

### TM Transfer Frame (CCSDS 132.0-B-2)

- **Primary header:** 48 bits (6 bytes)
- **Secondary header:** optional, 1-64 bytes
- **Data field:** fixed per mission, typically **1024 or 8920 bytes** for
  downlink
- **Trailer (OCF + FECF):** 4-6 bytes

---

## Space Packet Protocol (SPP) - CCSDS 133.0-B-2

The foundation of CCSDS data encapsulation. Every telemetry or telecommand
packet follows this structure.

### Primary Header (6 bytes, always present)

| Field                  | Bits | Byte Offset    | Description                          | Example Value |
| ---                    | ---  | ---            | ---                                  | ---           |
| Version Number         | 3    | 0 (bits 0-2)   | Always `000` for CCSDS v1            | `0b000`       |
| Packet Type            | 1    | 0 (bit 3)      | 0=TM (telemetry), 1=TC (telecommand) | `0`           |
| Secondary Header Flag  | 1    | 0 (bit 4)      | 1=present, 0=absent                  | `1`           |
| APID                   | 11   | 0-1 (bits 5-15)| Application Process ID (0x000-0x7FF) | `0x123` (291) |
| Sequence Flags         | 2    | 2 (bits 0-1)   | 00=cont, 01=first, 10=last, 11=alone | `0b11`        |
| Sequence Count         | 14   | 2-3 (bits 2-15)| Packet counter (0-16383, wraps)      | `100`         |
| Packet Data Length     | 16   | 4-5            | (Total data field bytes) - 1         | `0x0015` (21) |

**Primary Header Bit Layout:**

```text
 Bit  0                   1                   2                   3
      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |Ver(3)|T|S|          APID (11)          |SF(2)|  Seq Count(14) |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |              Packet Data Length (16)                          |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

     Ver = Version (3 bits, always 000)
     T   = Packet Type (1 bit: 0=TM telemetry, 1=TC telecommand)
     S   = Secondary Header Flag (1 bit: 1=present)
     SF  = Sequence Flags (2 bits: 11=standalone, 01=first, 10=last, 00=cont)
```

Note the APID spans the byte boundary between bytes 0 and 1 — the upper 3 bits
sit in byte 0 (bits 5-7), the lower 8 bits in byte 1. This is why
`__builtin_bswap16()` is required before masking with `0x07FF` on little-endian
systems.

**Example Primary Header (hex):**

```text
08 23 C0 64 00 15
```

Breakdown:

- `08 23` → Version=0, Type=0 (TM), SecHdr=1, APID=0x023 (35)
- `C0 64` → SeqFlags=11 (standalone), SeqCount=100
- `00 15` → Data length = 21 bytes (actual data field is 22 bytes)

**IMPORTANT:** The Packet Data Length field is `(actual_bytes - 1)`. This is a
common source of off-by-one bugs.

### Secondary Header (optional, mission-specific)

Typical fields for telemetry packets:

| Field               | Size       | Description                                 |
| ---                 | ---        | ---                                         |
| Coarse Time         | 32 bits    | Seconds since CCSDS epoch (Jan 1, 1958 TAI) |
| Fine Time           | 16-32 bits | Sub-second resolution (mission-defined)     |
| Packet Sub-Type     | Variable   | Mission-specific classification             |

**Secondary Header Bit Layout (typical coarse/fine timecode):**

```text
 Bit  0                   1                   2                   3
      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |                    Coarse Time (32 bits)                      |
     |              Seconds since Jan 1, 1958 TAI epoch              |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |          Fine Time (16 bits)          | Sub-second resolution |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

**CCSDS Time Epoch:** January 1, 1958, 00:00:00 TAI (not Unix epoch!)

Conversion to Unix time:

```c
#define CCSDS_EPOCH_OFFSET 378691200  // Seconds between 1958 and 1970

uint32_t ccsds_to_unix(uint32_t ccsds_time) {
    return ccsds_time - CCSDS_EPOCH_OFFSET;
}
```

### Reserved APIDs

| APID Range  | Purpose                                               |
| ---         | ---                                                   |
| 0x000-0x007 | Reserved for special purposes                         |
| 0x7FF       | Idle/fill packets (discard, used for frame alignment) |
| 0x7FE       | Time correlation packets                              |

**Critical Rule:** Always filter APID `0x7FF` before CRC verification to avoid
false corruption alarms from intentional fill data.

### Full Space Packet Layout

```text
 Byte  0     1     2     3     4     5
      +-----+-----+-----+-----+-----+-----+
      |   Packet ID   |  Seq Ctrl   | PDL |   Primary Header (6 bytes, fixed)
      +-----+-----+-----+-----+-----+-----+
      |         Coarse Time (4 bytes)      |
      +-----+-----+-----+-----+-----+-----+   Secondary Header (6 bytes, typical)
      |   Fine Time   |
      +-----+-----+--
      |                                   |
      |         User Data (variable)      |   1 to 65530 bytes
      |                                   |
      +-----+-----+-----+-----+-----+-----+
      |        CRC-16 (2 bytes)           |   Always last 2 bytes
      +-----+-----+

      PDL = Packet Data Length = (SecHdr + UserData + CRC bytes) - 1
      CRC covers everything from byte 0 through end of user data
```

### CRC-16/CCITT for Space Packets

**Polynomial:** 0x1021  
**Initial value:** 0xFFFF  
**Coverage:** Primary header + secondary header + user data  
**Appended:** 2 bytes at end of packet (big-endian)

Example from [ccsds_crc.c](ccsds_crc.c):

```c
#define CRC16_POLYNOMIAL 0x1021
#define CRC16_INIT       0xFFFF

uint16_t calculate_crc16(const uint8_t *data, size_t length) {
    uint16_t crc = CRC16_INIT;
    for (size_t i = 0; i < length; i++) {
        uint8_t index = (uint8_t)((crc >> 8) ^ data[i]);
        crc = (crc << 8) ^ crc16_table[index];
    }
    return crc;
}
```

## Proximity-1 Protocol - CCSDS 211.0-B-6

Used for short-range space links, particularly **orbiter↔lander** and
**orbiter↔rover** communications. This is the primary protocol for LunaNet
lunar surface-to-orbit links.

### Key Features for Lunar Communications

- **Hailing channel** — Autonomous link establishment without ground
  intervention. Critical when a rover has limited visibility windows to an
  orbiter.
- **Asymmetric data rates** — Optimized for rover→orbiter uplink being
  data-heavy (science instruments) vs. orbiter→rover downlink (commands).
- **UHF frequencies** — 390-450 MHz band reduces hardware complexity for
  surface assets.
- **Automatic handoff** — Supports multiple orbiters, automatically switching
  as satellites rise/set.

### Proximity-1 Transfer Frame Structure

| Field              | Size       | Description                                |
| ---                | ---        | ---                                        |
| Sync Marker        | 32 bits    | Frame synchronization (0x034776C7289A)     |
| Frame Header       | 48 bits    | SCID, PCID, Frame Length, Sequence Number  |
| Insert Zone        | Variable   | Optional overhead data                     |
| Data Field         | Variable   | User packets (up to 2043 bytes typical)    |
| Frame Error Control| 16 bits    | CRC-16/CCITT                               |

**Proximity-1 Sync Marker:** `0x034776C7289A` (not the same as AOS/TM frames)

### Error Correction for Proximity-1

Proximity-1 typically uses layered FEC:

- **Reed-Solomon RS(223,255)** — Outer code, corrects burst errors
- **Convolutional code (rate 1/2, constraint length 7)** — Inner code
- **Viterbi decoding** — Soft-decision decoding for the convolutional code

This combination provides robust error correction for the challenging
lunar surface↔orbit link budget.

## TM/TC Transfer Frames - CCSDS 132.0-B-2

Used for ground↔spacecraft long-haul communications. These frames encapsulate
Space Packets for transmission over the physical layer.

### Transfer Frame Structure

| Field                    | Size      | Description                                |
| ---                      | ---       | ---                                        |
| Sync Marker              | 32 bits   | 0x1ACFFC1D                                 |
| Frame Header             | 48 bits   | Version, SCID, VCID, Frame Count           |
| Insert Zone (optional)   | Variable  | Security header, timestamp                 |
| Data Field               | Variable  | Multiplexed Space Packets (typically 1115) |
| Operational Control Field| Variable  | CLCW (Command Link Control Word)           |
| Frame Error Control      | 16 bits   | CRC-16/CCITT or none                       |

**TM Transfer Frame Bit Layout:**

```text
 Bit  0                   1                   2                   3
      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |      Sync Marker = 0x1ACFFC1D (32 bits)                      |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |Ver|      SCID (10)      |  VCID(3) |OCF|MC |  Frame Count(8) |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |  First Header Pointer (11)  | Sync|  (Frame Header continued) |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |                                                               |
     |         Data Field — multiplexed Space Packets                |
     |         (typically 1115 bytes for standard downlink)          |
     |                                                               |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |    OCF (4 bytes, optional)    |    FECF CRC-16 (2 bytes)      |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

     SCID  = Spacecraft ID
     VCID  = Virtual Channel ID (demultiplexes data streams)
     OCF   = Operational Control Field flag
     MC    = Master Channel frame count flag
     FECF  = Frame Error Control Field
     First Header Pointer = byte offset to first Space Packet in data field
```

The **First Header Pointer** is critical for reassembly — Space Packets can
span frame boundaries, so the ground system uses this field to find where the
first complete packet starts in each frame.

**Sync Marker for TM Frames:** `0x1ACFFC1D` (different from Proximity-1)

### Virtual Channels (VCIDs)

Transfer Frames use Virtual Channel IDs (VCID) to multiplex different data
streams over a single physical link:

- **VCID 0** — High-priority commands
- **VCID 1** — Housekeeping telemetry
- **VCID 2-6** — Science instruments
- **VCID 7** — Fill frames (idle data)

Ground stations demultiplex by VCID and reassemble Space Packets that span
frame boundaries.

## Unified Space Data Link Protocol (USLP) - CCSDS 732.1-B-2

Modern replacement for TM/TC frames, designed for flexibility across all link
types (near-Earth, deep space, proximity).

**Key improvements over legacy TM/TC:**

- **Variable frame lengths** — No fixed size, adapts to link conditions
- **Quality of Service (QoS)** — Prioritized virtual channels
- **Security built-in** — Encryption and authentication at frame level
- **Scalable** — Works from CubeSats to deep space missions

USLP is being adopted for newer missions, including elements of LunaNet.

## Delay-Tolerant Networking (DTN) - CCSDS 734.1-B-1

For inter-planetary and cislunar networking where round-trip light time makes
TCP/IP infeasible.

**DTN Bundle Protocol characteristics:**

- **Store-and-forward** — Nodes store data until the next hop is available
- **Custody transfer** — Reliability through hop-by-hop acknowledgment
- **Time-to-live** — Bundles expire if not delivered within a time window
- **Compatible with CCSDS** — DTN bundles encapsulated in Space Packets

**Example DTN use case:**

A Mars rover sends data to a Mars orbiter (Proximity-1), which stores it. When
Earth becomes visible, the orbiter forwards it via TM Transfer Frames. Ground
station unpacks and routes to science teams. Total latency: 5-20 minutes
depending on planetary positions.

## Byte Order and Endianness

**CRITICAL:** All CCSDS protocols use **big-endian** (network byte order) for
multi-byte fields.

On x86/ARM systems (typically little-endian), you must byte-swap when
reading/writing CCSDS packets:

```c
// Reading a 16-bit APID from a packet
uint16_t packet_id_raw = *(uint16_t *)packet;
uint16_t packet_id = __builtin_bswap16(packet_id_raw);
uint16_t apid = packet_id & 0x07FF;

// Writing a CRC to a packet
uint16_t crc = calculate_crc16(data, len);
*(uint16_t *)(packet + len) = __builtin_bswap16(crc);
```

See [ccsds_crc.c](ccsds_crc.c) for production examples of byte-swapping.

## Relevance to CesiumAstro's LunaNet Work

CesiumAstro's radio units for NASA's LunaNet program would implement:

1. **Proximity-1** — For lunar surface assets communicating with Gateway or
   lunar orbiters
2. **Space Packet Protocol** — Application-layer data from instruments and
   subsystems
3. **DTN** — For store-and-forward between lunar assets and Earth
4. **UHF/S-band physical layer** — CesiumAstro's SDR handles modulation,
   the CCSDS stack sits on top

The phased array antennas would perform beamforming at the RF layer, while the
CCSDS protocols handle reliable data transfer at higher layers.

## Summary Table - Protocol Stack

| Layer                  | Protocol             | CesiumAstro Component     |
| ---                    | ---                  | ---                       |
| Application            | Space Packets (SPP)  | Firmware                  |
| Network                | DTN Bundles          | Firmware / Ground software|
| Data Link              | Proximity-1 / USLP   | Firmware                  |
| Physical (Coding)      | Reed-Solomon / LDPC  | FPGA / DSP                |
| Physical (Modulation)  | QPSK / 8PSK          | SDR / Phased Array        |

For hands-on examples of CCSDS Space Packet processing, see:

- [ccsds_crc.c](ccsds_crc.c) — C implementation with CRC-16
- [ccsds_crc.rs](ccsds_crc.rs) — Rust implementation with typed errors
- [asRust.md](asRust.md) — C vs. Rust comparison for safety-critical parsing

## References

- [CCSDS 133.0-B-2: Space Packet Protocol](https://public.ccsds.org/Pubs/133x0b2e1.pdf)
- [CCSDS 211.0-B-6: Proximity-1 Space Link Protocol](https://public.ccsds.org/Pubs/211x0b6.pdf)
- [CCSDS 132.0-B-2: TM Space Data Link Protocol](https://public.ccsds.org/Pubs/132x0b2.pdf)
- [CCSDS 732.1-B-2: Unified Space Data Link Protocol](https://public.ccsds.org/Pubs/732x1b2.pdf)
- [CCSDS 734.1-B-1: LTP for CCSDS (DTN)](https://public.ccsds.org/Pubs/734x1b1.pdf)
