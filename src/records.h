// Functions and data related to recording the XSP structures to disk.
#ifndef RECORDS_H
#define RECORDS_H

#include <stdbool.h>
#include <stdint.h>
#include "types.h"

#define PCG_PT_MAX_COUNT 32768
#define PCG_REF_MAX_COUNT (32768/8)
#define PCG_FRM_MAX_COUNT 32768

typedef struct XSBHeader
{
	uint16_t type;
	uint16_t ref_count;
	uint16_t frm_bytes;
	uint16_t pcg_count;
	uint16_t pal[16];
	uint32_t ref_offs;
	uint32_t frm_offs;
	uint32_t pcg_offs;
} XSBHeader;

// Prepares file handles/buffers for the files indicated by outname.
// If not bundling:
// <outname>.xsp or <outname>.sp depending on mode
// <outname>.frm (XSP only)
// <outname>.ref (XSP only)
//
// If bundling:
// <outname>.xsb for consumption by XSPman. See XSBHeader type
// Returns true if initialization was successful.
bool record_init(const char *outname, ConvMode mode, bool bundle);

// Commits file data and frees buffers.
bool record_complete(void);

// Records a REF entry.
void record_ref_dat(uint16_t sp_count, uint32_t frm_offs);

// Records an FRM entry.
void record_frm_dat(int16_t vx, int16_t vy, int16_t pt, uint16_t rv);

// Records a PCG entry.
// src points to a 128 byte chunk of PCG tile data.
void record_pcg_dat(const uint8_t *src);

// Sets the palette.
void record_pal_dat(int idx, uint16_t val);

// Checks if the PCG data pointed to by src has already been stored in the PCG
// record, and returns the pattern index if so (0-65535). Otherwise, a negative
// values is returned.
// src points to a 128 byte chunk of PCG tile data.
int record_find_pcg_dat(const uint8_t *src);

int record_get_pcg_count(void);
int record_get_frm_offs(void);
int record_get_ref_count(void);

#endif  // RECORDS_H
