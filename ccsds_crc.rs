// ─────────────────────────────────────────────────────────────────────────────
// CCSDS Space Packet Protocol — CRC-16/CCITT Verification in Rust
// Per CCSDS 133.0-B-2
//
// Compile:  rustc ccsds_crc.rs -o ccsds_crc
// Run:      ./ccsds_crc
// ─────────────────────────────────────────────────────────────────────────────

// ─── Constants ───────────────────────────────────────────────────────────────

const CRC16_POLY: u16 = 0x1021;
const CRC16_INIT: u16 = 0xFFFF;

const IDLE_APID:       u16 = 0x7FF;  // CCSDS reserved idle packet APID
const PRIMARY_HDR_LEN: usize = 6;    // CCSDS primary header is always 6 bytes
const SEC_HDR_LEN:     usize = 6;    // Our secondary header: 4 coarse + 2 fine
const CRC_LEN:         usize = 2;    // CRC-16 = 2 bytes
const PAYLOAD_LEN:     usize = 16;   // Simulated sensor payload bytes

// Total packet size
const PACKET_LEN: usize = PRIMARY_HDR_LEN + SEC_HDR_LEN + PAYLOAD_LEN + CRC_LEN;

// ─── Error Types ─────────────────────────────────────────────────────────────

#[derive(Debug, PartialEq)]
enum CcsdError {
    PacketTooShort { actual: usize, minimum: usize },
    CrcMismatch    { received: u16, computed: u16 },
    IdlePacket     (u16),
    InvalidLength  { field_value: u16, actual_data_bytes: usize },
}

impl std::fmt::Display for CcsdError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            CcsdError::PacketTooShort { actual, minimum } =>
                write!(f, "Packet too short: {} bytes (minimum {})", actual, minimum),
            CcsdError::CrcMismatch { received, computed } =>
                write!(f, "CRC mismatch: received=0x{:04X}, computed=0x{:04X}", received, computed),
            CcsdError::IdlePacket(apid) =>
                write!(f, "Idle/fill packet discarded (APID=0x{:03X})", apid),
            CcsdError::InvalidLength { field_value, actual_data_bytes } =>
                write!(f, "Length field mismatch: field says {} data bytes, actual={}", 
                       field_value + 1, actual_data_bytes),
        }
    }
}

// ─── CRC-16/CCITT ─────────────────────────────────────────────────────────────

struct Crc16 {
    table: [u16; 256],
}

impl Crc16 {
    fn new() -> Self {
        let mut table = [0u16; 256];
        for i in 0..256u16 {
            let mut crc = i << 8;
            for _ in 0..8 {
                crc = if crc & 0x8000 != 0 {
                    (crc << 1) ^ CRC16_POLY
                } else {
                    crc << 1
                };
            }
            table[i as usize] = crc;
        }
        Self { table }
    }

    fn calculate(&self, data: &[u8]) -> u16 {
        let mut crc = CRC16_INIT;
        for &byte in data {
            let index = ((crc >> 8) as u8 ^ byte) as usize;
            crc = (crc << 8) ^ self.table[index];
        }
        crc
    }
}

// ─── CCSDS Primary Header ─────────────────────────────────────────────────────
//
//  Bits  Field
//  ────  ─────────────────────────────────────────
//  3     Version Number (always 000)
//  1     Packet Type    (0=TM, 1=TC)
//  1     Sec Hdr Flag   (1=secondary header present)
//  11    APID
//  2     Sequence Flags (11=standalone, 01=first, 10=last, 00=continuation)
//  14    Packet Sequence Count
//  16    Packet Data Length (total data field octets - 1)

#[derive(Debug, Clone)]
struct PrimaryHeader {
    version:        u8,
    packet_type:    u8,   // 0 = Telemetry, 1 = Telecommand
    sec_hdr_flag:   bool,
    apid:           u16,
    seq_flags:      u8,
    seq_count:      u16,
    data_length:    u16,  // Raw field value (actual bytes = field + 1)
}

impl PrimaryHeader {
    /// Parse 6 raw bytes into a PrimaryHeader.
    /// Returns None if slice is too short.
    fn from_bytes(bytes: &[u8]) -> Option<Self> {
        if bytes.len() < PRIMARY_HDR_LEN {
            return None;
        }

        let word0 = u16::from_be_bytes([bytes[0], bytes[1]]);
        let word1 = u16::from_be_bytes([bytes[2], bytes[3]]);
        let word2 = u16::from_be_bytes([bytes[4], bytes[5]]);

        Some(Self {
            version:      ((word0 >> 13) & 0x07) as u8,
            packet_type:  ((word0 >> 12) & 0x01) as u8,
            sec_hdr_flag: ((word0 >> 11) & 0x01) != 0,
            apid:         word0 & 0x07FF,
            seq_flags:    ((word1 >> 14) & 0x03) as u8,
            seq_count:    word1 & 0x3FFF,
            data_length:  word2,
        })
    }

    /// Encode back to 6 bytes (big-endian, CCSDS wire format)
    fn to_bytes(&self) -> [u8; PRIMARY_HDR_LEN] {
        let word0: u16 = ((self.version as u16) << 13)
                       | ((self.packet_type as u16) << 12)
                       | ((self.sec_hdr_flag as u16) << 11)
                       | (self.apid & 0x07FF);

        let word1: u16 = ((self.seq_flags as u16) << 14)
                       | (self.seq_count & 0x3FFF);

        let word2: u16 = self.data_length;

        let w0 = word0.to_be_bytes();
        let w1 = word1.to_be_bytes();
        let w2 = word2.to_be_bytes();

        [w0[0], w0[1], w1[0], w1[1], w2[0], w2[1]]
    }

    /// Actual number of bytes in the data field (CCSDS field value + 1)
    fn data_field_len(&self) -> usize {
        self.data_length as usize + 1
    }

    fn seq_flags_str(&self) -> &'static str {
        match self.seq_flags {
            0b00 => "Continuation",
            0b01 => "First",
            0b10 => "Last",
            0b11 => "Standalone",
            _    => "Unknown",
        }
    }

    fn packet_type_str(&self) -> &'static str {
        if self.packet_type == 0 { "Telemetry" } else { "Telecommand" }
    }
}

// ─── CCSDS Secondary Header ───────────────────────────────────────────────────

#[derive(Debug, Clone)]
struct SecondaryHeader {
    coarse_time: u32,   // Seconds since CCSDS epoch (Jan 1, 1958)
    fine_time:   u16,   // Sub-second counts
}

impl SecondaryHeader {
    fn from_bytes(bytes: &[u8]) -> Option<Self> {
        if bytes.len() < SEC_HDR_LEN {
            return None;
        }
        Some(Self {
            coarse_time: u32::from_be_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]),
            fine_time:   u16::from_be_bytes([bytes[4], bytes[5]]),
        })
    }

    fn to_bytes(&self) -> [u8; SEC_HDR_LEN] {
        let ct = self.coarse_time.to_be_bytes();
        let ft = self.fine_time.to_be_bytes();
        [ct[0], ct[1], ct[2], ct[3], ft[0], ft[1]]
    }
}

// ─── Parsed Packet ────────────────────────────────────────────────────────────

#[derive(Debug)]
struct CcsdPacket {
    primary:   PrimaryHeader,
    secondary: SecondaryHeader,
    payload:   Vec<u8>,
}

// ─── Packet Builder ───────────────────────────────────────────────────────────

fn build_packet(
    crc: &Crc16,
    apid: u16,
    seq_count: u16,
    coarse_time: u32,
    fine_time: u16,
    payload: &[u8],
) -> Vec<u8> {
    // Data field = secondary header + payload + CRC
    let data_field_len = SEC_HDR_LEN + payload.len() + CRC_LEN;
    let data_length_field = (data_field_len - 1) as u16;  // CCSDS: field = actual - 1

    let primary = PrimaryHeader {
        version:      0,
        packet_type:  0,       // Telemetry
        sec_hdr_flag: true,
        apid,
        seq_flags:    0b11,    // Standalone
        seq_count,
        data_length:  data_length_field,
    };

    let secondary = SecondaryHeader { coarse_time, fine_time };

    let mut packet = Vec::with_capacity(PRIMARY_HDR_LEN + data_field_len);
    packet.extend_from_slice(&primary.to_bytes());
    packet.extend_from_slice(&secondary.to_bytes());
    packet.extend_from_slice(payload);

    // Compute and append CRC (covers everything except CRC itself)
    let crc_value = crc.calculate(&packet);
    packet.extend_from_slice(&crc_value.to_be_bytes());

    packet
}

// ─── Packet Verifier ──────────────────────────────────────────────────────────

fn verify_and_parse(crc: &Crc16, raw: &[u8]) -> Result<CcsdPacket, CcsdError> {

    // 1. Minimum length check
    let minimum = PRIMARY_HDR_LEN + CRC_LEN;
    if raw.len() < minimum {
        return Err(CcsdError::PacketTooShort { actual: raw.len(), minimum });
    }

    // 2. Parse primary header
    let primary = PrimaryHeader::from_bytes(raw).unwrap();

    // 3. Idle packet check — discard before CRC (saves CPU on fill frames)
    if primary.apid == IDLE_APID {
        return Err(CcsdError::IdlePacket(primary.apid));
    }

    // 4. Length field consistency check
    //    Total packet = primary header (6) + data field
    let expected_total = PRIMARY_HDR_LEN + primary.data_field_len();
    if raw.len() != expected_total {
        return Err(CcsdError::InvalidLength {
            field_value:       primary.data_length,
            actual_data_bytes: raw.len() - PRIMARY_HDR_LEN,
        });
    }

    // 5. CRC verification — split packet into data and CRC trailer
    let (data, crc_bytes) = raw.split_at(raw.len() - CRC_LEN);
    let received_crc = u16::from_be_bytes([crc_bytes[0], crc_bytes[1]]);
    let computed_crc = crc.calculate(data);

    if computed_crc != received_crc {
        return Err(CcsdError::CrcMismatch {
            received: received_crc,
            computed: computed_crc,
        });
    }

    // 6. Parse secondary header (if present)
    let sec_bytes = &raw[PRIMARY_HDR_LEN..PRIMARY_HDR_LEN + SEC_HDR_LEN];
    let secondary = SecondaryHeader::from_bytes(sec_bytes).unwrap();

    // 7. Extract payload (between secondary header and CRC)
    let payload_start = PRIMARY_HDR_LEN + SEC_HDR_LEN;
    let payload_end   = raw.len() - CRC_LEN;
    let payload       = raw[payload_start..payload_end].to_vec();

    Ok(CcsdPacket { primary, secondary, payload })
}

// ─── Display Helpers ──────────────────────────────────────────────────────────

fn print_packet(pkt: &CcsdPacket) {
    let p = &pkt.primary;
    let s = &pkt.secondary;
    println!("  APID           : 0x{:03X} ({})", p.apid, p.apid);
    println!("  Packet Type    : {}", p.packet_type_str());
    println!("  Sec Hdr Flag   : {}", p.sec_hdr_flag);
    println!("  Seq Flags      : {} (0b{:02b})", p.seq_flags_str(), p.seq_flags);
    println!("  Sequence Count : {}", p.seq_count);
    println!("  Data Field Len : {} bytes", p.data_field_len());
    println!("  Coarse Time    : {} s (since Jan 1, 1958)", s.coarse_time);
    println!("  Fine Time      : {}", s.fine_time);
    println!("  Payload        : {:02X?}", &pkt.payload);
}

fn print_result(label: &str, result: &Result<CcsdPacket, CcsdError>) {
    match result {
        Ok(pkt)  => println!("{}: PASS — APID=0x{:03X}, Seq={}", 
                             label, pkt.primary.apid, pkt.primary.seq_count),
        Err(e)   => println!("{}: FAIL — {}", label, e),
    }
}

// ─── Main ─────────────────────────────────────────────────────────────────────

fn main() {
    let crc = Crc16::new();

    let payload: Vec<u8> = vec![
        0x1A, 0x2B, 0x3C, 0x4D, 0x5E, 0x6F, 0x70, 0x81,
        0x92, 0xA3, 0xB4, 0xC5, 0xD6, 0xE7, 0xF8, 0x09,
    ];

    // ── Build a valid packet ──────────────────────────────────────────────────
    let mut raw = build_packet(
        &crc,
        0x123,       // APID (e.g. attitude control subsystem)
        100,         // Sequence count
        3822844800,  // Coarse time (seconds since CCSDS epoch)
        1234,        // Fine time
        &payload,
    );

    println!("=== Built CCSDS Telemetry Packet ({} bytes) ===", raw.len());
    println!("  Raw bytes: {:02X?}", raw);
    println!();

    // Parse and display the clean packet
    if let Ok(ref pkt) = verify_and_parse(&crc, &raw) {
        println!("=== Parsed Fields ===");
        print_packet(pkt);
        println!();
    }

    // ── Tests ─────────────────────────────────────────────────────────────────
    println!("=== Verification Tests ===");

    // Test 1: Clean packet
    let r = verify_and_parse(&crc, &raw);
    print_result("[Test 1] Clean packet              ", &r);

    // Test 2: Single bit flip in payload
    raw[10] ^= 0x01;
    let r = verify_and_parse(&crc, &raw);
    print_result("[Test 2] Single bit flip           ", &r);
    raw[10] ^= 0x01; // restore

    // Test 3: Corrupt primary header APID
    raw[1] ^= 0x55;
    let r = verify_and_parse(&crc, &raw);
    print_result("[Test 3] Corrupted APID            ", &r);
    raw[1] ^= 0x55; // restore

    // Test 4: Truncated packet
    let r = verify_and_parse(&crc, &raw[..4]);
    print_result("[Test 4] Truncated packet          ", &r);

    // Test 5: Idle/fill packet (APID = 0x7FF)
    let idle = build_packet(&crc, IDLE_APID, 0, 0, 0, &[0u8; 4]);
    let r = verify_and_parse(&crc, &idle);
    print_result("[Test 5] Idle packet (APID=0x7FF)  ", &r);

    // Test 6: Sequence gap detection across multiple packets
    println!();
    println!("=== Sequence Gap Detection ===");
    let counts = [0u16, 1, 2, 4, 5, 9];  // gaps at 3 and 6,7,8
    let mut last_seq: Option<u16> = None;
    for &count in &counts {
        let pkt_raw = build_packet(&crc, 0x123, count, 0, 0, &payload);
        match verify_and_parse(&crc, &pkt_raw) {
            Ok(pkt) => {
                let seq = pkt.primary.seq_count;
                if let Some(prev) = last_seq {
                    let expected = (prev + 1) & 0x3FFF; // 14-bit wraparound
                    if seq != expected {
                        println!("  GAP DETECTED: expected seq {}, got {} ({} missing)",
                            expected, seq, seq.wrapping_sub(expected) & 0x3FFF);
                    } else {
                        println!("  Seq {:>5} — OK", seq);
                    }
                } else {
                    println!("  Seq {:>5} — First packet", seq);
                }
                last_seq = Some(seq);
            }
            Err(e) => println!("  Parse error: {}", e),
        }
    }

    println!();
    println!("=== Done ===");
}
