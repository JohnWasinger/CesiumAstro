# About CesiumAstro

Q> CesiumAstro is a company that I am applying to. Any advice? Do you know of
their technology? What CCSDS messages do you think that they focus on for their
satellites?

## What CesiumAstro Does

CesiumAstro is an Austin, Texas based startup specializing in phased array
technology, building antennas and communications systems for satellites. It was
founded in 2017 by Shey Sabripour. They've grown significantly — recently
securing $470 million in growth capital in a Series C round, with around 75% of
their business in defense and government.

They've expanded from a phased array technology provider to ground antennas and
full satellites, now building a full technology stack from hardware, software,
and firmware — going from software-defined radios and phased arrays to full
end-to-end missions from ground to space.

## Key Programs to Know About

- **SDA Transport Layer** — CesiumAstro is a key supplier to the Space
  Development Agency's proliferated satellite architecture, providing Vireo
  Ka-band multi-beam phased array payloads for Raytheon's Tranche 1 Tracking
  Layer satellites, and was selected by Rocket Lab to deliver Vireo advanced RF
  payloads for 18 satellites as part of SDA's Tranche 2 Transport Layer
  constellation.

- **LunaNet** — In July 2024 they were awarded a NASA contract to deliver radio
  units for lunar navigation under the LunaNet Technology program.

## CCSDS Focus Areas

Based on their work, the CCSDS layers most relevant to CesiumAstro would
likely be:

**Space Packet Protocol (CCSDS 133.0-B-2)** — telemetry and telecommand packets
flowing through their SDR payloads. See [ccsds_crc.c](ccsds_crc.c) and
[ccsds_crc.rs](ccsds_crc.rs) for working implementations.

**TM/TC Transfer Frames (CCSDS 132.0-B-2)** — since they handle inter-satellite
crosslinks and ground downlinks, Transfer Frame synchronization and virtual
channel management would be core.

**Proximity-1 (CCSDS 211.0-B-5)** — given their LunaNet work, this short-range
space link protocol for surface-to-orbiter comms is directly relevant.

**Space Link Extension (SLE)** — for ground station interfaces, especially with
multi-mission ground networks.

## SDR-Relevant CCSDS Stack

Since CesiumAstro builds software-defined phased array payloads, understanding
how CCSDS sits on top of the SDR physical layer is important. The full stack
looks like this:

```
┌─────────────────────────────────────────┐
│     Space Packet Protocol (SPP)         │  ← ccsds_crc.c / ccsds_crc.rs
│         CCSDS 133.0-B-2                 │
├─────────────────────────────────────────┤
│     TM/TC Transfer Frames               │  ← Wrap packets for transmission
│   CCSDS 132.0-B-2 / 231.0-B-3          │
├─────────────────────────────────────────┤
│     Sync & Channel Coding               │  ← Where SDR gets involved
│         CCSDS 131.0-B-3                 │
├─────────────────────────────────────────┤
│     Physical Layer / SDR                │  ← CesiumAstro's hardware
│       (QPSK, 8PSK, Ka-band)             │
└─────────────────────────────────────────┘
```

## Application Advice

**Lead with CCSDS hands-on experience** — mention packet parsing, APID
management, sequence gap detection, and the specific layer of the stack you
worked at. That maps directly to what they build.

**The Rust angle is a differentiator** — most space software engineers come from
C/C++ backgrounds. Knowing Rust and understanding why it matters for
safety-critical data pipelines is a strong talking point, especially as the
industry starts looking at Rust for ground software. See
[asRust.md](asRust.md) for the full discussion.

**Brush up on SDR concepts** — since their core product is software-defined
radio, understanding how CCSDS sits on top of an SDR physical layer shows
depth. Key concepts worth knowing:

- **QPSK/8PSK framing** — Transfer Frames are what get modulated onto the
  carrier. The `0x1ACFFC1D` sync word is what a correlator in the SDR scans for
  in the demodulated bitstream.
- **Eb/N0 and link budgets** — the reason Reed-Solomon and LDPC exist is that
  you're operating at very low signal margins in space.
- **Doppler compensation** — LEO satellites move fast enough that frequency
  shift is significant, especially on Ka-band. SDRs handle this in the physical
  layer before CCSDS framing begins.
- **VITA 49 (VRT)** — the SDR industry standard for packetizing IQ sample
  streams. CesiumAstro's SDR payloads likely output VITA 49 streams internally
  before the CCSDS layer.

**Know the SDA constellation architecture** — they're deeply embedded in the
Space Development Agency's Transport and Tracking Layer programs. Knowing what
those constellations do and why inter-satellite links matter shows you've done
your homework.

## Summary

| Area                        | Relevance to CesiumAstro                        |
| ---                         | ---                                             |
| Space Packet Protocol       | Core — flows through all their SDR payloads     |
| TM/TC Transfer Frames       | Core — crosslinks and ground downlinks          |
| Proximity-1                 | High — LunaNet lunar navigation contract        |
| Space Link Extension (SLE)  | High — ground station interface standard        |
| Reed-Solomon / LDPC FEC     | High — Ka-band link margin requirements         |
| VITA 49 / VRT               | Useful — internal SDR IQ stream standard        |
| Rust for ground software    | Differentiator — emerging in the industry       |
