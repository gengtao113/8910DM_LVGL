#ifndef _QL_API_RTC_H_
#define _QL_API_RTC_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ql_rtc_time_struct {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
} ql_rtc_time_t;

typedef int ql_errcode_rtc_e;

ql_errcode_rtc_e ql_rtc_get_time(ql_rtc_time_t *tm);
ql_errcode_rtc_e ql_rtc_set_time(ql_rtc_time_t *tm);
void ql_rtc_print_time(ql_rtc_time_t tm);

#ifdef __cplusplus
}
#endif

#endif
