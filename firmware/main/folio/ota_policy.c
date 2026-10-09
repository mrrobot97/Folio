/* SPDX-License-Identifier: Apache-2.0
 * 保守 OTA 策略与版本比较。/ Conservative OTA policy and version comparison.
 */
#include "ota_policy.h"
#include <string.h>

bool folio_ota_version_parse(const char* v, uint16_t out[3]) {
    if (!v || !out) return false;
    for (unsigned i = 0; i < 3; ++i) {
        if (*v < '0' || *v > '9') return false;
        if (*v == '0' && v[1] >= '0' && v[1] <= '9') return false;
        unsigned n = 0;
        do {
            n = n * 10u + (unsigned)(*v++ - '0');
            if (n > 65535u) return false;
        } while (*v >= '0' && *v <= '9');
        out[i] = (uint16_t)n;
        if (i < 2 && *v++ != '.') return false;
    }
    return *v == '\0';
}

static bool eq(const char* a, const char* b) { return a && b && strcmp(a, b) == 0; }

folio_ota_result_t folio_ota_check(const folio_ota_manifest_t* m,
                                  const folio_ota_device_t* d) {
    uint16_t next[3], current[3];
    if (!m || !d || m->schema != 1 || !eq(m->chip, "esp32s3") ||
        !folio_ota_version_parse(m->version, next) || !m->sha256 ||
        strlen(m->sha256) != 64 || !m->image_size) return FOLIO_OTA_BAD_MANIFEST;
    for (unsigned i = 0; i < 64; ++i) {
        char c = m->sha256[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return FOLIO_OTA_BAD_MANIFEST;
    }
    if (!eq(m->board, FOLIO_OTA_BOARD) || !eq(d->board, m->board))
        return FOLIO_OTA_WRONG_BOARD;
    if (!d->ota_layout_valid || !eq(m->layout, FOLIO_OTA_LAYOUT))
        return FOLIO_OTA_LAYOUT_REQUIRED;
    if (!d->provisioning_verified) return FOLIO_OTA_PROVISIONING_REQUIRED;
    if (!d->rollback_enabled) return FOLIO_OTA_ROLLBACK_REQUIRED;
    if (!d->boot_confirmed) return FOLIO_OTA_UNCONFIRMED_BOOT;
    if (!folio_ota_version_parse(d->current_version, current)) return FOLIO_OTA_OLD_VERSION;
    int cmp = 0;
    for (unsigned i = 0; i < 3 && !cmp; ++i)
        cmp = next[i] > current[i] ? 1 : next[i] < current[i] ? -1 : 0;
    if (cmp <= 0) return FOLIO_OTA_OLD_VERSION;
    if (m->image_size > FOLIO_OTA_SLOT_SIZE || m->image_size > d->next_slot_size)
        return FOLIO_OTA_TOO_LARGE;
    if (!d->battery_known || d->battery_permille < FOLIO_OTA_MIN_BATTERY ||
        d->battery_permille > 1000u) return FOLIO_OTA_POWER_UNSAFE;
    if (!d->reading_state_saved) return FOLIO_OTA_UNSAVED_STATE;
    if (!d->trusted_transport) return FOLIO_OTA_UNTRUSTED_SOURCE;
    return FOLIO_OTA_OK;
}

const char* folio_ota_result_name(folio_ota_result_t r) {
    static const char* names[] = {"ok", "bad_manifest", "wrong_board", "ota_layout_required",
        "rollback_required", "unconfirmed_boot", "old_or_invalid_version", "image_too_large",
        "power_unsafe", "unsaved_state", "untrusted_source", "provisioning_required"};
    return (unsigned)r < sizeof(names)/sizeof(names[0]) ? names[r] : "unknown";
}
