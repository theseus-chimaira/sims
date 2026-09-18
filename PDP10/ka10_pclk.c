/* ka10_pclk.c: Petit Calendar Clock.

   Copyright (c) 2018, Lars Brinkhoff
   Copyright (c) 2020, Bruce Baumgart ( by editing Brinkhoff ka10_pd.c )

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "kx10_defs.h"

#ifndef NUM_DEVS_PCLK
#define NUM_DEVS_PCLK 0
#endif

#if (NUM_DEVS_PCLK > 0)

#define PCLK_DEVNUM 0730
#define PCLK_OFF       (1 << DEV_V_UF)
#define PCLK_REALTIME  (1 << UNIT_V_UF)
#if PDP6
#define PCLK_UNIT_MODE PCLK_REALTIME
#define PCLK_DEV_MODE  0
#else
#define PCLK_UNIT_MODE 0
#define PCLK_DEV_MODE  DEV_DIS
#endif
#define PIA_CH          u3
#define PIA_FLG         07
#define CLK_IRQ         010

t_stat         pclk_devio(uint32 dev, uint64 *data);
const char *pclk_description (DEVICE *dptr);
t_stat         pclk_srv(UNIT *uptr);
t_stat         pclk_set_on(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat         pclk_set_off(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat         pclk_show_on(FILE *st, UNIT *uptr, int32 val, CONST void *desc);

UNIT pclk_unit[] = {
    {UDATA(pclk_srv, UNIT_IDLE|UNIT_DISABLE|PCLK_UNIT_MODE, 0)},  /* 0 */
};
DIB pclk_dib = {PCLK_DEVNUM, 1, &pclk_devio, NULL};
MTAB pclk_mod[] = {
    { MTAB_VDV, 0, "ON", "ON", pclk_set_on, pclk_show_on },
    { MTAB_VDV, PCLK_OFF, NULL, "OFF", pclk_set_off },
    { PCLK_REALTIME, PCLK_REALTIME, "realtime", "REALTIME", NULL },
    { PCLK_REALTIME, 0, "historical", "HISTORICAL", NULL },
    { 0 }
    };
DEVICE pclk_dev = {
    "PCLK", pclk_unit, NULL, pclk_mod, 1, 8, 0, 1, 8, 36, NULL, NULL, NULL, NULL, NULL, NULL,
    &pclk_dib, DEV_DISABLE | PCLK_DEV_MODE | DEV_DEBUG, 0, NULL, NULL, NULL, NULL, NULL, NULL, &pclk_description
};
/*
        The original PCLK was installed on the PDP-6 I/O bus at the SAIL
        D.C. Power Lab in 1967.  HISTORICAL mode preserves the old SIMH
        re-enactment: Friday 1974-07-26 with the host local wall-clock time.
        REALTIME mode reports the actual host UTC calendar date and time.

        Months are encoded 4,5,6,7,8,9,A,B,C,D,E,F for January to December.
        The year uses two hexadecimal nibbles as decimal digits, so 1974 is
        encoded 0x74 rather than binary 74.  Day-of-month runs from 0 to 30.
*/
t_stat pclk_devio(uint32 dev, uint64 *data)
{
    time_t t = sim_get_time(NULL);
    struct tm *dt;
    uint64 year_month;
    uint64 day;
    uint64 hour;
    uint64 minute;
    uint64 second;
    uint64 coni_word;
    uint64 datai_word;

    if (pclk_unit[0].flags & PCLK_REALTIME) {
        unsigned int year;
        dt = gmtime(&t);
        if (dt == NULL)
            return SCPE_IERR;
        year = (unsigned int)(dt->tm_year + 1900) % 100;
        year_month = ((uint64)(year / 10) << 8) |
            ((uint64)(year % 10) << 4) | (uint64)(dt->tm_mon + 4);
        day = (uint64)(dt->tm_mday - 1);
    } else {
        dt = localtime(&t);
        if (dt == NULL)
            return SCPE_IERR;
        year_month = 0x74A;
        day = 25;
    }
    hour = (uint64)dt->tm_hour;
    minute = (uint64)dt->tm_min;
    second = (uint64)dt->tm_sec;
    coni_word = (minute << 26) | (second << 20);
    coni_word += 02020136700;       /* Petit/Panofsky offset. */
    datai_word = (year_month << 16) | (day << 11) |
        (hour << 6) | minute;
    datai_word += 05004;

    switch(dev & 3) {
    case DATAI:
      *data = datai_word;
      break;
    case CONI:
      *data = coni_word;
      break;
    case CONO:
        pclk_unit[0].PIA_CH &= ~(PIA_FLG);
        pclk_unit[0].PIA_CH |= (int32)(*data & PIA_FLG);
        break;
    default:
        break;
    }
    return SCPE_OK;
}

t_stat
pclk_srv(UNIT * uptr)
{
    if (uptr->PIA_CH & PIA_FLG) {
        uptr->PIA_CH |= CLK_IRQ;
        //        set_interrupt(PCLK_DEVNUM, uptr->PIA_CH);
    } else
        sim_cancel(uptr);
    return SCPE_OK;
}

const char *pclk_description (DEVICE *dptr)
{
    return "Stanford A.I. Lab Phil Petit calendar clock";
}

t_stat pclk_set_on(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    DEVICE *dptr = &pclk_dev;
    dptr->flags &= ~PCLK_OFF;
    return SCPE_OK;
}

t_stat pclk_set_off(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    DEVICE *dptr = &pclk_dev;
    dptr->flags |= PCLK_OFF;
    return SCPE_OK;
}

t_stat pclk_show_on(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    DEVICE *dptr = &pclk_dev;
    fprintf (st, "%s", (dptr->flags & PCLK_OFF) ? "off" : "on");
    return SCPE_OK;
}
#endif
