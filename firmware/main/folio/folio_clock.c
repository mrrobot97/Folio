/* SPDX-License-Identifier: Apache-2.0
 * 用单调计时维持手动校准日期，FAST 保留，ECO 重启待校时。
 * Maintain an explicitly calibrated date with monotonic time: retained in FAST, unset after ECO boot.
 */
#include "folio_clock.h"
#include "reading_service.h"
#include "esp_timer.h"

static int64_t s_epoch, s_monotonic;
int folio_clock_month_days(int year, int month) {
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month < 1 || month > 12) return 0;
    return days[month - 1] + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}
bool folio_clock_set_local(int year, int month, int day, int hour, int minute) {
    if (year < 2020 || year > 2099 || month < 1 || month > 12 || day < 1 ||
        day > folio_clock_month_days(year, month) || hour < 0 || hour > 23 || minute < 0 || minute > 59) return false;
    int64_t days = 0;
    for (int y = 1970; y < year; ++y) days += 365 + (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    for (int m = 1; m < month; ++m) days += folio_clock_month_days(year, m);
    s_epoch = (days + day - 1) * 86400 + hour * 3600 + minute * 60 - 8 * 3600;
    s_monotonic = esp_timer_get_time() / 1000;
    folio_reading_set_clock(s_epoch, 480, s_monotonic);
    return true;
}
int64_t folio_clock_unix_seconds(void) {
    if (!s_epoch) return 0;
    int64_t elapsed = esp_timer_get_time() / 1000 - s_monotonic;
    return elapsed >= 0 ? s_epoch + elapsed / 1000 : 0;
}
bool folio_clock_local(int* year, int* month, int* day, int* hour, int* minute) {
    int64_t now = folio_clock_unix_seconds();
    if (!now) return false;
    now += 8 * 3600;
    int64_t days = now / 86400;
    int y = 1970, m = 1;
    for (;;) {
        int n = 365 + (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
        if (days < n) break;
        days -= n; ++y;
    }
    while (days >= folio_clock_month_days(y, m)) days -= folio_clock_month_days(y, m++);
    if (year) *year = y;
    if (month) *month = m;
    if (day) *day = (int)days + 1;
    if (hour) *hour = (int)(now / 3600 % 24);
    if (minute) *minute = (int)(now / 60 % 60);
    return true;
}
bool folio_clock_date(int* year, int* month, int* day) {
    return folio_clock_local(year, month, day, 0, 0);
}
