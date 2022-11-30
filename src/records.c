#include "records.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// PCG output files and relevant indexing variables.
static FILE *sf_pcg_out = NULL;  // XSP or SP PCG sprite data.
static FILE *sf_frm_out = NULL;  // XSP_FRM_DAT data.
static FILE *sf_ref_out = NULL;  // XPS_REF_DAT data.

static uint8_t *s_pcg_dat;  // Allocated to the max sprite count.
static int s_pcg_count = 0;
static uint32_t s_frm_offs = 0;
static int s_ref_count = 0;

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

bool record_init(ConvMode mode, const char *outname)
{
	s_pcg_count = 0;
	s_frm_offs = 0;
	s_ref_count = 0;

	char fname_buffer[256];

	snprintf(fname_buffer, sizeof(fname_buffer), (mode == CONV_MODE_XOBJ) ? "%s.xsp" : "%s.sp", outname);
	sf_pcg_out = fopen(fname_buffer, "wb");
	if (!sf_pcg_out)
	{
		printf("Couldn't open %s for writing.\n", fname_buffer);
		return false;
	}

	if (mode == CONV_MODE_XOBJ)
	{
		snprintf(fname_buffer, sizeof(fname_buffer), "%s.frm", outname);
		sf_frm_out = fopen(fname_buffer, "wb");
		if (!sf_frm_out)
		{
			printf("Couldn't open %s for writing.\n", fname_buffer);
			fclose(sf_pcg_out);
			return false;
		}

		snprintf(fname_buffer, sizeof(fname_buffer), "%s.ref", outname);
		sf_ref_out = fopen(fname_buffer, "wb");
		if (!sf_frm_out)
		{
			printf("Couldn't open %s for writing.\n", fname_buffer);
			fclose(sf_pcg_out);
			fclose(sf_frm_out);
			return false;
		}
	}

	s_pcg_dat = malloc(128 * PCG_PT_MAX_COUNT);
	if (!s_pcg_dat)
	{
		printf("Couldn't allocate PCG data buffer.\n");
		fclose(sf_pcg_out);
		if (sf_frm_out) fclose(sf_frm_out);
		if (sf_ref_out) fclose(sf_ref_out);
		return false;
	}
	return true;
}

void record_complete(void)
{
	fwrite(s_pcg_dat, 128, s_pcg_count, sf_pcg_out);
	fclose(sf_pcg_out);
	if (sf_frm_out) fclose(sf_frm_out);
	if (sf_ref_out) fclose(sf_ref_out);

	free(s_pcg_dat);
}

// Motorola 68000, and therefore XSP, uses big-endian data.
static void fwrite_uint16be(uint16_t val, FILE *f)
{
	uint8_t buf[2];
	buf[0] = (val >> 8) & 0xFF;
	buf[1] = val & 0xFF;
	fwrite(buf, 1, sizeof(buf), f);
	fflush(f);
}

static void fwrite_int16be(int16_t val, FILE *f)
{
	fputc((val >> 8) & 0xFF, f);
	fputc(val & 0xFF, f);
}

static void fwrite_uint32be(uint32_t val, FILE *f)
{
	fwrite_uint16be((val >> 16) & 0xFFFF, f);
	fwrite_uint16be(val & 0xFFFF, f);
}


// Commits a metasprite to the REF_DAT file.
// sp_count: hardware sprites used in metasprite
// frm_offs: offset within FRM_DAT file for this metasprite
void record_ref_dat(uint16_t sp_count, uint32_t frm_offs)
{
	fwrite_uint16be(sp_count, sf_ref_out);
	fwrite_uint32be(frm_offs, sf_ref_out);
	fwrite_uint16be(0x0000, sf_ref_out);  // Reserved / padding.
	s_ref_count++;
}

void record_frm_dat(int16_t vx, int16_t vy, int16_t pt, uint16_t rv)
{
	fwrite_int16be(vx, sf_frm_out);
	fwrite_int16be(vy, sf_frm_out);
	fwrite_int16be(pt, sf_frm_out);
	fwrite_uint16be(rv, sf_frm_out);
//	printf("frm: %04d %04d %04d %04d \t$%04X%04X%04X%04X\n", vx, vy, pt, rv, vx, vy, pt, rv);
	s_frm_offs += 8;
}

// src points to a 128 byte chunk of PCG data
void record_pcg_dat(const uint8_t *src)
{
	memcpy(&s_pcg_dat[s_pcg_count * 128], src, 128);
//	fwrite(src, 1, 128, sf_pcg_out);
	s_pcg_count++;
}

// TODO: Consider storing hashes of the tiles alongside this and eliminating s_pcg_dat
int record_find_pcg_dat(const uint8_t *src)
{
	for (int i = 0; i < s_pcg_count; i++)
	{
		const uint8_t *candidate = &s_pcg_dat[i * 128];
		if (memcmp(candidate, src, 128) == 0) return i;
	}
	return -1;
}
