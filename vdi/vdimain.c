/*
 * vdimain.c - Many nongraphicle VDI functions
 *
 * Copyright (c) 1999 Caldera, Inc.
 *               2002 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */



#include "lineavars.h"
#include "portab.h"
#include "gsxdef.h"
#include "gsxextrn.h"

//#include "vdiconf.h"
#include "asm.h"



/* Prototypes for this module */

void v_clswk();
void v_opnwk();
void d_opnvwk();
void d_clsvwk();
void vex_butv();
void vex_motv();
void vex_curv();
void vex_timv();



/* External declarations */
extern struct attribute *trap();
extern long trap13(int, ...);

#define tickcal() (WORD)trap13(0x06)            /* ms between timer C calls */

#define X_MALLOC 0x48
#define X_MFREE 0x49


extern WORD SIZ_TAB_rom[];
extern WORD DEV_TAB_rom[];
extern WORD INQ_TAB_rom[];


/* Screen related variables */
extern UWORD v_planes;          // count of color planes
extern UWORD v_lin_wr;          // line wrap : bytes per line
extern UWORD v_hz_rez;          // screen horizontal resolution
extern UWORD v_vt_rez;          // screen vertical resolution
extern UWORD v_bytes_lin;       // width of line in bytes


void d_opnvwk()
{
    REG WORD handle;
    REG struct attribute *new_work, *work_ptr, *temp;

    /* Allocate the memory for a virtual workstation.  If none available,
       exit */

    new_work = trap(X_MALLOC, (LONG) (sizeof(struct attribute)));

    if (new_work == NULLPTR) {  /* No work available */
        CONTRL[6] = 0;
        return;
    }

    /* Now find a free handle */

    handle = 1;
    work_ptr = &virt_work;

    while (handle == work_ptr->handle) {
        handle++;
        if (work_ptr->next_work == NULLPTR)
            break;
        work_ptr = work_ptr->next_work;
    }

    /* Empty slot found, Insert the workstation here */

    if (work_ptr->next_work == NULLPTR) {       /* Add at end of chain */
        cur_work = work_ptr->next_work = new_work;
        new_work->next_work = NULLPTR;
    }

    else {                      /* Add in middle of chain */
        temp = work_ptr->next_work;
        cur_work = work_ptr->next_work = new_work;
        new_work->next_work = temp;
    }

    new_work->handle = CONTRL[6] = handle;
    init_wk();
}

void d_clsvwk()
{
    REG struct attribute *work_ptr;
    REG WORD handle;

    /* cur_work points to workstation to deallocate, find who points to me */

    handle = cur_work->handle;

    if (handle == 1)            /* Can't close physical this way */
        return;

    for (work_ptr = &virt_work; handle != work_ptr->next_work->handle;
         work_ptr = work_ptr->next_work);

    work_ptr->next_work = cur_work->next_work;
    trap(X_MFREE, cur_work);
}

/* OPEN_WORKSTATION: */
void v_opnwk()
{
    int i;

    /* We need to copy some initial table data from the ROM */
    for (i = 0; i < 12; i++) {
        SIZ_TAB[i] = SIZ_TAB_rom[i];
    }

    for (i = 0; i < 45; i++) {
        DEV_TAB[i] = DEV_TAB_rom[i];
        INQ_TAB[i] = INQ_TAB_rom[i];
    }

    /* Copy data from linea variables */
    DEV_TAB[0] = v_hz_rez-1;
    DEV_TAB[1] = v_vt_rez-1;
    INQ_TAB[4] = v_planes;

    /* Calculate colors allowed at one time */
    if (INQ_TAB[4] < 8)
        DEV_TAB[13] = 2<<(v_planes-1);
    else
        DEV_TAB[13] = 256;

    cur_work = &virt_work;
    CONTRL[6] = virt_work.handle = 1;
    virt_work.next_work = NULLPTR;

    line_cw = -1;               /* invalidate current line width */

    text_init();                /* initialize the SIZ_TAB info */

    init_wk();

    /* Input must be initialized here and not in init_wk */

    loc_mode = 0;               /* default is request mode  */
    val_mode = 0;               /* default is request mode  */
    chc_mode = 0;               /* default is request mode  */
    str_mode = 0;               /* default is request mode  */


    /* mouse settings */
    HIDE_CNT = 1;               /* mouse is initially hidden */

    GCURX = DEV_TAB[0] / 2;     /* initialize the mouse to center */
    GCURY = DEV_TAB[1] / 2;

    gfx_init();                 /* go into graphics mode */
}



/* CLOSE_WORKSTATION: */
void v_clswk()
{
    struct attribute *next_work;

    if (virt_work.next_work != NULLPTR) {       /* Are there VWs to close */
        cur_work = virt_work.next_work;
        do {
            next_work = cur_work->next_work;
            trap(X_MFREE, cur_work);
        } while ((cur_work = next_work));
    }

    gfx_exit();
}



/*
 * vq_extnd - Extended workstation inquire
 */

void vq_extnd()
{
    REG WORD i;
    REG WORD *dp, *sp;

    dp = CONTRL;
    *(dp + 2) = 6;
    *(dp + 4) = 45;

    FLIP_Y = 1;

    dp = PTSOUT;

    if (*(INTIN) == 0) {
        sp = SIZ_TAB;
        for (i = 0; i < 12; i++)
            *dp++ = *sp++;

        sp = DEV_TAB;
    }

    else {
        *dp++ = XMN_CLIP;       /* PTSOUT[0] */
        *dp++ = YMN_CLIP;       /* PTSOUT[1] */
        *dp++ = XMX_CLIP;       /* PTSOUT[2] */
        *dp++ = YMX_CLIP;       /* PTSOUT[3] */

        for (i = 4; i < 12; i++)
            *dp++ = 0;

        sp = INQ_TAB;
    }

    dp = INTOUT;
    for (i = 0; i < 45; i++)
        *dp++ = *sp++;

}

/*
 * vex_butv
 *
 * This routine replaces the mouse button change vector with
 * the address of a user-supplied routine.  The previous value
 * is returned so that it also may be called when there is a
 * change in the mouse button status.
 *
 * Inputs:
 *    contrl[7], contrl[8] - pointer to user routine
 *
 * Outputs:
 *    contrl[9], contrl[10] - pointer to old routine
 *
 * Registers Modified:     a0
 */

void vex_butv()
{
    LONG * pointer;

    pointer = (LONG*)&CONTRL[9];
    *pointer = (LONG)user_but;
    (LONG*)user_but = *--pointer;
}



/*
 * vex_motv
 *
 * This routine replaces the mouse coordinate change vector with the address
 * of a user-supplied routine.  The previous value is returned so that it
 * also may be called when there is a change in the mouse coordinates.
 *
 *  Inputs:
 *     contrl[7], contrl[8] - pointer to user routine
 *
 *  Outputs:
 *     contrl[9], contrl[10] - pointer to old routine
 *
 *  Registers Modified:     a0
 */

void vex_motv()
{
    LONG * pointer;

    pointer = (LONG*) &CONTRL[9];
    *pointer = (LONG) user_mot;
    (LONG*)user_mot = *--pointer;
}



/*
 * vex_curv
 *
 * This routine replaces the mouse draw vector with the
 * address of a user-supplied routine.  The previous value
 * is returned so that it also may be called when the mouse
 * is to be drawn.
 *
 * Inputs:
 *    contrl[7], contrl[8] - pointer to user routine
 *
 * Outputs:
 *    contrl[9], contrl[10] - pointer to old routine
 *
 * Registers Modified:     a0
 */

void vex_curv()
{
    LONG * pointer;

    pointer = (LONG*) &CONTRL[9];
    *pointer = (LONG) user_cur;
    (LONG*)user_cur = *--pointer;
}



/*
 * vex_timv - exchange timer interrupt vector
 * 
 * entry:          new vector in CONTRL[7-8]
 * exit:           old vector in CONTRL[9-10]
 * destroys:       a0
 */

void vex_timv()
{
    LONG * pointer;

    pointer = (LONG*) &CONTRL[9];

    ints_off();

    *pointer = (LONG) tim_addr;
    (LONG*)tim_addr = *--pointer;

    ints_on();

    INTOUT[0] = tickcal();
}
