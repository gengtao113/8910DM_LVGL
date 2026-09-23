#define _DEFAULT_SOURCE

#include "ql_api_rtc.h"
#include "ic_call_phone.h"
#include <time.h>
#include <stdio.h>
#include <string.h>

ql_errcode_rtc_e ql_rtc_get_time(ql_rtc_time_t *tm)
{
    time_t now;
    struct tm ti;

    if (tm == NULL)
        return 1;
    time(&now);
    now += 8 * 3600;
    if (gmtime_r(&now, &ti) == NULL)
        return 1;
    tm->tm_sec = ti.tm_sec;
    tm->tm_min = ti.tm_min;
    tm->tm_hour = ti.tm_hour;
    tm->tm_mday = ti.tm_mday;
    tm->tm_mon = ti.tm_mon + 1;
    tm->tm_year = ti.tm_year + 1900;
    tm->tm_wday = ti.tm_wday;
    return 0;
}

ql_errcode_rtc_e ql_rtc_set_time(ql_rtc_time_t *tm)
{
    (void)tm;
    return 0;
}

void ql_rtc_print_time(ql_rtc_time_t tm)
{
    printf("rtc %04d-%02d-%02d %02d:%02d:%02d\n",
           tm.tm_year, tm.tm_mon, tm.tm_mday,
           tm.tm_hour, tm.tm_min, tm.tm_sec);
}

void ic_main_entry(void)
{
}

void ic_voice_call_start(char *dial_num)
{
    printf("sim: ic_voice_call_start %s\n", dial_num ? dial_num : "");
}

uint8_t ic_get_callog_total(void)
{
    return 0;
}

void ic_get_number_information(ic_call_phone_str ss[])
{
    (void)ss;
}

void ic_entry_dial_screen(void)
{
    printf("sim: ic_entry_dial_screen\n");
}

void ic_node_del(call_list **pNode, int place)
{
    (void)pNode;
    (void)place;
}
