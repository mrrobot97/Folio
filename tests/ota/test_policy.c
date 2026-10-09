#include "ota_policy.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    uint16_t parts[3];
    assert(folio_ota_version_parse("1.2.65535",parts));
    const char* bad[] = {"", "v1.2.3", "01.2.3", "1.2.3-rc1", "1.2", "1.2.65536", "1.2.3.4", "1..2", "1.2.99999999999999999999999999"};
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) assert(!folio_ota_version_parse(bad[i],parts));
    folio_ota_manifest_t m={1,FOLIO_OTA_BOARD,"esp32s3",FOLIO_OTA_LAYOUT,"1.2.3",
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",1000000};
    folio_ota_device_t d={.board=FOLIO_OTA_BOARD,.current_version="1.2.2",
        .next_slot_size=FOLIO_OTA_SLOT_SIZE,.battery_permille=500,.battery_known=true,
        .ota_layout_valid=true,.rollback_enabled=true,.boot_confirmed=true,
        .reading_state_saved=true,.trusted_transport=true,.provisioning_verified=true};
    assert(folio_ota_check(&m,&d)==FOLIO_OTA_OK);
#define REJECT(field,value,result) do {folio_ota_device_t x=d;x.field=value;assert(folio_ota_check(&m,&x)==result);} while(0)
    REJECT(ota_layout_valid,false,FOLIO_OTA_LAYOUT_REQUIRED);
    REJECT(provisioning_verified,false,FOLIO_OTA_PROVISIONING_REQUIRED);
    REJECT(rollback_enabled,false,FOLIO_OTA_ROLLBACK_REQUIRED);
    REJECT(boot_confirmed,false,FOLIO_OTA_UNCONFIRMED_BOOT);
    REJECT(current_version,"1.2.3",FOLIO_OTA_OLD_VERSION);
    REJECT(current_version,"2.0.0",FOLIO_OTA_OLD_VERSION);
    REJECT(current_version,"git-abc",FOLIO_OTA_OLD_VERSION);
    REJECT(next_slot_size,999999,FOLIO_OTA_TOO_LARGE);
    REJECT(battery_known,false,FOLIO_OTA_POWER_UNSAFE);
    REJECT(battery_permille,299,FOLIO_OTA_POWER_UNSAFE);
    REJECT(battery_permille,1001,FOLIO_OTA_POWER_UNSAFE);
    REJECT(reading_state_saved,false,FOLIO_OTA_UNSAVED_STATE);
    REJECT(trusted_transport,false,FOLIO_OTA_UNTRUSTED_SOURCE);
    REJECT(board,"other",FOLIO_OTA_WRONG_BOARD);
    m.image_size=FOLIO_OTA_SLOT_SIZE+1;assert(folio_ota_check(&m,&d)==FOLIO_OTA_TOO_LARGE);
    m.image_size=0;assert(folio_ota_check(&m,&d)==FOLIO_OTA_BAD_MANIFEST);
    m.image_size=1;m.sha256="bad";assert(folio_ota_check(&m,&d)==FOLIO_OTA_BAD_MANIFEST);
    puts("OTA policy: admission, version and rejection cases passed");
}
