//=====================================================================
//
// ibmwink.c - animation
//
// NOTE:
// for more information, please see the readme file
//
//=====================================================================
#include "ibmwink.h"
#include "iblit386.h"
#include "ibmfont.h"
#include "ibmdata.h"

#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>


#ifdef _MSC_VER
#pragma warning(disable:4996)
#endif

//=====================================================================
// 滤波器与通道操作
//=====================================================================

static void ibitmap_fetch_8(const IBITMAP *src, IUINT8 *card8, int line)
{
	IUINT8 mask;
	int overflow;
	int size;
	int h;

	overflow = ibitmap_imode_const(src, overflow);

	if (ibitmap_pixfmt_guess(src) == IPIX_FMT_G8) {
		IUINT32 r, g, b, a;
		IRGBA_FROM_A8R8G8B8(src->mask, r, g, b, a);
		mask = (IUINT8)_ipixel_to_gray(r, g, b);
	}	else {
		IUINT32 r, g, b, a;
		IRGBA_FROM_A8R8G8B8(src->mask, r, g, b, a);
		mask = (IUINT8)a;
	}

	size = (int)src->w + 2;
	h = (int)src->h;

	switch (overflow) 
	{
	case IBOM_TRANSPARENT:
		if (line < 0 || line >= (int)src->h) {
			memset(card8, mask, src->w + 2);
			return;
		}
		card8[0] = card8[size - 1] = mask;
		break;
	case IBOM_REPEAT:
		if (line < 0) line = 0;
		else if (line >= (int)src->h) line = (int)src->h - 1;
		card8[0] = ((IUINT8*)src->line[line])[0];
		card8[size - 1] = ((IUINT8*)src->line[line])[(int)src->w - 1];
		break;
	case IBOM_WRAP:
		line = line % h;
		if (line < 0) line += h;
		card8[size - 1] = ((IUINT8*)src->line[line])[0];
		card8[0] = ((IUINT8*)src->line[line])[(int)src->w - 1];
		break;
	case IBOM_MIRROR:
		if (line < 0) line = (-line) % h;
		else if (line >= h) line = h - 1 - (line % h);
		card8[0] = ((IUINT8*)src->line[line])[0];
		card8[size - 1] = ((IUINT8*)src->line[line])[(int)src->w - 1];
		break;
	}
	memcpy(card8 + 1, src->line[line], size - 2);
}

static void ibitmap_fetch_32(const IBITMAP *src, IUINT32 *card, int line)
{
	const iColorIndex *index = (const iColorIndex*)src->extra;
	iFetchProc fetch;
	IUINT32 mask;
	int overflow;
	int size;
	int h;

	overflow = ibitmap_imode_const(src, overflow);
	fetch = ipixel_get_fetch(ibitmap_pixfmt_guess(src), 0);
	mask = (IUINT32)src->mask;

	size = (int)src->w + 2;
	h = (int)src->h;

	switch (overflow)
	{
	case IBOM_TRANSPARENT:
		if (line < 0 || line >= (int)src->h) {
			int i;
			for (i = 0; i < size; i++) *card++ = mask;
			return;
		}
		card[0] = card[size - 1] = mask;
		break;
	case IBOM_REPEAT:
		if (line < 0) line = 0;
		else if (line >= (int)src->h) line = (int)src->h - 1;
		fetch(src->line[line], 0, 1, card, index);
		fetch(src->line[line], (int)src->w - 1, 1, card + size - 1, index);
		break;
	case IBOM_WRAP:
		line = line % h;
		if (line < 0) line += h;
		fetch(src->line[line], (int)src->w - 1, 1, card, index);
		fetch(src->line[line], 0, 1, card + size - 1, index);
		break;
	case IBOM_MIRROR:
		if (line < 0) line = (-line) % h;
		else if (line >= h) line = h - 1 - (line % h);
		fetch(src->line[line], 0, 1, card, index);
		fetch(src->line[line], (int)src->w - 1, 1, card + size - 1, index);
		break;
	}

	fetch(src->line[line], 0, size - 2, card + 1, index);
}

static inline IUINT8 
ibitmap_filter_pass_8(const IUINT8 *pixel, const short *filter)
{
	int result;

#ifdef __x86__
	if (X86_FEATURE(X86_FEATURE_MMX)) {
	}
#endif

	result =	(int)(pixel[0]) * (int)(filter[0]) + 
				(int)(pixel[1]) * (int)(filter[1]) +
				(int)(pixel[2]) * (int)(filter[2]) +
				(int)(pixel[3]) * (int)(filter[3]) +
				(int)(pixel[4]) * (int)(filter[4]) +
				(int)(pixel[5]) * (int)(filter[5]) +
				(int)(pixel[6]) * (int)(filter[6]) +
				(int)(pixel[7]) * (int)(filter[7]) +
				(int)(pixel[8]) * (int)(filter[8]);

	result = (result) >> 8;
	if (result > 255) result = 255;
	else if (result < 0) result = 0;

	return (IUINT8)result;
}

static inline IUINT32
ibitmap_filter_pass_32(const IUINT32 *pixel, const short *filter)
{
	int r1 = 0, g1 = 0, b1 = 0, a1 = 0;
	int r2 = 0, g2 = 0, b2 = 0, a2 = 0;
	int f, i;

#ifdef __x86__
	if (X86_FEATURE(X86_FEATURE_MMX)) {
	}
#endif

	for (i = 9; i > 0; pixel++, filter++, i--) {
		IRGBA_FROM_A8R8G8B8(pixel[0], r2, g2, b2, a2);
		f = filter[0];
		r1 += r2 * f;
		g1 += g2 * f;
		b1 += b2 * f;
		a1 += a2 * f;
	}

	r1 = (r1) >> 8;
	g1 = (g1) >> 8;
	b1 = (b1) >> 8;
	a1 = (a1) >> 8;

	if (r1 > 255) r1 = 255;
	else if (r1 < 0) r1 = 0;
	if (g1 > 255) g1 = 255;
	else if (g1 < 0) g1 = 0;
	if (b1 > 255) b1 = 255;
	else if (b1 < 0) b1 = 0;
	if (a1 > 255) a1 = 255;
	else if (a1 < 0) a1 = 0;

	return IRGBA_TO_A8R8G8B8(r1, g1, b1, a1);
}

int ibitmap_filter_8(IBITMAP *dst, const IBITMAP *src, const short *filter)
{
	IUINT8 *buffer, *p1, *p2, *p3, *p4, *card;
	IUINT8 pixel[9];
	int line, i;

	buffer = (IUINT8*)malloc((src->w + 2) * 3);
	if (buffer == NULL) return -10;
	
	for (line = 0; line < (int)src->h; line++) {
		p1 = buffer;
		p2 = p1 + src->w + 2;
		p3 = p2 + src->w + 2;
		p4 = (IUINT8*)dst->line[line];
		card = p4;
		ibitmap_fetch_8(src, p1, line - 1);
		ibitmap_fetch_8(src, p2, line - 0);
		ibitmap_fetch_8(src, p3, line + 1);
		p1++; p2++; p3++;
		for (i = (int)src->w; i > 0; i--, p1++, p2++, p3++, card++) {
			pixel[0] = p1[-1]; pixel[1] = p1[0]; pixel[2] = p1[1];
			pixel[3] = p2[-1]; pixel[4] = p2[0]; pixel[5] = p2[1];
			pixel[6] = p3[-1]; pixel[7] = p3[0]; pixel[8] = p3[1];
			card[0] = ibitmap_filter_pass_8(pixel, filter);
		}
	}

	free(buffer);

	return 0;
}

int ibitmap_filter_32(IBITMAP *dst, const IBITMAP *src, const short *filter)
{
	iColorIndex *index = (iColorIndex*)dst->extra;
	IUINT32 *buffer, *p1, *p2, *p3, *p4, *card;
	IUINT32 pixel[9];
	iStoreProc store;
	int line, i;

	buffer = (IUINT32*)malloc((src->w + 2) * 4 * 4);
	if (buffer == NULL) return -10;

	store = ipixel_get_store(ibitmap_pixfmt_guess(dst), 0);

	for (line = 0; line < (int)src->h; line++) {
		p1 = buffer;
		p2 = p1 + src->w + 2;
		p3 = p2 + src->w + 2;
		p4 = p3 + src->w + 2;
		card = p4;
		ibitmap_fetch_32(src, p1, line - 1);
		ibitmap_fetch_32(src, p2, line - 0);
		ibitmap_fetch_32(src, p3, line + 1);
		p1++; p2++; p3++;
		for (i = (int)src->w; i > 0; i--, p1++, p2++, p3++, card++) {
			pixel[0] = p1[-1]; pixel[1] = p1[0]; pixel[2] = p1[1];
			pixel[3] = p2[-1]; pixel[4] = p2[0]; pixel[5] = p2[1];
			pixel[6] = p3[-1]; pixel[7] = p3[0]; pixel[8] = p3[1];
			card[0] = ibitmap_filter_pass_32(pixel, filter);
			//card[0] = pixel[4];
		}
		store(dst->line[line], p4, 0, (int)src->w, index);
	}
	free(buffer);
	return 0;
}


//---------------------------------------------------------------------
// 使用滤波器
// filter是一个长为9的数组，该函数将以每个像素为中心3x3的9个点中的各个
// 分量乘以filter中对应的值后相加，再除以256，作为该点的颜色保存
//---------------------------------------------------------------------
int ibitmap_filter(IBITMAP *dst, const short *filter)
{
	IBITMAP *src;
	int retval;
	int pixfmt;

	assert(dst);

	src = ibitmap_create((int)dst->w, (int)dst->h, (int)dst->bpp);
	if (src == NULL) return -1;

	src->mode = dst->mode;
	src->mask = dst->mask;
	src->extra = dst->extra;
	pixfmt = ibitmap_pixfmt_guess(src);

	memcpy(src->pixel, dst->pixel, dst->pitch * dst->h);

	if (pixfmt == IPIX_FMT_G8 || pixfmt == IPIX_FMT_A8) {
		retval = ibitmap_filter_8(dst, src, filter);
	}	else {
		retval = ibitmap_filter_32(dst, src, filter);
	}

	ibitmap_release(src);

#ifdef __x86__
	if (X86_FEATURE(X86_FEATURE_MMX)) {
		immx_emms();
	}
#endif

	return retval;
}


//---------------------------------------------------------------------
// 取得channel
// dst必须是8位的位图，filter的0,1,2,3代表取得src中的r,g,b,a分量
//---------------------------------------------------------------------
int ibitmap_channel_get(IBITMAP *dst, int dx, int dy, const IBITMAP *src,
	int sx, int sy, int sw, int sh, int channel)
{
	char _buffer[IBITMAP_STACK_BUFFER];
	char *buffer = _buffer;
	IUINT32 *card;
	int pixfmt;
	long size;
#if IWORDS_BIG_ENDIAN
	static const int table[4] = { 1, 2, 3, 0 };
#else
	static const int table[4] = { 2, 1, 0, 3 };
#endif

	if (ibitmap_clipex(dst, &dx, &dy, src, &sx, &sy, &sw, &sh, NULL, 0)) 
		return -1;

	if (dst->bpp != 8) 
		return -2;

	pixfmt = ibitmap_pixfmt_guess(src);
	channel = table[channel & 3];

	if (pixfmt == IPIX_FMT_A8R8G8B8) {
		int y, x;
		for (y = 0; y < sh; y++) {
			IUINT8 *ss = (IUINT8*)(src->line[sy + y]) + 4 * sx;
			IUINT8 *dd = (IUINT8*)(dst->line[dy + y]) + 1 * dx;
			for (x = sw; x > 0; ss += 4, dd++, x--) {
				dd[0] = ss[channel];
			}
		}
	}	
	else {
		const iColorIndex *index = (const iColorIndex*)src->extra;
		iFetchProc fetch;
		int y, x;
		size = sizeof(IUINT32) * sw;
		if (size > IBITMAP_STACK_BUFFER) {
			buffer = (char*)malloc(size);
			if (buffer == NULL) return -3;
		}
		card = (IUINT32*)buffer;
		fetch = ipixel_get_fetch(pixfmt, 0);
		for (y = 0; y < sh; y++) {
			IUINT8 *ss = (IUINT8*)card;
			IUINT8 *dd = (IUINT8*)(dst->line[dy + y]) + 1 * dx;
			fetch(src->line[sy + y], sx, sw, card, index);
			for (x = sw; x > 0; ss += 4, dd++, x--) {
				dd[0] = ss[channel];
			}
		}
		if (buffer != _buffer) free(buffer);
	}

	return 0;
}


//---------------------------------------------------------------------
// 设置channel
// src必须是8位的位图，filter的0,1,2,3代表设置dst中的r,g,b,a分量
//---------------------------------------------------------------------
int ibitmap_channel_set(IBITMAP *dst, int dx, int dy, const IBITMAP *src,
	int sx, int sy, int sw, int sh, int channel)
{
	char _buffer[IBITMAP_STACK_BUFFER];
	char *buffer = _buffer;
	IUINT32 *card;
	int pixfmt;
	long size;
#if IWORDS_BIG_ENDIAN
	static const int table[4] = { 1, 2, 3, 0 };
#else
	static const int table[4] = { 2, 1, 0, 3 };
#endif

	if (ibitmap_clipex(dst, &dx, &dy, src, &sx, &sy, &sw, &sh, NULL, 0)) 
		return -1;

	if (src->bpp != 8) 
		return -2;

	pixfmt = ibitmap_pixfmt_guess(dst);
	channel = table[channel & 3];

	if (pixfmt == IPIX_FMT_A8R8G8B8) {
		int y, x;
		for (y = 0; y < sh; y++) {
			IUINT8 *ss = (IUINT8*)(src->line[sy + y]) + 1 * sx;
			IUINT8 *dd = (IUINT8*)(dst->line[dy + y]) + 4 * dx;
			for (x = sw; x > 0; ss++, dd += 4, x--) {
				dd[channel] = ss[0];
			}
		}
	}	
	else {
		const iColorIndex *index = (const iColorIndex*)src->extra;
		iFetchProc fetch;
		iStoreProc store;
		int x, y;
		size = sizeof(IUINT32) * sw;
		if (size > IBITMAP_STACK_BUFFER) {
			buffer = (char*)malloc(size);
			if (buffer == NULL) return -3;
		}
		card = (IUINT32*)buffer;
		fetch = ipixel_get_fetch(pixfmt, 0);
		store = ipixel_get_store(pixfmt, 0);
		for (y = 0; y < sh; y++) {
			IUINT8 *ss = (IUINT8*)(src->line[sy + y]) + 1 * sx;
			IUINT8 *dd = (IUINT8*)card;
			fetch(dst->line[dy + y], dx, sw, card, index);
			for (x = sw; x > 0; ss++, dd += 4, x--) {
				dd[channel] = ss[0];
			}
			store(dst->line[dy + y], card, dx, sw, index);
		}
		if (buffer != _buffer) free(buffer);
	}

	return 0;
}



//=====================================================================
// 基础特效
//=====================================================================
IBITMAP *ibitmap_effect_drop_shadow(const IBITMAP *src, int dir, int level)
{
	static short f0[9] = { 16, 16, 16,  16, 128, 16,  16, 16, 16 };
	IBITMAP *alpha;
	short *filter;
	int i;

	alpha = ibitmap_create((int)src->w, (int)src->h, 8);
	if (alpha == NULL) return NULL;

	ibitmap_pixfmt_set(alpha, IPIX_FMT_A8);
	ibitmap_channel_get(alpha, 0, 0, src, 0, 0, (int)src->w, (int)src->h, 3);

	filter = f0;
	
	for (i = 0; i < level; i++) 
		ibitmap_filter(alpha, filter);

	return alpha;
}

static const IINT32 g_stack_blur8_mul[255] = {
        512,512,456,512,328,456,335,512,405,328,271,456,388,335,292,512,
        454,405,364,328,298,271,496,456,420,388,360,335,312,292,273,512,
        482,454,428,405,383,364,345,328,312,298,284,271,259,496,475,456,
        437,420,404,388,374,360,347,335,323,312,302,292,282,273,265,512,
        497,482,468,454,441,428,417,405,394,383,373,364,354,345,337,328,
        320,312,305,298,291,284,278,271,265,259,507,496,485,475,465,456,
        446,437,428,420,412,404,396,388,381,374,367,360,354,347,341,335,
        329,323,318,312,307,302,297,292,287,282,278,273,269,265,261,512,
        505,497,489,482,475,468,461,454,447,441,435,428,422,417,411,405,
        399,394,389,383,378,373,368,364,359,354,350,345,341,337,332,328,
        324,320,316,312,309,305,301,298,294,291,287,284,281,278,274,271,
        268,265,262,259,257,507,501,496,491,485,480,475,470,465,460,456,
        451,446,442,437,433,428,424,420,416,412,408,404,400,396,392,388,
        385,381,377,374,370,367,363,360,357,354,350,347,344,341,338,335,
        332,329,326,323,320,318,315,312,310,307,304,302,299,297,294,292,
        289,287,285,282,280,278,275,273,271,269,267,265,263,261,259
};

static const IINT32 g_stack_blur8_shr[255] = {
          9, 11, 12, 13, 13, 14, 14, 15, 15, 15, 15, 16, 16, 16, 16, 17, 
         17, 17, 17, 17, 17, 17, 18, 18, 18, 18, 18, 18, 18, 18, 18, 19, 
         19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 20, 20, 20,
         20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 21,
         21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
         21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 22, 22, 22, 22, 22, 22, 
         22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22,
         22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 23, 
         23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23,
         23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23,
         23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 
         23, 23, 23, 23, 23, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 
         24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
         24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
         24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
         24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24
};


void ipixel_stackblur_4(void *src, long pitch, int w, int h, int rx, int ry)
{
	unsigned x, y, xp, yp, i;
	unsigned stack_ptr;
	unsigned stack_start;

	const unsigned char * src_pix_ptr;
	unsigned char * dst_pix_ptr;
	unsigned char * stack_pix_ptr;

	IUINT32 sum_r;
	IUINT32 sum_g;
	IUINT32 sum_b;
	IUINT32 sum_a;
	IUINT32 sum_in_r;
	IUINT32 sum_in_g;
	IUINT32 sum_in_b;
	IUINT32 sum_in_a;
	IUINT32 sum_out_r;
	IUINT32 sum_out_g;
	IUINT32 sum_out_b;
	IUINT32 sum_out_a;

	IUINT32 wm  = (IUINT32)w - 1;
	IUINT32 hm  = (IUINT32)h - 1;

	IUINT32 div;
	IUINT32 mul_sum;
	IUINT32 shr_sum;

	IUINT32 stack[512];

	if (rx > 0) {
		if (rx > 254) rx = 254;
		div = rx * 2 + 1;
		mul_sum = g_stack_blur8_mul[rx];
		shr_sum = g_stack_blur8_shr[rx];

		for (y = 0; y < (IUINT32)h; y++) {
			sum_r = 
			sum_g = 
			sum_b = 
			sum_a = 
			sum_in_r = 
			sum_in_g = 
			sum_in_b = 
			sum_in_a = 
			sum_out_r = 
			sum_out_g = 
			sum_out_b = 
			sum_out_a = 0;

			src_pix_ptr = (unsigned char*)src + y * pitch;

			for (i = 0; i <= (IUINT32)rx; i++) {
				stack_pix_ptr    = (unsigned char*)&stack[i];
				stack_pix_ptr[0] = src_pix_ptr[0];
				stack_pix_ptr[1] = src_pix_ptr[1];
				stack_pix_ptr[2] = src_pix_ptr[2];
				stack_pix_ptr[3] = src_pix_ptr[3];
				sum_r           += src_pix_ptr[0] * (i + 1);
				sum_g           += src_pix_ptr[1] * (i + 1);
				sum_b           += src_pix_ptr[2] * (i + 1);
				sum_a           += src_pix_ptr[3] * (i + 1);
				sum_out_r       += src_pix_ptr[0];
				sum_out_g       += src_pix_ptr[1];
				sum_out_b       += src_pix_ptr[2];
				sum_out_a       += src_pix_ptr[3];
			}
			for (i = 1; i <= (IUINT32)rx; i++) {
				if (i <= wm) src_pix_ptr += 4;
				stack_pix_ptr = (unsigned char*)&stack[i + rx];
				stack_pix_ptr[0] = src_pix_ptr[0];
				stack_pix_ptr[1] = src_pix_ptr[1];
				stack_pix_ptr[2] = src_pix_ptr[2];
				stack_pix_ptr[3] = src_pix_ptr[3];
				sum_r           += src_pix_ptr[0] * (rx + 1 - i);
				sum_g           += src_pix_ptr[1] * (rx + 1 - i);
				sum_b           += src_pix_ptr[2] * (rx + 1 - i);
				sum_a           += src_pix_ptr[3] * (rx + 1 - i);
				sum_in_r        += src_pix_ptr[0];
				sum_in_g        += src_pix_ptr[1];
				sum_in_b        += src_pix_ptr[2];
				sum_in_a        += src_pix_ptr[3];
			}

			stack_ptr = rx;
			xp = rx;
			if (xp > wm) xp = wm;

			src_pix_ptr = (unsigned char*)src + y * pitch + xp * 4;
			dst_pix_ptr = (unsigned char*)src + y * pitch;

			for (x = 0; x < (IUINT32)w; x++) {
				dst_pix_ptr[0] = (sum_r * mul_sum) >> shr_sum;
				dst_pix_ptr[1] = (sum_g * mul_sum) >> shr_sum;
				dst_pix_ptr[2] = (sum_b * mul_sum) >> shr_sum;
				dst_pix_ptr[3] = (sum_a * mul_sum) >> shr_sum;
				dst_pix_ptr += 4;

				sum_r -= sum_out_r;
				sum_g -= sum_out_g;
				sum_b -= sum_out_b;
				sum_a -= sum_out_a;

				stack_start = stack_ptr + div - rx;
				if (stack_start >= div) stack_start -= div;
				stack_pix_ptr = (unsigned char*)&stack[stack_start];

				sum_out_r -= stack_pix_ptr[0];
				sum_out_g -= stack_pix_ptr[1];
				sum_out_b -= stack_pix_ptr[2];
				sum_out_a -= stack_pix_ptr[3];

				if(xp < wm) {
					src_pix_ptr += 4;
					++xp;
				}

				stack_pix_ptr[0] = src_pix_ptr[0];
				stack_pix_ptr[1] = src_pix_ptr[1];
				stack_pix_ptr[2] = src_pix_ptr[2];
				stack_pix_ptr[3] = src_pix_ptr[3];

				sum_in_r += src_pix_ptr[0];
				sum_in_g += src_pix_ptr[1];
				sum_in_b += src_pix_ptr[2];
				sum_in_a += src_pix_ptr[3];
				sum_r    += sum_in_r;
				sum_g    += sum_in_g;
				sum_b    += sum_in_b;
				sum_a    += sum_in_a;

				++stack_ptr;
				if (stack_ptr >= div) stack_ptr = 0;
				stack_pix_ptr = (unsigned char*)&stack[stack_ptr];

				sum_out_r += stack_pix_ptr[0];
				sum_out_g += stack_pix_ptr[1];
				sum_out_b += stack_pix_ptr[2];
				sum_out_a += stack_pix_ptr[3];
				sum_in_r  -= stack_pix_ptr[0];
				sum_in_g  -= stack_pix_ptr[1];
				sum_in_b  -= stack_pix_ptr[2];
				sum_in_a  -= stack_pix_ptr[3];
			}
		}
	}

	if (ry > 0) {
		if (ry > 254) ry = 254;
		div = ry * 2 + 1;
		mul_sum = g_stack_blur8_mul[ry];
		shr_sum = g_stack_blur8_shr[ry];

		for (x = 0; x < (IUINT32)w; x++) {
			sum_r = 
			sum_g = 
			sum_b = 
			sum_a = 
			sum_in_r = 
			sum_in_g = 
			sum_in_b = 
			sum_in_a = 
			sum_out_r = 
			sum_out_g = 
			sum_out_b = 
			sum_out_a = 0;

			src_pix_ptr = (unsigned char*)src + x * 4;

			for (i = 0; i <= (IUINT32)ry; i++) {
				stack_pix_ptr    = (unsigned char*)&stack[i];
				stack_pix_ptr[0] = src_pix_ptr[0];
				stack_pix_ptr[1] = src_pix_ptr[1];
				stack_pix_ptr[2] = src_pix_ptr[2];
				stack_pix_ptr[3] = src_pix_ptr[3];
				sum_r           += src_pix_ptr[0] * (i + 1);
				sum_g           += src_pix_ptr[1] * (i + 1);
				sum_b           += src_pix_ptr[2] * (i + 1);
				sum_a           += src_pix_ptr[3] * (i + 1);
				sum_out_r       += src_pix_ptr[0];
				sum_out_g       += src_pix_ptr[1];
				sum_out_b       += src_pix_ptr[2];
				sum_out_a       += src_pix_ptr[3];
			}
			for (i = 1; i <= (IUINT32)ry; i++) {
				if (i <= hm) src_pix_ptr += pitch; 
				stack_pix_ptr = (unsigned char*)&stack[i + ry];
				stack_pix_ptr[0] = src_pix_ptr[0];
				stack_pix_ptr[1] = src_pix_ptr[1];
				stack_pix_ptr[2] = src_pix_ptr[2];
				stack_pix_ptr[3] = src_pix_ptr[3];
				sum_r           += src_pix_ptr[0] * (ry + 1 - i);
				sum_g           += src_pix_ptr[1] * (ry + 1 - i);
				sum_b           += src_pix_ptr[2] * (ry + 1 - i);
				sum_a           += src_pix_ptr[3] * (ry + 1 - i);
				sum_in_r        += src_pix_ptr[0];
				sum_in_g        += src_pix_ptr[1];
				sum_in_b        += src_pix_ptr[2];
				sum_in_a        += src_pix_ptr[3];
			}

			stack_ptr = ry;
			yp = ry;
			if(yp > hm) yp = hm;

			src_pix_ptr = (unsigned char*)src + yp * pitch + x * 4;
			dst_pix_ptr = (unsigned char*)src + x * 4;

			for (y = 0; y < (IUINT32)h; y++) {
				dst_pix_ptr[0] = (sum_r * mul_sum) >> shr_sum;
				dst_pix_ptr[1] = (sum_g * mul_sum) >> shr_sum;
				dst_pix_ptr[2] = (sum_b * mul_sum) >> shr_sum;
				dst_pix_ptr[3] = (sum_a * mul_sum) >> shr_sum;
				dst_pix_ptr += pitch;

				sum_r -= sum_out_r;
				sum_g -= sum_out_g;
				sum_b -= sum_out_b;
				sum_a -= sum_out_a;

				stack_start = stack_ptr + div - ry;
				if (stack_start >= div) stack_start -= div;

				stack_pix_ptr = (unsigned char*)&stack[stack_start];
				sum_out_r -= stack_pix_ptr[0];
				sum_out_g -= stack_pix_ptr[1];
				sum_out_b -= stack_pix_ptr[2];
				sum_out_a -= stack_pix_ptr[3];

				if (yp < hm) {
					src_pix_ptr += pitch;
					++yp;
				}

				stack_pix_ptr[0] = src_pix_ptr[0];
				stack_pix_ptr[1] = src_pix_ptr[1];
				stack_pix_ptr[2] = src_pix_ptr[2];
				stack_pix_ptr[3] = src_pix_ptr[3];

				sum_in_r += src_pix_ptr[0];
				sum_in_g += src_pix_ptr[1];
				sum_in_b += src_pix_ptr[2];
				sum_in_a += src_pix_ptr[3];
				sum_r    += sum_in_r;
				sum_g    += sum_in_g;
				sum_b    += sum_in_b;
				sum_a    += sum_in_a;

				++stack_ptr;
				if (stack_ptr >= div) stack_ptr = 0;
				stack_pix_ptr = (unsigned char*)&stack[stack_ptr];

				sum_out_r += stack_pix_ptr[0];
				sum_out_g += stack_pix_ptr[1];
				sum_out_b += stack_pix_ptr[2];
				sum_out_a += stack_pix_ptr[3];
				sum_in_r  -= stack_pix_ptr[0];
				sum_in_g  -= stack_pix_ptr[1];
				sum_in_b  -= stack_pix_ptr[2];
				sum_in_a  -= stack_pix_ptr[3];
			}
		}
	}
}


void ibitmap_stackblur(IBITMAP *src, int rx, int ry, const IRECT *bound)
{
	int x, y, w, h;
	IRECT rect;

	if (bound == NULL) {
		bound = &rect;
		rect.left = 0;
		rect.top = 0;
		rect.right = (int)src->w;
		rect.bottom = (int)src->h;
	}

	x = bound->left;
	y = bound->top;
	w = bound->right - bound->left;
	h = bound->bottom - bound->top;

	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x + w >= (int)src->w) w = src->w - x;
	if (y + h >= (int)src->h) h = src->h - y;
	if (w <= 0 || h <= 0) return;

	if (src->bpp != 32) {
		IBITMAP *newbmp = ibitmap_create(w, h, 32);
		if (newbmp == NULL) return;
		ibitmap_pixfmt_set(newbmp, IPIX_FMT_A8R8G8B8);
		ibitmap_convert(newbmp, 0, 0, src, x, y, w, h, NULL, 0);
		ipixel_stackblur_4(newbmp->pixel, (long)newbmp->pitch,
			w, h, rx, ry);
		ibitmap_convert(src, x, y, newbmp, 0, 0, w, h, NULL, 0);
		ibitmap_release(newbmp);
		return;
	}

	ipixel_stackblur_4((char*)src->line[y] + x * 4, (long)src->pitch,
		w, h, rx, ry);
}


//---------------------------------------------------------------------
// 原始作图
//---------------------------------------------------------------------


//---------------------------------------------------------------------
// internal
//---------------------------------------------------------------------
#define _iabs(x)    ( ((x) < 0) ? (-(x)) : (x) )
#define _imin(x, y) ( ((x) < (y)) ? (x) : (y) )
#define _imax(x, y) ( ((x) > (y)) ? (x) : (y) )


//---------------------------------------------------------------------
// this line clipping based heavily off of code from
// http://www.ncsa.uiuc.edu/Vis/Graphics/src/clipCohSuth.c 
//---------------------------------------------------------------------
#define LEFT_EDGE   0x1
#define RIGHT_EDGE  0x2
#define BOTTOM_EDGE 0x4
#define TOP_EDGE    0x8
#define INSIDE(a)   (!a)
#define REJECT(a,b) (a&b)
#define ACCEPT(a,b) (!(a|b))

static inline int _iencode(int x, int y, int left, int top, int right, 
	int bottom)
{
	int code = 0;
	if (x < left)   code |= LEFT_EDGE;
	if (x > right)  code |= RIGHT_EDGE;
	if (y < top)    code |= TOP_EDGE;
	if (y > bottom) code |= BOTTOM_EDGE;
	return code;
}

static inline int _iencode_float(float x, float y, int left, int top, 
	int right, int bottom)
{
	int code = 0;
	if (x < left)   code |= LEFT_EDGE;
	if (x > right)  code |= RIGHT_EDGE;
	if (y < top)    code |= TOP_EDGE;
	if (y > bottom) code |= BOTTOM_EDGE;
	return code;
}

static inline int _iclipline(int* pts, int left, int top, int right, 
	int bottom)
{
	int x1 = pts[0];
	int y1 = pts[1];
	int x2 = pts[2];
	int y2 = pts[3];
	int code1, code2;
	int draw = 0;
	int swaptmp;
	float m; /*slope*/

	right--;
	bottom--;

	while(1)
	{
		code1 = _iencode(x1, y1, left, top, right, bottom);
		code2 = _iencode(x2, y2, left, top, right, bottom);
		if (ACCEPT(code1, code2)) {
			draw = 1;
			break;
		}
		else if (REJECT(code1, code2))
			break;
		else {
			if (INSIDE(code1)) {
				swaptmp = x2; x2 = x1; x1 = swaptmp;
				swaptmp = y2; y2 = y1; y1 = swaptmp;
				swaptmp = code2; code2 = code1; code1 = swaptmp;
			}
			if (x2 != x1)
				m = (y2 - y1) / (float)(x2 - x1);
			else
				m = 1.0f;
			if (code1 & LEFT_EDGE) {
				y1 += (int)((left - x1) * m);
				x1 = left;
			}
			else if (code1 & RIGHT_EDGE) {
				y1 += (int)((right - x1) * m);
				x1 = right;
			}
			else if(code1 & BOTTOM_EDGE) {
				if(x2 != x1)
					x1 += (int)((bottom - y1) / m);
				y1 = bottom;
			}
			else if (code1 & TOP_EDGE) {
				if (x2 != x1)
					x1 += (int)((top - y1) / m);
				y1 = top;
			}
		}
	}
	if (draw) {
		pts[0] = x1; pts[1] = y1;
		pts[2] = x2; pts[3] = y2;
	}
	return draw;
}

#define ipaint_push_pixel(paint, x, y) do { \
		*paint++ = (IUINT32)x; \
		*paint++ = (IUINT32)y; \
	}	while (0)


int ibitmap_put_line_low(IBITMAP *dst, int x1, int y1, int x2, int y2,
	IUINT32 color, int additive, const IRECT *clip, void *workmem)
{
	IUINT32 *paint;
	int cl, ct, cr, cb;
	int x, y, p, n, tn;
	int pts[4];
	int count;

	if (workmem == NULL) {
		int dx = (x1 < x2)? (x2 - x1) : (x1 - x2);
		int dy = (y1 < y2)? (y2 - y1) : (y1 - y2);
		long ds1 = (dst->w + dst->h);
		long ds2 = (dx + dy);
		long size = (ds1 < ds2)? ds1 : ds2;
		return (size + 2) * 4 * sizeof(IUINT32);
	}

	if (clip) {
		cl = clip->left;
		ct = clip->top;
		cr = clip->right - 1;
		cb = clip->bottom - 1;
	}	else {
		cl = 0;
		ct = 0;
		cr = (int)dst->w - 1;
		cb = (int)dst->h - 1;
	}

	if (cr < cl || cb < ct) 
		return -1;

	pts[0] = x1; 
	pts[1] = y1;
	pts[2] = x2;
	pts[3] = y2;

	if (_iclipline(pts, cl, ct, cr, cb) == 0 || (color >> 24) == 0) 
		return -2;

	x1 = pts[0];
	y1 = pts[1];
	x2 = pts[2];
	y2 = pts[3];

	paint = (IUINT32*)workmem;
	
	if (y1 == y2) {	
		if (x1 > x2) x = x1, x1 = x2, x2 = x;
		for (x = x1; x <= x2; x++) ipaint_push_pixel(paint, x, y1);
	}
	else if (x1 == x2)	{  
		if (y1 > y2) y = y2, y2 = y1, y1 = y;
		for (y = y1; y <= y2; y++) ipaint_push_pixel(paint, x1, y);
	}
	else if (_iabs(y2 - y1) <= _iabs(x2 - x1)) {
		if ((y2 < y1 && x2 < x1) || (y1 <= y2 && x1 > x2)) {
			x = x2, y = y2, x2 = x1, y2 = y1, x1 = x, y1 = y;
		}
		if (y2 >= y1 && x2 >= x1) {
			x = x2 - x1, y = y2 - y1;
			p = 2 * y, n = 2 * x - 2 * y, tn = x;
			for (; x1 <= x2; x1++) {
				if (tn >= 0) tn -= p;
				else tn += n, y1++;
				ipaint_push_pixel(paint, x1, y1);
			}
		}	else {
			x = x2 - x1; y = y2 - y1;
			p = -2 * y; n = 2 * x + 2 * y; tn = x;
			for (; x1 <= x2; x1++) {
				if (tn >= 0) tn -= p;
				else tn += n, y1--;
				ipaint_push_pixel(paint, x1, y1);
			}
		}
	}	else {
		x = x1; x1 = y2; y2 = x; y = y1; y1 = x2; x2 = y;
		if ((y2 < y1 && x2 < x1) || (y1 <= y2 && x1 > x2)) {
			x = x2, y = y2, x2 = x1, x1 = x, y2 = y1, y1 = y;
		}
		if (y2 >= y1 && x2 >= x1) {
			x = x2 - x1; y = y2 - y1;
			p = 2 * y; n = 2 * x - 2 * y; tn = x;
			for (; x1 <= x2; x1++)  {
				if (tn >= 0) tn -= p;
				else tn += n, y1++;
				ipaint_push_pixel(paint, y1, x1);
			}
		}	else	{
			x = x2 - x1; y = y2 - y1;
			p = -2 * y; n = 2 * x + 2 * y; tn = x;
			for (; x1 <= x2; x1++) {
				if (tn >= 0) tn -= p;
				else { tn += n; y1--; }
				ipaint_push_pixel(paint, y1, x1);
			}
		}
	}

	count = (int)(paint - (IUINT32*)workmem) / 2;

	ibitmap_draw_pixel_list_sc(dst, (IUINT32*)workmem, count, color,
		additive);

	return 0;
}


int ibitmap_put_line(IBITMAP *dst, int x1, int y1, int x2, int y2,
	IUINT32 color, int additive, const IRECT *clip)
{
	char _buffer[IBITMAP_STACK_BUFFER];
	char *buffer = _buffer;
	int dx = (x1 < x2)? (x2 - x1) : (x1 - x2);
	int dy = (y1 < y2)? (y2 - y1) : (y1 - y2);
	long ds1 = (dst->w + dst->h);
	long ds2 = (dx + dy);
	long size = (ds1 < ds2)? ds1 : ds2;
	int retval = 0;

	size = (size + 2) * 4 * sizeof(IUINT32);
	if (size > IBITMAP_STACK_BUFFER) {
		buffer = (char*)malloc(size);
		if (buffer == NULL) return -100;
	}

	retval = ibitmap_put_line_low(dst, x1, y1, x2, y2, 
		color, additive, clip, buffer);

	if (buffer != _buffer) free(buffer);

	return retval;
}



//---------------------------------------------------------------------
// 缓存管理
//---------------------------------------------------------------------

// 初始化缓存
void cvector_init(struct CVECTOR *vector)
{
	vector->data = NULL;
	vector->size = 0;
	vector->block = 0;
}

// 销毁缓存
void cvector_destroy(struct CVECTOR *vector)
{
	if (vector->data) free(vector->data);
	vector->data = NULL;
	vector->size = 0;
	vector->block = 0;
}

// 改变缓存大小
int cvector_resize(struct CVECTOR *vector, size_t size)
{
	unsigned char*lptr;
	size_t block, min;
	size_t nblock;

	if (vector == NULL) return -1;

	if (size >= vector->size && size <= vector->block) { 
		vector->size = size; 
		return 0; 
	}

	if (size == 0) {
		if (vector->block > 0) {
			free(vector->data);
			vector->block = 0;
			vector->data = NULL;
		}
		return 0;
	}

	for (nblock = sizeof(char*); nblock < size; ) nblock <<= 1;
	block = nblock;

	if (block == vector->block) { 
		vector->size = size; 
		return 0; 
	}

	if (vector->block == 0 || vector->data == NULL) {
		vector->data = (unsigned char*)malloc(block);
		if (vector->data == NULL) return -1;
		vector->size = size;
		vector->block = block;
	}   else {
		lptr = (unsigned char*)malloc(block);
		if (lptr == NULL) return -1;

		min = (vector->size <= size)? vector->size : size;
		memcpy(lptr, vector->data, (size_t)min);
		free(vector->data);

		vector->data = lptr;
		vector->size = size;
		vector->block = block;
	}

	return 0;
}

// 添加数据
int cvector_push(struct CVECTOR *vector, const void *data, size_t size)
{
	size_t offset = vector->size;
	if (cvector_resize(vector, vector->size + size) != 0) 
		return -1;
	if (data) {
		memcpy(vector->data + offset, data, size);
	}
	return 0;
}


//---------------------------------------------------------------------
// 原始作图
//---------------------------------------------------------------------
static struct CVECTOR ipixel_scratch = { 0, 0, 0 };

// 批量填充梯形
int ipixel_render_traps(IBITMAP *dst, const ipixel_trapezoid_t *traps, 
	int ntraps, IBITMAP *alpha, IUINT32 color, int isadditive, 
	const IRECT *clip, struct CVECTOR *scratch)
{
	iHLineDrawProc hline;
	ipixel_span_t *spans;
	IRECT bound, rect;
	int cx, cy, ch, i;
	long size;

	if (ipixel_trapezoid_bound(traps, ntraps, &bound) == 0)
		return -1;

	if (clip == NULL) {
		clip = &rect;
		rect.left = 0;
		rect.top = 0;
		rect.right = (int)dst->w;
		rect.bottom = (int)dst->h;
	}

	ipixel_rect_intersection(&bound, clip);

	if (bound.right - bound.left > (int)alpha->w) 
		bound.right = bound.left + (int)alpha->w;

	if (bound.bottom - bound.top > (int)alpha->h)
		bound.bottom = bound.top + (int)alpha->h;

	if (bound.right <= bound.left || bound.bottom <= bound.top)
		return -2;

	cx = bound.left;
	cy = bound.top;
	ch = bound.bottom - bound.top;

	ipixel_rect_offset(&bound, -cx, -cy);
	
	if (scratch == NULL) scratch = &ipixel_scratch;

	size = ch * sizeof(ipixel_span_t) * 2;
	if (size > (long)scratch->size) {
		if (cvector_resize(scratch, size) != 0)
			return -3;
	}

	spans = (ipixel_span_t*)scratch->data;

	size = ipixel_trapezoid_spans(traps, ntraps, spans, -cx, -cy, &bound);
	hline = ipixel_get_hline_proc(ibitmap_pixfmt_guess(dst), isadditive, 0);

	for (i = 0; i < (int)size; i++) {
		int y = spans[i].y;
		IUINT8 *mask = (IUINT8*)alpha->line[y] + spans[i].x;
		memset(mask, 0, spans[i].w);
	}

	ipixel_raster_traps(alpha, traps, ntraps, -cx, -cy, &bound);

	for (i = 0; i < (int)size; i++) {
		int y = spans[i].y;
		IUINT8 *mask = (IUINT8*)alpha->line[y] + spans[i].x;
		hline(dst->line[cy + y], cx + spans[i].x, spans[i].w, color, mask,
			(const iColorIndex*)dst->extra);
	}

	return 0;
}


// 绘制多边形
int ipixel_render_polygon(IBITMAP *dst, const ipixel_point_fixed_t *pts,
	int npts, IBITMAP *alpha, IUINT32 color, int isadditive,
	const IRECT *clip, struct CVECTOR *scratch)
{
	char _buffer[IBITMAP_STACK_BUFFER];
	char *buffer = _buffer;
	ipixel_trapezoid_t *traps;
	int ntraps;
	long size;
	int retval;

	if (npts < 3) return -10;

	size = sizeof(ipixel_trapezoid_t) * npts * 2;
	if (size > IBITMAP_STACK_BUFFER) {
		buffer = (char*)malloc(size);
		if (buffer == NULL) return -20;
	}

	traps = (ipixel_trapezoid_t*)buffer;
	size = sizeof(ipixel_point_fixed_t) * npts;

	if (scratch == NULL) scratch = &ipixel_scratch;

	if (size > (long)scratch->size) {
		if (cvector_resize(scratch, size) != 0) {
			retval = -30;
			goto exit_label;
		}
	}

	ntraps = ipixel_traps_from_polygon(traps, pts, npts, 0, scratch->data);

	if (ntraps == 0) {
		ntraps = ipixel_traps_from_polygon(traps, pts, npts, 1, 
			scratch->data);
		if (ntraps == 0) {
			retval = -40;
			goto exit_label;
		}
	}

	retval = ipixel_render_traps(dst, traps, ntraps, alpha, color, 
		isadditive, clip, scratch);

exit_label:
	if (buffer != _buffer) free(buffer);

	return retval;
}



//---------------------------------------------------------------------
// 作图接口
//---------------------------------------------------------------------
static void ipaint_init(ipaint_t *paint)
{
	paint->image = NULL;
	paint->alpha = NULL;
	paint->additive = 0;
	paint->pts = NULL;
	paint->npts = 0;
	paint->color = 0xff000000;
	paint->text_color = 0xff000000;
	paint->text_backgrnd = 0xffffffff;
	paint->npts = 0;
	paint->line_width = 1.0;
	cvector_init(&paint->scratch);
	cvector_init(&paint->points);
	cvector_init(&paint->pointf);
}

int ipaint_set_image(ipaint_t *paint, IBITMAP *image)
{
	int subpixel = 0;

	paint->image = image;

	if (paint->alpha) {
		subpixel = ibitmap_imode(paint->alpha, subpixel);
		if (paint->alpha->w < image->w || paint->alpha->h < image->h) {
			ibitmap_release(paint->alpha);
			paint->alpha = NULL;
		}
	}

	if (paint->alpha == NULL) {
		paint->alpha = ibitmap_create((int)image->w, (int)image->h, 8);
		if (paint->alpha == NULL) 
			return -1;
		ibitmap_pixfmt_set(paint->alpha, IPIX_FMT_A8);
	}

	ibitmap_imode(paint->alpha, subpixel) = subpixel;

	paint->npts = 0;
	ipaint_set_clip(paint, NULL);

	return 0;
}

ipaint_t *ipaint_create(IBITMAP *image)
{
	ipaint_t *paint;
	paint = (ipaint_t*)malloc(sizeof(ipaint_t));
	if (paint == NULL) return NULL;
	ipaint_init(paint);
	if (ipaint_set_image(paint, image) != 0) {
		free(paint);
		return NULL;
	}
	return paint;
}

void ipaint_destroy(ipaint_t *paint)
{
	if (paint->alpha) ibitmap_release(paint->alpha);
	paint->alpha = NULL;
	cvector_destroy(&paint->scratch);
	cvector_destroy(&paint->points);
	cvector_destroy(&paint->pointf);
}


static inline int ipaint_point_push(ipaint_t *paint, double x, double y)
{
	long size = sizeof(ipixel_point_t) * (paint->npts + 1);
	if (size > (long)paint->points.size) {
		if (size < 256) size = 256;
		if (cvector_resize(&paint->points, size) != 0)
			return -1;
		paint->pts = (ipixel_point_t*)paint->points.data;
	}
	paint->pts[paint->npts].x = x;
	paint->pts[paint->npts].y = y;
	paint->npts++;
	return 0;
}

int ipaint_point_append(ipaint_t *paint, const ipixel_point_t *pts, int n)
{
	long size = sizeof(ipixel_point_t) * (n + paint->npts);
	if (size > (long)paint->points.size) {
		if (cvector_resize(&paint->points, size) != 0)
			return -1;
		paint->pts = (ipixel_point_t*)paint->points.data;
	}
	memcpy(paint->pts + paint->npts, pts, n * sizeof(ipixel_point_t));
	paint->npts += n;
	return 0;
}

void ipaint_point_reset(ipaint_t *paint)
{
	paint->npts = 0;
}


static int ipaint_point_convert(ipaint_t *paint)
{
	ipixel_point_fixed_t *dst = (ipixel_point_fixed_t*)paint->pointf.data;
	ipixel_point_t *src = (ipixel_point_t*)paint->points.data;
	int npts = paint->npts;
	int i;

	for (i = 0; i < npts; i++) {
		dst->x = cfixed_from_double(src->x);
		dst->y = cfixed_from_double(src->y);
		dst++;
		src++;
	}

	return 0;
}

static ipixel_point_fixed_t *ipaint_point_to_fixed(ipaint_t *paint)
{
	ipixel_point_fixed_t *pts;
	long size;

	if (paint->npts == 0) 
		return NULL;

	size = sizeof(ipixel_point_fixed_t) * paint->npts;

	if (size > (long)paint->pointf.size) {
		if (size < 256) size = 256;
		if (cvector_resize(&paint->pointf, size) != 0)
			return NULL;
	}

	pts = (ipixel_point_fixed_t*)paint->pointf.data;

	if (ipaint_point_convert(paint) != 0)
		return NULL;

	return pts;
}

int ipaint_draw_primitive(ipaint_t *paint)
{
	ipixel_point_fixed_t *pts;
	int retval;

	if (paint->npts < 3) return -10;
	pts = ipaint_point_to_fixed(paint);

	if (pts == NULL) return -20;

	retval = ipixel_render_polygon(paint->image, pts, paint->npts, 
		paint->alpha, paint->color, paint->additive, &paint->clip, 
		&paint->scratch);

	return retval;
}

int ipaint_draw_traps(ipaint_t *paint, ipixel_trapezoid_t *traps, int ntraps)
{
	int retval;
	retval = ipixel_render_traps(paint->image, traps, ntraps, paint->alpha,
		paint->color, paint->additive, &paint->clip, &paint->scratch);
	return retval;
}


void ipaint_set_color(ipaint_t *paint, IUINT32 color)
{
	paint->color = color;
}

void ipaint_set_clip(ipaint_t *paint, const IRECT *clip)
{
	if (clip != NULL) {
		IRECT size;
		ipixel_rect_set(&size, 0, 0, (int)paint->image->w, (int)paint->image->h);
		ipixel_rect_intersection(&size, clip);
		paint->clip = size;
	}	else {
		paint->clip.left = 0;
		paint->clip.top = 0;
		paint->clip.right = (int)paint->image->w;
		paint->clip.bottom = (int)paint->image->h;
	}
}

void ipaint_text_color(ipaint_t *paint, IUINT32 color)
{
	paint->text_color = color;
}

void ipaint_text_background(ipaint_t *paint, IUINT32 color)
{
	paint->text_backgrnd = color;
}

void ipaint_anti_aliasing(ipaint_t *paint, int level)
{
	if (level < 0) level = 0;
	if (level > 2) level = 2;
	switch (level)
	{
	case 0:
		ibitmap_imode(paint->alpha, subpixel) = IPIXEL_SUBPIXEL_1;
		break;
	case 1:
		ibitmap_imode(paint->alpha, subpixel) = IPIXEL_SUBPIXEL_4;
		break;
	case 2:
		ibitmap_imode(paint->alpha, subpixel) = IPIXEL_SUBPIXEL_8;
		break;
	}
}

int ipaint_draw_polygon(ipaint_t *paint, const ipixel_point_t *pts, int n)
{
	ipaint_point_reset(paint);
	if (ipaint_point_append(paint, pts, n) != 0)
		return -100;
	return ipaint_draw_primitive(paint);
}

#include <stdio.h>

int ipaint_draw_line(ipaint_t *paint, double x1, double y1, double x2,
	double y2)
{
	double dx, dy, dist, nx, ny, hx, hy, half;
	ipaint_point_reset(paint);

	dx = x2 - x1;
	dy = y2 - y1;

	dist = sqrt(dx * dx + dy * dy);
	if (dist == 0.0) return -1;
	if (paint->line_width <= 0.01) return -1;

	dist = 1.0 / dist;
	nx = dx * dist;
	ny = dy * dist;
	hx = -ny * paint->line_width * 0.5;
	hy = nx * paint->line_width * 0.5;
	half = paint->line_width * 0.5;
	
	if (paint->line_width < 1.5) {
		ipaint_point_push(paint, x1 - hx, y1 - hy);
		ipaint_point_push(paint, x1 + hx, y1 + hy);
		ipaint_point_push(paint, x2 + hx, y2 + hy);
		ipaint_point_push(paint, x2 - hx, y2 - hy);
	}	else {
		int count = (int)half + 1;
		ipixel_point_t *pts;
		double theta, start;
		long size;
		int i;
		if (count > 8) count = 8;
		if (half > 30.0) {
			if (half < 70.0) count = 24;
			else if (half < 150) count = 48;
			else if (half < 250) count = 60;
			else count = 70;
		}
		size = count * sizeof(ipixel_point_t);
		if (size > (long)paint->scratch.size) {
			if (cvector_resize(&paint->scratch, size) != 0)
				return -1;
		}
		pts = (ipixel_point_t*)paint->scratch.data;

		ipaint_point_push(paint, x1 + hx, y1 + hy);

		start = atan2(hy, hx);
		theta = 3.1415926535 / (count + 2);
		start += theta;

		//printf("
		for (i = 0; i < count; i++) {
			double cx = cos(start) * half;
			double cy = sin(start) * half;
			ipaint_point_push(paint, x1 + cx, y1 + cy);
			pts[i].x = -cx;
			pts[i].y = -cy;
			start += theta;
		}

		ipaint_point_push(paint, x1 - hx, y1 - hy);
		ipaint_point_push(paint, x2 - hx, y2 - hy);

		for (i = 0; i < count; i++) {
			ipaint_point_push(paint, x2 + pts[i].x, y2 + pts[i].y);
		}

		ipaint_point_push(paint, x2 + hx, y2 + hy);
	}

	ipaint_draw_primitive(paint);

	return 0;
}


void ipaint_line_width(ipaint_t *paint, double width)
{
	paint->line_width = width;
}

int ipaint_draw_ellipse(ipaint_t *paint, double x, double y, double rx,
	double ry)
{
	int rm = (int)((rx > ry)? rx : ry) + 1;
	ipixel_trapezoid_t *traps;
	int count, ntraps, i;
	double theta, dt;
	long size;

	if (rm < 2) count = 4;
	else if (rm < 10) count = 5;
	else if (rm < 50) count = 6;
	else if (rm < 100) count = 7;
	else if (rm < 200) count = 10;
	else count = rm / 100;

	ntraps = (count + 1) * 2;
	size = ntraps * sizeof(ipixel_trapezoid_t);

	if (size > (long)paint->pointf.size) {
		if (cvector_resize(&paint->pointf, size) != 0)
			return -100;
	}

	traps = (ipixel_trapezoid_t*)paint->pointf.data;

	dt = 3.141592653589793 * 0.5 / count;
	theta = 3.141592653589793 * 0.5;

	for (i = 0, ntraps = 0; i < count; i++) {
		double xt, yt, xb, yb;
		ipixel_trapezoid_t t;
		if (i == 0) {
			xt = 0;
			yt = ry;
		}	else {
			xt = rx * cos(theta);
			yt = ry * sin(theta);
		}
		if (i == count - 1) {
			xb = rx;
			yb = 0;
		}	else {
			xb = rx * cos(theta - dt);
			yb = ry * sin(theta - dt);
		}

		theta -= dt;

		t.top = cfixed_from_double(y - yt);
		t.bottom = cfixed_from_double(y - yb);
		t.left.p1.x = cfixed_from_double(x - xt);
		t.left.p1.y = t.top;
		t.right.p1.x = cfixed_from_double(x + xt);
		t.right.p1.y = t.top;
		t.left.p2.x = cfixed_from_double(x - xb);
		t.left.p2.y = t.bottom;
		t.right.p2.x = cfixed_from_double(x + xb);
		t.right.p2.y = t.bottom;
		traps[ntraps++] = t;

		t.top = cfixed_from_double(y + yb);
		t.bottom = cfixed_from_double(y + yt);
		t.left.p1.x = cfixed_from_double(x - xb);
		t.left.p1.y = t.top;
		t.right.p1.x = cfixed_from_double(x + xb);
		t.right.p1.y = t.top;
		t.left.p2.x = cfixed_from_double(x - xt);
		t.left.p2.y = t.bottom;
		t.right.p2.x = cfixed_from_double(x + xt);
		t.right.p2.y = t.bottom;
		traps[ntraps++] = t;
	}
	
	return ipaint_draw_traps(paint, traps, ntraps);
}

int ipaint_draw_circle(ipaint_t *paint, double x, double y, double r)
{
	return ipaint_draw_ellipse(paint, x, y, r, r);
}

void ipaint_fill(ipaint_t *paint, const IRECT *rect, IUINT32 color)
{
	IRECT box;
	if (rect == NULL) {
		box.left = 0;
		box.top = 0;
		box.right = (int)paint->image->w;
		box.bottom = (int)paint->image->h;
	}	else {
		box = *rect;
	}
	ipixel_rect_intersection(&box, &paint->clip);
	ibitmap_rectfill(paint->image, box.left, box.top, box.right - box.left,
		box.bottom - box.top, color);
}

void ipaint_cprintf(ipaint_t *paint, int x, int y, const char *fmt, ...)
{
	char buffer[4096];
	va_list argptr;
	va_start(argptr, fmt);
	vsprintf(buffer, fmt, argptr);
	va_end(argptr);
	ibitmap_draw_text(paint->image, x, y, buffer, &paint->clip,
		paint->text_color, paint->text_backgrnd, paint->additive);
}


void ipaint_sprintf(ipaint_t *paint, int x, int y, const char *fmt, ...)
{
	char buffer[4096];
	va_list argptr;
	va_start(argptr, fmt);
	vsprintf(buffer, fmt, argptr);
	va_end(argptr);
	ibitmap_draw_text(paint->image, x + 1, y + 1, buffer, &paint->clip,
		0x80000000, 0, paint->additive);
	ibitmap_draw_text(paint->image, x, y, buffer, &paint->clip,
		paint->text_color, 0, paint->additive);
}


int ipaint_raster(ipaint_t *paint, const ipixel_point_t *pts, 
	const IBITMAP *image, const IRECT *rect, IUINT32 color, int flag)
{
	return ibitmap_raster_float(paint->image, pts, image, rect, color, 
		flag, &paint->clip);
}


int ipaint_raster_draw(ipaint_t *paint, double x, double y, const IBITMAP *src,
	const IRECT *rect, double off_x, double off_y, double scale_x, 
	double scale_y, double angle, IUINT32 color)
{
	return ibitmap_raster_draw(paint->image, x, y, src, rect, off_x, off_y,
		scale_x, scale_y, angle, color, &paint->clip);
}

int ipaint_raster_draw_3d(ipaint_t *paint, double x, double y, double z, 
	const IBITMAP *src, const IRECT *rect, double off_x, double off_y,
	double scale_x, double scale_y, double angle_x, double angle_y,
	double angle_z, IUINT32 color)
{
	return ibitmap_raster_draw_3d(paint->image, x, y, z, src, rect, off_x, 
		off_y, scale_x, scale_y, angle_x, angle_y, angle_z, color,
		&paint->clip);
}

int ipaint_draw(ipaint_t *paint, int x, int y, const IBITMAP *src, 
	const IRECT *bound, IUINT32 color, int flags)
{
	int sx, sy, sw, sh;
	if (bound == NULL) {
		sx = sy = 0;
		sw = (int)src->w;
		sh = (int)src->h;
	}	else {
		sx = bound->left;
		sy = bound->top;
		sw = bound->right - bound->left;
		sh = bound->bottom - bound->top;
	}
	ibitmap_blend(paint->image, x, y, src, sx, sy, sw, sh,
		color, &paint->clip, flags);
	return 0;
}

