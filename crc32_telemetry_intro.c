#include <stdio.h>
#include <stdint.h>
#include <string.h>

// CRC-32 polynomial (IEEE 802.3)
#define CRC32_POLYNOMIAL 0xEDB88320

// Simulated satellite telemetry packet
typedef struct {
    uint16_t spacecraft_id;
    uint32_t timestamp;
    float    temperature;   // degrees Celsius
    float    voltage;       // bus voltage
    float    attitude[3];   // roll, pitch, yaw in degrees
    uint32_t sequence_num;
    uint32_t crc;           // CRC appended last
} TelemetryPacket;

// Build CRC-32 lookup table
uint32_t crc_table[256];

void build_crc_table(void) {
    for (int i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ CRC32_POLYNOMIAL;
            else
                crc >>= 1;
        }
        crc_table[i] = crc;
    }
}

// Calculate CRC-32 over a byte buffer
uint32_t calculate_crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        uint8_t index = (crc ^ data[i]) & 0xFF;
        crc = (crc >> 8) ^ crc_table[index];
    }
    return crc ^ 0xFFFFFFFF;  // Final XOR
}

// Populate a fake telemetry packet
void create_telemetry_packet(TelemetryPacket *pkt) {
    pkt->spacecraft_id = 0x1A2B;
    pkt->timestamp     = 1740000000;  // Unix-style mission epoch
    pkt->temperature   = -45.7f;
    pkt->voltage       = 28.3f;
    pkt->attitude[0]   = 0.12f;      // roll
    pkt->attitude[1]   = -1.05f;     // pitch
    pkt->attitude[2]   = 178.9f;     // yaw

    pkt->sequence_num  = 42;
    pkt->crc           = 0;          // Zero out before computing

    // Compute CRC over everything except the crc field itself
    size_t data_len = sizeof(TelemetryPacket) - sizeof(uint32_t);
    pkt->crc = calculate_crc32((const uint8_t *)pkt, data_len);
}

// Verify the packet on the receiving end
int verify_packet(const TelemetryPacket *pkt) {
    size_t data_len = sizeof(TelemetryPacket) - sizeof(uint32_t);
    uint32_t computed = calculate_crc32((const uint8_t *)pkt, data_len);
    return computed == pkt->crc;
}

void print_packet(const TelemetryPacket *pkt) {
    printf("  Spacecraft ID : 0x%04X\n", pkt->spacecraft_id);
    printf("  Timestamp     : %u\n",     pkt->timestamp);
    printf("  Temperature   : %.2f C\n", pkt->temperature);
    printf("  Voltage       : %.2f V\n", pkt->voltage);
    printf("  Attitude      : Roll=%.2f  Pitch=%.2f  Yaw=%.2f\n",
           pkt->attitude[0], pkt->attitude[1], pkt->attitude[2]);
    printf("  Sequence #    : %u\n",     pkt->sequence_num);
    printf("  CRC-32        : 0x%08X\n", pkt->crc);
}

int main(void) {
    build_crc_table();

    TelemetryPacket pkt;
    create_telemetry_packet(&pkt);

    printf("=== Satellite Telemetry Packet ===\n");
    print_packet(&pkt);

    // --- Test 1: Clean packet ---
    printf("\n[Test 1] Verifying clean packet... ");
    printf("%s\n", verify_packet(&pkt) ? "PASS - CRC OK" : "FAIL - CRC mismatch");

    // --- Test 2: Simulate single-bit corruption ---
    printf("[Test 2] Simulating bit-flip corruption...\n");
    pkt.voltage = 99.9f;   // corrupted in transit
    printf("  Verifying corrupted packet... ");
    printf("%s\n", verify_packet(&pkt) ? "PASS (unexpected)" : "FAIL - Corruption detected!");

    // --- Test 3: Restore and re-verify ---
    pkt.voltage = 28.3f;
    pkt.crc = 0;
    size_t data_len = sizeof(TelemetryPacket) - sizeof(uint32_t);
    pkt.crc = calculate_crc32((const uint8_t *)&pkt, data_len);
    printf("[Test 3] After restoring data and recomputing CRC... ");
    printf("%s\n", verify_packet(&pkt) ? "PASS - CRC OK" : "FAIL");

    return 0;
}
