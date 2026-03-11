#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "ccsds_tables.h"

// ─────────────────────────────────────────────────────────────────────────────
// Runtime Mission Config
// Loaded from mission_config.json at startup. These are the only tables
// used at runtime — ccsds_tables.h provides the types and logic only.
// ─────────────────────────────────────────────────────────────────────────────

#define MAX_RUNTIME_APIDS   32
#define MAX_RUNTIME_LIMITS  64

typedef struct {
    ApidEntry  apids[MAX_RUNTIME_APIDS];
    size_t     apid_count;
    LimitEntry limits[MAX_RUNTIME_LIMITS];
    size_t     limit_count;
} MissionConfig;

// ─────────────────────────────────────────────────────────────────────────────
// Minimal JSON loader
// In production use cJSON or jsmn. This hand-rolled parser avoids external
// dependencies while demonstrating the loading concept.
// ─────────────────────────────────────────────────────────────────────────────

static int json_get_string(const char *json, const char *key,
                            char *out, size_t out_len) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return 0;
    p += strlen(search);
    while (*p == ' ' || *p == ':') p++;
    if (*p == '"') {
        p++;
        size_t i = 0;
        while (*p && *p != '"' && i < out_len - 1)
            out[i++] = *p++;
        out[i] = '\0';
        return 1;
    }
    return 0;
}

static int json_get_float(const char *json, const char *key, float *out) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return 0;
    p += strlen(search);
    while (*p == ' ' || *p == ':') p++;
    *out = (float)atof(p);
    return 1;
}

static uint16_t parse_hex(const char *s) {
    return (uint16_t)strtol(s, NULL, 16);
}

static PacketType parse_packet_type(const char *s) {
    if (strcmp(s, "housekeeping") == 0) return PKT_TYPE_HOUSEKEEPING;
    if (strcmp(s, "science")      == 0) return PKT_TYPE_SCIENCE;
    if (strcmp(s, "adcs")         == 0) return PKT_TYPE_ADCS;
    if (strcmp(s, "power")        == 0) return PKT_TYPE_POWER;
    if (strcmp(s, "comms")        == 0) return PKT_TYPE_COMMS;
    if (strcmp(s, "payload")      == 0) return PKT_TYPE_PAYLOAD;
    if (strcmp(s, "idle")         == 0) return PKT_TYPE_IDLE;
    return PKT_TYPE_UNKNOWN;
}

int load_mission_config(const char *filename, MissionConfig *cfg) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Cannot open config: %s\n", filename);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *buf = malloc(size + 1);
    if (!buf) { fclose(f); return 0; }
    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);

    cfg->apid_count  = 0;
    cfg->limit_count = 0;

    // Parse apid_table
    const char *section = strstr(buf, "\"apid_table\"");
    if (section) {
        const char *p = strchr(section, '[');
        while (p && cfg->apid_count < MAX_RUNTIME_APIDS) {
            const char *s = strchr(p, '{');
            const char *e = strchr(p, '}');
            if (!s || !e) break;
            size_t len = e - s + 1;
            char entry[256] = {0};
            if (len < sizeof(entry)) {
                strncpy(entry, s, len);
                ApidEntry *a = &cfg->apids[cfg->apid_count];
                char apid_str[16] = {0};
                char type_str[32] = {0};
                if (json_get_string(entry, "apid",        apid_str,      sizeof(apid_str)) &&
                    json_get_string(entry, "type",        type_str,      sizeof(type_str)) &&
                    json_get_string(entry, "subsystem",   a->subsystem,  sizeof(a->subsystem)) &&
                    json_get_string(entry, "description", a->description,sizeof(a->description))) {
                    a->apid = parse_hex(apid_str);
                    a->type = parse_packet_type(type_str);
                    cfg->apid_count++;
                }
            }
            p = e + 1;
        }
    }

    // Parse limits_table
    section = strstr(buf, "\"limits_table\"");
    if (section) {
        const char *p = strchr(section, '[');
        while (p && cfg->limit_count < MAX_RUNTIME_LIMITS) {
            const char *s = strchr(p, '{');
            const char *e = strchr(p, '}');
            if (!s || !e) break;
            size_t len = e - s + 1;
            char entry[256] = {0};
            if (len < sizeof(entry)) {
                strncpy(entry, s, len);
                LimitEntry *l = &cfg->limits[cfg->limit_count];
                char apid_str[16] = {0};
                if (json_get_string(entry, "apid",        apid_str,      sizeof(apid_str)) &&
                    json_get_string(entry, "param",       l->param_name, sizeof(l->param_name)) &&
                    json_get_string(entry, "units",       l->units,      sizeof(l->units)) &&
                    json_get_float (entry, "red_low",    &l->red_low)    &&
                    json_get_float (entry, "yellow_low", &l->yellow_low) &&
                    json_get_float (entry, "yellow_high",&l->yellow_high) &&
                    json_get_float (entry, "red_high",   &l->red_high)) {
                    l->apid = parse_hex(apid_str);
                    cfg->limit_count++;
                }
            }
            p = e + 1;
        }
    }

    free(buf);
    printf("Loaded %zu APIDs and %zu limits from %s\n\n",
           cfg->apid_count, cfg->limit_count, filename);
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo — APID lookup
// ─────────────────────────────────────────────────────────────────────────────

void demo_apid_lookup(const MissionConfig *cfg) {
    printf("--- APID Table Lookup ---\n");
    uint16_t test_apids[] = { 0x001, 0x020, 0x030, 0x100, 0x7FF, 0x999 };
    for (size_t i = 0; i < sizeof(test_apids)/sizeof(test_apids[0]); i++) {
        uint16_t apid = test_apids[i];
        const ApidEntry *e = apid_lookup(cfg->apids, cfg->apid_count, apid);
        if (e)
            printf("  APID 0x%03X → [%-8s] %s\n", apid, e->subsystem, e->description);
        else
            printf("  APID 0x%03X → NOT FOUND\n", apid);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo — Sequence gap detection
// ─────────────────────────────────────────────────────────────────────────────

void demo_seq_tracking(void) {
    printf("\n--- Sequence Gap Detection ---\n");
    SeqTrackTable seq_tbl;
    seq_track_init(&seq_tbl);

    uint16_t adcs_stream[] = { 0, 1, 2, 4, 5, 9, 10, 11 };
    for (size_t i = 0; i < sizeof(adcs_stream)/sizeof(adcs_stream[0]); i++) {
        uint16_t seq     = adcs_stream[i];
        uint16_t missing = seq_track_update(&seq_tbl, 0x020, seq);
        if (missing > 0)
            printf("  APID 0x020 seq %3u — GAP: %u packet(s) missing\n", seq, missing);
        else
            printf("  APID 0x020 seq %3u — OK\n", seq);
    }

    SeqTrackEntry *e = seq_track_get(&seq_tbl, 0x020);
    if (e) {
        printf("\n  APID 0x020 Summary:\n");
        printf("    Total received : %u\n", e->total_received);
        printf("    Gap events     : %u\n", e->total_gaps);
        printf("    Total missing  : %u\n", e->total_missing);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo — Engineering limits check
// ─────────────────────────────────────────────────────────────────────────────

void demo_limits(const MissionConfig *cfg) {
    printf("\n--- Engineering Limits Check ---\n");

    struct { const char *param; float value; } eps_values[] = {
        { "bus_voltage",   28.3f  },  // Normal
        { "bus_voltage",   23.5f  },  // Yellow — low
        { "bus_voltage",   21.0f  },  // Red — critically low
        { "battery_temp",  25.0f  },  // Normal
        { "battery_temp",  47.0f  },  // Yellow — high
        { "solar_current",  0.05f },  // Yellow — low current
    };

    for (size_t i = 0; i < sizeof(eps_values)/sizeof(eps_values[0]); i++) {
        const LimitEntry *lim = limit_lookup(cfg->limits, cfg->limit_count,
                                              0x010, eps_values[i].param);
        if (lim) {
            LimitStatus s = limit_check(lim, eps_values[i].value);
            printf("  EPS %-20s = %7.2f %-6s → %s\n",
                   eps_values[i].param,
                   eps_values[i].value,
                   lim->units,
                   limit_status_str(s));
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

int main(void) {
    MissionConfig cfg;

    if (!load_mission_config("mission_config.json", &cfg))
        return 1;

    demo_apid_lookup(&cfg);
    demo_seq_tracking();
    demo_limits(&cfg);

    return 0;
}
