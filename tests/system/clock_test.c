/* SPDX-License-Identifier: Apache-2.0 */
#include "folio_clock.h"
#include <assert.h>
#include <stdio.h>
static int64_t now, set_epoch;
static int updates;
int64_t esp_timer_get_time(void) { return now; }
void folio_reading_set_clock(int64_t epoch, int32_t offset, int64_t monotonic) {
    assert(offset == 480 && monotonic == now / 1000); set_epoch = epoch; ++updates;
}
int main(void) {
    int y = 7,m = 7,d = 7,h = 7,min = 7;
    assert(!folio_clock_local(&y,&m,&d,&h,&min) && y == 7 && !folio_clock_unix_seconds());
    assert(!folio_clock_set_local(2025,2,29,10,0));
    assert(!folio_clock_set_local(2026,13,1,10,0));
    assert(!folio_clock_set_local(2026,1,1,24,0) && !updates);
    assert(folio_clock_set_local(2024,2,29,23,59));
    assert(set_epoch == 1709222340 && updates == 1);
    now += 120000000;
    assert(folio_clock_local(&y,&m,&d,&h,&min));
    assert(y == 2024 && m == 3 && d == 1 && h == 0 && min == 1);
    assert(folio_clock_set_local(2099,12,31,23,59));
    now += 60000000;
    assert(folio_clock_date(&y,&m,&d) && y == 2100 && m == 1 && d == 1);
    assert(folio_clock_month_days(2100,2) == 28 && folio_clock_month_days(2000,2) == 29);
    assert(folio_clock_set_local(2026,10,9,15,12));
    now -= 1000000;
    assert(!folio_clock_date(&y,&m,&d));
    puts("clock: unknown boot, strict date validation, leap day, midnight, UTC+8 and timer rollback passed");
}
