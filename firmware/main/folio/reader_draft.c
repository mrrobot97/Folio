/*
 * SPDX-FileCopyrightText: 2026 Folio contributors
 * SPDX-License-Identifier: Apache-2.0
 * 字号会话、已应用偏好和休眠来源的可恢复双槽事务。
 * Recoverable dual-slot transactions for font-size sessions, active preferences and suspension origins.
 * 冻结：暂存不应用；未开放的 DEFAULTS/字体子流程拒绝持久恢复，原始记录不自动删除。
 * Frozen: checkpointing never applies; unsupported DEFAULTS/font children fail closed, preserving raw records.
 */
#include "reader_draft.h"
#include "checkpoint_store.h"
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>

#define DRAFT_SCHEMA 3
#define DRAFT_BYTES 640
#define RESUME_BYTES (FOLIO_READING_PATH_MAX + 24)
static void put(unsigned char** at, uint64_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) *(*at)++ = (unsigned char)(value >> (8 * i));
}
static uint64_t get(const unsigned char** at, unsigned bytes) {
    uint64_t value = 0;
    for (unsigned i = 0; i < bytes; ++i) value |= (uint64_t)*(*at)++ << (8 * i);
    return value;
}
static bool valid_px(int px) { return px >= 36 && px <= 72 && (px - 36) % 4 == 0; }
static bool draft_stem(const char* path, char out[32]) {
    if (!path || !path[0] || strlen(path) >= FOLIO_READING_PATH_MAX) return false;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (const unsigned char* p = (const unsigned char*)path; *p; ++p) hash = (hash ^ *p) * UINT64_C(1099511628211);
    snprintf(out, 32, "layout_%016llx", (unsigned long long)hash);
    return true;
}
static bool new_book_within_quota(const char* directory) {
    DIR* dir = opendir(directory);
    if (!dir) return false;
    size_t records = 0;
    struct dirent* entry;
    while ((entry = readdir(dir))) {
        size_t len = strlen(entry->d_name);
        // 每本计一次；仅剩 b 槽或损坏记录也不自动回收。
        // Count each book once; retain b-only or damaged records without automatic reclamation.
        if (len == 25 && !strncmp(entry->d_name, "layout_", 7) && entry->d_name[23] == '.' &&
            (entry->d_name[24] == 'a' || entry->d_name[24] == 'b')) {
            if (entry->d_name[24] == 'a') ++records;
            else {
                char path[384]; struct stat st;
                int len = snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
                if (len < 0 || (size_t)len >= sizeof(path)) { closedir(dir); return false; }
                path[len - 1] = 'a';
                if (stat(path, &st)) ++records;
            }
        }
    }
    bool ok = closedir(dir) == 0;
    return ok && records < FOLIO_FORMAT_MAX_BOOKS;
}
static bool valid(const folio_reader_draft_t* d) {
    if (!d || !memchr(d->path, 0, sizeof(d->path)) || !d->path[0] ||
        !memchr(d->font_path, 0, sizeof(d->font_path)) || !valid_px(d->applied_px) || !valid_px(d->draft_px) ||
        !valid_px(d->field_entry_px) || !valid_px(d->pending_px) || !d->session_id || !d->revision ||
        d->base_revision == UINT64_MAX || d->pending_px != d->draft_px ||
        d->scope != FOLIO_FORMAT_BOOK || d->supported_fields != FOLIO_FORMAT_FIELD_SIZE || d->pending_fields ||
        d->font_child || d->return_depth > FOLIO_FORMAT_RETURN_DEPTH ||
        (d->resume_origin != FOLIO_RESUME_READER && d->resume_origin != FOLIO_RESUME_EDITOR) ||
        d->panel > 1 || d->selection > 8 ||
        (!d->editing && (d->applied_px != d->draft_px || d->editor_visible))) return false;
    for (unsigned i = 0; i < d->return_depth; ++i) if (d->return_stack[i] < 1 || d->return_stack[i] > 2) return false;
    return true;
}
static void encode(const folio_reader_draft_t* d, unsigned char data[DRAFT_BYTES]) {
    memset(data, 0, DRAFT_BYTES);
    unsigned char* p = data;
    memcpy(p, d->path, sizeof(d->path)); p += sizeof(d->path);
    put(&p, d->file_size, 4); put(&p, d->applied_px, 4); put(&p, d->draft_px, 4);
    put(&p, d->field_entry_px, 4); put(&p, d->pending_px, 4);
    put(&p, d->scope, 1); put(&p, d->editing, 1); put(&p, d->editor_visible, 1); put(&p, d->font_child, 1);
    put(&p, d->supported_fields, 4); put(&p, d->pending_fields, 4);
    put(&p, d->session_id, 8); put(&p, d->revision, 8); put(&p, d->base_revision, 8); put(&p, d->suspension_id, 8);
    put(&p, d->resume_origin, 1); put(&p, d->panel, 1); put(&p, d->selection, 1); put(&p, d->return_depth, 1);
    memcpy(p, d->return_stack, sizeof(d->return_stack)); p += sizeof(d->return_stack);
    memcpy(p, d->font_path, sizeof(d->font_path)); p += sizeof(d->font_path);
    put(&p, d->font_file_size, 4); put(&p, d->book_mtime, 8);
}
static bool decode(const unsigned char data[DRAFT_BYTES], folio_reader_draft_t* d) {
    memset(d, 0, sizeof(*d));
    const unsigned char* p = data;
    memcpy(d->path, p, sizeof(d->path)); p += sizeof(d->path);
    d->file_size = (uint32_t)get(&p, 4); d->applied_px = (int)get(&p, 4); d->draft_px = (int)get(&p, 4);
    d->field_entry_px = (int)get(&p, 4); d->pending_px = (int)get(&p, 4);
    d->scope = (folio_format_scope_t)get(&p, 1);
    unsigned editing = (unsigned)get(&p, 1), visible = (unsigned)get(&p, 1);
    d->editing = editing; d->editor_visible = visible; d->font_child = (uint8_t)get(&p, 1);
    d->supported_fields = (uint32_t)get(&p, 4); d->pending_fields = (uint32_t)get(&p, 4);
    d->session_id = get(&p, 8); d->revision = get(&p, 8); d->base_revision = get(&p, 8); d->suspension_id = get(&p, 8);
    d->resume_origin = (folio_format_origin_t)get(&p, 1); d->panel = (uint8_t)get(&p, 1);
    d->selection = (uint8_t)get(&p, 1); d->return_depth = (uint8_t)get(&p, 1);
    memcpy(d->return_stack, p, sizeof(d->return_stack)); p += sizeof(d->return_stack);
    memcpy(d->font_path, p, sizeof(d->font_path)); p += sizeof(d->font_path);
    d->font_file_size = (uint32_t)get(&p, 4); d->book_mtime = get(&p, 8);
    while (p < data + DRAFT_BYTES) if (*p++) return false;
    d->dirty = d->applied_px != d->draft_px;
    return editing <= 1 && visible <= 1 && valid(d);
}
folio_checkpoint_result_t folio_reader_draft_load_result(const char* directory, const char* path, folio_reader_draft_t* out) {
    char stem[32];
    if (!out || !draft_stem(path, stem)) return FOLIO_CHECKPOINT_IO;
    unsigned char data[DRAFT_BYTES]; size_t len = 0; uint64_t generation = 0;
    folio_checkpoint_result_t result=folio_checkpoint_load(directory, stem, DRAFT_SCHEMA, data, sizeof(data), &len, &generation);
    if(result!=FOLIO_CHECKPOINT_OK)return result;
    folio_reader_draft_t value;
    if (len!=sizeof(data)||!decode(data, &value) || strcmp(value.path, path)) return FOLIO_CHECKPOINT_CORRUPT;
    value.confirmed_generation = generation;
    *out = value;
    return FOLIO_CHECKPOINT_OK;
}
bool folio_reader_draft_load(const char* directory,const char* path,folio_reader_draft_t* out){return folio_reader_draft_load_result(directory,path,out)==FOLIO_CHECKPOINT_OK;}
bool folio_reader_draft_save(const char* directory, const folio_reader_draft_t* draft) {
    char stem[32];
    if (!valid(draft) || !draft_stem(draft->path, stem)) return false;
    unsigned char data[DRAFT_BYTES]; size_t len = 0; uint64_t generation = 0;
    folio_checkpoint_result_t result = folio_checkpoint_load(directory, stem, DRAFT_SCHEMA, data, sizeof(data), &len, &generation);
    if (result != FOLIO_CHECKPOINT_OK && result != FOLIO_CHECKPOINT_MISSING) return false;
    if (result == FOLIO_CHECKPOINT_MISSING && !new_book_within_quota(directory)) return false;
    if (result == FOLIO_CHECKPOINT_OK) {
        folio_reader_draft_t previous;
        if (len != sizeof(data) || !decode(data, &previous) || strcmp(previous.path, draft->path) ||
            previous.file_size != draft->file_size || (previous.book_mtime && draft->book_mtime && previous.book_mtime != draft->book_mtime)) return false;
    }
    encode(draft, data);
    return folio_checkpoint_save(directory, stem, DRAFT_SCHEMA, data, sizeof(data), &generation) == FOLIO_CHECKPOINT_OK;
}
void folio_reader_draft_begin(folio_reader_draft_t* draft, const char* path, uint32_t size, int applied_px) {
    if (!draft || !path || strlen(path) >= sizeof(draft->path) || !valid_px(applied_px)) return;
    bool different = strcmp(draft->path, path) || draft->file_size != size || draft->applied_px != applied_px;
    if (different) {
        memset(draft, 0, sizeof(*draft));
        memcpy(draft->path, path, strlen(path) + 1);
        draft->file_size = size; draft->applied_px = draft->draft_px = applied_px;
    }
    if (!draft->editing) {
        if (draft->session_id == UINT64_MAX) return;
        ++draft->session_id; draft->revision = 1;
        draft->scope = FOLIO_FORMAT_BOOK; draft->supported_fields = FOLIO_FORMAT_FIELD_SIZE;
        draft->field_entry_px = draft->pending_px = draft->draft_px;
        draft->return_depth = 1; draft->return_stack[0] = FOLIO_RESUME_READER;
        draft->resume_origin = FOLIO_RESUME_EDITOR; draft->panel = 1;
    }
    draft->editing = draft->editor_visible = true;
}
bool folio_reader_draft_adjust(folio_reader_draft_t* draft, int direction) {
    if (!draft || !draft->editing || draft->revision == UINT64_MAX || (direction != -1 && direction != 1)) return false;
    int next = draft->draft_px + direction * 4;
    if (!valid_px(next)) return false;
    draft->draft_px = draft->pending_px = next; ++draft->revision;
    draft->dirty = draft->draft_px != draft->applied_px;
    return true;
}
void folio_reader_draft_applied(folio_reader_draft_t* draft) {
    if (!draft || !valid_px(draft->draft_px) || draft->base_revision == UINT64_MAX) return;
    draft->applied_px = draft->field_entry_px = draft->pending_px = draft->draft_px;
    ++draft->base_revision;
    draft->editing = draft->dirty = draft->editor_visible = false;
}
void folio_reader_draft_cancel(folio_reader_draft_t* draft) {
    if (!draft) return;
    draft->draft_px = draft->pending_px = draft->field_entry_px = draft->applied_px;
    draft->editing = draft->dirty = draft->editor_visible = false;
}
bool folio_reader_draft_suspend(const char* directory, folio_reader_draft_t* draft, folio_format_origin_t origin) {
    if (!draft || !draft->editing || !valid(draft) || draft->suspension_id == UINT64_MAX ||
        (origin != FOLIO_RESUME_READER && origin != FOLIO_RESUME_EDITOR)) return false;
    folio_reader_draft_t candidate = *draft;
    ++candidate.suspension_id;
    candidate.resume_origin = origin;
    candidate.editor_visible = origin == FOLIO_RESUME_EDITOR;
    candidate.panel = candidate.editor_visible ? 1 : 0;
    if (!folio_reader_draft_save(directory, &candidate) || !folio_reader_draft_load(directory, candidate.path, &candidate)) return false;
    unsigned char reference[RESUME_BYTES] = {0}; unsigned char* p = reference;
    memcpy(p, candidate.path, sizeof(candidate.path)); p += sizeof(candidate.path);
    put(&p, candidate.session_id, 8); put(&p, candidate.suspension_id, 8); put(&p, candidate.confirmed_generation, 8);
    uint64_t generation = 0;
    if (folio_checkpoint_save(directory, "format_resume", 1, reference, sizeof(reference), &generation) != FOLIO_CHECKPOINT_OK) return false;
    *draft = candidate;
    return true;
}
bool folio_reader_draft_resume(const char* directory, folio_reader_draft_t* out) {
    if (!out) return false;
    unsigned char reference[RESUME_BYTES]; size_t len = 0; uint64_t generation = 0;
    if (folio_checkpoint_load(directory, "format_resume", 1, reference, sizeof(reference), &len, &generation) != FOLIO_CHECKPOINT_OK ||
        len != sizeof(reference) || !memchr(reference, 0, FOLIO_READING_PATH_MAX)) return false;
    const unsigned char* p = reference + FOLIO_READING_PATH_MAX;
    uint64_t session = get(&p, 8), suspension = get(&p, 8), expected_generation = get(&p, 8);
    folio_reader_draft_t draft;
    if (!folio_reader_draft_load(directory, (const char*)reference, &draft) || !draft.editing ||
        draft.session_id != session || draft.suspension_id != suspension || draft.confirmed_generation != expected_generation) return false;
    *out = draft;
    return true;
}
bool folio_reader_draft_clear_resume(const char* directory) {
    unsigned char reference[RESUME_BYTES] = {0}; uint64_t generation = 0;
    return folio_checkpoint_save(directory, "format_resume", 1, reference, sizeof(reference), &generation) == FOLIO_CHECKPOINT_OK;
}
bool folio_reader_draft_recovery_candidate(const char* directory, const folio_reader_draft_t* live, folio_reader_draft_t* out) {
    if (!out) return false;
    if (live && live->editing) {
        if (!valid(live)) return false;
        *out = *live;
        return true;
    }
    return folio_reader_draft_resume(directory, out);
}
folio_draft_sleep_action_t folio_reader_draft_sleep_policy(bool has_session, bool owner_active, bool context_supported) {
    if (!has_session) return FOLIO_DRAFT_SLEEP_CLEAR_ORIGIN;
    return owner_active && context_supported ? FOLIO_DRAFT_SLEEP_CHECKPOINT : FOLIO_DRAFT_SLEEP_BLOCK;
}
bool folio_reader_draft_preserve_before_open(const char* directory, const folio_reader_draft_t* live, const char* next_path, uint32_t next_size) {
    if (!next_path) return false;
    if (!live || !live->editing || (!strcmp(live->path, next_path) && live->file_size == next_size)) return true;
    return folio_reader_draft_save(directory, live);
}
