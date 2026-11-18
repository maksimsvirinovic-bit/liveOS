#ifndef RTC_H
#define RTC_H

#include <stdint.h>

typedef struct {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
} RtcTime;

void rtc_read(RtcTime* t);

#endif