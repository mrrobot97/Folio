/* Test-only fake platform. No network, devices or real cryptographic implementation. */
#include "fake_sdk.h"
#include "ota_service.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool layout=true, checkpoint_ok=true, provision_ok=true, cancelled=false;
static bool http_failure=false, digest_failure=false, write_failure=false, end_failure=false;
static unsigned power=500;
static int begins,writes,aborts,ends,boot_selects,clients,confirms,opens;
static char manifest_text[512];
static esp_partition_t parts[]={{0x9000,0x5000,false},{0xe000,0x1000,false},
    {0x10000,0x400000,false},{0x410000,0x500000,false},{0x910000,0x400000,false},{0xd10000,0x2000,false}};
static esp_app_desc_t current={"0.1.0","Read_Pico"};
void* esp_flash_default_chip=(void*)1;
void* esp_crt_bundle_attach=(void*)1;
esp_err_t esp_flash_get_size(void* p,uint32_t* size){*size=0x1000000;return ESP_OK;}
esp_err_t esp_flash_read(void* p,void* out,uint32_t offset,uint32_t size){memset(out,0,size);return ESP_OK;}
const esp_partition_t* esp_partition_find_first(int type,int subtype,const char* label){
    if(!layout)return NULL;
    if(type==0 && subtype==0)return NULL;
    const char* labels[]={"nvs","phy_init","ota_0","storage","ota_1","otadata"};
    for(int i=0;i<6;i++)if(label && !strcmp(label,labels[i]))return &parts[i];return NULL;
}
const esp_partition_t* esp_ota_get_running_partition(void){return &parts[2];}
const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t* p){return &parts[4];}
esp_err_t esp_ota_get_state_partition(const esp_partition_t* p,int* state){*state=ESP_OTA_IMG_PENDING_VERIFY;return ESP_OK;}
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void){confirms++;return ESP_OK;}
const esp_app_desc_t* esp_app_get_description(void){return &current;}
esp_err_t esp_ota_begin(const esp_partition_t* p,size_t size,unsigned* handle){assert(p==&parts[4]);assert(size==100);begins++;*handle=1;return ESP_OK;}
esp_err_t esp_ota_write(unsigned h,const void* p,size_t size){writes++;return write_failure?ESP_FAIL:ESP_OK;}
esp_err_t esp_ota_end(unsigned h){ends++;return end_failure?ESP_FAIL:ESP_OK;}
esp_err_t esp_ota_abort(unsigned h){aborts++;return ESP_OK;}
esp_err_t esp_ota_get_partition_description(const esp_partition_t* p,esp_app_desc_t* desc){*desc=(esp_app_desc_t){"0.2.0","Read_Pico"};return ESP_OK;}
esp_err_t esp_ota_set_boot_partition(const esp_partition_t* p){boot_selects++;return ESP_OK;}
int psa_crypto_init(void){return 0;}
int psa_hash_setup(psa_hash_operation_t* p,int algorithm){p->state=0;return 0;}
int psa_hash_update(psa_hash_operation_t* p,const uint8_t* data,size_t size){p->state+=(int)size;return 0;}
int psa_hash_finish(psa_hash_operation_t* p,uint8_t* digest,size_t cap,size_t* size){memset(digest,0,32);if(digest_failure && p->state==100)digest[0]=1;*size=32;return 0;}
int psa_hash_abort(psa_hash_operation_t* p){return 0;}
struct mock_http {bool manifest;};
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t* c){
    assert(c->disable_auto_redirect && c->crt_bundle_attach);assert(strstr(c->url,"https://github.com/mrrobot97/Folio/releases/download/v0.2.0/")==c->url);
    struct mock_http* h=malloc(sizeof(*h));h->manifest=strstr(c->url,"ota.txt")!=NULL;clients++;return h;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t h,int n){opens++;return http_failure?ESP_FAIL:ESP_OK;}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t h){free(h);clients--;return ESP_OK;}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t h){return h->manifest?(int64_t)strlen(manifest_text):100;}
int esp_http_client_get_status_code(esp_http_client_handle_t h){return 200;}
esp_err_t esp_http_client_set_user_data(esp_http_client_handle_t h,void* p){return ESP_OK;}
int esp_http_client_read_response(esp_http_client_handle_t h,char* out,int size){memcpy(out,manifest_text,size);return size;}
int esp_http_client_read(esp_http_client_handle_t h,char* out,int size){memset(out,0,size);return size;}
static bool provisioning(void* p,folio_ota_provisioning_t* out){*out=(folio_ota_provisioning_t){.board=FOLIO_OTA_BOARD,.bootloader_size=4096,.physical_board_verified=true,.installed_bootloader_rollback_verified=true};return provision_ok;}
static bool checkpoint(void* p){return checkpoint_ok;}
static bool battery(void* p,unsigned* out){*out=power;return true;}
static bool cancel(void* p){return cancelled;}
static void reset(void){
    begins=writes=aborts=ends=boot_selects=clients=opens=0;
    layout=checkpoint_ok=provision_ok=true;cancelled=http_failure=digest_failure=write_failure=end_failure=false;power=500;
    snprintf(manifest_text,sizeof(manifest_text),"FOLIO-OTA-1\nRDP-G01-W\nesp32s3\nfolio-ota-v1\n0.2.0\nRead_Pico\n100\n%064d\n",0);
}
int main(void){
    folio_ota_request_t r={.version="0.2.0",.provisioning=provisioning,.checkpoint=checkpoint,.battery=battery,.cancelled=cancel};
    folio_ota_result_t refusal;
    reset();assert(folio_ota_confirm_boot(FOLIO_OTA_HEALTH_DISPLAY)==ESP_ERR_INVALID_STATE);assert(confirms==0);
    assert(folio_ota_install(&r,&refusal)!=ESP_OK);assert(refusal==FOLIO_OTA_UNCONFIRMED_BOOT);assert(begins==0);
    assert(folio_ota_confirm_boot(FOLIO_OTA_HEALTH_ALL)==ESP_OK);assert(confirms==1);
    reset();assert(folio_ota_install(&r,&refusal)==ESP_OK);assert(begins==1 && writes==1 && ends==1 && boot_selects==1 && clients==0 && aborts==0);
    reset();layout=false;assert(folio_ota_install(&r,&refusal)!=ESP_OK);assert(refusal==FOLIO_OTA_LAYOUT_REQUIRED && !begins && !opens);
    reset();provision_ok=false;assert(folio_ota_install(&r,&refusal)!=ESP_OK);assert(refusal==FOLIO_OTA_PROVISIONING_REQUIRED && !begins && !opens);
    reset();checkpoint_ok=false;assert(folio_ota_install(&r,&refusal)!=ESP_OK);assert(refusal==FOLIO_OTA_UNSAVED_STATE && !begins && !opens);
    reset();power=299;assert(folio_ota_install(&r,&refusal)!=ESP_OK);assert(refusal==FOLIO_OTA_POWER_UNSAFE && !begins && !opens);
    reset();cancelled=true;assert(folio_ota_install(&r,&refusal)!=ESP_OK);assert(!begins && !opens);
    reset();http_failure=true;assert(folio_ota_install(&r,&refusal)!=ESP_OK);assert(!begins && !clients);
    reset();digest_failure=true;assert(folio_ota_install(&r,&refusal)==ESP_ERR_INVALID_CRC);assert(aborts==1 && !boot_selects && !clients);
    reset();write_failure=true;assert(folio_ota_install(&r,&refusal)!=ESP_OK);assert(aborts==1 && !boot_selects && !clients);
    reset();end_failure=true;assert(folio_ota_install(&r,&refusal)!=ESP_OK);assert(ends==1 && !aborts && !boot_selects && !clients);
    reset();strcat(manifest_text,"junk");assert(folio_ota_install(&r,&refusal)!=ESP_OK);assert(!begins && !clients);
    puts("OTA adapter host mock: health, provisioning, layout, power, cancellation, HTTPS error, hash/write/end cleanup passed");
}
