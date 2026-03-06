#include <stdio.h>
#include <stdint.h>
#include <string.h>

// CCSDS uses CRC-16/CCITT-FALSE
// Polynomial: 0x1021, Init: 0xFFFF, No reflection
#define CRC16_POLYNOMIAL 0x1021
#define CRC16_INIT       0xFFFF

// ─────────────────────────────────────────────
// CCSDS Space Packet Primary Header (6 bytes)
// Per CCSDS 133.0-B-2
// ─────────────────────────────────────────────
typedef struct {
    uint16_t packet_id;        // Version(3) | Type(1) | SecHdrFlag(1) | APID(11)
    uint16_t packet_seq_ctrl;  // SeqFlags(2) | Sequence Count(14)
    uint16_t packet_data_len;  // Total packet length - 7 (per CCSDS spec)
} __attribute__((packed)) CCSDSPrimaryHeader;

// Secondary header (optional, simplified timecode here)
typedef struct {
    uint32_t coarse_time;      // Seconds since CCSDS epoch (Jan 1, 1958)
    uint16_t fine_time;        // Sub-second resolution
} __attribute__((packed)) CCSDSSecondaryHeader;

// Full telemetry packet with payload and CRC
typedef struct {
    CCSDSPrimaryHeader  primary;
    CCSDSSecondaryHeader secondary;
    uint8_t             payload[16];   // Simulated sensor data
    uint16_t            crc;           // CRC-16 appended at end
} __attribute__((packed)) CCSDSPacket;

// ─────────────────────────────────────────────
// CRC-16/CCITT Table-driven implementation
// ─────────────────────────────────────────────
uint16_t crc16_table[256];

void build_crc16_table(void) {
    for (int i = 0; i < 256; i++) {
        uint16_t crc = (uint16_t)(i << 8);
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ CRC16_POLYNOMIAL;
            else
                crc <<= 1;
        }
        crc16_table[i] = crc;
    }
}

uint16_t calculate_crc16(const uint8_t *data, size_t length) {
    uint16_t crc = CRC16_INIT;
    for (size_t i = 0; i < length; i++) {
        uint8_t index = (uint8_t)((crc >> 8) ^ data[i]);
        crc = (crc << 8) ^ crc16_table[index];
    }
    return crc;
}

// ─────────────────────────────────────────────
// CCSDS field helpers
// ─────────────────────────────────────────────

// Extract APID from Packet ID field
uint16_t get_apid(const CCSDSPrimaryHeader *hdr) {
    return __builtin_bswap16(hdr->packet_id) & 0x07FF;
}

// Extract Sequence Count
uint16_t get_sequence_count(const CCSDSPrimaryHeader *hdr) {
    return __builtin_bswap16(hdr->packet_seq_ctrl) & 0x3FFF;
}

// Extract Sequence Flags (00=continuation, 01=first, 10=last, 11=standalone)
uint8_t get_sequence_flags(const CCSDSPrimaryHeader *hdr) {
    return (__builtin_bswap16(hdr->packet_seq_ctrl) >> 14) & 0x03;
}

// Packet data length: actual user data length = field value + 1
uint16_t get_user_data_length(const CCSDSPrimaryHeader *hdr) {
    return __builtin_bswap16(hdr->packet_data_len) + 1;
}

// ─────────────────────────────────────────────
// Build a simulated CCSDS telemetry packet
// ─────────────────────────────────────────────
void build_packet(CCSDSPacket *pkt, uint16_t apid, uint16_t seq_count) {
    memset(pkt, 0, sizeof(CCSDSPacket));

    // Primary header
    // Version=000, Type=0 (telemetry), SecHdrFlag=1, APID
    uint16_t pid = (0 << 13) | (0 << 12) | (1 << 11) | (apid & 0x07FF);
    pkt->primary.packet_id = __builtin_bswap16(pid);

    // Sequence flags = 11 (standalone), sequence count
    uint16_t psc = (0x03 << 14) | (seq_count & 0x3FFF);
    pkt->primary.packet_seq_ctrl = __builtin_bswap16(psc);

    // Data length = (secondary header + payload + CRC) - 1
    uint16_t data_len = sizeof(CCSDSSecondaryHeader)
                      + sizeof(pkt->payload)
                      + sizeof(pkt->crc) - 1;
    pkt->primary.packet_data_len = __builtin_bswap16(data_len);

    // Secondary header — seconds since Jan 1 1958 epoch
    pkt->secondary.coarse_time = __builtin_bswap32(2082844800 + 1740000000);
    pkt->secondary.fine_time   = __builtin_bswap16(1234);

    // Simulated payload (temperature, voltage, attitude raw counts)
    uint8_t fake_data[] = {
        0x1A, 0x2B, 0x3C, 0x4D, 0x5E, 0x6F, 0x70, 0x81,
        0x92, 0xA3, 0xB4, 0xC5, 0xD6, 0xE7, 0xF8, 0x09
    };
    memcpy(pkt->payload, fake_data, sizeof(pkt->payload));

    // CRC covers everything except the CRC field itself
    size_t crc_len = sizeof(CCSDSPacket) - sizeof(uint16_t);
    uint16_t crc = calculate_crc16((const uint8_t *)pkt, crc_len);
    pkt->crc = __builtin_bswap16(crc);  // Big-endian on wire
}

// ─────────────────────────────────────────────
// Verify a received CCSDS packet
// ─────────────────────────────────────────────
int verify_ccsds_packet(const CCSDSPacket *pkt) {
    size_t crc_len = sizeof(CCSDSPacket) - sizeof(uint16_t);
    uint16_t computed = calculate_crc16((const uint8_t *)pkt, crc_len);
    uint16_t received = __builtin_bswap16(pkt->crc);
    return computed == received;
}

void print_packet_info(const CCSDSPacket *pkt) {
    printf("  APID            : 0x%03X (%u)\n",
           get_apid(&pkt->primary), get_apid(&pkt->primary));
    printf("  Sequence Count  : %u\n",    get_sequence_count(&pkt->primary));
    printf("  Sequence Flags  : 0x%X\n",  get_sequence_flags(&pkt->primary));
    printf("  User Data Len   : %u bytes\n", get_user_data_length(&pkt->primary));
    printf("  Coarse Time     : %u\n",    __builtin_bswap32(pkt->secondary.coarse_time));
    printf("  Fine Time       : %u\n",    __builtin_bswap16(pkt->secondary.fine_time));
    printf("  CRC-16 (wire)   : 0x%04X\n", __builtin_bswap16(pkt->crc));
}

// ─────────────────────────────────────────────
// Main — build, verify, corrupt, re-verify
// ─────────────────────────────────────────────
int main(void) {
    build_crc16_table();

    CCSDSPacket pkt;
    build_packet(&pkt, 0x123, 100);  // APID 0x123, sequence count 100

    printf("=== CCSDS Telemetry Packet ===\n");
    print_packet_info(&pkt);

    printf("\n[Test 1] Verify clean packet          : ");
    printf("%s\n", verify_ccsds_packet(&pkt) ? "PASS - CRC OK" : "FAIL");

    // Simulate a bit error in the payload
    printf("[Test 2] Flip a bit in payload         : ");
    pkt.payload[4] ^= 0x01;
    printf("%s\n", verify_ccsds_packet(&pkt) ? "PASS (unexpected)" : "FAIL - Corruption detected!");

    // Restore and recompute
    pkt.payload[4] ^= 0x01;
    size_t crc_len = sizeof(CCSDSPacket) - sizeof(uint16_t);
    uint16_t crc = calculate_crc16((const uint8_t *)&pkt, crc_len);
    pkt.crc = __builtin_bswap16(crc);

    printf("[Test 3] Restored and recomputed CRC   : ");
    printf("%s\n", verify_ccsds_packet(&pkt) ? "PASS - CRC OK" : "FAIL");

    // Simulate wrong APID (routing error)
    printf("[Test 4] Corrupt primary header APID   : ");
    pkt.primary.packet_id ^= 0x0500;
    printf("%s\n", verify_ccsds_packet(&pkt) ? "PASS (unexpected)" : "FAIL - Header corruption detected!");

    return 0;
}
