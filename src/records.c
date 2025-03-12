#include "records.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#define ARRAYSIZE(x) (sizeof(x) / sizeof(x[0]))

// Parameters.
static struct
{
	ConvMode mode;
	const char *outname;
	bool bundle;
} s_param;

// REF data
static uint8_t *s_ref_dat;
static int s_ref_count = 0;

// FRM data
static uint8_t *s_frm_dat;
static uint32_t s_frm_offs = 0;

// PCG Data
static uint8_t *s_pcg_dat;  // Allocated to the max sprite count.
static int s_pcg_count = 0;

// PAL data
static uint16_t s_pal_dat[16];

int record_get_pcg_count(void)
{
	return s_pcg_count;
}

int record_get_frm_offs(void)
{
	return s_frm_offs;
}

int record_get_ref_count(void)
{
	return s_ref_count;
}

//
// Motorola 68000, and therefore XSP, uses big-endian data.
//

static void set_uint16be(uint8_t *buf, uint16_t val)
{
	buf[0] = (val >> 8) & 0xFF;
	buf[1] = val & 0xFF;
}

static void set_int16be(uint8_t *buf, int16_t val)
{
	buf[0] = (val >> 8) & 0xFF;
	buf[1] = val & 0xFF;
}

static void set_uint32be(uint8_t *buf, uint32_t val)
{
	set_uint16be(buf, (val >> 16) & 0xFFFF);
	set_uint16be(buf + 2, val & 0xFFFF);
}

//
// Init
//

bool record_init(const char *outname, ConvMode mode, bool bundle)
{
	s_pcg_count = 0;
	s_frm_offs = 0;
	s_ref_count = 0;

	s_param.mode = mode;
	s_param.outname = outname;
	s_param.bundle = bundle;

	// File buffers
	s_pcg_dat = malloc(128 * PCG_PT_MAX_COUNT);
	if (!s_pcg_dat)
	{
		printf("Couldn't allocate PCG data buffer.\n");
		return false;
	}

	s_ref_dat = malloc(8 * PCG_REF_MAX_COUNT);
	if (!s_ref_dat)
	{
		printf("Couldn't allocate REF data buffer.\n");
		free(s_pcg_dat);
		return false;
	}

	s_frm_dat = malloc(PCG_FRM_MAX_COUNT);
	if (!s_frm_dat)
	{
		printf("Couldn't allocate FRM data buffer.\n");
		free(s_pcg_dat);
		free(s_ref_dat);
		return false;
	}

	return true;
}

bool record_complete(void)
{
	bool ret = false;
	if (s_pcg_dat <= 0)
	{
		printf("No PCG data to write.\n");
		goto done;
	}

	char fname_buffer[256];
	if (s_param.bundle)
	{
		snprintf(fname_buffer, sizeof(fname_buffer), "%s.xsb", s_param.outname);
		FILE *f = fopen(fname_buffer, "wb");
		if (!f) goto fwberror;

		XSBHeader header;
		// Header fields have their endianness reversed for 68000 use.
		set_uint16be((uint8_t *)&header.type, (s_param.mode == CONV_MODE_XOBJ) ? 0 : 1);
		set_uint16be((uint8_t *)&header.ref_count, s_ref_count);
		set_uint16be((uint8_t *)&header.frm_bytes, s_frm_offs);
		set_uint16be((uint8_t *)&header.pcg_count, s_pcg_count);
		for (int i = 0; i < 16; i++)
		{
			set_uint16be((uint8_t *)&header.pal[i], s_pal_dat[i]);
		}
		const uint32_t ref_offs = sizeof(XSBHeader);
		const uint32_t frm_offs = ref_offs + 8 * s_ref_count;
		const uint32_t pcg_offs = frm_offs + s_frm_offs;
		set_uint32be((uint8_t *)&header.ref_offs, ref_offs);
		set_uint32be((uint8_t *)&header.frm_offs, frm_offs);
		set_uint32be((uint8_t *)&header.pcg_offs, pcg_offs);
		fwrite(&header, sizeof(header), 1, f);

		// Data blobs are written as-is as they already respected endianness.
		if (s_param.mode == CONV_MODE_XOBJ)
		{
			fwrite(s_ref_dat, 8, s_ref_count, f);
			fwrite(s_frm_dat, 1, s_frm_offs, f);
		}
		fwrite(s_pcg_dat, 128, s_pcg_count, f);
		fclose(f);
	}
	else
	{
		snprintf(fname_buffer, sizeof(fname_buffer), (s_param.mode == CONV_MODE_XOBJ) ? "%s.xsp" : "%s.sp", s_param.outname);
		FILE *f = fopen(fname_buffer, "wb");
		if (!f) goto fwberror;
		fwrite(s_pcg_dat, 128, s_pcg_count, f);
		fclose(f);

		snprintf(fname_buffer, sizeof(fname_buffer), "%s.pal", s_param.outname);
		f = fopen(fname_buffer, "wb");
		if (!f) goto fwberror;
		for (int i = 0; i < ARRAYSIZE(s_pal_dat); i++)
		{
			fputc(s_pal_dat[i] >> 8, f);
			fputc(s_pal_dat[i] & 0xFF, f);
		}
		fclose(f);

		if (s_param.mode == CONV_MODE_XOBJ)
		{
			snprintf(fname_buffer, sizeof(fname_buffer), "%s.ref", s_param.outname);
			f = fopen(fname_buffer, "wb");
			if (!f) goto fwberror;
			fwrite(s_ref_dat, 8, s_ref_count, f);
			fclose(f);

			snprintf(fname_buffer, sizeof(fname_buffer), "%s.frm", s_param.outname);
			f = fopen(fname_buffer, "wb");
			if (!f) goto fwberror;
			fwrite(s_frm_dat, 1, s_frm_offs, f);
			fclose(f);
		}
	}
	ret = true;

	goto done;

fwberror:
	ret = false;
	printf("Couldn't open %s for writing.\n", fname_buffer);

done:
	free(s_pcg_dat);
	free(s_ref_dat);
	free(s_frm_dat);
	return ret;
}

//
// Data commit functions
//

// Commits a metasprite to the REF_DAT file.
// sp_count: hardware sprites used in metasprite
// frm_offs: offset within FRM_DAT file for this metasprite
void record_ref_dat(uint16_t sp_count, uint32_t frm_offs)
{
	if (s_ref_count >= PCG_REF_MAX_COUNT) return;
	uint8_t *ref = &s_ref_dat[s_ref_count * 8];
	set_uint16be(ref, sp_count);
	set_uint32be(ref + 2, frm_offs);
	set_uint16be(ref + 6, 0);  // Reserved / padding.
	s_ref_count++;
}

void record_frm_dat(int16_t vx, int16_t vy, int16_t pt, uint16_t rv)
{
	if (s_frm_offs >= PCG_FRM_MAX_COUNT) return;
	uint8_t *frm = &s_frm_dat[s_frm_offs];
	set_int16be(frm, vx);
	set_int16be(frm + 2, vy);
	set_int16be(frm + 4, pt);
	set_uint16be(frm + 6, rv);
//	printf("frm: %04d %04d %04d %04d \t$%04X%04X%04X%04X\n", vx, vy, pt, rv, vx, vy, pt, rv);
	s_frm_offs += 8;
}

// src points to a 128 byte chunk of PCG data
void record_pcg_dat(const uint8_t *src)
{
	if (s_pcg_count >= PCG_PT_MAX_COUNT) return;
	memcpy(&s_pcg_dat[s_pcg_count * 128], src, 128);
//	fwrite(src, 1, 128, sf_pcg_out);
	s_pcg_count++;
}

void record_pal_dat(int idx, uint16_t val)
{
	if (idx >= ARRAYSIZE(s_pal_dat) || idx < 0) return;
	s_pal_dat[idx] = val;
}

// TODO: Consider storing hashes of the tiles alongside this
int record_find_pcg_dat(const uint8_t *src)
{
	for (int i = 0; i < s_pcg_count; i++)
	{
		const uint8_t *candidate = &s_pcg_dat[i * 128];
		if (memcmp(candidate, src, 128) == 0) return i;
	}
	return -1;
}
