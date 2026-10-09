/* SPDX-License-Identifier: Apache-2.0
 * OTA 准入策略；不读写硬件。/ OTA admission policy; no hardware I/O.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define FOLIO_OTA_BOARD "RDP-G01-W"
#define FOLIO_OTA_LAYOUT "folio-ota-v1"
#define FOLIO_OTA_SLOT_SIZE (4u * 1024u * 1024u)
#define FOLIO_OTA_MIN_BATTERY 300u

typedef enum {
    FOLIO_OTA_OK = 0,
    FOLIO_OTA_BAD_MANIFEST,
    FOLIO_OTA_WRONG_BOARD,
    FOLIO_OTA_LAYOUT_REQUIRED,
    FOLIO_OTA_ROLLBACK_REQUIRED,
    FOLIO_OTA_UNCONFIRMED_BOOT,
    FOLIO_OTA_OLD_VERSION,
    FOLIO_OTA_TOO_LARGE,
    FOLIO_OTA_POWER_UNSAFE,
    FOLIO_OTA_UNSAVED_STATE,
    FOLIO_OTA_UNTRUSTED_SOURCE,
    FOLIO_OTA_PROVISIONING_REQUIRED,
} folio_ota_result_t;

typedef struct {
    unsigned schema;
    const char* board;
    const char* chip;
    const char* layout;
    const char* version;
    const char* sha256;
    size_t image_size;
} folio_ota_manifest_t;

typedef struct {
    const char* board;
    const char* current_version;
    size_t next_slot_size;
    unsigned battery_permille;
    bool battery_known;
    bool ota_layout_valid;
    bool rollback_enabled;
    bool boot_confirmed;
    bool reading_state_saved;
    bool trusted_transport;
    bool provisioning_verified; ///< 已核验物理板型与设备实际 bootloader。/ Physical board and installed bootloader verified.
} folio_ota_device_t;

/// 仅接受规范 x.y.z，每段 <=65535。/ Only canonical x.y.z, each <=65535.
bool folio_ota_version_parse(const char* version, uint16_t parts[3]);
/// 严格检查所有写入前置条件。/ Strictly check all preconditions before any write.
folio_ota_result_t folio_ota_check(const folio_ota_manifest_t* manifest,
                                  const folio_ota_device_t* device);
/// 稳定英文诊断码。/ Stable English diagnostic code.
const char* folio_ota_result_name(folio_ota_result_t result);
