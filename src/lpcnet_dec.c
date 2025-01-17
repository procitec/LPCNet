/*
   lpcnet_dec.c
   Feb 2019

   LPCNet to bit stream decoder, converts fully quantised bit stream
   on stdin (in 1 bit per char format) to 16 kHz signed 16 bit speech
   samples on stdout.
*/

/* Copyright (c) 2018 Mozilla */
/*
   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>

#include "lpcnet_freedv.h"
#include "lpcnet_dump.h"
#include "lpcnet_quant.h"
#include "lpcnet_freedv_internal.h"
#include "nnet_rw.h"
#include "from_codec2/defines.h"

void lpcnet_open_test_file(LPCNetState *lpcnet, char file_name[]);

int main(int argc, char **argv) {
    FILE *fin, *fout;

    /* quantiser defaults */

    int   dec = 3;
    float pred = 0.9;    
    int   mbest_survivors = 5;
    float weight = 1.0/sqrt(NB_BANDS);    
    int   pitch_bits = 6;
    float ber = 0.0;
    int   num_stages = pred_num_stages;
    int   *m = pred_m;
    float *vq = pred_vq;
    int   logmag = 0;
    int   vq_type = LPCNET_PRED;
    int   ber_st=0, ber_en=-1;
    
    fin = stdin;
    fout = stdout;

    /* quantiser options */
    
    static struct option long_options[] = {
        {"infile",      required_argument, 0, 'i'},
        {"outfile",     required_argument, 0, 'u'},
        {"ber",         required_argument, 0, 'b'},
        {"ber_st",      required_argument, 0, 'c'},
        {"ber_en",      required_argument, 0, 'e'},
        {"decimate",    required_argument, 0, 'd'},
        {"nnet",        required_argument, 0, 'r'},
        {"numstages",   required_argument, 0, 'n'},
        {"pitchquant",  required_argument, 0, 'o'},
        {"pred",        required_argument, 0, 'p'},
        {"split",       no_argument,       0, 's'},
        {"indexopt",    no_argument,       0, 'x'},
        {"verbose",     no_argument,       0, 'v'},
        {0, 0, 0, 0}
    };

    int   c;
    int opt_index = 0;

    while ((c = getopt_long (argc, argv, "b:c:e:d:n:o:p:sxvi:u:r:", long_options, &opt_index)) != -1) {
        switch (c) {
 	case 'i':
            if ((fin = fopen(optarg, "rb")) == NULL) {
                fprintf(stderr, "Couldn't open input file: %s\n", optarg);
                exit(1);
            }
            break;
	case 'u':
            if ((fout = fopen(optarg, "wb")) == NULL) {
                fprintf(stderr, "Couldn't open output file: %s\n", optarg);
                exit(1);
            }
            break;
        case 'b':
            ber = atof(optarg);
            fprintf(stderr, "BER = %f\n", ber);
            break;
        case 'c':
            ber_st = atoi(optarg);
            break;
        case 'd':
            dec = atoi(optarg);
            fprintf(stderr, "dec = %d\n", dec);
            break;
        case 'e':
            ber_en = atoi(optarg);
            break;
        case 'n':
            num_stages = atoi(optarg);
            fprintf(stderr, "%d VQ stages\n",  num_stages);
            break;
        case 'o':
            pitch_bits = atoi(optarg);
            fprintf(stderr, "pitch quantised to %d bits\n",  pitch_bits);
            break;
        case 'p':
            pred = atof(optarg);
            fprintf(stderr, "pred = %f\n", pred);
            break;
	case 'r':
	    fprintf(stderr, "loading nnet: %s\n", optarg);
	    nnet_read(optarg);
	    break;            
        case 's':
            vq_type = LPCNET_DIRECT_SPLIT;
            m = direct_split_m; vq = direct_split_vq; pred = 0.0; logmag = 1; weight = 1.0;
            fprintf(stderr, "direct split VQ\n");
            break;
        case 'x':
            vq_type = LPCNET_DIRECT_SPLIT_INDEX_OPT;
            m = direct_split_indopt_m; vq = direct_split_indopt_vq; pred = 0.0; logmag = 1; weight = 1.0;
            fprintf(stderr, "index optimised direct split VQ\n");
            break;
        case 'v':
            lpcnet_verbose = 1;
            break;
        default:
            fprintf(stderr,"usage: %s [Options]:\n", argv[0]);
            fprintf(stderr,"  [-b --ber BER]\n");
            fprintf(stderr,"  [--ber_st bit   Bit in frame where we start inserting errors (default 0)]\n");
            fprintf(stderr,"  [--ber_en bit   Bit in frame just after we stop inserting errors (default 51)]\n");
            fprintf(stderr,"  [-d --decimation 1/2/3...]\n");
            fprintf(stderr,"  [-n --numstages]\n  [-o --pitchbits nBits]\n");
            fprintf(stderr,"  [-p --pred predCoff]\n");
            fprintf(stderr,"  [-s --split]\n");
            fprintf(stderr,"  [-v --verbose]\n");
            exit(1);
        }
    }
    
    LPCNetFreeDV *lf = lpcnet_freedv_create(vq_type);
    lpcnet_open_test_file(lf->net, "test_lpcnet_statesq.f32");
    LPCNET_QUANT *q = lf->q;

    
    // this program allows us to tweak params via command line
    q->weight = weight; q->pred = pred; q->mbest = mbest_survivors;
    q->pitch_bits = pitch_bits; q->dec = dec; q->logmag = logmag;
    q->num_stages = num_stages; q->m = m; q->vq = vq; 
    lpcnet_quant_compute_bits_per_frame(q);
    
    fprintf(stderr, "dec: %d pred: %3.2f num_stages: %d mbest: %d bits_per_frame: %d frame: %2d ms bit_rate: %5.2f bits/s",
            q->dec, q->pred, q->num_stages, q->mbest, q->bits_per_frame, dec*10, (float)q->bits_per_frame/(dec*0.01));
    fprintf(stderr, "\n");

    int nbits = 0, nerrs = 0;
    VLA_CALLOC(char, frame, q->bits_per_frame);
    int bits_read = 0;
    VLA_CALLOC(short, pcm, lpcnet_samples_per_frame(lf));
    if (ber_en == -1) ber_en = q->bits_per_frame-1;

    do {

        bits_read = fread(frame, sizeof(char), q->bits_per_frame, fin);
        nbits += ber_en - ber_st;
        if (ber != 0.0) {
            int i;
            for(i=ber_st; i<=ber_en; i++) {
                float r = (float)rand()/RAND_MAX;
                if (r < ber) {
                    frame[i] = (frame[i] ^ 1) & 0x1;
                    nerrs++;
                }
            }
        }

        lpcnet_dec(lf,frame,pcm);
        fwrite(pcm, sizeof(short), lpcnet_samples_per_frame(lf), fout);

        if (fout == stdout) fflush(stdout);

    } while(bits_read != 0);

    fclose(fin);
    fclose(fout);

    lpcnet_freedv_destroy(lf);

    if (ber != 0.0)
        fprintf(stderr, "ber_st: %d ber_en: %d nbits: %d nerr: %d BER: %4.3f\n", ber_st, ber_en,
                nbits, nerrs, (float)nerrs/nbits);
    VLA_FREE(frame, pcm);
    return 0;
}
