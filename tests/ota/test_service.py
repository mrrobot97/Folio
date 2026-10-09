"""Compile the IDF adapter against a deliberately fake SDK to test call ordering/cleanup.
This does not verify ESP-IDF ABI, TLS, real hashing, flash or hardware behavior.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
HEADER=r'''
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_NOT_SUPPORTED 2
#define ESP_ERR_INVALID_ARG 3
#define ESP_ERR_INVALID_SIZE 4
#define ESP_ERR_NOT_FOUND 5
#define ESP_ERR_INVALID_CRC 6
#define ESP_ERR_INVALID_VERSION 7
#define CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE 1
#define CONFIG_MBEDTLS_CERTIFICATE_BUNDLE 1
#define CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK 0
typedef int esp_partition_type_t;
typedef int esp_partition_subtype_t;
#define ESP_PARTITION_TYPE_APP 0
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_APP_FACTORY 0
#define ESP_PARTITION_SUBTYPE_APP_OTA_0 16
#define ESP_PARTITION_SUBTYPE_APP_OTA_1 17
#define ESP_PARTITION_SUBTYPE_DATA_OTA 0
#define ESP_PARTITION_SUBTYPE_DATA_PHY 1
#define ESP_PARTITION_SUBTYPE_DATA_NVS 2
#define ESP_PARTITION_SUBTYPE_DATA_FAT 129
typedef struct {uint32_t address,size;bool encrypted;} esp_partition_t;
typedef int esp_ota_img_states_t;
#define ESP_OTA_IMG_PENDING_VERIFY 1
#define ESP_OTA_IMG_VALID 2
#define ESP_OTA_IMG_UNDEFINED 3
typedef unsigned esp_ota_handle_t;
typedef struct {char version[32];char project_name[32];} esp_app_desc_t;
extern void* esp_flash_default_chip;
esp_err_t esp_flash_get_size(void*,uint32_t*);
esp_err_t esp_flash_read(void*,void*,uint32_t,uint32_t);
const esp_partition_t* esp_partition_find_first(int,int,const char*);
const esp_partition_t* esp_ota_get_running_partition(void);
const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*);
esp_err_t esp_ota_get_state_partition(const esp_partition_t*,esp_ota_img_states_t*);
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void);
const esp_app_desc_t* esp_app_get_description(void);
esp_err_t esp_ota_begin(const esp_partition_t*,size_t,esp_ota_handle_t*);
esp_err_t esp_ota_write(esp_ota_handle_t,const void*,size_t);
esp_err_t esp_ota_end(esp_ota_handle_t);
esp_err_t esp_ota_abort(esp_ota_handle_t);
esp_err_t esp_ota_get_partition_description(const esp_partition_t*,esp_app_desc_t*);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t*);
typedef struct {int state;} psa_hash_operation_t;
#define PSA_HASH_OPERATION_INIT {0}
#define PSA_SUCCESS 0
#define PSA_ALG_SHA_256 256
int psa_crypto_init(void);
int psa_hash_setup(psa_hash_operation_t*,int);
int psa_hash_update(psa_hash_operation_t*,const uint8_t*,size_t);
int psa_hash_finish(psa_hash_operation_t*,uint8_t*,size_t,size_t*);
int psa_hash_abort(psa_hash_operation_t*);
#define HTTP_EVENT_ON_HEADER 1
typedef struct {int event_id;char *header_key,*header_value;void* user_data;} esp_http_client_event_t;
typedef struct {const char* url;int timeout_ms,buffer_size;bool disable_auto_redirect;void* crt_bundle_attach;esp_err_t(*event_handler)(esp_http_client_event_t*);void* user_data;} esp_http_client_config_t;
typedef struct mock_http* esp_http_client_handle_t;
extern void* esp_crt_bundle_attach;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t*);
esp_err_t esp_http_client_open(esp_http_client_handle_t,int);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
esp_err_t esp_http_client_set_user_data(esp_http_client_handle_t,void*);
int esp_http_client_read_response(esp_http_client_handle_t,char*,int);
int esp_http_client_read(esp_http_client_handle_t,char*,int);
'''
class AdapterTest(unittest.TestCase):
    def test_ordering_and_failure_cleanup(self):
        with tempfile.TemporaryDirectory() as folder:
            folder=Path(folder);(folder/'psa').mkdir()
            (folder/'fake_sdk.h').write_text(HEADER)
            for name in ['sdkconfig.h','esp_err.h','esp_app_desc.h','esp_flash.h','esp_ota_ops.h','esp_partition.h','esp_crt_bundle.h','esp_http_client.h','psa/crypto.h']:
                (folder/name).write_text('#include "fake_sdk.h"\n')
            executable=folder/'test'
            command=['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined',
                '-I'+str(folder),'-I'+str(ROOT/'firmware/main/folio'),str(ROOT/'firmware/main/folio/ota_policy.c'),
                str(ROOT/'firmware/main/folio/ota_service.c'),str(ROOT/'tests/ota/test_service.c'),'-o',str(executable)]
            subprocess.run(command,check=True)
            subprocess.run([str(executable)],check=True)
if __name__=='__main__':unittest.main()
