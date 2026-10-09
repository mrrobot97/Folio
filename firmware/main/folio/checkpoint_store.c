/*
 * SPDX-FileCopyrightText: 2026 Folio contributors
 * SPDX-License-Identifier: Apache-2.0
 * 双文件检查点使用固定小端头、CRC32 和落盘后回读。
 * Dual-file checkpoints use a fixed little-endian header, CRC32 and readback.
 * 冻结：始终只改非当前槽；新 schema 阻止旧固件覆盖数据。
 * Frozen: mutate only the inactive slot; new schemas prevent older firmware writes.
 */
#include "checkpoint_store.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define HEADER_LEN 28
#define MAX_PAYLOAD (1024U * 1024U)
typedef struct { folio_checkpoint_result_t result; uint64_t generation; uint32_t len; } slot_t;
static uint32_t get32(const unsigned char* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put32(unsigned char* p, uint32_t n) {
    for (int i = 0; i < 4; ++i) p[i] = (unsigned char)(n >> (i * 8));
}
static uint32_t crc_update(uint32_t crc, const unsigned char* p, size_t n) {
    while (n--) {
        crc ^= *p++;
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return crc;
}
static bool paths(const char* directory, const char* stem, char names[2][384]) {
    if (!directory || !stem || !stem[0]) return false;
    for (const char* c = stem; *c; ++c)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') ||
              (*c >= '0' && *c <= '9') || *c == '_')) return false;
    struct stat st;
    if (stat(directory, &st) || !S_ISDIR(st.st_mode)) return false;
    for (int i = 0; i < 2; ++i) {
        int len = snprintf(names[i], 384, "%s/%s.%c", directory, stem, 'a' + i);
        if (len < 0 || len >= 384) return false;
    }
    return true;
}
static slot_t inspect(const char* path, uint32_t schema) {
    slot_t slot = { .result = FOLIO_CHECKPOINT_CORRUPT };
    FILE* f = fopen(path, "rb");
    if (!f) {
        slot.result = errno == ENOENT ? FOLIO_CHECKPOINT_MISSING : FOLIO_CHECKPOINT_IO;
        return slot;
    }
    unsigned char header[HEADER_LEN], buffer[512];
    if (fread(header, 1, sizeof(header), f) != sizeof(header)) goto done;
    if (memcmp(header, "FOLIOCP1", 8)) goto done;
    // 未知版本即使 CRC 损坏也不覆盖，以免降级销毁新数据。
    // Never overwrite unknown versions, even with bad CRC, to protect newer data during downgrade.
    if (get32(header + 8) != schema) { slot.result = FOLIO_CHECKPOINT_SCHEMA; goto done; }
    slot.len = get32(header + 12);
    slot.generation = (uint64_t)get32(header + 16) | (uint64_t)get32(header + 20) << 32;
    if (slot.len > MAX_PAYLOAD || !slot.generation) goto done;
    uint32_t crc = crc_update(UINT32_MAX, header, 24);
    size_t left = slot.len;
    while (left) {
        size_t n = left < sizeof(buffer) ? left : sizeof(buffer);
        if (fread(buffer, 1, n, f) != n) goto done;
        crc = crc_update(crc, buffer, n);
        left -= n;
    }
    if ((crc ^ UINT32_MAX) != get32(header + 24) || fgetc(f) != EOF) goto done;
    slot.result = FOLIO_CHECKPOINT_OK;
done:
    if (ferror(f)) slot.result = FOLIO_CHECKPOINT_IO;
    if (fclose(f)) slot.result = FOLIO_CHECKPOINT_IO;
    return slot;
}
static int newest(const slot_t slots[2]) {
    if (slots[0].result == FOLIO_CHECKPOINT_OK && slots[1].result == FOLIO_CHECKPOINT_OK)
        return slots[1].generation > slots[0].generation ? 1 : 0;
    if (slots[0].result == FOLIO_CHECKPOINT_OK) return 0;
    return slots[1].result == FOLIO_CHECKPOINT_OK ? 1 : -1;
}
static folio_checkpoint_result_t no_valid(const slot_t slots[2]) {
    for (int i = 0; i < 2; ++i) if (slots[i].result == FOLIO_CHECKPOINT_SCHEMA) return FOLIO_CHECKPOINT_SCHEMA;
    for (int i = 0; i < 2; ++i) if (slots[i].result == FOLIO_CHECKPOINT_IO) return FOLIO_CHECKPOINT_IO;
    if (slots[0].result == FOLIO_CHECKPOINT_MISSING && slots[1].result == FOLIO_CHECKPOINT_MISSING)
        return FOLIO_CHECKPOINT_MISSING;
    return FOLIO_CHECKPOINT_CORRUPT;
}
folio_checkpoint_result_t folio_checkpoint_load(const char* directory, const char* stem,
        uint32_t schema, void* data, size_t cap, size_t* len, uint64_t* generation) {
    char names[2][384];
    if (!paths(directory, stem, names) || !data || !len || !generation) return FOLIO_CHECKPOINT_IO;
    slot_t slots[2] = {inspect(names[0], schema), inspect(names[1], schema)};
    if (slots[0].result == FOLIO_CHECKPOINT_SCHEMA || slots[1].result == FOLIO_CHECKPOINT_SCHEMA)
        return FOLIO_CHECKPOINT_SCHEMA;
    int selected = newest(slots);
    if (selected < 0) return no_valid(slots);
    if (slots[selected].len > cap) return FOLIO_CHECKPOINT_SIZE;
    FILE* f = fopen(names[selected], "rb");
    if (!f) return FOLIO_CHECKPOINT_IO;
    bool ok = fseek(f, HEADER_LEN, SEEK_SET) == 0 && fread(data, 1, slots[selected].len, f) == slots[selected].len;
    if (fclose(f)) ok = false;
    if (!ok) return FOLIO_CHECKPOINT_IO;
    *len = slots[selected].len;
    *generation = slots[selected].generation;
    return FOLIO_CHECKPOINT_OK;
}
folio_checkpoint_result_t folio_checkpoint_save(const char* directory, const char* stem,
        uint32_t schema, const void* data, size_t len, uint64_t* generation) {
    char names[2][384];
    if (!paths(directory, stem, names) || !data || !generation) return FOLIO_CHECKPOINT_IO;
    if (len > MAX_PAYLOAD) return FOLIO_CHECKPOINT_SIZE;
    slot_t slots[2] = {inspect(names[0], schema), inspect(names[1], schema)};
    for (int i = 0; i < 2; ++i)
        if (slots[i].result == FOLIO_CHECKPOINT_SCHEMA || slots[i].result == FOLIO_CHECKPOINT_IO) return slots[i].result;
    int current = newest(slots);
    if (current < 0 && no_valid(slots) != FOLIO_CHECKPOINT_MISSING) return no_valid(slots);
    uint64_t next = current < 0 ? 1 : slots[current].generation + 1;
    if (!next) return FOLIO_CHECKPOINT_SIZE;
    int target = current < 0 ? 0 : 1 - current;
    unsigned char header[HEADER_LEN] = {0};
    memcpy(header, "FOLIOCP1", 8);
    put32(header + 8, schema); put32(header + 12, (uint32_t)len);
    put32(header + 16, (uint32_t)next); put32(header + 20, (uint32_t)(next >> 32));
    uint32_t crc = crc_update(UINT32_MAX, header, 24);
    crc = crc_update(crc, data, len) ^ UINT32_MAX;
    put32(header + 24, crc);
    FILE* f = fopen(names[target], "wb");
    if (!f) return FOLIO_CHECKPOINT_IO;
    bool ok = fwrite(header, 1, sizeof(header), f) == sizeof(header) && fwrite(data, 1, len, f) == len;
    if (fflush(f)) ok = false;
    if (ok && fsync(fileno(f))) ok = false;
    if (fclose(f)) ok = false;
    if (!ok) return FOLIO_CHECKPOINT_IO;
    slot_t written = inspect(names[target], schema);
    if (written.result != FOLIO_CHECKPOINT_OK || written.generation != next) return FOLIO_CHECKPOINT_IO;
    *generation = next;
    return FOLIO_CHECKPOINT_OK;
}
