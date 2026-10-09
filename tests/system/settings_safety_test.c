/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "settings.h"
#include "nvs.h"
#include "nvs_flash.h"

static int init_error, opens, erases, writes, open_error, set_error, commit_error;
static unsigned char saved_progress[4] = {0x12, 0x34, 0xab, 0xcd};
esp_err_t nvs_flash_init(void) { return init_error; }
esp_err_t nvs_flash_erase(void) {
    ++erases;
    memset(saved_progress, 0, sizeof(saved_progress));
    return ESP_OK;
}
esp_err_t nvs_open(const char* ns, int mode, nvs_handle_t* h) {
    assert(!strcmp(ns, "read_pico")); (void)mode; ++opens; *h = 1; return open_error;
}
void nvs_close(nvs_handle_t h) { assert(h == 1); }
esp_err_t nvs_get_u8(nvs_handle_t h, const char* key, uint8_t* value) {
    (void)h;
    if (!strcmp(key, "sleep")) { *value = APP_SLEEP_LIGHT; return ESP_OK; }
    return ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_get_str(nvs_handle_t h, const char* k, char* v, size_t* n) {
    (void)h; (void)k; (void)v; (void)n; return ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_set_str(nvs_handle_t h, const char* k, const char* v) {
    (void)h; (void)k; (void)v; ++writes; return set_error;
}
esp_err_t nvs_set_u8(nvs_handle_t h, const char* k, uint8_t v) {
    (void)h; (void)k; (void)v; ++writes; return set_error;
}
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return commit_error; }
int main(void) {
    const int errors[] = {ESP_ERR_NVS_NO_FREE_PAGES, ESP_ERR_NVS_NEW_VERSION_FOUND, ESP_FAIL};
    for (unsigned i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        init_error = errors[i];
        app_settings_init();
        assert(app_settings_storage_error() == init_error);
        assert(app_settings_sleep_mode() == APP_SLEEP_DEEP);
        assert(!opens && !erases && !writes);
        assert(!memcmp(saved_progress, "\x12\x34\xab\xcd", 4));
    }
    init_error = ESP_OK;
    app_settings_init();
    assert(app_settings_storage_error() == ESP_OK);
    assert(opens == 1 && app_settings_sleep_mode() == APP_SLEEP_LIGHT);
    assert(!erases && !writes);
    for (int fault = 0; fault < 3; ++fault) {
        open_error = fault == 0 ? ESP_FAIL : ESP_OK;
        set_error = fault == 1 ? ESP_ERR_NVS_NOT_ENOUGH_SPACE : ESP_OK;
        commit_error = fault == 2 ? ESP_FAIL : ESP_OK;
        app_settings_set_sleep_mode(APP_SLEEP_OFF);
        assert(app_settings_sleep_mode() == APP_SLEEP_LIGHT && app_settings_storage_error() != ESP_OK);
        app_settings_set_book_px(64);
        assert(app_settings_book_px() == 48 && app_settings_storage_error() != ESP_OK);
        app_settings_set_pickup_wake(true); assert(!app_settings_pickup_wake());
        app_settings_set_book_shake(true); assert(!app_settings_book_shake());
        app_settings_set_font_path("/sdcard/fonts/test.ttf"); assert(!app_settings_font_path()[0]);
        assert(!app_settings_set_wallpaper_path("/sdcard/wallpapers/test.fwp"));
        assert(!app_settings_wallpaper_path()[0] && !erases);
    }
    open_error = set_error = commit_error = 0;
    app_settings_set_book_px(64); assert(app_settings_book_px() == 64 && app_settings_storage_error() == ESP_OK);
    assert(app_settings_set_wallpaper_path("/sdcard/wallpapers/test.fwp"));
    assert(!strcmp(app_settings_wallpaper_path(),"/sdcard/wallpapers/test.fwp"));
    puts("settings: full, incompatible and failed NVS retain progress; healthy NVS still loads");
    puts("settings: open/set/commit failures preserve prior preferences and wallpaper; successful retry applies");
}
