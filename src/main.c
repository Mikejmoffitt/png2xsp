// png2xsp
// (c) 2022 Michael Moffitt
//
// Utility to convert a spritesheet PNG into data for use with XSP.
// See https://yosshin4004.github.io/x68k/xsp/index.html for information on the
// usage and theory of operation of the XSP library.
//
// Usage:
//
//     png2xsp spritesheet.png frame_width frame_height output_name <origin>
//
// <orgin> is a set of two characters that indicate where (0, 0) lies. It is an
// optional argument; without it, the default is "cc", for centering in X and Y.
//
// Some notes on XSP:
// XSP is initialized with data passed in by the user. This data contains:
// * Sprite PCG texture data (XSP, SP)
// * Metasprite definition data (FRM)
// * List of metasprite definitions (REF)
//
// XSP can either draw a single hardware sprite (SP), or a complex metasprite
// composed of multiple hardware sprites (in XSP parliance, an XOBJ).
//
// To draw a hardware sprite (SP), only texture data is required.
//
// For XOBJ drawing, XSP allows the user to specify what is to be drawn by a
// single pattern number. FRM definitions exist to provide instructions on how
// to compose an XOBJ pattern from multiple hardware sprites. The REF data
// indexes within FRM data to note where definitions start and end for a frame.
//
// Looking at this, you may wonder "Why not have one small set of XSP, FRM, and
// REF files for a single frame, and load them one at a time?" More than
// anything else, the reason is that PCG data may be reused between different
// metasprites. It would be a waste of memory and load time to duplicate tile
// data between multiple frames that barely have any changes in them.
//
// So, in general, drawing or generating an indexed .png of a sprite sheet is
// recommended, as corresponding XSP, FRM, and REF data can be efficiently
// emitted from it.
//
// For this program, rather than require a metadata file that specifies sprite
// size and clipping regions for each one, I've opted for a simple design that
// operates on a fixed sprite size for the whole sheet. The program will omit
// unused space, so feel free to edit enormous sprites that don't use most of
// their frame.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "lodepng.h"

#include "types.h"
#include "records.h"
#include "util.h"

static void show_usage(const char *prog_name)
{
	printf("Usage: %s sprites.png w h outname <o>\n", prog_name);
	printf("      w: Width of sprite within spritesheet (decimal or hex)\n");
	printf("      h: Height of sprite within spritesheet (decimal or hex)\n");
	printf("outname: Base file path and name for output.\n");
	printf("      o: Origin (XY, where both characters form an argument\n");
	printf("         t/l: top/left\n");
	printf("           c: center\n");
	printf("         b/r: bottom/right\n");
	printf("         e.g. \"lt\" uses the left-top as the origin.\n");
	printf("\n");
	printf("Example:\n");
	printf("    %s player.png 32 48 out/player x cb\n", prog_name);
	printf("\n");
	printf("The result is player.png being chopped into a series of 32x48\n");
	printf("XOBJ sprites with the origin at the center-bottom of the frame.\n");
}

static bool check_arg_sanity(int argc, char **argv)
{
	if (argc < 5)
	{
		show_usage(argv[0]);
		return false;
	}

	const int frame_w = strtoul(argv[2], NULL, 0);
	const int frame_h = strtoul(argv[3], NULL, 0);
	if (frame_w < 0 || frame_h < 0)
	{
		printf("Invalid frame size %d x %d\n", frame_w, frame_h);
		return false;
	}
	return true;
}

// Free after usage. NULL on error.
static uint8_t *load_png_data(const char *fname,
                              unsigned int *png_w, unsigned int *png_h)
{
	uint8_t *ret;
	const unsigned int error = lodepng_decode_file(&ret, png_w, png_h,
	                                               fname, LCT_PALETTE, 8);
	if (error)
	{
		printf("LodePNG error %u: %s\n", error, lodepng_error_text(error));
		return NULL;
	}

	printf("Loaded \"%s\": %d x %d\n", fname, *png_w, *png_h);
	return ret;
}

// Hunt top-down, then left-right, for a sprite to clip from imgdat.
// Returns false if imgdat is empty.
static bool claim(const uint8_t *imgdat,
                  int iw, int ih,
                  int sx, int sy, int sw, int sh,
                  int *col, int *row)
{
	// Walk down row by row looking for non-transparent pixel data.
	*row = -1;
	for (int y = sy; y < sy + sh; y++)
	{
		for (int x = sx; x < sx + sw; x++)
		{
			if (imgdat[x + (y * iw)] == 0) continue;
			// Note the row image data was found on, and break out.
			*row = y;
			break;
		}
		// Break out if we are done searching.
		if (*row >= 0) break;
	}
	if (*row < 0) return false;  // We never found a filled row, so it's empty.

	// We have the top row, but we need to scan within a 16x16 block to find a
	// viable sprite chunk to extract.
	// Scan rightwards to find the left edge of the sprite.
	*col = -1;
	for (int x = sx; x < sx + sw; x++)
	{
		// As our test column extends 16px below the starting line, we have to
		// ensure we don't exceed the boundaries of the sprite clipping region
		// or the source image data.
		const int ylim = (*row + PCG_TILE_PX) < (sy + sh) ? (*row + PCG_TILE_PX) : (sy + sh);
		for (int y = *row; y < ylim; y++)
		{
			if (imgdat[x + (y * iw)] == 0) continue;
			// Found it; break out.
			*col = x;
			break;
		}
		// If the column is set, we are done.
		if (*col >= 0) break;
	}
	// Sanity check that something hasn't gone wrong.
	// TODO: Remove?
	if (*col < 0)
	{
		printf("Unexpectedly empty strip from row %d?\n", *row);
		return false;
	}

	return true;
}

// Takes sprite data from imgdat and generates XSP entry data for it.
// Adds to the PCG, FRM, and REF files as necessary.
static void chop_sprite(uint8_t *imgdat, int iw, int ih, ConvMode mode, ConvOrigin origin,
                        int sx, int sy, int sw, int sh)
{
	// Data that gets placed into the ref dat at the end.
	// frm_offs needs to point at the start of the XOBJ_FRM_DAT for this
	// sprite. s_frm_offs will be added for every hardware sprite chopped
	// out from the metasprite data.
	uint16_t sp_count = 0;  // SP count in REF dat
	const uint32_t frm_offs = record_get_frm_offs();

	int ox, oy;
	origin_for_sp(origin, sw, sh, &ox, &oy);

	// If the sprite area from imgdat isn't empty:
	// 1) Search existing PCG data, see if we have the image data already.
	//    Do check for X and Y mirrored versions as well.
	//    If we already have it,
	//      a) store position in PCG data / 128 to get pattern index
	//      b) record X/Y mirroring if used to place the sprite.
	//    If we don't have it,
	//      a) store PCG count as the pattern index
	//      b) call record_pcg_dat for the sprite data.
	// 1.5) If in SP mode, skip to step 4.
	// 2) Set vx and vy for PCG sprite's position relative to sprite origin.
	//    Mind that hardware sprites use 0,0 for their top-left.
	// 3) Call record_frm_dat with data from above.
	// 4) Erase the 16x16 image data from imgdat (set it to zero)
	// 5) Increment FRM.

	// DEBUG
	// TODO: Verbose #define
//		render_region(imgdat, iw, ih, sx, sy, sw, sh);

	int clip_x, clip_y;
	int last_vx = 0;
	int last_vy = 0;
	// TODO: In SP mode, should we just process the entire image?
	while (claim(imgdat, iw, ih, sx, sy, sw, sh, &clip_x, &clip_y))
	{
		sp_count++;
		uint8_t pcg_data[32 * 4];  // Four 8x8 tiles, row interleaved.
		const int limx = sx + sw;
		const int limy = sy + sh;
		clip_8x8_tile(imgdat, iw, clip_x, clip_y,
		              limx, limy, &pcg_data[32 * 0]);
		clip_8x8_tile(imgdat, iw, clip_x, clip_y + 8,
		              limx, limy, &pcg_data[32 * 1]);
		clip_8x8_tile(imgdat, iw, clip_x + 8, clip_y,
		              limx, limy, &pcg_data[32 * 2]);
		clip_8x8_tile(imgdat, iw, clip_x + 8, clip_y + 8,
		              limx, limy, &pcg_data[32 * 3]);

		// In XOBJ mode, duplicate tiles are removed.
		int pt_idx = (mode == CONV_MODE_XOBJ)
		             ? record_find_pcg_dat(pcg_data)
		             : -1;
		if (pt_idx < 0)
		{
			pt_idx = record_get_pcg_count();
			if (pt_idx >= PCG_PT_MAX_COUNT)
			{
				printf("PCG area is full! cannot record any more tiles.\n");
				return;
			}
			else
			{
				record_pcg_dat(pcg_data);
			}
		}

		if (mode != CONV_MODE_XOBJ) continue;

		const int vx = ((clip_x % sw) - ox);
		const int vy = ((clip_y % sh) - oy);
		record_frm_dat(vx - last_vx, vy - last_vy, pt_idx, 0);

		last_vx = vx;
		last_vy = vy;
	}

	if (mode != CONV_MODE_XOBJ) return;
	record_ref_dat(sp_count, frm_offs);
}

int main(int argc, char **argv)
{
	if (!check_arg_sanity(argc, argv)) return 0;
	
	// User parameters.
	const char *fname = argv[1];
	const int frame_w = strtoul(argv[2], NULL, 0);
	const int frame_h = strtoul(argv[3], NULL, 0);
	const char *outname = argv[4];
	const ConvOrigin origin = conv_origin_from_args(argc, argv);

	// Prepare the PNG image.
	unsigned int png_w = 0;
	unsigned int png_h = 0;
	uint8_t *imgdat = load_png_data(fname, &png_w, &png_h);
	if (!imgdat) return -1;
	if (frame_w > png_w || frame_h > png_h)
	{
		printf("Frame size (%d x %d) exceed source image (%d x %d)\n",
		       frame_w, frame_h, png_w, png_h);
		goto finished;
	}

	// TODO: Make an option for mode?
	const ConvMode mode = CONV_MODE_XOBJ;

	// Set up output handles.
	if (!record_init(mode, outname)) goto finished;

	// Chop sprites out of the image data.
	const int sprite_rows = png_h / frame_h;
	const int sprite_columns = png_w / frame_w;
	for (int y = 0; y < sprite_rows; y++)
	{
		for (int x = 0; x < sprite_columns; x++)
		{
			chop_sprite(imgdat, png_w, png_h, mode, origin,
			            x * frame_w, y * frame_h, frame_w, frame_h);
		}
	}

	if (mode == CONV_MODE_SP)
	{
		printf("%d SP.\n", record_get_pcg_count());
	}
	else
	{
		printf("%d XSP.\n", record_get_pcg_count());
		printf("%d FRM.\n", record_get_frm_offs() / 8);
		printf("%d REF.\n", record_get_ref_count());
	}

	record_complete();

finished:
	free(imgdat);

	return 0;
}
