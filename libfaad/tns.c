/*
** FAAD - Freeware Advanced Audio Decoder
** Copyright (C) 2002 M. Bakker
**  
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
** 
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
** 
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software 
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
**
** $Id: tns.c,v 1.1 2002/01/14 19:15:57 menno Exp $
**/

#ifdef __ICL
#include <mathf.h>
#else
#include <math.h>
#endif
#include "syntax.h"
#include "tns.h"


/* TNS decoding for one channel and frame */
void tns_decode_frame(ic_stream *ics, tns_info *tns, int sr_index,
                      int object_type, float *spec)
{
    int w, bottom, f, top, start, end, size, inc;
    int tns_order;
    float lpc[TNS_MAX_ORDER+1];

    if (!ics->tns_data_present)
        return;

    for (w = 0; w < ics->num_windows; w++)
    {
        bottom = ics->num_swb;

        for (f = 0; f < tns->n_filt[w]; f++)
        {
            top = bottom;
            bottom = max(top - tns->length[w][f], 0);
            tns_order = min(tns->order[w][f], tns_max_order(ics, sr_index,
                object_type));
            if (!tns_order)
                continue;

            tns_decode_coef(tns_order, tns->coef_res[w]+3,
                tns->coef_compress[w][f], tns->coef[w][f], lpc);

            start = ics->swb_offset[min(bottom,
                min(tns_max_bands(ics, sr_index), ics->max_sfb))];
            end = ics->swb_offset[min(top,
                min(tns_max_bands(ics, sr_index), ics->max_sfb))];

            if ((size = end - start) <= 0)
                continue;

            if (tns->direction[w][f])
            {
                inc = -1;
                start = end - 1;
            } else
                inc = 1;

            tns_ar_filter(&spec[(w*128)+start], size, inc, lpc, tns_order);
        }
    }
}

/* TNS encoding for one channel and frame */
void tns_encode_frame(ic_stream *ics, tns_info *tns, int sr_index,
                      int object_type, float *spec)
{
    int w, bottom, f, top, start, end, size, inc;
    int tns_order;
    float lpc[TNS_MAX_ORDER+1];

    if (!ics->tns_data_present)
        return;

    for (w = 0; w < ics->num_windows; w++)
    {
        bottom = ics->num_swb;

        for (f = 0; f < tns->n_filt[w]; f++)
        {
            top = bottom;
            bottom = max(top - tns->length[w][f], 0);
            tns_order = min(tns->order[w][f], tns_max_order(ics, sr_index,
                object_type));
            if (!tns_order)
                continue;

            tns_decode_coef(tns_order, tns->coef_res[w]+3,
                tns->coef_compress[w][f], tns->coef[w][f], lpc);

            start = ics->swb_offset[min(bottom,
                min(tns_max_bands(ics, sr_index), ics->max_sfb))];
            end = ics->swb_offset[min(top,
                min(tns_max_bands(ics, sr_index), ics->max_sfb))];

            if ((size = end - start) <= 0)
                continue;

            if (tns->direction[w][f])
            {
                inc = -1;
                start = end - 1;
            } else
                inc = 1;

            tns_ma_filter(&spec[(w*128)+start], size, inc, lpc, tns_order);
        }
    }
}

/* Decoder transmitted coefficients for one TNS filter */
static void tns_decode_coef(int order, int coef_res_bits, int coef_compress,
                            int *coef, float *a)
{
    int i, m;
    int coef_res2, s_mask, n_mask;
    int tmp[TNS_MAX_ORDER+1];
    float tmp2[TNS_MAX_ORDER+1], b[TNS_MAX_ORDER+1];
    float iqfac, iqfac_m;

    /* Some internal tables */
    int sgn_mask[] = { 0x2, 0x4, 0x8 };
    int neg_mask[] = { ~0x3, ~0x7, ~0xf };

    /* size used for transmission */
    coef_res2 = coef_res_bits - coef_compress;
    s_mask = sgn_mask[coef_res2 - 2]; /* mask for sign bit */
    n_mask = neg_mask[coef_res2 - 2]; /* mask for padding neg. values */

    /* Conversion to signed integer */
    for (i = 0; i < order; i++)
        tmp[i] = (coef[i] & s_mask) ? (coef[i] | n_mask) : coef[i];

    /* Inverse quantization */
    iqfac = ((1 << (coef_res_bits-1)) - 0.5f) / (M_PI/2.0f);
    iqfac_m = ((1 << (coef_res_bits-1)) + 0.5f) / (M_PI/2.0f);

    for (i = 0; i < order; i++)
#ifdef __ICL
        tmp2[i] = sinf(tmp[i] / ((tmp[i] >= 0) ? iqfac : iqfac_m));
#else
        tmp2[i] = (float)sin(tmp[i] / ((tmp[i] >= 0) ? iqfac : iqfac_m));
#endif

    /* Conversion to LPC coefficients */
    a[0] = 1;
    for (m = 1; m <= order; m++)
    {
        for (i = 1; i < m; i++) /* loop only while i<m */
            b[i] = a[i] + tmp2[m-1] * a[m-i];

        for (i = 1; i < m; i++) /* loop only while i<m */
            a[i] = b[i];

        a[m] = tmp2[m-1]; /* changed */
    }
}

static void tns_ar_filter(float *spectrum, int size, int inc, float *lpc,
                          int order)
{
    /*
     - Simple all-pole filter of order "order" defined by
       y(n) = x(n) - lpc[1]*y(n-1) - ... - lpc[order]*y(n-order)
     - The state variables of the filter are initialized to zero every time
     - The output data is written over the input data ("in-place operation")
     - An input vector of "size" samples is processed and the index increment
       to the next data sample is given by "inc"
    */

    int i, j;
    float y, state[TNS_MAX_ORDER];

    for (i = 0; i < order; i++)
        state[i] = 0;

    for (i = 0; i < size; i++)
    {
        y = *spectrum;

        for (j = 0; j < order; j++)
            y -= lpc[j+1] * state[j];

        for (j = order-1; j > 0; j--)
            state[j] = state[j-1];

        state[0] = y;
        *spectrum = y;
        spectrum += inc;
    }
}

static void tns_ma_filter(float *spectrum, int size, int inc, float *lpc,
                          int order)
{
    /*
     - Simple all-zero filter of order "order" defined by
       y(n) =  x(n) + a(2)*x(n-1) + ... + a(order+1)*x(n-order)
     - The state variables of the filter are initialized to zero every time
     - The output data is written over the input data ("in-place operation")
     - An input vector of "size" samples is processed and the index increment
       to the next data sample is given by "inc"
    */

    int i, j;
    float y, state[TNS_MAX_ORDER];

    for (i = 0; i < order; i++)
        state[i] = 0;

    for (i = 0; i < size; i++)
    {
        y = *spectrum;

        for (j = 0; j < order; j++)
            y += lpc[j+1] * state[j];

        for (j = order-1; j > 0; j--)
            state[j] = state[j-1];

        state[0] = *spectrum;
        *spectrum = y;
        spectrum += inc;
    }
}

static int tns_max_bands_table[12][4] =
{
    /* entry for each sampling rate
     * 1    Main/LC long window
     * 2    Main/LC short window
     * 3    SSR long window
     * 4    SSR short window
     */
    { 31,  9, 28, 7 },       /* 96000 */
    { 31,  9, 28, 7 },       /* 88200 */
    { 34, 10, 27, 7 },       /* 64000 */
    { 40, 14, 26, 6 },       /* 48000 */
    { 42, 14, 26, 6 },       /* 44100 */
    { 51, 14, 26, 6 },       /* 32000 */
    { 46, 14, 29, 7 },       /* 24000 */
    { 46, 14, 29, 7 },       /* 22050 */
    { 42, 14, 23, 8 },       /* 16000 */
    { 42, 14, 23, 8 },       /* 12000 */
    { 42, 14, 23, 8 },       /* 11025 */
    { 39, 14, 19, 7 },       /* 8000  */
};

static int tns_max_bands(ic_stream *ics, int sr_index)
{
    int i;

    i = (ics->window_sequence == EIGHT_SHORT_SEQUENCE) ? 1 : 0;

    return tns_max_bands_table[sr_index][i];
}

static int tns_max_order(ic_stream *ics, int sr_index,
                         int object_type)
{
    /* Correction in 14496-3 Cor. 1
       Works like MPEG2-AAC (13818-7) now

       For other object types (scalable) the following goes for tns max order
       for long windows:
       if (sr_index <= 5)
           return 12;
       else
           return 20;
    */
    if (ics->window_sequence != EIGHT_SHORT_SEQUENCE)
    {
        switch (object_type)
        {
        case MAIN:
        case LTP:
            return 20;
        case LC:
        case SSR:
            return 12;
        }
    } else {
        return 7;
    }

    return 0;
}
