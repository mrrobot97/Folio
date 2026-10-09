/* SPDX-License-Identifier: Apache-2.0
 * 统计/草稿/落盘故障的主机行为测试。/ Host behavioral tests for statistics, drafts and persistence faults.
 */
#include "reading_service.h"
#include "reader_draft.h"
#include "checkpoint_store.h"
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static unsigned checks;
#define CHECK(expr) do { ++checks; if (!(expr)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); } } while (0)
static char root[256];
static void directory(char out[320], const char* name) {
    snprintf(out, 320, "%s/%s", root, name);
    CHECK(mkdir(out, 0700) == 0);
}
static void corrupt(const char* directory, const char* suffix, off_t offset) {
    char file[400]; snprintf(file, sizeof(file), "%s/%s", directory, suffix);
    int fd = open(file, O_RDWR); CHECK(fd >= 0);
    unsigned char byte = 0;
    CHECK(pread(fd, &byte, 1, offset) == 1); byte ^= 0x40;
    CHECK(pwrite(fd, &byte, 1, offset) == 1); CHECK(close(fd) == 0);
}
static void test_unknown_idle_pause(void) {
    char dir[320]; directory(dir, "idle"); CHECK(folio_reading_init(dir));
    folio_reading_set_idle_ms(10000);
    CHECK(folio_reading_open("/sdcard/books/a.txt", "书 A", 1000));
    folio_reading_tick(11001, true);
    CHECK(folio_reading_summary().total_ms == 10000);
    CHECK(folio_reading_summary().unknown_ms == 10000);
    CHECK(folio_reading_summary().today_day == 0 && folio_reading_summary().today_ms == 0);
    int32_t first, last; folio_reading_date_bounds(&first, &last); CHECK(!first && !last);
    folio_reading_activity(31000); folio_reading_tick(32000, true);
    CHECK(folio_reading_summary().total_ms == 11000);
    folio_reading_pause(33000); folio_reading_tick(133000, false);
    CHECK(folio_reading_summary().total_ms == 12000);
    folio_reading_tick(134000, true); folio_reading_tick(135000, true);
    CHECK(folio_reading_summary().total_ms == 13000);
    folio_reading_tick(134999, true);
    CHECK(folio_reading_summary().total_ms == 13000);
    CHECK(folio_reading_checkpoint()); CHECK(!folio_reading_summary().dirty);
    folio_reading_deinit(); CHECK(folio_reading_init(dir));
    CHECK(folio_reading_summary().total_ms == 13000 && folio_reading_summary().book_count == 1);
    CHECK(folio_reading_summary().today_day == 0);
    folio_reading_book_t book;
    CHECK(folio_reading_book(0, &book)); CHECK(!strcmp(book.title, "书 A") && book.total_ms == 13000);
    CHECK(!folio_reading_book(1, &book));
    folio_reading_deinit();
}
static void test_midnight_and_clock_change(void) {
    char dir[320]; directory(dir, "midnight"); CHECK(folio_reading_init(dir));
    CHECK(folio_reading_open("/flash/books/a.epub", "A", 0));
    folio_reading_set_clock(1791475199, 480, 1000);
    folio_reading_tick(4000, true);
    CHECK(folio_reading_summary().total_ms == 4000);
    CHECK(folio_reading_summary().unknown_ms == 1000);
    CHECK(folio_reading_day_ms(20261008) == 1000);
    CHECK(folio_reading_day_ms(20261009) == 2000);
    CHECK(folio_reading_summary().today_day == 20261009);
    CHECK(folio_reading_open("/flash/books/b.epub", "B", 4000));
    folio_reading_tick(4500, true);
    CHECK(folio_reading_book_day_ms(1, 20261009) == 500);
    folio_reading_set_clock(1835395199, 0, 4500);
    folio_reading_tick(7500, true);
    CHECK(folio_reading_day_ms(20280228) == 1000 && folio_reading_day_ms(20280229) == 2000);
    int32_t first, last; folio_reading_date_bounds(&first, &last); CHECK(first == 20261008 && last == 20280229);
    folio_reading_set_clock(-1, 0, 7500); folio_reading_tick(8000, true);
    CHECK(folio_reading_summary().unknown_ms == 1500 && folio_reading_summary().today_day == 0);
    CHECK(folio_reading_checkpoint()); folio_reading_deinit(); CHECK(folio_reading_init(dir));
    CHECK(folio_reading_day_ms(20261009) == 2500 && folio_reading_summary().total_ms == 8000);
    folio_reading_deinit();
}
static void test_readonly_date_after_pause(void) {
    char dir[320]; directory(dir, "readonly_date"); CHECK(folio_reading_init(dir));
    folio_reading_set_clock(1791475199, 480, 0);
    CHECK(folio_reading_open("/a.txt", "A", 0)); folio_reading_pause(500);
    CHECK(folio_reading_checkpoint());
    folio_reading_summary_t home = folio_reading_summary_at(121000);
    CHECK(home.today_day == 20261009 && home.today_ms == 0 && home.total_ms == 500 && !home.dirty);
    CHECK(folio_reading_day_ms(20261008) == 500);
    // 停留统计页跨日；查询既不重置 idle，也不累计暂停时间。
    // Staying on statistics across midnight neither resets idle nor accrues paused time.
    CHECK(folio_reading_summary_at(86402000).today_day == 20261010);
    CHECK(folio_reading_summary_at(86402000).total_ms == 500 && !folio_reading_summary_at(86402000).dirty);
    // FAST 保留单调时钟，唤醒后的快照更新日期，睡眠时长仍为零。
    // FAST preserves monotonic time; wake changes the snapshot date without counting sleep.
    folio_reading_activity(86403000); folio_reading_tick(86403000, true); folio_reading_tick(86403500, true);
    CHECK(folio_reading_summary_at(86403500).today_day == 20261010);
    CHECK(folio_reading_summary_at(86403500).today_ms == 500 && folio_reading_summary_at(86403500).total_ms == 1000);
    folio_reading_deinit();
}
static void test_checkpoint_failures(void) {
    char dir[320], file[400]; directory(dir, "slots");
    uint64_t generation = 0, loaded_generation = 0;
    char out[64] = {0}; size_t len = 0;
    CHECK(folio_checkpoint_load(dir, "test", 1, out, sizeof(out), &len, &loaded_generation) == FOLIO_CHECKPOINT_MISSING);
    CHECK(folio_checkpoint_save(dir, "test", 1, "first", 6, &generation) == FOLIO_CHECKPOINT_OK && generation == 1);
    CHECK(folio_checkpoint_save(dir, "test", 1, "second", 7, &generation) == FOLIO_CHECKPOINT_OK && generation == 2);
    corrupt(dir, "test.b", 29);
    CHECK(folio_checkpoint_load(dir, "test", 1, out, sizeof(out), &len, &loaded_generation) == FOLIO_CHECKPOINT_OK);
    CHECK(!strcmp(out, "first") && loaded_generation == 1);
    CHECK(folio_checkpoint_save(dir, "test", 1, "third", 6, &generation) == FOLIO_CHECKPOINT_OK && generation == 2);
    snprintf(file, sizeof(file), "%s/test.a", dir); CHECK(truncate(file, 9) == 0);
    CHECK(folio_checkpoint_load(dir, "test", 1, out, sizeof(out), &len, &loaded_generation) == FOLIO_CHECKPOINT_OK && !strcmp(out, "third"));
    CHECK(unlink(file) == 0 && mkdir(file, 0700) == 0);
    CHECK(folio_checkpoint_save(dir, "test", 1, "failed", 7, &generation) == FOLIO_CHECKPOINT_IO && generation == 2);
    CHECK(folio_checkpoint_load(dir, "test", 1, out, sizeof(out), &len, &loaded_generation) == FOLIO_CHECKPOINT_OK && !strcmp(out, "third"));
    CHECK(rmdir(file) == 0);
    CHECK(folio_checkpoint_save(dir, "test", 1, "next", 5, &generation) == FOLIO_CHECKPOINT_OK && generation == 3);
    CHECK(folio_checkpoint_load(dir, "test", 2, out, sizeof(out), &len, &loaded_generation) == FOLIO_CHECKPOINT_SCHEMA);
    CHECK(folio_checkpoint_save(dir, "test", 2, "downgrade", 10, &generation) == FOLIO_CHECKPOINT_SCHEMA && generation == 3);
    CHECK(folio_checkpoint_load(dir, "test", 1, out, 2, &len, &loaded_generation) == FOLIO_CHECKPOINT_SIZE);
    corrupt(dir, "test.a", 28); corrupt(dir, "test.b", 28);
    CHECK(folio_checkpoint_load(dir, "test", 1, out, sizeof(out), &len, &loaded_generation) == FOLIO_CHECKPOINT_CORRUPT);
    CHECK(folio_checkpoint_save(dir, "test", 1, "erase", 6, &generation) == FOLIO_CHECKPOINT_CORRUPT);
    CHECK(folio_checkpoint_save(dir, "../evil", 1, "x", 2, &generation) == FOLIO_CHECKPOINT_IO);
}
static void test_stats_failed_write(void) {
    char dir[320], file[400]; directory(dir, "stats_failure"); CHECK(folio_reading_init(dir));
    CHECK(folio_reading_open("/a.txt", "A", 0)); folio_reading_tick(1000, true); CHECK(folio_reading_checkpoint());
    folio_reading_tick(2000, true);
    snprintf(file, sizeof(file), "%s/reading.b", dir); CHECK(mkdir(file, 0700) == 0);
    CHECK(!folio_reading_checkpoint()); CHECK(folio_reading_summary().dirty && !folio_reading_summary().storage_ok);
    CHECK(folio_reading_summary().total_ms == 2000);
    CHECK(rmdir(file) == 0); CHECK(folio_reading_checkpoint());
    folio_reading_tick(3000, true);
    // 未检查点的 1 秒模拟断电丢失，不伪装已保存。/ Simulate power loss of one uncheckpointed second.
    folio_reading_deinit(); CHECK(folio_reading_init(dir)); CHECK(folio_reading_summary().total_ms == 2000);
    folio_reading_deinit(); corrupt(dir, "reading.a", 28); corrupt(dir, "reading.b", 28);
    CHECK(!folio_reading_init(dir)); CHECK(!folio_reading_summary().storage_ok);
    CHECK(folio_reading_open("/b.txt", "B", 0)); folio_reading_tick(1000, true);
    CHECK(!folio_reading_checkpoint()); folio_reading_deinit();
}
static void test_remount_merge(void) {
    char parent[320], dir[400], offline[400], blocker[440]; directory(parent, "remount");
    snprintf(dir, sizeof(dir), "%s/folio", parent); snprintf(offline, sizeof(offline), "%s.offline", parent);
    CHECK(folio_reading_init(dir)); CHECK(folio_reading_open("/saved.txt", "Saved", 0));
    folio_reading_tick(2000, true); CHECK(folio_reading_open("/shared.txt", "Shared", 2000));
    folio_reading_tick(3000, true); CHECK(folio_reading_checkpoint()); folio_reading_deinit();
    CHECK(rename(parent, offline) == 0); CHECK(!folio_reading_init(dir));
    CHECK(folio_reading_open("/shared.txt", "Shared", 0)); folio_reading_tick(500, true);
    CHECK(folio_reading_open("/new.txt", "New", 500)); folio_reading_tick(1200, true);
    CHECK(folio_reading_summary().total_ms == 1200); CHECK(rename(offline, parent) == 0);
    snprintf(blocker, sizeof(blocker), "%s/reading.b", dir); CHECK(mkdir(blocker, 0700) == 0);
    CHECK(!folio_reading_retry_storage(dir));
    CHECK(folio_reading_summary().total_ms == 4200 && folio_reading_summary().dirty && !folio_reading_summary().storage_ok);
    folio_reading_book_t book; CHECK(folio_reading_book(1, &book) && book.total_ms == 1500);
    CHECK(folio_reading_book(2, &book) && book.total_ms == 700);
    CHECK(rmdir(blocker) == 0); CHECK(folio_reading_retry_storage(dir));
    CHECK(folio_reading_retry_storage(dir)); CHECK(folio_reading_summary().total_ms == 4200);
    // 活跃书索引在合并后正确映射，后续时间不会记到另一书。
    // Remap the active book after merging so later time cannot accrue to a different book.
    folio_reading_tick(1300, true); CHECK(folio_reading_book(2, &book) && book.total_ms == 800);
    CHECK(folio_reading_summary().total_ms == 4300 && folio_reading_summary().unknown_ms == 4300);
    CHECK(folio_reading_checkpoint()); folio_reading_deinit(); CHECK(folio_reading_init(dir));
    CHECK(folio_reading_summary().total_ms == 4300); folio_reading_deinit();

    directory(parent, "remount_unknown"); snprintf(dir, sizeof(dir), "%s/folio", parent);
    snprintf(offline, sizeof(offline), "%s.offline", parent); CHECK(rename(parent, offline) == 0);
    CHECK(!folio_reading_init(dir)); CHECK(folio_reading_open("/ram.txt", "RAM", 0)); folio_reading_tick(500, true);
    CHECK(rename(offline, parent) == 0); CHECK(mkdir(dir, 0700) == 0);
    uint64_t generation = 0; CHECK(folio_checkpoint_save(dir, "reading", 99, "new schema", 11, &generation) == FOLIO_CHECKPOINT_OK);
    CHECK(!folio_reading_retry_storage(dir)); CHECK(folio_reading_summary().total_ms == 500 && folio_reading_summary().dirty);
    CHECK(!folio_reading_checkpoint()); folio_reading_deinit();

    directory(parent, "remount_empty"); snprintf(dir, sizeof(dir), "%s/folio", parent);
    snprintf(offline, sizeof(offline), "%s.offline", parent); CHECK(rename(parent, offline) == 0);
    CHECK(!folio_reading_init(dir)); CHECK(folio_reading_open("/ram.txt", "RAM", 0)); folio_reading_tick(500, true);
    CHECK(rename(offline, parent) == 0); CHECK(folio_reading_retry_storage(dir));
    CHECK(folio_reading_summary().total_ms == 500 && folio_reading_summary().storage_ok && !folio_reading_summary().dirty);
    folio_reading_deinit();
}
static void test_draft(void) {
    char dir[320]; directory(dir, "draft");
    folio_reader_draft_t draft = {0}, restored = {0};
    folio_reader_draft_begin(&draft, "/sdcard/books/a.epub", 1234, 48);
    CHECK(folio_reader_draft_adjust(&draft, 1)); CHECK(draft.applied_px == 48 && draft.draft_px == 52 && draft.dirty);
    CHECK(folio_reader_draft_save(dir, &draft)); CHECK(folio_reader_draft_load(dir, "/sdcard/books/a.epub", &restored));
    CHECK(restored.editing && restored.applied_px == 48 && restored.draft_px == 52);
    folio_reader_draft_begin(&restored, "/sdcard/books/a.epub", 1234, 48);
    CHECK(restored.draft_px == 52);
    folio_reader_draft_applied(&restored); CHECK(!restored.editing && !restored.dirty && restored.applied_px == 52);
    CHECK(folio_reader_draft_save(dir, &restored));
    CHECK(folio_reader_draft_load(dir, "/sdcard/books/a.epub", &draft)); CHECK(draft.applied_px == 52 && !draft.editing);
    folio_reader_draft_begin(&draft, "/sdcard/books/a.epub", 1235, 48); CHECK(draft.draft_px == 48);
    CHECK(folio_reader_draft_adjust(&draft, -1)); folio_reader_draft_cancel(&draft);
    CHECK(draft.applied_px == 48 && draft.draft_px == 48 && !draft.editing);
    folio_reader_draft_begin(&draft, "/different.epub", 1235, 48); CHECK(draft.draft_px == 48);
    CHECK(!folio_reader_draft_adjust(&draft, 2));
    for (int i = 0; i < 6; ++i) CHECK(folio_reader_draft_adjust(&draft, 1));
    CHECK(draft.draft_px == 72 && !folio_reader_draft_adjust(&draft, 1));
}
static void test_draft_transaction_failure(void) {
    char dir[320], blocker[400] = {0}; directory(dir, "draft_transaction");
    folio_reader_draft_t draft = {0}, candidate, restored;
    folio_reader_draft_begin(&draft, "/book.epub", 123, 48);
    CHECK(folio_reader_draft_adjust(&draft, 1)); CHECK(folio_reader_draft_save(dir, &draft));
    DIR* stream = opendir(dir); CHECK(stream != NULL);
    struct dirent* ent;
    while ((ent = readdir(stream))) {
        size_t len = strlen(ent->d_name);
        if (len > 2 && !strcmp(ent->d_name + len - 2, ".a")) {
            snprintf(blocker, sizeof(blocker), "%s/%s", dir, ent->d_name);
            blocker[strlen(blocker) - 1] = 'b';
        }
    }
    CHECK(closedir(stream) == 0); CHECK(blocker[0] && mkdir(blocker, 0700) == 0);
    folio_reader_draft_t live_before = draft;
    CHECK(!folio_reader_draft_preserve_before_open(dir, &draft, "/other.epub", 456));
    CHECK(!memcmp(&draft, &live_before, sizeof(draft)));
    CHECK(folio_reader_draft_preserve_before_open(dir, &draft, "/book.epub", 123));
    CHECK(!folio_reader_draft_preserve_before_open(dir, &draft, "/book.epub", 124));
    candidate = draft; folio_reader_draft_applied(&candidate);
    CHECK(!folio_reader_draft_save(dir, &candidate));
    CHECK(folio_reader_draft_load(dir, "/book.epub", &restored));
    CHECK(restored.applied_px == 48 && restored.draft_px == 52 && restored.editing);
    candidate = draft; folio_reader_draft_cancel(&candidate);
    CHECK(!folio_reader_draft_save(dir, &candidate));
    CHECK(folio_reader_draft_load(dir, "/book.epub", &restored) && restored.editing);
    CHECK(rmdir(blocker) == 0);
    CHECK(folio_reader_draft_preserve_before_open(dir, &draft, "/flash/books/after-card-loss.epub", 456));
    CHECK(!memcmp(&draft, &live_before, sizeof(draft)));
    CHECK(folio_reader_draft_save(dir, &candidate));
    CHECK(folio_reader_draft_load(dir, "/book.epub", &restored) && !restored.editing && restored.applied_px == 48);
    candidate.file_size = 999;
    CHECK(!folio_reader_draft_save(dir, &candidate));
    CHECK(folio_reader_draft_load(dir, "/book.epub", &restored) && restored.file_size == 123);
    folio_reader_draft_begin(&candidate, "/another.epub", 123, 64);
    CHECK(folio_reader_draft_save(dir, &candidate));
    CHECK(folio_reader_draft_load(dir, "/book.epub", &restored) && restored.applied_px == 48);
    CHECK(folio_reader_draft_load(dir, "/another.epub", &restored) && restored.applied_px == 64);
}
static void test_suspend_journal(void) {
    char dir[320], blocker[400]; directory(dir, "suspend");
    folio_reader_draft_t draft = {0}, restored;
    folio_reader_draft_begin(&draft, "/book.epub", 123, 48);
    CHECK(folio_reader_draft_adjust(&draft, 1));
    snprintf(draft.font_path, sizeof(draft.font_path), "builtin");
    draft.book_mtime = 12345;
    CHECK(folio_reader_draft_suspend(dir, &draft, FOLIO_RESUME_EDITOR));
    CHECK(draft.confirmed_generation != 0 && draft.suspension_id == 1);
    CHECK(folio_reader_draft_resume(dir, &restored));
    CHECK(restored.session_id == draft.session_id && restored.revision == draft.revision);
    CHECK(restored.draft_px == 52 && restored.applied_px == 48 && restored.editor_visible);
    CHECK(restored.scope == FOLIO_FORMAT_BOOK && restored.pending_fields == 0 && restored.field_entry_px == 48);
    CHECK(restored.return_depth == 1 && restored.return_stack[0] == FOLIO_RESUME_READER);
    CHECK(!strcmp(restored.font_path, "builtin") && restored.book_mtime == 12345);
    folio_reader_draft_t newer = draft, selected;
    CHECK(folio_reader_draft_adjust(&newer, 1));
    CHECK(folio_reader_draft_recovery_candidate("/no-storage-needed-for-live-session", &newer, &selected));
    CHECK(selected.draft_px == 56 && selected.revision == newer.revision && selected.applied_px == 48);
    CHECK(folio_reader_draft_recovery_candidate(dir, NULL, &selected) && selected.draft_px == 52);
    newer.scope = FOLIO_FORMAT_DEFAULTS;
    CHECK(!folio_reader_draft_recovery_candidate(dir, &newer, &selected));
    CHECK(newer.draft_px == 56 && newer.editing);
    CHECK(folio_reader_draft_adjust(&draft, 1));
    snprintf(blocker, sizeof(blocker), "%s/format_resume.b", dir); CHECK(mkdir(blocker, 0700) == 0);
    CHECK(!folio_reader_draft_suspend(dir, &draft, FOLIO_RESUME_EDITOR));
    // 包已更新但来源 ACK 失败：不得用旧来源冒充当前快照。/ Updated envelope without origin ACK must not reuse a stale origin.
    CHECK(!folio_reader_draft_resume(dir, &restored));
    CHECK(rmdir(blocker) == 0); CHECK(folio_reader_draft_suspend(dir, &draft, FOLIO_RESUME_READER));
    CHECK(folio_reader_draft_resume(dir, &restored) && !restored.editor_visible && restored.draft_px == 56);
    CHECK(folio_reader_draft_clear_resume(dir)); CHECK(!folio_reader_draft_resume(dir, &restored));
    CHECK(folio_reader_draft_suspend(dir, &draft, FOLIO_RESUME_EDITOR));
    folio_reader_draft_applied(&draft); CHECK(folio_reader_draft_save(dir, &draft));
    CHECK(!folio_reader_draft_resume(dir, &restored));
    folio_reader_draft_begin(&draft, "/book.epub", 123, 56); CHECK(folio_reader_draft_adjust(&draft, -1));
    uint64_t session = draft.session_id;
    CHECK(folio_reader_draft_suspend(dir, &draft, FOLIO_RESUME_EDITOR));
    CHECK(folio_reader_draft_resume(dir, &restored) && restored.session_id == session && restored.applied_px == 56);
    draft.scope = FOLIO_FORMAT_DEFAULTS; CHECK(!folio_reader_draft_suspend(dir, &draft, FOLIO_RESUME_EDITOR));
    draft.scope = FOLIO_FORMAT_BOOK; draft.font_child = 1; CHECK(!folio_reader_draft_save(dir, &draft));
    draft.font_child = 0; draft.pending_fields = 2; CHECK(!folio_reader_draft_save(dir, &draft));
    draft.pending_fields = 0; draft.return_depth = 9; CHECK(!folio_reader_draft_save(dir, &draft));
}
static void test_sleep_context_policy(void) {
    // RAM 会话离开书页到设置/首页，即使磁盘草稿已写成功也不可清来源并断电。
    // A RAM session left behind in settings/home must not clear its origin or power off, even after a successful draft write.
    CHECK(folio_reader_draft_sleep_policy(true, false, true) == FOLIO_DRAFT_SLEEP_BLOCK);
    CHECK(folio_reader_draft_sleep_policy(true, false, false) == FOLIO_DRAFT_SLEEP_BLOCK);
    CHECK(folio_reader_draft_sleep_policy(true, true, false) == FOLIO_DRAFT_SLEEP_BLOCK);
    CHECK(folio_reader_draft_sleep_policy(true, true, true) == FOLIO_DRAFT_SLEEP_CHECKPOINT);
    for (int owner = 0; owner < 2; ++owner) for (int context = 0; context < 2; ++context)
        CHECK(folio_reader_draft_sleep_policy(false, owner, context) == FOLIO_DRAFT_SLEEP_CLEAR_ORIGIN);
}
static void test_layout_quota(void) {
    char dir[320], file[400]; directory(dir, "layout_quota");
    // 填满配额，原损坏记录保留；新记录拒绝，已有记录仍可更新。
    // Fill the quota while preserving raw damaged slots; reject new books, permit existing updates.
    folio_reader_draft_t draft = {0}; folio_reader_draft_begin(&draft, "/existing.epub", 123, 48);
    CHECK(folio_reader_draft_save(dir, &draft));
    for (unsigned i = 0; i < FOLIO_FORMAT_MAX_BOOKS - 1; ++i) {
        snprintf(file, sizeof(file), "%s/layout_%016llx.a", dir, (unsigned long long)i);
        FILE* out = fopen(file, "wb"); CHECK(out != NULL); CHECK(fclose(out) == 0);
    }
    CHECK(folio_reader_draft_adjust(&draft, 1)); CHECK(folio_reader_draft_save(dir, &draft));
    folio_reader_draft_begin(&draft, "/new.epub", 123, 48); CHECK(!folio_reader_draft_save(dir, &draft));
}
static void test_capacity_and_performance(void) {
    char dir[320]; directory(dir, "capacity"); CHECK(folio_reading_init(dir));
    for (int i = 0; i < FOLIO_READING_MAX_BOOKS + 1; ++i) {
        char path[64]; snprintf(path, sizeof(path), "/book%d.txt", i);
        CHECK(folio_reading_open(path, path, i * 1000) == (i < FOLIO_READING_MAX_BOOKS));
        folio_reading_tick(i * 1000 + 1000, true);
    }
    CHECK(folio_reading_summary().book_count == 64 && folio_reading_summary().total_ms == 65000);
    CHECK(folio_reading_summary().capacity_full && folio_reading_summary().unallocated_ms == 1000);
    CHECK(folio_reading_checkpoint()); folio_reading_deinit(); CHECK(folio_reading_init(dir));
    CHECK(folio_reading_summary().total_ms == 65000 && folio_reading_summary().unallocated_ms == 1000);
    folio_reading_deinit(); directory(dir, "day_capacity"); CHECK(folio_reading_init(dir));
    CHECK(folio_reading_open("/one.txt", "One", 0));
    for (int i = 0; i < FOLIO_READING_MAX_DAYS + 1; ++i) {
        folio_reading_set_clock(1791504000 + (int64_t)i * 86400, 0, i * 1000);
        folio_reading_activity(i * 1000); folio_reading_tick(i * 1000 + 1000, true);
    }
    CHECK(folio_reading_summary().capacity_full && folio_reading_summary().unallocated_ms == 1000);
    CHECK(folio_reading_summary().total_ms == 4097000);
    clock_t started = clock(); uint64_t sum = 0;
    for (int i = 0; i < 10000; ++i) sum += folio_reading_day_ms(20261009);
    double query_ms = (clock() - started) * 1000.0 / CLOCKS_PER_SEC;
    CHECK(sum == 10000000); CHECK(folio_reading_checkpoint());
    struct stat st; char file[400]; snprintf(file, sizeof(file), "%s/reading.a", dir); CHECK(stat(file, &st) == 0);
    printf("Host-only: 4096 day records, checkpoint %lld bytes, 10000 full-history day queries %.3f ms (sanitizer build)\n", (long long)st.st_size, query_ms);
    folio_reading_deinit(); CHECK(folio_reading_init(dir)); CHECK(folio_reading_summary().total_ms == 4097000);
    folio_reading_deinit();
}
int main(void) {
    snprintf(root, sizeof(root), "/tmp/folio-reader-XXXXXX"); CHECK(mkdtemp(root) != NULL);
    test_unknown_idle_pause(); test_midnight_and_clock_change(); test_readonly_date_after_pause(); test_checkpoint_failures();
    test_stats_failed_write(); test_remount_merge(); test_draft(); test_draft_transaction_failure(); test_suspend_journal(); test_sleep_context_policy(); test_layout_quota(); test_capacity_and_performance();
    printf("PASS %u checks; fault artifacts: %s\n", checks, root);
    return 0;
}
