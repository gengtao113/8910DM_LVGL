/// @file ic_hal.c
/// @Synopsis:  hal layer adaptor for win32
/// @author Wang Hua (whfyzg@gmail.com))
/// @version V1.0
/// @date 2020-11-14

#include "stdint.h"
#include "stdbool.h"
#include <string.h>
#include "stdio.h"
#include "stdlib.h"

#include "time.h"

#include "ic_hal_rtc.h"

/*******************************************************
 *
 * rtc function layer
 ******************************************************/
bool ic_hal_rtc_init(void)
{
    return true;
}

bool ic_hal_rtc_deinit(void)
{
    return true;
}


bool ic_hal_rtc_get_time(ic_hal_rtc_t *rtc)
{
    time_t my_time;
    struct tm timeinfo;

    time(&my_time);
    my_time += 8 * 3600;
#ifdef _WIN32
    {
        struct tm *p = gmtime(&my_time);
        if (p == NULL)
            return false;
        timeinfo = *p;
    }
#else
    if (gmtime_r(&my_time, &timeinfo) == NULL)
        return false;
#endif

    rtc->year = (uint8_t)(timeinfo.tm_year + 1900 - 2000);
    rtc->mon  = (uint8_t)(timeinfo.tm_mon + 1);
    rtc->day  = (uint8_t)timeinfo.tm_mday;
    rtc->hour = (uint8_t)timeinfo.tm_hour;
    rtc->min  = (uint8_t)timeinfo.tm_min;
    rtc->sec  = (uint8_t)timeinfo.tm_sec;
    rtc->week = (uint8_t)timeinfo.tm_wday;
    rtc->msec = 0;

    return true;
}

bool ic_hal_rtc_set_time(ic_hal_rtc_t *rtc)
{
    return true;
}

bool ic_hal_rtc_set_time_notification(ic_hal_time_notification_t period, 
        ic_hal_time_notification_cb_t time_cb, void *user_data)
{
    return false;
}

bool ic_hal_rtc_get_alarm_time(ic_hal_rtc_t *rtc)
{
    return false;
}

bool ic_hal_rtc_set_alarm_time(ic_hal_rtc_t *rtc)
{
    return false;
}

bool ic_hal_rtc_set_alarm_cb(ic_hal_time_notification_cb_t callback, void *user_data) {
    return false;
}

bool ic_hal_rtc_enable_alarm(void)
{
    return false;
}

bool ic_hal_rtc_disable_alarm(void)
{
    return false;
}
