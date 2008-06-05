/*
 * Common code between AC3 and E-AC3 decoder
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

/**
 * @file ac3.h
 * Common code between AC3 encoder and decoder.
 */

#ifndef FFMPEG_AC3DEC_H
#define FFMPEG_AC3DEC_H

#include "libavutil/random.h"
#include "ac3tab.h"
#include "ac3.h"
#include "ac3dec_data.h"
#include "bitstream.h"
#include "dsputil.h"

#define TEST_SPX 0
#define TEST_ECPL 0

/* override ac3.h to include coupling channel */
#undef AC3_MAX_CHANNELS
#define AC3_MAX_CHANNELS 7

#define CPL_CH 0

#define AC3_OUTPUT_LFEON  8

#define AC3_MAX_COEFS   256
#define AC3_BLOCK_SIZE  256
#define MAX_BLOCKS 6
#define MAX_SPX_CODES 18

typedef struct AC3DecodeContext {
    AVCodecContext *avctx;  ///< parent context
    GetBitContext gbc;      ///< bitstream reader
    uint8_t *input_buffer;  ///< temp buffer to prevent overread

///@defgroup bsi bit stream information
///@{
    int frame_type;         ///< frame type (strmtyp)
    int substreamid;        ///< substream identification
    int frame_size;         ///< frame size, in bytes
    int bit_rate;           ///< bitrate, in bits-per-second
    int sample_rate;        ///< sample rate, in Hz
    int num_blocks;         ///< number of audio blocks
    int channel_mode;       ///< channel mode (acmod)
    int lfe_on;             ///< low frequency effect channel on (lfeon)
    int channel_map;        ///< custom channel map
    int bitstream_id;       ///< bit stream identification (bsid)
    int center_mix_level;   ///< center mix level index
    int surround_mix_level; ///< surround mix level index
    int eac3;               ///< indicates if current frame is E-AC-3
///@}

///@defgroup audfrm frame syntax parameters
    int snr_offset_strategy;    ///< SNR offset strategy (snroffststr)
    int block_switch_syntax;    ///< block switch syntax enabled (blkswe)
    int dither_flag_syntax;     ///< dither flag syntax enabled (dithflage)
    int bit_allocation_syntax;  ///< bit allocation model syntax enabled (bamode)
    int fast_gain_syntax;       ///< fast gain codes enabled (frmfgaincode)
    int dba_syntax;             ///< delta bit allocation syntax enabled (dbaflde)
    int skip_syntax;            ///< skip field syntax enabled (skipflde)
///@}

///@defgroup cpl Standard Coupling
    int cpl_in_use[MAX_BLOCKS];             ///< coupling in use (cplinu)
    int cpl_strategy_exists[MAX_BLOCKS];    ///< coupling strategy exists (cplstre)
    int channel_in_cpl[AC3_MAX_CHANNELS];   ///< channel in coupling (chincpl)
    int phase_flags_in_use;                 ///< phase flag in use (phsflginu)
    int phase_flags[18];                    ///< phase flag
    int num_cpl_subbands;                   ///< number of coupling sub bands (ncplsubnd)
    int num_cpl_bands;                      ///< number of coupling bands (ncplbnd)
    int cpl_band_struct[18];                ///< coupling band structure (cplbndstrc)
    int firstchincpl;                       ///< first channel in coupling
    int first_cpl_coords[AC3_MAX_CHANNELS]; ///< first coupling coordinates states (firstcplcos)
    int cpl_coords[AC3_MAX_CHANNELS][18];   ///< coupling coordinates (cplco)
///@}

///@defgroup aht adaptive hybrid transform
    int channel_uses_aht[AC3_MAX_CHANNELS];                 ///< channel AHT in use (chahtinu)
    int pre_mantissa[6][AC3_MAX_CHANNELS][AC3_MAX_COEFS];   ///< pre-IDCT mantissas
///@}

#if TEST_SPX
///@defgroup spx spectral extension
    int channel_uses_spx[AC3_MAX_CHANNELS]; ///< channel in spectral extension attenuation process (chinspxatten)
    int spx_atten_code[AC3_MAX_CHANNELS];   ///< spectral extension attenuation code (spxattencod)
    int spx_in_use;                         ///< spectral extension in use
    int chinspx[AC3_MAX_CHANNELS];          ///< channel in spectral extension
    int spxstrtf;                           ///< spectral extension start copy frequency code
    int spxbegf;                            ///< spectral extension begin frequency code
    int spxendf;                            ///< spectral extension end frequency code
    int nspxbnds;                           ///< number of structured spectral extension bands
    int spxbndsztab[MAX_SPX_CODES];         ///< sizes of spectral extension bands
    int spxbndstrc[MAX_SPX_CODES];          ///< spectral extension band structure
    int spxcoe[AC3_MAX_CHANNELS];           ///< spectral extension coordinates exists
    int spxblnd[AC3_MAX_CHANNELS];          ///< spectral extension blend
    int firstspxcos[AC3_MAX_CHANNELS];      ///< first spectral extension coordinates states
    float spxco[AC3_MAX_CHANNELS][18];      ///< spectral extension coordinates
///@}
#endif

#if TEST_ECPL
///@defgroup ecpl enhanced coupling
    int ecpl_in_use;                        ///< enhanced coupling in use
    int ecplbegf;                           ///< enhanced coupling begin frequency code
    int ecplendf;                           ///< enhanced coupling end frequency code
    int ecpl_start_subbnd;                  ///< enhanced coupling begin frequency
    int ecpl_end_subbnd;                    ///< enhanced coupling end frequency
    int necplbnd;                           ///< number of structured enhanced coupling bands
    int ecplbndstrc[23];                    ///< enhanced coupling band structure
    int ecplangleintrp;                     ///< enhanced coupling angle interpolation flag
    int ecplparam1e[AC3_MAX_CHANNELS];      ///< enhanced coupling parameters 1 exists
    int ecplparam2e[AC3_MAX_CHANNELS];      ///< enhanced coupling parameters 2 exists
    int ecplamp[AC3_MAX_CHANNELS][23];      ///< enhanced coupling amplitude scaling
    int ecplangle[AC3_MAX_CHANNELS][23];    ///< enhanced coupling angle
    int ecplchaos[AC3_MAX_CHANNELS][23];    ///< enhanced coupling chaos
    int ecpltrans[AC3_MAX_CHANNELS];        ///< enhanced coupling transient present
///@}
#endif

///@defgroup channel channel
    int fbw_channels;                           ///< number of full bandwidth channels
    int channels;                               ///< total of all channels
    int lfe_ch;                                 ///< index of LFE channel
    float downmix_coeffs[AC3_MAX_CHANNELS][2];  ///< stereo downmix coefficients
    float downmix_coeff_adjust[2];              ///< adjustment needed for each output channel when downmixing
    int downmixed;                              ///< indicates if transform coeffs are currently downmixed
    int output_mode;                            ///< output channel configuration
    int out_channels;                           ///< number of output channels
///@}

///@defgroup dynrng dynamic range
    float dynamic_range[2]; ///< dynamic range gain (dynrng)
///@}

///@defgroup bandwidth bandwidth
    int start_freq[AC3_MAX_CHANNELS];   ///< start frequency bin (strtmant)
    int end_freq[AC3_MAX_CHANNELS];     ///< end frequency bin (endmant)
///@}

///@defgroup rematrixing rematrixing
    int num_rematrixing_bands;  ///< number of rematrixing bands (nrematbnds)
    int rematrixing_flags[4];   ///< rematrixing flags (rematflg)
///@}

///@defgroup exponents exponents
    int num_exp_groups[AC3_MAX_CHANNELS];           ///< number of exponent groups
    uint8_t dexps[AC3_MAX_CHANNELS][AC3_MAX_COEFS]; ///< differential exponents
    int exp_strategy[MAX_BLOCKS][AC3_MAX_CHANNELS]; ///< channel exponent strategy (chexpstr)
///@}

///@defgroup bitalloc bit allocation
    AC3BitAllocParameters bit_alloc_params;         ///< bit allocation parameters
    int first_cpl_leak;                             ///< first coupling leak state (firstcplleak)
    int snr_offset[AC3_MAX_CHANNELS];               ///< SNR offset (snroffst)
    int fast_gain[AC3_MAX_CHANNELS];                ///< channel fast gain (fgain)
    uint8_t bap[AC3_MAX_CHANNELS][AC3_MAX_COEFS];   ///< bit allocation pointers
    int16_t psd[AC3_MAX_CHANNELS][AC3_MAX_COEFS];   ///< scaled exponents
    int16_t band_psd[AC3_MAX_CHANNELS][50];         ///< interpolated exponents (bndpsd)
    int16_t mask[AC3_MAX_CHANNELS][50];             ///< masking values
    uint8_t dba_mode[AC3_MAX_CHANNELS];             ///< delta bit allocation mode (deltbae)
    uint8_t dba_nsegs[AC3_MAX_CHANNELS];            ///< number of delta segments (deltnseg)
    uint8_t dba_offsets[AC3_MAX_CHANNELS][9];       ///< delta segment offsets (deltoffst)
    uint8_t dba_lengths[AC3_MAX_CHANNELS][9];       ///< delta segment lengths (deltlen)
    uint8_t dba_values[AC3_MAX_CHANNELS][9];        ///< delta values for each segment (deltba)
///@}

///@defgroup dithering zero-mantissa dithering
    int dither_all;                     ///< indicates whether all channels use dithering
    int dither_flag[AC3_MAX_CHANNELS];  ///< dither flag (dithflag)
    AVRandomState dith_state;           ///< for dither generation
///@}

///@defgroup imdct IMDCT
    int block_switch[AC3_MAX_CHANNELS]; ///< block switch flag (blksw)
    MDCTContext imdct_512;              ///< for 512 sample imdct transform
    MDCTContext imdct_256;              ///< for 256 sample imdct transform
///@}

///@defgroup opt optimization
    DSPContext  dsp;    ///< for optimization
    float add_bias;     ///< offset for float_to_int16 conversion
    float mul_bias;     ///< scaling for float_to_int16 conversion
///@}

    int fixed_coeffs[AC3_MAX_CHANNELS][AC3_MAX_COEFS];  ///< fixed-point transform coefficients

///@defgroup arrays aligned arrays
    DECLARE_ALIGNED_16(float, transform_coeffs[AC3_MAX_CHANNELS][AC3_MAX_COEFS]);   ///< frequency coefficients
    DECLARE_ALIGNED_16(float, delay[AC3_MAX_CHANNELS][AC3_BLOCK_SIZE]);             ///< delay - added to the next block
    DECLARE_ALIGNED_16(float, window[AC3_BLOCK_SIZE]);                              ///< window coefficients
    DECLARE_ALIGNED_16(float, tmp_output[AC3_BLOCK_SIZE * 24]);                     ///< temp storage for output before windowing
    DECLARE_ALIGNED_16(float, tmp_imdct[AC3_BLOCK_SIZE * 24]);                      ///< temp storage for imdct transform
    DECLARE_ALIGNED_16(float, output[AC3_MAX_CHANNELS][AC3_BLOCK_SIZE]);            ///< output after IMDCT transform and windowing
    DECLARE_ALIGNED_16(int16_t, int_output[AC3_MAX_CHANNELS][AC3_BLOCK_SIZE]);      ///< final 16-bit integer output
///@}
} AC3DecodeContext;

int ff_eac3_parse_header(AC3DecodeContext *s);
void ff_eac3_get_transform_coeffs_aht_ch(AC3DecodeContext *s, int ch);
void ff_eac3_idct_transform_coeffs_ch(AC3DecodeContext *s, int ch, int blk);
void ff_eac3_tables_init(void);
void ff_eac3_log_missing_feature(AVCodecContext *avctx, const char *log);

#endif /* FFMPEG_AC3DEC_H */
