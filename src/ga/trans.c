#include <gorilla/ga.h>
#include <gorilla/ga_internal.h>

#include <stdio.h>

#include <string.h>

//todo higher-quality bicubic algorithm?

// https://ccrma.stanford.edu/~jos/resample/resample.pdf

enum { WINDOWSIZE = 2 };

struct GaResamplingState {
	u32 win_start;
	s32 diff;
	const u32 srate, drate;
	const u32 nch;
	s16 window[];
};

static u32 igcd(u32 x,u32 y){
	while (true) {
		x %= y;
		if (!x) return y;
		y %= x;
		if (!y) return x;
	}
}

void ga_trans_resample_point_s16(GaResamplingState *rs, s16 *dst, usz dlen, s16 *src, usz slen) {
	f32 r = slen / (f32)dlen;
	usz d = 0;
	f32 s = 0;
	for (; d < dlen && s < slen; d++, s += r) {
		for (u32 c = 0; c < rs->nch; c++) {
			dst[d*rs->nch + c] = src[(s32)s*rs->nch + c];
		}
	}

	r = 0;
	return;
}

// dlen and slen are frames
// invariant: dlen ~ (rs->drate/rs->srate)*slen
// taken from sndio; licensed as follows:
/*
 * Copyright (c) 2008-2012 Alexandre Ratchov <alex@caoua.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
void ga_trans_resample_linear_s16(GaResamplingState *rs, s16 *dst, usz dlen, s16 *src, usz slen) {
	s32 srate = rs->srate;
	s32 drate = rs->drate;
	s32 diff = rs->diff;
	s16 *windowbase = rs->window, *window;
	u32 win_start = rs->win_start;
	u32 nch = rs->nch;

	while (true) {
		if (diff >= drate) {
			if (!slen) break;
			win_start ^= 1;
			window = windowbase + win_start;
			for (u32 c = 0; c < nch; c++) {
				*window = *src++;
				window += WINDOWSIZE;
			}

			diff -= drate;
			slen--;
		} else {
			if (!dlen) break;
			window = windowbase;
			for (u32 c = 0; c < nch; c++) {
				s32 s = window[win_start ^ 1];
				s32 ds = window[win_start] - s;
				window += WINDOWSIZE;
				*dst++ = s + ds * diff / drate;
			}

			diff += srate;
			dlen--;
		}
	}

	rs->win_start = win_start;
	rs->diff = diff;
}
// I want to get out frames.  How many frames should I put in?
usz ga_trans_resample_howmany(GaResamplingState *rs, usz out) {
	return (out * rs->srate + rs->diff) / rs->drate;
}

GaResamplingState *ga_trans_resample_setup(u32 drate, u32 srate, u32 nch) {
	GaResamplingState *ret = ga_alloc(sizeof(GaResamplingState) + WINDOWSIZE * nch * sizeof(s16));
	if (!ret) return NULL;

	u32 g = igcd(drate, srate);
	memcpy(ret, &(GaResamplingState){.drate=drate/g, .srate=srate/g, .nch=nch}, sizeof(GaResamplingState));
	return ret;
}

void ga_trans_resample_teardown(GaResamplingState *rs) { ga_free(rs); }
