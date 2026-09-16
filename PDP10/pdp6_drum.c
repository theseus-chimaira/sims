/* pdp6_drum.c: PDP-6 Type 167 I/O Processor and Type 236 drum.

   Copyright (c) 2026, Theseus

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
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
   THE SOFTWARE.

   The programming interface implemented here follows the PDP-6 JOSS-II
   sources.  The Type 167 supplies the direct-memory word count/address
   state; the Type 236 supplies four fixed-head drum units.
*/

#include "kx10_defs.h"
#include "sim_timer.h"
#include <math.h>

#ifndef NUM_DEVS_DP167
#define NUM_DEVS_DP167 0
#endif
#ifndef NUM_DEVS_DR236
#define NUM_DEVS_DR236 0
#endif

#if (NUM_DEVS_DP167 > 0) && (NUM_DEVS_DR236 > 0)

#define DP167_DEVNUM        0010
#define DR236_DEVNUM        0400
#define DR236_UNITS         4

#define DP167_PIA           0000007
#define DP167_DONE          0000010
#define DP167_NXM           0000020
#define DP167_MISSED        0000040
#define DP167_OUT           0000100
#define DP167_RDY           0000200
#define DP167_PARITY        0100000
#define DP167_ERROR         (DP167_MISSED | DP167_NXM | DP167_PARITY)
#define DP167_WC_MASK       0777777
#define DP167_MA_MASK       0777777

#define DR236_PIA           0000007
#define DR236_DONE          0000100
#define DR236_ERROR         0001000
#define DR236_CLEAR_ERROR   0200000
#define DR236_CMD_MASK      0000770
#define DR236_COMPARE       0000210
#define DR236_WRITE         0000220
#define DR236_READ          0000230
#define DR236_SELECT        0000260
#define DR236_DESELECT      0000270

#define DR236_WORDS_TRACK   8192u
#define DR236_TRACKS        128u
#define DR236_WORDS_UNIT    (DR236_WORDS_TRACK * DR236_TRACKS)
#define DR236_GROUP_WORDS   16u
#define DR236_GROUPS_UNIT   (DR236_WORDS_UNIT / DR236_GROUP_WORDS)
#define DR236_GROUP_MASK    (DR236_GROUPS_UNIT - 1u)
/* JOSS uses the low 16 bits as 16-word drum groups; bits 16-17 select unit. */
#define DR236_ADDR_MASK     0777777u
#define DR236_UNIT_SHIFT    16
#define DR236_WORD_MASK     (DR236_WORDS_UNIT - 1u)
#define DR236_WORD_USEC     6.4
#define DR236_REV_USEC      (DR236_WORD_USEC * DR236_WORDS_TRACK)

#define DR236_PHASE_IDLE    0
#define DR236_PHASE_XFER    1
#define DR236_PHASE_FINISH  2

static uint32 dp167_status;
static uint32 dp167_wc;
static uint32 dp167_ma;
static uint32 dp167_out;
static t_bool dp167_ready;

static uint32 dr236_status;
static uint32 dr236_addr;
static uint32 dr236_pia;
static uint32 dr236_op;
static uint32 dr236_word;
static uint32 dr236_phase;
static int32 dr236_selected = -1;
static t_bool dr236_busy;

static t_stat dp167_devio(uint32 dev, uint64 *data);
static t_stat dr236_devio(uint32 dev, uint64 *data);
static t_stat dr236_svc(UNIT *uptr);
static t_stat dp167_reset(DEVICE *dptr);
static t_stat dr236_reset(DEVICE *dptr);
static t_stat dr236_attach(UNIT *uptr, CONST char *cptr);
static t_stat dr236_detach(UNIT *uptr);
static t_stat dr236_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                         const char *cptr);
static const char *dp167_description(DEVICE *dptr);
static const char *dr236_description(DEVICE *dptr);

static DIB dp167_dib = { DP167_DEVNUM, 1, &dp167_devio, NULL };
static DIB dr236_dib = { DR236_DEVNUM, 1, &dr236_devio, NULL };

static UNIT dp167_unit[] = {
    { UDATA(NULL, UNIT_IDLE, 0) }
};

static UNIT dr236_unit[] = {
    { UDATA(&dr236_svc, UNIT_FIX|UNIT_ATTABLE|UNIT_DISABLE|UNIT_ROABLE,
            DR236_WORDS_UNIT) },
    { UDATA(&dr236_svc, UNIT_FIX|UNIT_ATTABLE|UNIT_DISABLE|UNIT_ROABLE,
            DR236_WORDS_UNIT) },
    { UDATA(&dr236_svc, UNIT_FIX|UNIT_ATTABLE|UNIT_DISABLE|UNIT_ROABLE,
            DR236_WORDS_UNIT) },
    { UDATA(&dr236_svc, UNIT_FIX|UNIT_ATTABLE|UNIT_DISABLE|UNIT_ROABLE,
            DR236_WORDS_UNIT) }
};

static REG dp167_reg[] = {
    { ORDATA(STATUS, dp167_status, 18) },
    { ORDATA(WC, dp167_wc, 18) },
    { ORDATA(MA, dp167_ma, 18) },
    { FLDATA(OUT, dp167_out, 0) },
    { FLDATA(READY, dp167_ready, 0) },
    { 0 }
};

static REG dr236_reg[] = {
    { ORDATA(STATUS, dr236_status, 18) },
    { ORDATA(ADDR, dr236_addr, 18) },
    { ORDATA(PIA, dr236_pia, 3) },
    { ORDATA(OP, dr236_op, 9) },
    { ORDATA(WORD, dr236_word, 20) },
    { FLDATA(BUSY, dr236_busy, 0) },
    { 0 }
};

static MTAB dr236_mod[] = {
    { MTAB_XTD|MTAB_VUN, 0, "write enabled", "WRITEENABLED",
      &set_writelock, &show_writelock, NULL, "Write enable drum" },
    { MTAB_XTD|MTAB_VUN, 1, NULL, "LOCKED",
      &set_writelock, NULL, NULL, "Write lock drum" },
    { 0 }
};

DEVICE dp167_dev = {
    "DP", dp167_unit, dp167_reg, NULL,
    1, 8, 18, 1, 8, 36,
    NULL, NULL, &dp167_reset, NULL, NULL, NULL,
    &dp167_dib, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, NULL, NULL, NULL, &dp167_description
};

DEVICE dr236_dev = {
    "DR", dr236_unit, dr236_reg, dr236_mod,
    DR236_UNITS, 8, 18, 1, 8, 36,
    NULL, NULL, &dr236_reset, NULL, &dr236_attach, &dr236_detach,
    &dr236_dib, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &dr236_help, NULL, NULL, &dr236_description
};

static void dp167_interrupt(void)
{
    if (dp167_status & DP167_PIA)
        set_interrupt(DP167_DEVNUM, dp167_status);
}

static void dp167_complete(void)
{
    dp167_ready = FALSE;
    dp167_status &= ~DP167_RDY;
    dp167_status |= DP167_DONE;
    dp167_interrupt();
}

static void dp167_error(uint32 flag)
{
    dp167_ready = FALSE;
    dp167_status &= ~DP167_RDY;
    dp167_status |= flag;
    dp167_interrupt();
}

static void dp167_advance(void)
{
    dp167_wc = (dp167_wc + 1u) & DP167_WC_MASK;
    dp167_ma = (dp167_ma + 1u) & DP167_MA_MASK;
    if (dp167_wc == 0)
        dp167_complete();
}

static uint64 dp167_datai(void)
{
    return ((((uint64)dp167_wc) & DP167_WC_MASK) << 18) |
           (((uint64)dp167_ma) & DP167_MA_MASK);
}

static void dr236_interrupt(void)
{
    if (dr236_pia)
        set_interrupt(DR236_DEVNUM, dr236_pia);
}

static void dr236_finish(t_bool error)
{
    dr236_busy = FALSE;
    dr236_phase = DR236_PHASE_IDLE;
    dr236_status |= DR236_DONE;
    if (error)
        dr236_status |= DR236_ERROR;
    dr236_interrupt();
}

static void dr236_cancel(void)
{
    int i;

    for (i = 0; i < DR236_UNITS; i++)
        sim_cancel(&dr236_unit[i]);
    dr236_busy = FALSE;
    dr236_phase = DR236_PHASE_IDLE;
}

static double dr236_rotation_delay(uint32 word)
{
    double ips;
    double usec;
    double phase;
    double target;
    double delta;

    ips = sim_timer_inst_per_sec();
    if (ips <= 0.0)
        ips = 1000000.0;
    usec = (sim_gtime() * 1000000.0) / ips;
    phase = fmod(usec, DR236_REV_USEC) / DR236_WORD_USEC;
    target = (double)(word & (DR236_WORDS_TRACK - 1u));
    delta = target - phase;
    if (delta < 0.0)
        delta += (double)DR236_WORDS_TRACK;
    return delta * DR236_WORD_USEC;
}

static int dr236_media_read(UNIT *uptr, uint32 word, uint64 *data)
{
    t_offset offset;
    size_t n;

    offset = (t_offset)word * (t_offset)sizeof(*data);
    if (sim_fseek(uptr->fileref, offset, SEEK_SET) != 0)
        return 1;
    n = sim_fread(data, sizeof(*data), 1, uptr->fileref);
    if (n == 0) {
        clearerr(uptr->fileref);
        *data = 0;
        return 0;
    }
    if (n != 1)
        return 1;
    *data &= FMASK;
    return 0;
}

static int dr236_media_write(UNIT *uptr, uint32 word, uint64 data)
{
    t_offset offset;

    data &= FMASK;
    offset = (t_offset)word * (t_offset)sizeof(data);
    if (sim_fseek(uptr->fileref, offset, SEEK_SET) != 0)
        return 1;
    if (sim_fwrite(&data, sizeof(data), 1, uptr->fileref) != 1)
        return 1;
    if (word >= uptr->hwmark)
        uptr->hwmark = word + 1u;
    return 0;
}

static void dr236_advance(void)
{
    uint32 unit_bits;
    uint32 group;

    dr236_word = (dr236_word + 1u) & DR236_WORD_MASK;
    if ((dr236_word & (DR236_GROUP_WORDS - 1u)) == 0) {
        unit_bits = dr236_addr & ~DR236_GROUP_MASK;
        group = (dr236_addr + 1u) & DR236_GROUP_MASK;
        dr236_addr = (unit_bits | group) & DR236_ADDR_MASK;
    }
}

static void dr236_start(void)
{
    UNIT *uptr;
    double delay;

    if (dr236_selected < 0 || dr236_selected >= DR236_UNITS) {
        dr236_finish(TRUE);
        return;
    }
    uptr = &dr236_unit[dr236_selected];
    if ((uptr->flags & UNIT_ATT) == 0) {
        dr236_finish(TRUE);
        return;
    }
    if ((dr236_op == DR236_WRITE) &&
        (uptr->flags & (UNIT_RO | UNIT_WLK))) {
        dr236_finish(TRUE);
        return;
    }
    if (!dp167_ready) {
        dp167_error(DP167_MISSED);
        dr236_finish(TRUE);
        return;
    }
    if ((dr236_op == DR236_READ && dp167_out) ||
        ((dr236_op == DR236_WRITE || dr236_op == DR236_COMPARE) &&
         !dp167_out)) {
        dp167_error(DP167_MISSED);
        dr236_finish(TRUE);
        return;
    }

    dr236_word = (dr236_addr & DR236_GROUP_MASK) * DR236_GROUP_WORDS;
    dr236_busy = TRUE;
    dr236_phase = DR236_PHASE_XFER;
    delay = dr236_rotation_delay(dr236_word);
    sim_timer_activate_after(uptr, delay);
}

static t_stat dr236_svc(UNIT *uptr)
{
    uint64 mem;
    uint64 media;
    int nxm;

    if (!dr236_busy || dr236_selected < 0 ||
        uptr != &dr236_unit[dr236_selected])
        return SCPE_OK;

    if (dr236_phase == DR236_PHASE_FINISH) {
        dr236_finish((dr236_status & DR236_ERROR) != 0);
        return SCPE_OK;
    }

    if (dr236_phase != DR236_PHASE_XFER)
        return SCPE_OK;

    media = 0;
    mem = 0;
    nxm = 0;

    if (dr236_op == DR236_READ) {
        if (dr236_media_read(uptr, dr236_word, &media)) {
            dr236_finish(TRUE);
            return SCPE_OK;
        }
        mem = media;
        nxm = Mem_write_word(dp167_ma, &mem, 0);
    } else {
        nxm = Mem_read_word(dp167_ma, &mem, 0);
        if (!nxm) {
            if (dr236_op == DR236_WRITE) {
                if (dr236_media_write(uptr, dr236_word, mem)) {
                    dr236_finish(TRUE);
                    return SCPE_OK;
                }
            } else if (dr236_op == DR236_COMPARE) {
                if (dr236_media_read(uptr, dr236_word, &media)) {
                    dr236_finish(TRUE);
                    return SCPE_OK;
                }
                if ((mem & FMASK) != (media & FMASK))
                    dr236_status |= DR236_ERROR;
            }
        }
    }

    if (nxm) {
        dp167_error(DP167_NXM);
        dr236_status |= DR236_ERROR;
        dr236_phase = DR236_PHASE_FINISH;
        sim_timer_activate_after(uptr, DR236_WORD_USEC);
        return SCPE_OK;
    }

    dp167_advance();
    dr236_advance();

    if (!dp167_ready) {
        dr236_phase = DR236_PHASE_FINISH;
        sim_timer_activate_after(uptr, DR236_WORD_USEC);
    } else {
        sim_timer_activate_after(uptr, DR236_WORD_USEC);
    }
    return SCPE_OK;
}

static t_stat dp167_devio(uint32 dev, uint64 *data)
{
    switch (dev & 03) {
    case CONO:
        clr_interrupt(DP167_DEVNUM);
        dp167_status = (uint32)(*data & (DP167_PIA | DP167_OUT));
        dp167_out = ((*data & DP167_OUT) != 0);
        break;
    case CONI:
        *data = dp167_status;
        break;
    case DATAO:
        dp167_wc = (uint32)((*data >> 18) & DP167_WC_MASK);
        dp167_ma = (uint32)(*data & DP167_MA_MASK);
        dp167_status &= ~(DP167_DONE | DP167_RDY);
        clr_interrupt(DP167_DEVNUM);
        /* WC is an 18-bit negative count; zero denotes a full 256K transfer. */
        dp167_ready = TRUE;
        dp167_status |= DP167_RDY;
        break;
    case DATAI:
        *data = dp167_datai();
        break;
    }
    return SCPE_OK;
}

static t_stat dr236_devio(uint32 dev, uint64 *data)
{
    uint32 cmd;

    switch (dev & 03) {
    case CONO:
        if (*data & DR236_CLEAR_ERROR)
            dr236_status &= ~DR236_ERROR;
        cmd = (uint32)(*data & DR236_CMD_MASK);
        if (cmd == DR236_DESELECT) {
            dr236_cancel();
            dr236_selected = -1;
            dr236_status &= ~DR236_DONE;
            dr236_pia = 0;
            clr_interrupt(DR236_DEVNUM);
        } else if (cmd == DR236_SELECT) {
            dr236_cancel();
            dr236_selected = (int32)((dr236_addr >> DR236_UNIT_SHIFT) & 03);
            dr236_status &= ~DR236_DONE;
            clr_interrupt(DR236_DEVNUM);
        } else if (cmd == DR236_COMPARE || cmd == DR236_WRITE ||
                   cmd == DR236_READ) {
            dr236_pia = (uint32)(*data & DR236_PIA);
            dr236_op = cmd;
            dr236_status &= ~DR236_DONE;
            clr_interrupt(DR236_DEVNUM);
            dr236_start();
        }
        break;
    case CONI:
        *data = dr236_status | dr236_pia;
        break;
    case DATAO:
        dr236_addr = (uint32)(*data & DR236_ADDR_MASK);
        break;
    case DATAI:
        *data = dr236_addr & DR236_ADDR_MASK;
        break;
    }
    return SCPE_OK;
}

static t_stat dp167_reset(DEVICE *dptr)
{
    dp167_status = 0;
    dp167_wc = 0;
    dp167_ma = 0;
    dp167_out = 0;
    dp167_ready = FALSE;
    clr_interrupt(DP167_DEVNUM);
    return SCPE_OK;
}

static t_stat dr236_reset(DEVICE *dptr)
{
    dr236_cancel();
    dr236_status = 0;
    dr236_addr = 0;
    dr236_pia = 0;
    dr236_op = 0;
    dr236_word = 0;
    dr236_selected = -1;
    clr_interrupt(DR236_DEVNUM);
    return SCPE_OK;
}

static t_stat dr236_attach(UNIT *uptr, CONST char *cptr)
{
    return attach_unit(uptr, cptr);
}

static t_stat dr236_detach(UNIT *uptr)
{
    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_OK;
    if (sim_is_active(uptr))
        sim_cancel(uptr);
    if (dr236_selected >= 0 && uptr == &dr236_unit[dr236_selected]) {
        dr236_busy = FALSE;
        dr236_phase = DR236_PHASE_IDLE;
        dr236_selected = -1;
    }
    return detach_unit(uptr);
}

static t_stat dr236_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                         const char *cptr)
{
    fprintf(st, "PDP-6 Type 236 fixed-head magnetic drum (DR)\n\n");
    fprintf(st, "Four units are supported.  Each unit stores 1,048,576 36-bit words.\n");
    fprintf(st, "The Type 236 transfers through the Type 167 I/O Processor (DP).\n");
    fprint_set_help(st, dptr);
    fprint_show_help(st, dptr);
    return SCPE_OK;
}

static const char *dp167_description(DEVICE *dptr)
{
    return "PDP-6 Type 167 I/O Processor";
}

static const char *dr236_description(DEVICE *dptr)
{
    return "PDP-6 Type 236 fixed-head magnetic drum";
}

#endif
