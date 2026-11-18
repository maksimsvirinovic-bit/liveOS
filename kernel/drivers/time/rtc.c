#include "rtc.h"
#include "x86_64/io.h"

#define CMOS_INDEX 0x70
#define CMOS_DATA  0x71

static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_INDEX, reg);
    return inb(CMOS_DATA);
}

static int cmos_update_in_progress(void) {
    return cmos_read(0x0A) & 0x80;
}

static uint8_t bcd_to_bin(uint8_t bcd) {
    return (bcd & 0x0F) + ((bcd >> 4) * 10);
}

void rtc_read(RtcTime* t) {
    while (cmos_update_in_progress());

    t->second = cmos_read(0x00);
    t->minute = cmos_read(0x02);
    t->hour   = cmos_read(0x04);
    t->day    = cmos_read(0x07);
    t->month  = cmos_read(0x08);
    t->year   = cmos_read(0x09);

    uint8_t regB = cmos_read(0x0B);
    if (!(regB & 0x04)) {
        t->second = bcd_to_bin(t->second);
        t->minute = bcd_to_bin(t->minute);
        uint8_t pm = t->hour & 0x80;
        t->hour = bcd_to_bin(t->hour & 0x7F);
        if (!(regB & 0x02) && pm) t->hour = (t->hour + 12) % 24;
        t->day   = bcd_to_bin(t->day);
        t->month = bcd_to_bin(t->month);
        t->year  = bcd_to_bin(t->year);
    }

    t->year += 2000;
}