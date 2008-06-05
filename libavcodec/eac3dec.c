/*
 * EAC3 decoder
 * Copyright (c) 2007 Bartlomiej Wolowiec <bartek.wolowiec@gmail.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "avcodec.h"
#include "ac3.h"
#include "ac3_parser.h"
#include "ac3dec.h"

/** Channel gain adaptive quantization mode */
typedef enum {
    EAC3_GAQ_NO =0,
    EAC3_GAQ_12,
    EAC3_GAQ_14,
    EAC3_GAQ_124
} EAC3GaqMode;

#define EAC3_SR_CODE_REDUCED  3

static int idct_cos_tab[6][5];

static int gaq_ungroup_tab[32][3];

#if TEST_SPX
/**
 * Table E3.14 Spectral Extension Attenuation Table
 */
static float spx_attenuation_tab[32][3];
#endif

void ff_eac3_log_missing_feature(AVCodecContext *avctx, const char *log){
    av_log(avctx, AV_LOG_ERROR, "%s is not implemented. If you want to help, "
            "update your FFmpeg version to the newest one from SVN. If the "
            "problem still occurs, it means that your file has extension "
            "which has not been tested due to a lack of samples exhibiting "
            "this feature. Upload a sample of the audio from this file to "
            "ftp://upload.mplayerhq.hu/incoming and contact the ffmpeg-devel "
            "mailing list.\n", log);
}

#if 0
static void spectral_extension(AC3DecodeContext *s){
    //Now turned off, because there are no samples for testing it.
    int copystartmant, copyendmant, copyindex, insertindex;
    int wrapflag[18];
    int bandsize, bnd, bin, spxmant, filtbin, ch;
    float nratio, accum, nscale, sscale, spxcotemp;
    float noffset[AC3_MAX_CHANNELS], nblendfact[AC3_MAX_CHANNELS][18], sblendfact[AC3_MAX_CHANNELS][18];
    float rmsenergy[AC3_MAX_CHANNELS][18];

    //XXX spxbandtable[bnd] = 25 + 12 * bnd ?

    copystartmant = spxbandtable[s->spxstrtf];
    copyendmant = spxbandtable[s->spxbegf];

    for (ch = 1; ch <= s->fbw_channels; ch++) {
        if (!s->chinspx[ch])
            continue;

        copyindex = copystartmant;
        insertindex = copyendmant;

        for (bnd = 0; bnd < s->nspxbnds; bnd++){
            bandsize = s->spxbndsztab[bnd];
            if ((copyindex + bandsize) > copyendmant) {
                copyindex = copystartmant;
                wrapflag[bnd] = 1;
            } else
                wrapflag[bnd] = 0;
            for (bin = 0; bin < bandsize; bin++){
                if (copyindex == copyendmant)
                    copyindex = copystartmant;
                s->transform_coeffs[ch][insertindex++] = s->transform_coeffs[ch][copyindex++];
            }
        }

        noffset[ch] = s->spxblnd[ch] / 32.0;
        spxmant = spxbandtable[s->spxbegf];
        if (s->spxcoe[ch]) {
            for (bnd = 0; bnd < s->nspxbnds; bnd++){
                bandsize = s->spxbndsztab[bnd];
                nratio = ((spxmant + 0.5*bandsize) / spxbandtable[s->spxendf]) - noffset[ch];
                nratio = FFMAX(FFMIN(nratio, 1.0), 0.0);
                nblendfact[ch][bnd] = sqrt(nratio);
                sblendfact[ch][bnd] = sqrt(1 - nratio);
                spxmant += bandsize;
            }
        }

        spxmant = spxbandtable[s->spxbegf];
        for (bnd = 0; bnd < s->nspxbnds; bnd++){
            bandsize = s->spxbndsztab[bnd];
            accum = 0;
            for (bin = 0; bin < bandsize; bin++){
                accum += (s->transform_coeffs[ch][spxmant] * s->transform_coeffs[ch][spxmant]);
                spxmant++;
            }
            rmsenergy[ch][bnd] = sqrt(accum / bandsize);
        }

        if (s->channel_uses_spx[ch]) {
            /* apply notch filter at baseband / extension region border */
            filtbin = spxbandtable[s->spxbegf] - 2;
            for (bin = 0; bin < 3; bin++){
                s->transform_coeffs[ch][filtbin] *= ff_eac3_spxattentab[s->spx_atten_code[ch]][bin];
                filtbin++;
            }
            for (bin = 1; bin >= 0; bin--){
                s->transform_coeffs[ch][filtbin] *= ff_eac3_spxattentab[s->spx_atten_code[ch]][bin];
                filtbin++;
            }
            filtbin += s->spxbndsztab[0];
            /* apply notch at all other wrap points */
            for (bnd = 1; bnd < s->nspxbnds; bnd++){
                if (wrapflag[bnd]) {
                    filtbin -= 5;
                    for (bin = 0; bin < 3; bin++){
                        s->transform_coeffs[ch][filtbin] *= ff_eac3_spxattentab[s->spx_atten_code[ch]][bin];
                        filtbin++;
                    }
                    for (bin = 1; bin >= 0; bin--){
                        s->transform_coeffs[ch][filtbin] *= ff_eac3_spxattentab[s->spx_atten_code[ch]][bin];
                        filtbin++;
                    }
                }
                filtbin += s->spxbndsztab[bnd];
            }
        }

        spxmant = spxbandtable[s->spxbegf];
        for (bnd = 0; bnd < s->nspxbnds; bnd++){
            nscale = rmsenergy[ch][bnd] * nblendfact[ch][bnd];
            sscale = sblendfact[ch][bnd];
            for (bin = 0; bin < s->spxbndsztab[bnd]; bin++){
                //TODO generate noise()
                s->transform_coeffs[ch][spxmant] =
                    s->transform_coeffs[ch][spxmant] * sscale + noise() * nscale;
                spxmant++;
            }
        }

        spxmant = spxbandtable[s->spxbegf];
        for (bnd = 0; bnd < s->nspxbnds; bnd++){
            spxcotemp = s->spxco[ch][bnd];
            for (bin = 0; bin < s->spxbndsztab[bnd]; bin++){
                s->transform_coeffs[ch][spxmant] *= spxcotemp * 32;
                spxmant++;
            }
        }
    }
}
#endif

void ff_eac3_get_transform_coeffs_aht_ch(AC3DecodeContext *s, int ch){
    int bin, blk, gs;
    int end_bap, gaq_mode;
    GetBitContext *gbc = &s->gbc;
    int gaq_gain[AC3_MAX_COEFS];

    gaq_mode = get_bits(gbc, 2);
    end_bap = (gaq_mode < 2) ? 12 : 17;

    /* if GAQ gain is used, decode gain codes for bins with hebap between
       8 and end_bap */
    if (gaq_mode == EAC3_GAQ_12 || gaq_mode == EAC3_GAQ_14) {
        /* read 1-bit GAQ gain codes */
        gs = 0;
        for (bin = s->start_freq[ch]; bin < s->end_freq[ch]; bin++) {
            if (s->bap[ch][bin] > 7 && s->bap[ch][bin] < end_bap)
                gaq_gain[gs++] = get_bits1(gbc) << (gaq_mode-1);
        }
    } else if (gaq_mode == EAC3_GAQ_124) {
        /* read 1.67-bit GAQ gain codes (3 codes in 5 bits) */
        int gc = 2;
        gs = 0;
        for (bin = s->start_freq[ch]; bin < s->end_freq[ch]; bin++) {
            if (s->bap[ch][bin] > 7 && s->bap[ch][bin] < end_bap) {
                if(gc++ == 2) {
                    int group_gain = get_bits(gbc, 5);
                    gaq_gain[gs++] = gaq_ungroup_tab[group_gain][0];
                    gaq_gain[gs++] = gaq_ungroup_tab[group_gain][1];
                    gaq_gain[gs++] = gaq_ungroup_tab[group_gain][2];
                    gc = 0;
                }
            }
        }
    }

    gs=0;
    for (bin = s->start_freq[ch]; bin < s->end_freq[ch]; bin++) {
        int hebap = s->bap[ch][bin];
        int bits = ff_eac3_bits_vs_hebap[hebap];
        if (!hebap) {
            /* hebap=0 */
            for (blk = 0; blk < 6; blk++) {
                s->pre_mantissa[blk][ch][bin] = (av_random(&s->dith_state) & 0x7FFFFF) - 4194304;
            }
        } else if (hebap < 8) {
            /* Vector Quantization */
            int v = get_bits(gbc, bits);
            for (blk = 0; blk < 6; blk++) {
                s->pre_mantissa[blk][ch][bin] = ff_eac3_vq_hebap[hebap][v][blk] << 8;
            }
        } else {
            /* Gain Adaptive Quantization */
            int gbits, log_gain;
            if (gaq_mode != EAC3_GAQ_NO && hebap < end_bap) {
                log_gain = gaq_gain[gs++];
            } else {
                log_gain = 0;
            }
            gbits = bits - log_gain;

            for (blk = 0; blk < 6; blk++) {
                int mant;
                int pre_mantissa = get_sbits(gbc, gbits);
                if (pre_mantissa == -(1 << (gbits-1))) {
                    /* large mantissa */
                    int64_t a, b;
                    mant = get_sbits(gbc, bits-2+log_gain) << (26-log_gain-bits);
                    /* remap mantissa value to correct for asymmetric quantization */
                    a = ff_eac3_gaq_remap_2_4_a[hebap-8][log_gain-1] + 32768;
                    if(mant >= 0)
                        b = 32768 >> log_gain;
                    else
                        b = ff_eac3_gaq_remap_2_4_b[hebap-8][log_gain-1];
                    mant = (a * mant + b) >> 15;
                } else {
                    /* small mantissa, no GAQ, or Gk=1 */
                    mant = pre_mantissa << (24 - bits);
                    if(!log_gain) {
                        /* remap mantissa value for no GAQ or Gk=1 */
                        int64_t a = ff_eac3_gaq_remap_1[hebap-8] + 32768;
                        mant = (a * mant) >> 15;
                    }
                }
                s->pre_mantissa[blk][ch][bin] = mant;
            }
        }
    }
}

void ff_eac3_idct_transform_coeffs_ch(AC3DecodeContext *s, int ch, int blk){
    int bin, i;
    int64_t tmp;
    for (bin = s->start_freq[ch]; bin < s->end_freq[ch]; bin++) {
        tmp = s->pre_mantissa[0][ch][bin];
        for (i = 1; i < 6; i++) {
            tmp += ((int64_t)idct_cos_tab[blk][i-1] * (int64_t)s->pre_mantissa[i][ch][bin]) >> 23;
        }
        s->fixed_coeffs[ch][bin] = tmp >> s->dexps[ch][bin];
    }
}

static int parse_bsi(AC3DecodeContext *s){
    int i, blk;
    GetBitContext *gbc = &s->gbc;

    /* an E-AC3 stream can have multiple independent streams which the
       application can select from. each independent stream can also contain
       dependent streams which are used to add or replace channels. */
    if (s->frame_type == EAC3_FRAME_TYPE_DEPENDENT) {
        ff_eac3_log_missing_feature(s->avctx, "Dependent substream");
        return AC3_PARSE_ERROR_FRAME_TYPE;
    } else if (s->frame_type == EAC3_FRAME_TYPE_RESERVED) {
        av_log(s->avctx, AV_LOG_ERROR, "Reserved frame type\n");
        return AC3_PARSE_ERROR_FRAME_TYPE;
    }

    /* the substream id indicates which substream this frame belongs to. each
       independent stream has its own substream id, and the dependent streams
       associated to an independent stream have matching substream id's */
    if (s->substreamid) {
        // TODO: allow user to select which substream to decode
        av_log(s->avctx, AV_LOG_INFO, "Skipping additional substream #%d\n",
               s->substreamid);
        return AC3_PARSE_ERROR_FRAME_TYPE;
    }

    if (s->bit_alloc_params.sr_code == EAC3_SR_CODE_REDUCED) {
        /* The E-AC3 specification does not tell how to handle reduced sample
           rates in bit allocation.  The best assumption would be that it is
           handled like AC3 DolbyNet, but we cannot be sure until we have a
           sample which utilizes this feature. */
        ff_eac3_log_missing_feature(s->avctx, "Reduced Sampling Rates");
        return -1;
    }
    skip_bits(gbc, 5); // skip bitstream id

    /* dialog normalization and compression gain are volume control params */
    for (i = 0; i < (s->channel_mode ? 1 : 2); i++) {
        skip_bits(gbc, 5); // skip dialog normalization
        if (get_bits1(gbc)) {
            skip_bits(gbc, 8); //skip Compression gain word
        }
    }

#if 0
    /* dependent stream channel map */
    if (s->frame_type == EAC3_FRAME_TYPE_DEPENDENT) {
        if (get_bits1(gbc)) {
            s->channel_map = get_bits(gbc, 16); //custom channel map
        } else {
            //default channel map based on acmod and lfeon
            s->channel_map = ff_eac3_default_chmap[s->channel_mode];
            if(s->lfe_on)
                s->channel_map |= AC3_CHMAP_LFE;
        }
    }
#endif

    /* mixing metadata */
    if (get_bits1(gbc)) {
        /* center and surround mix levels */
        if (s->channel_mode > AC3_CHMODE_STEREO) {
            skip_bits(gbc, 2);  // skip preferred stereo downmix mode
            if (s->channel_mode & 1) {
                /* if three front channels exist */
                skip_bits(gbc, 3); //skip Lt/Rt center mix level
                s->center_mix_level = get_bits(gbc, 3);
            }
            if (s->channel_mode & 4) {
                /* if a surround channel exists */
                skip_bits(gbc, 3); //skip Lt/Rt surround mix level
                s->surround_mix_level = get_bits(gbc, 3);
            }
        }

        /* lfe mix level */
        if (s->lfe_on && get_bits1(gbc)) {
            // TODO: use LFE mix level
            skip_bits(gbc, 5); // skip LFE mix level code
        }

        /* info for mixing with other streams and substreams */
        if (s->frame_type == EAC3_FRAME_TYPE_INDEPENDENT) {
            for (i = 0; i < (s->channel_mode ? 1 : 2); i++) {
                // TODO: apply program scale factor
                if (get_bits1(gbc)) {
                    skip_bits(gbc, 6);  // skip program scale factor
                }
            }
            if (get_bits1(gbc)) {
                skip_bits(gbc, 6);  // skip external program scale factor
            }
            /* skip mixing parameter data */
            switch(get_bits(gbc, 2)) {
                case 1: skip_bits(gbc, 5);  break;
                case 2: skip_bits(gbc, 12); break;
                case 3: {
                    int mix_data_size = (get_bits(gbc, 5) + 2) << 3;
                    skip_bits_long(gbc, mix_data_size);
                    break;
                }
            }
            /* skip pan information for mono or dual mono source */
            if (s->channel_mode < AC3_CHMODE_STEREO) {
                for (i = 0; i < (s->channel_mode ? 1 : 2); i++) {
                    if (get_bits1(gbc)) {
                        /* note: this is not in the ATSC A/52B specification
                           reference: ETSI TS 102 366 V1.1.1
                                      section: E.1.3.1.25 */
                        skip_bits(gbc, 8);  // skip Pan mean direction index
                        skip_bits(gbc, 6);  // skip reserved paninfo bits
                    }
                }
            }
            /* skip mixing configuration information */
            if (get_bits1(gbc)) {
                if (s->num_blocks == 1) {
                    skip_bits(gbc, 5);
                } else {
                    for (blk = 0; blk < s->num_blocks; blk++) {
                        if (get_bits1(gbc)) {
                            skip_bits(gbc, 5);
                        }
                    }
                }
            }
        }
    }

    /* informational metadata */
    if (get_bits1(gbc)) {
        skip_bits(gbc, 3); //skip Bit stream mode
        skip_bits(gbc, 2); //skip copyright bit and original bitstream bit
        if (s->channel_mode == AC3_CHMODE_STEREO) {
            skip_bits(gbc, 4); //skip Dolby surround and headphone mode
        }
        if (s->channel_mode >= AC3_CHMODE_2F2R) {
            skip_bits(gbc, 2); //skip Dolby surround EX mode
        }
        for (i = 0; i < (s->channel_mode ? 1 : 2); i++) {
            if (get_bits1(gbc)) {
                skip_bits(gbc, 8); //skip Mix level, Room type and A/D converter type
            }
        }
        if (s->bit_alloc_params.sr_code != EAC3_SR_CODE_REDUCED) {
            skip_bits1(gbc); //skip Source sample rate code
        }
    }

    /* converter synchronization flag
       if frames are less than six blocks, this bit should be turned on
       once every 6 blocks to indicate the start of a frame set.
       reference: RFC 4598, Section 2.1.3  Frame Sets */
    if (s->frame_type == EAC3_FRAME_TYPE_INDEPENDENT && s->num_blocks != 6) {
        skip_bits1(gbc); //converter synchronization flag
    }

    /* original frame size code if this stream was converted from AC3 */
    if (s->frame_type == EAC3_FRAME_TYPE_AC3_CONVERT &&
            (s->num_blocks == 6 || get_bits1(gbc))) {
        skip_bits(gbc, 6); // skip Frame size code
    }

    /* additional bitstream info */
    if (get_bits1(gbc)) {
        int addbsil = get_bits(gbc, 6);
        for (i = 0; i < addbsil + 1; i++) {
            skip_bits(gbc, 8); // skip additional bit stream information
        }
    }

    return 0;
}

/** Audio frame syntax flags, strategy data, and per-frame data */
static int parse_audfrm(AC3DecodeContext *s){
    int blk, ch;
    int ac3_exponent_strategy, parse_aht_info, parse_spx_atten_data;
    int parse_transient_proc_info;
    int num_cpl_blocks;
    GetBitContext *gbc = &s->gbc;

    if (s->num_blocks == 6) {
        /* LUT-based exponent strategy syntax */
        ac3_exponent_strategy = get_bits1(gbc);
        parse_aht_info = get_bits1(gbc);
    } else {
        /* AC-3 style exponent strategy syntax */
        ac3_exponent_strategy = 1;
        parse_aht_info = 0;
    }
    s->snr_offset_strategy = get_bits(gbc, 2);
    parse_transient_proc_info = get_bits1(gbc);
    s->block_switch_syntax = get_bits1(gbc);
    if (!s->block_switch_syntax) {
        for (ch = 1; ch <= s->fbw_channels; ch++)
            s->block_switch[ch] = 0;
    }
    s->dither_flag_syntax = get_bits1(gbc);
    if (!s->dither_flag_syntax) {
        s->dither_all = 1;
        for (ch = 1; ch <= s->fbw_channels; ch++)
            s->dither_flag[ch] = 1; /* dither on */
    }
    s->dither_flag[CPL_CH] = s->dither_flag[s->lfe_ch] = 0;

    /* frame-based syntax flags */
    s->bit_allocation_syntax = get_bits1(gbc);
    if (!s->bit_allocation_syntax) {
        /* set default bit allocation parameters */
        s->bit_alloc_params.slow_decay = ff_ac3_slow_decay_tab[2];  /* Table 7.6 */
        s->bit_alloc_params.fast_decay = ff_ac3_fast_decay_tab[1];  /* Table 7.7 */
        s->bit_alloc_params.slow_gain  = ff_ac3_slow_gain_tab [1];  /* Table 7.8 */
        s->bit_alloc_params.db_per_bit = ff_ac3_db_per_bit_tab[2];  /* Table 7.9 */
        s->bit_alloc_params.floor      = ff_ac3_floor_tab     [7];  /* Table 7.10 */
    }
    s->fast_gain_syntax = get_bits1(gbc);
    s->dba_syntax = get_bits1(gbc);
    s->skip_syntax = get_bits1(gbc);
    parse_spx_atten_data = get_bits1(gbc);

    /* coupling strategy occurance and coupling use per block */
    num_cpl_blocks = 0;
    if (s->channel_mode > 1) {
        for (blk = 0; blk < s->num_blocks; blk++) {
            s->cpl_strategy_exists[blk] = (!blk || get_bits1(gbc));

            if (s->cpl_strategy_exists[blk]) {
                s->cpl_in_use[blk] = get_bits1(gbc);
            } else {
                s->cpl_in_use[blk] = s->cpl_in_use[blk-1];
            }
            num_cpl_blocks += s->cpl_in_use[blk];
        }
    } else {
        memset(s->cpl_in_use, 0, sizeof(s->cpl_in_use));
    }

    /* Exponent strategy data */
    if (ac3_exponent_strategy) {
        /* AC-3 style exponent strategy syntax */
        for (blk = 0; blk < s->num_blocks; blk++) {
            for (ch = !s->cpl_in_use[blk]; ch <= s->fbw_channels; ch++) {
                s->exp_strategy[blk][ch] = get_bits(gbc, 2);
            }
        }
    } else {
        /* LUT-based exponent strategy syntax */
        int frmchexpstr;
        for (ch = !((s->channel_mode > 1) && num_cpl_blocks); ch <= s->fbw_channels; ch++) {
            frmchexpstr = get_bits(gbc, 5);
            for (blk = 0; blk < 6; blk++) {
                s->exp_strategy[blk][ch] = ff_eac3_frm_expstr[frmchexpstr][blk];
            }
        }
    }
    /* LFE exponent strategy */
    if (s->lfe_on) {
        for (blk = 0; blk < s->num_blocks; blk++) {
            s->exp_strategy[blk][s->lfe_ch] = get_bits1(gbc);
        }
    }
    /* original exponent strategies if this stream was converted from AC3 */
    if (s->frame_type == EAC3_FRAME_TYPE_INDEPENDENT &&
            (s->num_blocks == 6 || get_bits1(gbc))) {
        for (ch = 1; ch <= s->fbw_channels; ch++) {
            skip_bits(gbc, 5); //skip Converter channel exponent strategy
        }
    }

    /* determine which channels use AHT */
    if (parse_aht_info) {
        /* AHT is only available in 6 block mode (numblkscod ==3) */
        /* coupling can use AHT only when coupling in use for all blocks */
        /* ncplregs derived from cplstre and cplexpstr - see Section E3.3.2 */
        int nchregs;
        s->channel_uses_aht[CPL_CH]=0;
        for (ch = (num_cpl_blocks != 6); ch <= s->channels; ch++) {
            nchregs = 0;
            for (blk = 0; blk < 6; blk++) {
                if (ch)
                    nchregs += (s->exp_strategy[blk][ch] != EXP_REUSE);
                else
                    nchregs += s->cpl_strategy_exists[blk] ||
                               (s->exp_strategy[blk][CPL_CH] != EXP_REUSE);
            }
            s->channel_uses_aht[ch] = (nchregs == 1) && get_bits1(gbc);
        }
    } else {
        memset(s->channel_uses_aht, 0, sizeof(s->channel_uses_aht));
    }

    /* per-frame SNR offset */
    if (!s->snr_offset_strategy) {
        int csnroffst = (get_bits(gbc, 6) - 15) << 4;
        int snroffst = (csnroffst + get_bits(gbc, 4)) << 2;
        for (ch = 0; ch <= s->channels; ch++)
            s->snr_offset[ch] = snroffst;
    }

    /* transient pre-noise processing data */
    if (parse_transient_proc_info) {
        for (ch = 1; ch <= s->fbw_channels; ch++) {
            if (get_bits1(gbc)) { // channel in transient processing
                skip_bits(gbc, 10); // skip transient processing location
                skip_bits(gbc, 8);  // skip transient processing length
            }
        }
    }

#if TEST_SPX
    /* Spectral extension attenuation data */
    if (parse_spx_atten_data) {
        for (ch = 1; ch <= s->fbw_channels; ch++) {
            s->channel_uses_spx[ch] = get_bits1(gbc);
            if (s->channel_uses_spx[ch]) {
                s->spx_atten_code[ch] = get_bits(gbc, 5);
            }
        }
    } else {
        for (ch = 1; ch <= s->fbw_channels; ch++)
            s->channel_uses_spx[ch]=0;
    }
#endif

    /* Block start information */
    if (s->num_blocks > 1 && get_bits1(gbc)) {
        /* reference: Section E2.3.2.27
           nblkstrtbits = (numblks - 1) * (4 + ceiling(log2(words_per_frame)))
           Great that the spec tells how to parse. Unfortunately it doesn't say
           what this data is or what it's used for. */
        int block_start_bits = (s->num_blocks-1) * (4 + av_log2(s->frame_size-2));
        skip_bits(gbc, block_start_bits);
    }

    /* Syntax state initialization */
    for (ch = 1; ch <= s->fbw_channels; ch++) {
#if TEST_SPX
        s->firstspxcos[ch] = 1;
#endif
        s->first_cpl_coords[ch] = 1;
    }
    s->first_cpl_leak = 1;

    return 0;
}

int ff_eac3_parse_header(AC3DecodeContext *s)
{
    int err = parse_bsi(s);
    if(!err)
        err = parse_audfrm(s);
    return err;
}

#if 0
int ff_eac3_parse_audio_block(AC3DecodeContext *s, const int blk){
    //int grp, sbnd, n, bin;
    int seg, bnd, ch, i, chbwcod, grpsize;
    int got_cplchan;
    int ecpl_in_use=0;
    GetBitContext *gbc = &s->gbc;
    uint8_t bit_alloc_stages[AC3_MAX_CHANNELS];

    memset(bit_alloc_stages, 0, AC3_MAX_CHANNELS);

    /* Block switch and dither flags */
    if (!s->eac3 || s->block_switch_syntax) {
        for (ch = 1; ch <= s->fbw_channels; ch++) {
            s->block_switch[ch] = get_bits1(gbc);
        }
    }
    if (!s->eac3 || s->dither_flag_syntax) {
        s->dither_all = 1;
        for (ch = 1; ch <= s->fbw_channels; ch++) {
            s->dither_flag[ch] = get_bits1(gbc);
            if(!s->dither_flag[ch])
                s->dither_all = 0;
        }
    }

    /* Dynamic range control */
    for (i = 0; i < (s->channel_mode ? 1 : 2); i++) {
        if (get_bits1(gbc)) {
            s->dynamic_range[i] = ff_ac3_dynamic_range_tab[get_bits(gbc, 8)];
        } else if (!blk) {
            s->dynamic_range[i] = 1.0f;
        }
    }
    /* Spectral extension strategy information */
    if (s->eac3 && (!blk || get_bits1(gbc))) {
        s->spx_in_use = get_bits1(gbc);
        if (s->spx_in_use) {
#if TEST_SPX
            if (s->channel_mode == AC3_CHMODE_MONO) {
                s->chinspx[1] = 1;
            } else {
                for (ch = 1; ch <= s->fbw_channels; ch++) {
                    s->chinspx[ch] = get_bits1(gbc);
                }
            }
#if 0
            {
                int nspx=0;
                for (ch = 1; ch <= s->fbw_channels; ch++) {
                    nspx+=s->chinspx[ch];
                }
                if (!nspx)
                    av_log(s->avctx, AV_LOG_INFO, "No channels in spectral extension\n");
            }
#endif
            s->spxstrtf = get_bits(gbc, 2);
            s->spxbegf = get_bits(gbc, 3);
            s->spxendf = get_bits(gbc, 3);
            if (s->spxbegf < 6) {
                s->spxbegf += 2;
            } else {
                s->spxbegf = s->spxbegf * 2 - 3;
            }
            if (s->spxendf < 3) {
                s->spxendf += 5;
            } else {
                s->spxendf = s->spxendf * 2 + 3;
            }
            for (ch = 1; ch <= s->fbw_channels; ch++) {
                if (s->chinspx[ch])
                    s->end_freq[ch] = 25 + 12 * s->spxbegf;
            }
            if (get_bits1(gbc)) {
                for (bnd = s->spxbegf + 1; bnd < s->spxendf; bnd++) {
                    s->spxbndstrc[bnd] = get_bits1(gbc);
                }
            } else if (!blk) {
                for (bnd = 0; bnd < 17; bnd++)
                    s->spxbndstrc[bnd] = ff_eac3_defspxbndstrc[bnd];
            }
            // calculate number of spectral extension bands
            s->nspxbnds = 1;
            s->spxbndsztab[0] = 12;
            for (bnd = s->spxbegf+1; bnd < s->spxendf; bnd ++){
                if (!s->spxbndstrc[bnd]) {
                    s->spxbndsztab[s->nspxbnds] = 12;
                    s->nspxbnds++;
                } else {
                    s->spxbndsztab[s->nspxbnds - 1] += 12;
                }
            }
#else
            ff_eac3_log_missing_feature(s->avctx, "Spectral extension");
            return -1;
#endif
#if TEST_SPX
        } else {
            /* !spx_in_use */
            for (ch = 1; ch <= s->fbw_channels; ch++) {
                s->chinspx[ch] = 0;
                s->firstspxcos[ch] = 1;
            }
#endif
        }
    }

#if TEST_SPX
    /* Spectral extension coordinates */
    if (s->spx_in_use) {
        for (ch = 1; ch <= s->fbw_channels; ch++) {
            if (s->chinspx[ch]) {
                if (s->firstspxcos[ch]) {
                    s->spxcoe[ch] = 1;
                    s->firstspxcos[ch] = 0;
                } else {
                    /* !firstspxcos[ch] */
                    s->spxcoe[ch] = get_bits1(gbc);
                }
                if (!blk && !s->spxcoe[ch]) {
                    av_log(s->avctx, AV_LOG_ERROR, "no spectral extension coordinates in first block\n");
                    return -1;
                }

                if (s->spxcoe[ch]) {
                    int spxcoexp, spxcomant, mstrspxco;
                    s->spxblnd[ch] = get_bits(gbc, 5);
                    mstrspxco = get_bits(gbc, 2);
                    mstrspxco*=3;
                    /* nspxbnds determined from spxbegf, spxendf, and spxbndstrc[ ] */
                    for (bnd = 0; bnd < s->nspxbnds; bnd++) {
                        spxcoexp = get_bits(gbc, 4);
                        spxcomant = get_bits(gbc, 2);
                        if (spxcoexp == 15)
                            s->spxco[ch][bnd] = spxcomant / 4.0f;
                        else
                            s->spxco[ch][bnd] = (spxcomant+4) / 8.0f;
                        s->spxco[ch][bnd] *= ff_ac3_scale_factors[spxcoexp + mstrspxco];
                    }
                }
            } else {
                /* !chinspx[ch] */
                s->firstspxcos[ch] = 1;
            }
        }
    }
#endif

    /* Coupling strategy and enhanced coupling strategy information */
    if ((s->eac3 && s->cpl_strategy_exists[blk]) || (!s->eac3 && get_bits1(gbc))) {
        memset(bit_alloc_stages, 3, AC3_MAX_CHANNELS);
        if (!s->eac3)
            s->cpl_in_use[blk] = get_bits1(gbc);
        if (s->cpl_in_use[blk]) {
            if (s->eac3)
                ecpl_in_use = get_bits1(gbc);
            else
                ecpl_in_use = 0;
            if (s->eac3 && s->channel_mode == AC3_CHMODE_STEREO) {
                s->channel_in_cpl[1] = 1;
                s->channel_in_cpl[2] = 1;
            } else {
                for (ch = 1; ch <= s->fbw_channels; ch++) {
                    s->channel_in_cpl[ch] = get_bits1(gbc);
                }
            }
            if (!ecpl_in_use) {
                /* standard coupling in use */
                int cpl_begin, cpl_end;

                /* determine if phase flags are used */
                if (s->channel_mode == AC3_CHMODE_STEREO) {
                    s->phase_flags_in_use = get_bits1(gbc);
                }

                /* get start and end subbands for coupling */
                cpl_begin = get_bits(gbc, 4);
#if TEST_SPX
                if (!s->eac3 || !s->spx_in_use) {
                    cpl_end = get_bits(gbc, 4) + 3;
                } else {
                    cpl_end = s->spxbegf - 1;
                }
#else
                cpl_end = get_bits(gbc, 4) + 3;
#endif
                s->num_cpl_subbands =  cpl_end - cpl_begin;

                /* calculate start and end frequency bins for coupling */
                s->start_freq[CPL_CH] = 37 + (12 * cpl_begin);
                s->end_freq[CPL_CH] = 37 + (12 * cpl_end);
                if (s->start_freq[CPL_CH] > s->end_freq[CPL_CH]) {
                    av_log(s->avctx, AV_LOG_ERROR, "cplstrtmant > cplendmant [blk=%i]\n", blk);
                    return -1;
                }
                for (ch = 1; ch <= s->fbw_channels; ch++) {
                    if (s->channel_in_cpl[ch])
                        s->end_freq[ch] = s->start_freq[CPL_CH];
                }

                /* read coupling band structure or use default */
                if (!s->eac3 || get_bits1(gbc)) {
                    for (bnd = 0; bnd < s->num_cpl_subbands-1; bnd++) {
                        s->cpl_band_struct[bnd] = get_bits1(gbc);
                    }
                } else if (!blk) {
                    for (bnd = 0; bnd < s->num_cpl_subbands-1; bnd++)
                        s->cpl_band_struct[bnd] = ff_eac3_default_cpl_band_struct[bnd+cpl_begin+1];
                }
                s->cpl_band_struct[s->num_cpl_subbands-1] = 0;

                /* calculate number of coupling bands based on band structure */
                s->num_cpl_bands = s->num_cpl_subbands;
                for (bnd = 0; bnd < s->num_cpl_subbands-1; bnd++) {
                    s->num_cpl_bands -= s->cpl_band_struct[bnd];
                }
            } else {
                /* enhanced coupling in use */
#if TEST_ECPL
                int sbnd;
                s->ecplbegf = get_bits(gbc, 4);
                if (s->ecplbegf < 3) {
                    s->ecpl_start_subbnd = s->ecplbegf * 2;
                } else {
                    if (s->ecplbegf < 13) {
                        s->ecpl_start_subbnd = s->ecplbegf + 2;
                    } else {
                        s->ecpl_start_subbnd = s->ecplbegf * 2 - 10;
                    }
                }
                if (!s->spx_in_use) {
                    /* if SPX not in use */
                    s->ecplendf = get_bits(gbc, 4);
                    s->ecpl_end_subbnd = s->ecplendf + 7;
                } else {
                    /* SPX in use */
                    if (s->spxbegf < 6) {
                        s->ecpl_end_subbnd = s->spxbegf + 5;
                    } else {
                        s->ecpl_end_subbnd = s->spxbegf * 2;
                    }
                }
                if (get_bits1(gbc)) {
                    for (sbnd = FFMAX(9, s->ecpl_start_subbnd + 1);
                            sbnd < s->ecpl_end_subbnd; sbnd++){
                        s->ecplbndstrc[sbnd] = get_bits1(gbc);
                    }
                } else if (!blk) {
                    for (sbnd = 0; sbnd < 22; sbnd++)
                        s->ecplbndstrc[sbnd] = ff_eac3_defecplbndstrc[sbnd];
                }
                //necplbnd = ecpl_end_subbnd - ecpl_start_subbnd;
                //necplbnd -= ecplbndstrc[ecpl_start_subbnd] + ... + ecplbndstrc[ecpl_end_subbnd -1]
                s->necplbnd = s->ecpl_end_subbnd - s->ecpl_start_subbnd;
                for (bnd = s->ecpl_start_subbnd; bnd < s->ecpl_end_subbnd; bnd++) {
                    s->necplbnd -= s->ecplbndstrc[bnd];
                }
#else
                ff_eac3_log_missing_feature(s->avctx, "Enhanced coupling");
                return -1;
#endif
            }
        } else {
            /* coupling not used for this block */
            for (ch = 1; ch <= s->fbw_channels; ch++) {
                s->channel_in_cpl[ch] = 0;
                s->first_cpl_coords[ch] = 1;
            }
            s->first_cpl_leak = 1;
            s->phase_flags_in_use = 0;
            ecpl_in_use = 0;
        }
    } else if (!s->eac3 && blk) {
        /* for AC3, copy coupling use strategy from last block */
        s->cpl_in_use[blk] = s->cpl_in_use[blk-1];
    }
    /* Coupling coordinates */
    if (s->cpl_in_use[blk]) {
        if (!ecpl_in_use) {
            /* standard coupling in use */
            int cpl_coords_exist = 0;
            for (ch = 1; ch <= s->fbw_channels; ch++) {
                if (s->channel_in_cpl[ch]) {
                    int cpl_coords_ch = 0;

                    /* determine if coupling coordinates are new or reused */
                    if (s->eac3 && s->first_cpl_coords[ch]) {
                        cpl_coords_ch = 1;
                        s->first_cpl_coords[ch] = 0;
                    } else {
                        cpl_coords_ch = get_bits1(gbc);
                    }
                    cpl_coords_exist |= cpl_coords_ch;

                    if (cpl_coords_ch) {
                        /* read coupling coordinates from bitstream */
                        int cpl_exp, cpl_mant, cpl_master;
                        cpl_master = 3 * get_bits(gbc, 2);
                        for (bnd = 0; bnd < s->num_cpl_bands; bnd++) {
                            cpl_exp = get_bits(gbc, 4);
                            cpl_mant = get_bits(gbc, 4);
                            if (cpl_exp == 15)
                                s->cpl_coords[ch][bnd] = cpl_mant / 16.0f;
                            else
                                s->cpl_coords[ch][bnd] = (cpl_mant + 16.0f) / 32.0f;
                            s->cpl_coords[ch][bnd] *= ff_ac3_scale_factors[cpl_exp + cpl_master];
                        }
                    } else if (!blk) {
                        av_log(s->avctx, AV_LOG_ERROR,  "no coupling coordinates in first block\n");
                        return -1;
                    }
                } else {
                    /* channel not in coupling */
                    s->first_cpl_coords[ch] = 1;
                }
            }
            if (s->channel_mode == AC3_CHMODE_STEREO && cpl_coords_exist) {
                for (bnd = 0; bnd < s->num_cpl_bands; bnd++) {
                    s->phase_flags[bnd] = s->phase_flags_in_use? get_bits1(gbc) : 0;
                }
            }
        } else {
            /* enhanced coupling in use */
            //TODO calc nchgrps[CPL_CH]
#if TEST_ECPL
            s->firstchincpl = -1;
            s->ecplangleintrp = get_bits1(gbc);
            for (ch = 1; ch <= s->fbw_channels; ch++) {
                if (s->channel_in_cpl[ch]) {
                    if (s->firstchincpl == -1) {
                        s->firstchincpl = ch;
                    }
                    if (s->first_cpl_coords[ch]) {
                        s->ecplparam1e[ch] = 1;
                        if (ch > s->firstchincpl) {
                            s->ecplparam2e[ch] = 1;
                        } else {
                            s->ecplparam2e[ch] = 0;
                        }
                        s->first_cpl_coords[ch] = 0;
                    } else {
                        s->ecplparam1e[ch] = get_bits1(gbc);
                        if (ch > s->firstchincpl) {
                            s->ecplparam2e[ch] = get_bits1(gbc);
                        } else {
                            s->ecplparam2e[ch] = 0;
                        }
                    }
                    if (s->ecplparam1e[ch]) {
                        /* necplbnd derived from ecpl_start_subbnd, ecpl_end_subbnd, and ecplbndstrc */
                        for (bnd = 0; bnd < s->necplbnd; bnd++) {
                            s->ecplamp[ch][bnd] = get_bits(gbc, 5);
                        }
                    }
                    if (s->ecplparam2e[ch]) {
                        /* necplbnd derived from ecpl_start_subbnd, ecpl_end_subbnd, and ecplbndstrc */
                        for (bnd = 0; bnd < s->necplbnd; bnd++) {
                            s->ecplangle[ch][bnd] = get_bits(gbc, 6);
                            s->ecplchaos[ch][bnd] = get_bits(gbc, 3);
                        }
                    }
                    if (ch > s->firstchincpl) {
                        s->ecpltrans[ch] = get_bits1(gbc);
                    }
                } else {
                    /* !chincpl[ch] */
                    s->first_cpl_coords[ch] = 1;
                }
            } /* ch */
#endif
        }
    }
    /* Rematrixing operation in the 2/0 mode */
    if (s->channel_mode == AC3_CHMODE_STEREO && ((s->eac3 && !blk) || get_bits1(gbc))) {
        /* nrematbnds determined from cplinu, ecplinu, spx_in_use, cplbegf, ecplbegf and spxbegf */
        // TODO spx in one channel
        int end = (s->cpl_in_use[blk] || (s->eac3 && s->spx_in_use)) ?
            FFMIN(s->end_freq[1], s->end_freq[2]) : (ff_ac3_rematrix_band_tab[4]-1);
        for (bnd = 0; ff_ac3_rematrix_band_tab[bnd] <= end; bnd++) {
            s->rematrixing_flags[bnd] = get_bits1(gbc);
        }
        s->num_rematrixing_bands = bnd;
    }

    /* AC-3 exponent strategy */
    if(!s->eac3) {
        s->exp_strategy[blk][CPL_CH] = EXP_REUSE;
        s->exp_strategy[blk][s->lfe_ch] = EXP_REUSE;
        for (ch = !s->cpl_in_use[blk]; ch <= s->channels; ch++) {
            if(ch == s->lfe_ch)
                s->exp_strategy[blk][ch] = get_bits(gbc, 1);
            else
                s->exp_strategy[blk][ch] = get_bits(gbc, 2);
        }
    }
    s->num_exp_groups[CPL_CH] = (s->end_freq[CPL_CH] - s->start_freq[CPL_CH]) /
                         (3 << (s->exp_strategy[blk][CPL_CH] - 1));

    /* check exponent strategies to set bit allocation stages */
    for (ch = !s->cpl_in_use[blk]; ch <= s->channels; ch++) {
        if(s->exp_strategy[blk][ch] != EXP_REUSE)
            bit_alloc_stages[ch] = 3;
    }

    /* Channel bandwidth code */
    for (ch = 1; ch <= s->fbw_channels; ch++) {
        if (!blk && s->exp_strategy[blk][ch] == EXP_REUSE) {
            av_log(s->avctx, AV_LOG_ERROR,  "no channel exponent strategy in first block\n");
            return -1;
        }
        if (s->exp_strategy[blk][ch] != EXP_REUSE) {
            s->start_freq[ch] = 0;
#if TEST_SPX
            if ((!s->channel_in_cpl[ch]) && (!s->eac3 || !s->chinspx[ch])) {
#else
            if (!s->channel_in_cpl[ch]) {
#endif
                int prev = s->end_freq[ch];
                chbwcod = get_bits(gbc, 6);
                if (chbwcod > 60) {
                    av_log(s->avctx, AV_LOG_ERROR, "chbwcod > 60\n");
                    return -1;
                }
                s->end_freq[ch] = ((chbwcod + 12) * 3) + 37; /* (ch is not coupled) */
                /* if bandwidth changes, run full bit allocation */
                if(blk && s->end_freq[ch] != prev) {
                    bit_alloc_stages[ch] = 3;
                }
            }
            grpsize = 3 << (s->exp_strategy[blk][ch] - 1);
            s->num_exp_groups[ch] = (s->end_freq[ch] + grpsize - 4) / grpsize;
        }
    }
    /* Exponents */
    for (ch = !s->cpl_in_use[blk]; ch <= s->channels; ch++) {
        if (s->exp_strategy[blk][ch] != EXP_REUSE) {
            s->dexps[ch][0] = get_bits(gbc, 4) << !ch;
            ff_ac3_decode_exponents(gbc, s->exp_strategy[blk][ch], s->num_exp_groups[ch],
                    s->dexps[ch][0], s->dexps[ch]+s->start_freq[ch]+!!ch);
            if (ch != CPL_CH && ch != s->lfe_ch)
                skip_bits(gbc, 2); /* skip gainrng */
        }
    }

    /* Bit-allocation parametric information */
    if (!s->eac3 || s->bit_allocation_syntax) {
        if (get_bits1(gbc)) {
            s->bit_alloc_params.slow_decay = ff_ac3_slow_decay_tab[get_bits(gbc, 2)];   /* Table 7.6 */
            s->bit_alloc_params.fast_decay = ff_ac3_fast_decay_tab[get_bits(gbc, 2)];   /* Table 7.7 */
            s->bit_alloc_params.slow_gain  = ff_ac3_slow_gain_tab [get_bits(gbc, 2)];   /* Table 7.8 */
            s->bit_alloc_params.db_per_bit = ff_ac3_db_per_bit_tab[get_bits(gbc, 2)];   /* Table 7.9 */
            s->bit_alloc_params.floor      = ff_ac3_floor_tab     [get_bits(gbc, 3)];   /* Table 7.10 */
            /* run at least last 2 stages of bit allocation */
            for(ch=!s->cpl_in_use[blk]; ch<=s->channels; ch++) {
                bit_alloc_stages[ch] = FFMAX(bit_alloc_stages[ch], 2);
            }
        } else if (!blk) {
            av_log(s->avctx, AV_LOG_ERROR, "no bit allocation information in first block\n");
            return -1;
        }
    }

    if ((!s->eac3 || (s->snr_offset_strategy && !blk)) && get_bits1(gbc)) {
        int csnroffst = (get_bits(gbc, 6) - 15) << 4;
        int snroffst = 0;
        for (ch = !s->cpl_in_use[blk]; ch <= s->channels; ch++){
            if (!s->eac3 || ch == !s->cpl_in_use[blk] || s->snr_offset_strategy == 2)
                snroffst = (csnroffst + get_bits(gbc, 4)) << 2;
            /* run at least last bit allocation stage if snr offset changes */
            if(blk && s->snr_offset[ch] != snroffst) {
                bit_alloc_stages[ch] = FFMAX(bit_alloc_stages[ch], 1);
            }
            s->snr_offset[ch] = snroffst;
            if(!s->eac3) {
                int prev = s->fast_gain[ch];
                s->fast_gain[ch] = ff_ac3_fast_gain_tab[get_bits(gbc, 3)];
                /* run last 2 bit allocation stages if fast gain changes */
                if(blk && prev != s->fast_gain[ch])
                    bit_alloc_stages[ch] = 2;
            }
        }
    }

    if(s->eac3) {
        if (s->fast_gain_syntax && get_bits1(gbc)) {
            for (ch = !s->cpl_in_use[blk]; ch <= s->channels; ch++) {
                int prev = s->fast_gain[ch];
                s->fast_gain[ch] = ff_ac3_fast_gain_tab[get_bits(gbc, 3)];
                /* run last 2 bit allocation stages if fast gain changes */
                if(blk && prev != s->fast_gain[ch])
                    bit_alloc_stages[ch] = 2;
            }
        } else if (!blk) {
            for (ch = !s->cpl_in_use[blk]; ch <= s->channels; ch++)
                s->fast_gain[ch] = ff_ac3_fast_gain_tab[4];
        }
        if (s->frame_type == EAC3_FRAME_TYPE_INDEPENDENT && get_bits1(gbc)) {
            skip_bits(gbc, 10); //Converter SNR offset
        }
    }
    if (s->cpl_in_use[blk]) {
        if ((s->eac3 && s->first_cpl_leak) || get_bits1(gbc)) {
            int prev_fl = s->bit_alloc_params.cpl_fast_leak;
            int prev_sl = s->bit_alloc_params.cpl_slow_leak;
            s->bit_alloc_params.cpl_fast_leak = get_bits(gbc, 3);
            s->bit_alloc_params.cpl_slow_leak = get_bits(gbc, 3);
            /* run last 2 bit allocation stages for coupling channel if
               coupling leak changes */
            if(blk && (prev_fl != s->bit_alloc_params.cpl_fast_leak ||
                    prev_sl != s->bit_alloc_params.cpl_slow_leak)) {
                bit_alloc_stages[CPL_CH] = FFMAX(bit_alloc_stages[CPL_CH], 2);
            }
        }
        if(s->first_cpl_leak)
            s->first_cpl_leak = 0;
    }
    /* Delta bit allocation information */
    if ((!s->eac3 || s->dba_syntax) && get_bits1(gbc)) {
        for (ch = !s->cpl_in_use[blk]; ch <= s->fbw_channels; ch++) {
            s->dba_mode[ch] = get_bits(gbc, 2);
        }
        for (ch = !s->cpl_in_use[blk]; ch <= s->fbw_channels; ch++) {
            if (s->dba_mode[ch] == DBA_NEW) {
                s->dba_nsegs[ch] = get_bits(gbc, 3);
                for (seg = 0; seg <= s->dba_nsegs[ch]; seg++) {
                    s->dba_offsets[ch][seg] = get_bits(gbc, 5);
                    s->dba_lengths[ch][seg] = get_bits(gbc, 4);
                    s->dba_values[ch][seg] = get_bits(gbc, 3);
                }
                /* run last 2 bit allocation stages if dba values change */
                bit_alloc_stages[ch] = FFMAX(bit_alloc_stages[ch], 2);
            }
        }
    } else if (!blk) {
        for (ch = 0; ch <= s->channels; ch++) {
            s->dba_mode[ch] = DBA_NONE;
        }
    }

    /* Inclusion of unused dummy data */
    if ((!s->eac3 || s->skip_syntax) && get_bits1(gbc)) {
        int skipl = get_bits(gbc, 9);
        while(skipl--) skip_bits(gbc, 8);
    }

    /* run bit allocation */
    for (ch = !s->cpl_in_use[blk]; ch <= s->channels; ch++) {
        if(bit_alloc_stages[ch] > 2) {
            ff_ac3_bit_alloc_calc_psd((int8_t *)s->dexps[ch], s->start_freq[ch],
                    s->end_freq[ch], s->psd[ch], s->band_psd[ch]);
        }

        if(bit_alloc_stages[ch] > 1) {
            ff_ac3_bit_alloc_calc_mask(&s->bit_alloc_params, s->band_psd[ch],
                    s->start_freq[ch], s->end_freq[ch], s->fast_gain[ch],
                    (ch == s->lfe_ch), s->dba_mode[ch], s->dba_nsegs[ch],
                    s->dba_offsets[ch], s->dba_lengths[ch], s->dba_values[ch],
                    s->mask[ch]);
        }

        if(bit_alloc_stages[ch] > 0) {
            if (!s->eac3 || s->channel_uses_aht[ch] == 0)
                ff_ac3_bit_alloc_calc_bap(s->mask[ch], s->psd[ch],
                        s->start_freq[ch], s->end_freq[ch], s->snr_offset[ch],
                        s->bit_alloc_params.floor, ff_ac3_bap_tab, s->bap[ch]);
            else if (s->channel_uses_aht[ch] == 1)
                ff_ac3_bit_alloc_calc_bap(s->mask[ch], s->psd[ch],
                        s->start_freq[ch], s->end_freq[ch], s->snr_offset[ch],
                        s->bit_alloc_params.floor, ff_eac3_hebap_tab,
                        s->hebap[ch]);
        }
    }

    got_cplchan = 0;

    /* Quantized mantissa values */
    if(ff_ac3_get_transform_coeffs(s, blk)) {
        av_log(s->avctx, AV_LOG_ERROR, "error decoding transform coefficients\n");
        return -1;
    }

#if 0
    //apply spectral extension
    if (s->spx_in_use)
        spectral_extension(s);
#endif

    return 0;
}
#endif

void ff_eac3_tables_init(void) {
    int blk, i;

    // initialize IDCT cosine table for use with AHT
    for(blk=0; blk<6; blk++) {
        for(i=1; i<6; i++) {
            idct_cos_tab[blk][i-1] = (M_SQRT2 * cos(M_PI*i*(2*blk + 1)/12) * 8388608.0) + 0.5;
        }
    }

    // initialize ungrouping table for 1.67-bit GAQ gain codes
    for(i=0; i<32; i++) {
        gaq_ungroup_tab[i][0] = i / 9;
        gaq_ungroup_tab[i][1] = (i % 9) / 3;
        gaq_ungroup_tab[i][2] = i % 3;
    }

#if TEST_SPX
    {
    int j;
    /* initialize spectral extension attenuation table */
    for (j = 0; j < 3; j++) {
        for (i = 0; i < 32; i++) {
            spx_attenuation_tab[i][j] = pow(2.0, (i+1.0)*(j+1.0)/-15.0);
            av_log(NULL, AV_LOG_DEBUG, "spxattentab[%d][%d] = %1.9f\n", j, i, spx_attenuation_tab[i][j]);
        }
    }
    }
#endif
}
