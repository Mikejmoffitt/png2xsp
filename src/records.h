// Functions and data related to recording the XSP structures to disk.
#ifndef RECORDS_H
#define RECORDS_H

#include <stdbool.h>
#include <stdint.h>
#include "types.h"

// Prepares file handles/buffers for the files indicated by outname.
// Based on mode, the following file handles will be opened:
// <outname>.xsp or <outname>.sp
// <outname>.frm
// <outname>.ref
// Returns true if initialization was successful.
bool record_init(ConvMode mode, const char *outname);

// Closes file handles and frees buffers.
void record_complete(void);

// Records a REF entry.
void record_ref_dat(uint16_t sp_count, uint32_t frm_offs);

// Records an FRM entry.
void record_frm_dat(int16_t vx, int16_t vy, uint16_t pt, uint16_t rv);

// Records a PCG entry.
// src points to a 128 byte chunk of PCG tile data.
void record_pcg_dat(const uint8_t *src);

// Checks if the PCG data pointed to by src has already been stored in the PCG
// record, and returns the pattern index if so (0-65535). Otherwise, a negative
// values is returned.
// src points to a 128 byte chunk of PCG tile data.
int record_find_pcg_dat(const uint8_t *src);

int record_get_pcg_count(void);
int record_get_frm_offs(void);
int record_get_ref_count(void);

#endif  // RECORDS_H
