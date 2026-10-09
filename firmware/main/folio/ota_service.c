/* SPDX-License-Identifier: Apache-2.0
 * 固定 GitHub 来源，流式校验后才选择新启动槽；不修改分区、密钥或 eFuse。
 * Fixed GitHub source, stream verification before boot selection; no partition/key/eFuse changes.
 */
#include "ota_service.h"
#include "sdkconfig.h"
#include "esp_app_desc.h"
#include "esp_flash.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE && !CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK
static bool s_boot_healthy;
#endif
static atomic_flag s_installing = ATOMIC_FLAG_INIT;

static bool partition_matches(esp_partition_type_t type, esp_partition_subtype_t subtype,
                              const char* label, uint32_t offset, uint32_t size) {
    const esp_partition_t* p = esp_partition_find_first(type, subtype, label);
    return p && p->address == offset && p->size == size && !p->encrypted;
}

bool folio_ota_layout_available(void) {
    uint32_t flash_size = 0;
    return esp_flash_get_size(esp_flash_default_chip, &flash_size) == ESP_OK &&
        flash_size == 0x1000000 &&
        !esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL) &&
        partition_matches(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0,
                          "ota_0", 0x10000, FOLIO_OTA_SLOT_SIZE) &&
        partition_matches(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1,
                          "ota_1", 0x910000, FOLIO_OTA_SLOT_SIZE) &&
        partition_matches(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA,
                          "otadata", 0xD10000, 0x2000) &&
        partition_matches(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS,
                          "nvs", 0x9000, 0x5000) &&
        partition_matches(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_PHY,
                          "phy_init", 0xE000, 0x1000) &&
        partition_matches(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT,
                          "storage", 0x410000, 0x500000);
}

esp_err_t folio_ota_confirm_boot(unsigned checks) {
    if ((checks & FOLIO_OTA_HEALTH_ALL) != FOLIO_OTA_HEALTH_ALL) return ESP_ERR_INVALID_STATE;
    if (!folio_ota_layout_available()) return ESP_ERR_NOT_SUPPORTED;
#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE && !CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK
    esp_ota_img_states_t state;
    esp_err_t err = esp_ota_get_state_partition(esp_ota_get_running_partition(), &state);
    if (err == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        err = esp_ota_mark_app_valid_cancel_rollback();
    } else if (err == ESP_ERR_NOT_FOUND || (err == ESP_OK &&
               (state == ESP_OTA_IMG_VALID || state == ESP_OTA_IMG_UNDEFINED))) {
        err = ESP_OK;
    } else if (err == ESP_OK) {
        err = ESP_ERR_INVALID_STATE;
    }
    s_boot_healthy = err == ESP_OK;
    return err;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE && CONFIG_MBEDTLS_CERTIFICATE_BUNDLE && !CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "psa/crypto.h"
#include <strings.h>

#define RELEASE_ROOT "https://github.com/mrrobot97/Folio/releases/download/v"
#define ASSET_ROOT "https://release-assets.githubusercontent.com/"

typedef struct { char location[2048]; } redirect_t;

static esp_err_t http_event(esp_http_client_event_t* e) {
    if (e->event_id == HTTP_EVENT_ON_HEADER && e->header_key && e->header_value &&
        e->user_data && strcasecmp(e->header_key, "Location") == 0) {
        redirect_t* r = e->user_data;
        if (strlen(e->header_value) >= sizeof(r->location)) return ESP_ERR_INVALID_SIZE;
        strcpy(r->location, e->header_value);
    }
    return ESP_OK;
}

static bool trusted_redirect(const char* u) {
    // 固定 HTTPS 主机；禁止凭据、端口、反斜杠和控制字符。/ Fixed HTTPS host; no credentials, ports or control chars.
    if (strncmp(u, ASSET_ROOT, sizeof(ASSET_ROOT)-1) != 0) return false;
    for (; *u; ++u) if ((unsigned char)*u < 0x21 || *u == '\\') return false;
    return true;
}

static esp_http_client_handle_t open_asset(const char* version, const char* file,
                                            int64_t* length) {
    char url[2048];
    snprintf(url, sizeof(url), RELEASE_ROOT "%s/%s", version, file);
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        redirect_t redirect = {0};
        esp_http_client_config_t cfg = {
            .url = url, .timeout_ms = 15000, .buffer_size = 4096,
            .disable_auto_redirect = true, .crt_bundle_attach = esp_crt_bundle_attach,
            .event_handler = http_event, .user_data = &redirect,
        };
        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (!client) return NULL;
        if (esp_http_client_open(client, 0) != ESP_OK) {
            esp_http_client_cleanup(client); return NULL;
        }
        *length = esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        if (status == 200 && *length > 0) {
            // 回调只处理头部；清空栈上上下文，避免读取体时悬垂引用。
            // Header callback is complete; detach stack context before reading body.
            esp_http_client_set_user_data(client, NULL);
            return client;
        }
        esp_http_client_cleanup(client);
        if ((status != 301 && status != 302 && status != 307 && status != 308) ||
            !trusted_redirect(redirect.location)) return NULL;
        strcpy(url, redirect.location);
    }
    return NULL;
}

static bool verify_provisioning(const folio_ota_request_t* r) {
    folio_ota_provisioning_t p = {0};
    if (!r->provisioning || !r->provisioning(r->context, &p) || !p.board ||
        strcmp(p.board, FOLIO_OTA_BOARD) || !p.physical_board_verified ||
        !p.installed_bootloader_rollback_verified || !p.bootloader_size ||
        p.bootloader_size > 0x8000) return false;
    psa_hash_operation_t hash = PSA_HASH_OPERATION_INIT;
    if (psa_crypto_init() != PSA_SUCCESS || psa_hash_setup(&hash, PSA_ALG_SHA_256) != PSA_SUCCESS)
        return false;
    uint8_t block[1024], digest[32]; size_t digest_size;
    bool ok = false;
    for (size_t offset = 0; offset < p.bootloader_size; offset += sizeof(block)) {
        size_t n = p.bootloader_size - offset;
        if (n > sizeof(block)) n = sizeof(block);
        if (esp_flash_read(esp_flash_default_chip, block, offset, n) != ESP_OK ||
            psa_hash_update(&hash, block, n) != PSA_SUCCESS) goto done;
    }
    ok = psa_hash_finish(&hash, digest, sizeof(digest), &digest_size) == PSA_SUCCESS &&
         digest_size == 32 && memcmp(digest, p.bootloader_sha256, 32) == 0;
done:
    psa_hash_abort(&hash);
    return ok;
}

static esp_err_t install_impl(const folio_ota_request_t* r, folio_ota_result_t* refusal) {
    uint16_t parts[3];
    if (!r || !r->checkpoint || !r->battery || !r->cancelled ||
        !folio_ota_version_parse(r->version, parts)) return ESP_ERR_INVALID_ARG;
    if (!folio_ota_layout_available()) {
        *refusal = FOLIO_OTA_LAYOUT_REQUIRED; return ESP_ERR_NOT_SUPPORTED;
    }
    if (!verify_provisioning(r)) {
        *refusal = FOLIO_OTA_PROVISIONING_REQUIRED; return ESP_ERR_INVALID_STATE;
    }
    if (!s_boot_healthy) { *refusal = FOLIO_OTA_UNCONFIRMED_BOOT; return ESP_ERR_INVALID_STATE; }
    if (r->cancelled(r->context)) return ESP_ERR_INVALID_STATE;
    unsigned battery = 0;
    if (!r->battery(r->context, &battery) || battery < FOLIO_OTA_MIN_BATTERY || battery > 1000) {
        *refusal = FOLIO_OTA_POWER_UNSAFE; return ESP_ERR_INVALID_STATE;
    }
    if (!r->checkpoint(r->context)) {
        *refusal = FOLIO_OTA_UNSAVED_STATE; return ESP_ERR_INVALID_STATE;
    }
    // 小型定长文本清单，避免增加 JSON 解析依赖。/ Small bounded text manifest avoids another JSON dependency.
    int64_t length = 0;
    esp_http_client_handle_t client = open_asset(r->version, "ota.txt", &length);
    if (!client) { *refusal = FOLIO_OTA_UNTRUSTED_SOURCE; return ESP_FAIL; }
    char text[512];
    int got = length > 0 && length < (int64_t)sizeof(text) ?
        esp_http_client_read_response(client, text, (int)length) : -1;
    esp_http_client_cleanup(client);
    if (got != length || got < 0 || (size_t)got >= sizeof(text)) return ESP_ERR_INVALID_SIZE;
    text[got] = 0;
    char board[32], chip[16], layout[32], version[32], project[32], size_text[11], sha[65];
    int end = 0;
    if (sscanf(text, "FOLIO-OTA-1\n%31[^\n]\n%15[^\n]\n%31[^\n]\n%31[^\n]\n%31[^\n]\n%10[0-9]\n%64[0-9a-f]\n%n",
               board, chip, layout, version, project, size_text, sha, &end) != 7 || end != got ||
        strcmp(version, r->version) || strcmp(project, esp_app_get_description()->project_name)) {
        *refusal = FOLIO_OTA_BAD_MANIFEST; return ESP_ERR_INVALID_ARG;
    }
    unsigned long long parsed_size = strtoull(size_text, NULL, 10);
    if (!parsed_size || parsed_size > FOLIO_OTA_SLOT_SIZE) {
        *refusal = FOLIO_OTA_TOO_LARGE; return ESP_ERR_INVALID_SIZE;
    }
    unsigned size = (unsigned)parsed_size;
    char canonical[512];
    snprintf(canonical, sizeof(canonical), "FOLIO-OTA-1\n%s\n%s\n%s\n%s\n%s\n%u\n%s\n",
             board, chip, layout, version, project, size, sha);
    if (strcmp(text, canonical)) { *refusal = FOLIO_OTA_BAD_MANIFEST; return ESP_ERR_INVALID_ARG; }
    const esp_partition_t* next = esp_ota_get_next_update_partition(NULL);
    folio_ota_manifest_t m = {1, board, chip, layout, version, sha, size};
    folio_ota_device_t d = {
        .board = FOLIO_OTA_BOARD, .current_version = esp_app_get_description()->version,
        .next_slot_size = next ? next->size : 0, .battery_permille = battery,
        .battery_known = true, .ota_layout_valid = true, .rollback_enabled = true,
        .boot_confirmed = true, .reading_state_saved = true, .trusted_transport = true, .provisioning_verified = true,
    };
    *refusal = folio_ota_check(&m, &d);
    if (*refusal != FOLIO_OTA_OK) return ESP_ERR_INVALID_STATE;
    client = open_asset(version, "app.bin", &length);
    if (!client) return ESP_FAIL;
    esp_err_t err = ESP_FAIL;
    esp_ota_handle_t ota = 0;
    psa_hash_operation_t hash = PSA_HASH_OPERATION_INIT;
    if (length != size || psa_crypto_init() != PSA_SUCCESS ||
        psa_hash_setup(&hash, PSA_ALG_SHA_256) != PSA_SUCCESS) goto done;
    if ((err = esp_ota_begin(next, size, &ota)) != ESP_OK) goto done;
    uint8_t buffer[4096];
    size_t received = 0, next_power_check = 0;
    while (received < size) {
        if (r->cancelled(r->context)) { err = ESP_ERR_INVALID_STATE; goto done; }
        if (received >= next_power_check) {
            if (!r->battery(r->context, &battery) || battery < FOLIO_OTA_MIN_BATTERY || battery > 1000) {
                *refusal = FOLIO_OTA_POWER_UNSAFE; err = ESP_ERR_INVALID_STATE; goto done;
            }
            next_power_check = received + 65536;
        }
        int n = esp_http_client_read(client, (char*)buffer,
                                    (size - received < sizeof(buffer)) ? size - received : sizeof(buffer));
        if (n <= 0) { err = ESP_FAIL; goto done; }
        if (psa_hash_update(&hash, buffer, n) != PSA_SUCCESS) { err = ESP_FAIL; goto done; }
        if ((err = esp_ota_write(ota, buffer, n)) != ESP_OK) goto done;
        received += n;
        if (r->progress) r->progress(r->context, received, size);
    }
    uint8_t digest[32]; size_t digest_size = 0;
    if (psa_hash_finish(&hash, digest, sizeof(digest), &digest_size) != PSA_SUCCESS || digest_size != 32) {
        err = ESP_FAIL; goto done;
    }
    char hex[65];
    for (unsigned i = 0; i < 32; ++i) snprintf(hex + i*2, 3, "%02x", digest[i]);
    if (strcmp(sha, hex)) { err = ESP_ERR_INVALID_CRC; goto done; }
    err = esp_ota_end(ota); ota = 0;
    if (err != ESP_OK) goto done;
    esp_app_desc_t desc;
    err = esp_ota_get_partition_description(next, &desc);
    if (err != ESP_OK || strncmp(desc.version, version, sizeof(desc.version)) ||
        strncmp(desc.project_name, project, sizeof(desc.project_name))) {
        err = ESP_ERR_INVALID_VERSION; goto done;
    }
    // 再次检查电量及取消；保持旧槽，直到所有检查通过。/ Recheck power/cancel; keep old slot until every check passes.
    if (r->cancelled(r->context)) { err = ESP_ERR_INVALID_STATE; goto done; }
    if (!r->battery(r->context, &battery) || battery < FOLIO_OTA_MIN_BATTERY || battery > 1000) {
        *refusal = FOLIO_OTA_POWER_UNSAFE; err = ESP_ERR_INVALID_STATE; goto done;
    }
    err = esp_ota_set_boot_partition(next);
done:
    if (ota) esp_ota_abort(ota);
    psa_hash_abort(&hash);
    esp_http_client_cleanup(client);
    return err;
}
#endif

esp_err_t folio_ota_install(const folio_ota_request_t* r, folio_ota_result_t* refusal) {
    folio_ota_result_t local = FOLIO_OTA_OK;
    if (!refusal) refusal = &local;
    *refusal = FOLIO_OTA_OK;
    if (atomic_flag_test_and_set(&s_installing)) return ESP_ERR_INVALID_STATE;
#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE && CONFIG_MBEDTLS_CERTIFICATE_BUNDLE && !CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK
    esp_err_t err = install_impl(r, refusal);
#else
    (void)r;
    *refusal = folio_ota_layout_available() ? FOLIO_OTA_ROLLBACK_REQUIRED : FOLIO_OTA_LAYOUT_REQUIRED;
    esp_err_t err = ESP_ERR_NOT_SUPPORTED;
#endif
    atomic_flag_clear(&s_installing);
    return err;
}
