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
#include <unistd.h>

#include "lodepng.h"

#include "types.h"
#include "records.h"
#include "util.h"

static void show_usage(const char *prog_name)
{
	printf("Usage: %s input.png <-o output> <-w width> <-h height> [-x xorigin] [-y yorigin] [-b]\n", prog_name);
	printf("-o: Output file path (base)\n");
	printf("    Specifies the base filepath for newly created file(s).\n");
	printf("    For classic XOBJ use, multiple files are created with the\n");
	printf("    extensions XSP/SP, FRM, REF, and PAL.\n");
	printf("    When creating a bundle (see -b), the path is used directly.\n");
	printf("\n");
	printf("-w, -h: Frame dimensions (pixels).\n");
	printf("    Size of one frame within the spritesheet. Must be >= 1.\n");
	printf("    If both parameters are <= 16, SP data is emitted, and\n");
	printf("    REF/FRM data is not necessary.\n");
	printf("\n");
	printf("-x, -y: Frame origin (pixels; center default\n");
	printf("    Specifies the location within the frame to be treated as\n");
	printf("    the center of the sprite. If no argument is specified, the\n");
	printf("    center of a frame is used (frame size / 2).\n");
	printf("    It is also possible to specify edges of the frame using\n");
	printf("    the terms \"top\", \"bottom\", \"left\", and \"right\".\n");
	printf("\n");
	printf("-b: Bundle\n");
	printf("    If bundle is set, then instead of generating a number of\n");
	printf("    files, only a single \"XSB\" bundle is emitted. This is a\n");
	printf("    binary blob with a small header containing metadata and\n");
	printf("    offsets to REF, FRM, and XSP within. This allows for one\n");
	printf("    object set to be loaded from a single file.\n");
	printf("\n");
	printf("Sample usage:\n");
	printf("    %s player.png -w 32 -h 48 -y 40 -o out/PLAYER\n", prog_name);
	printf("\n");
	printf("\"player.png\" is loaded, and these files will be emitted:\n\n");
	printf("    out/PLAYER.XSP  <-- Graphical texture data\n");
	printf("    out/PLAYER.FRM  <-- Frame composition data\n");
	printf("    out/PLAYER.REF  <-- Frame refrence data\n");
	printf("    out/PLAYER.PAL  <-- Palette data (in X68000 color format)\n");
	printf("\n");
	printf("In a similar example, a bundle is generated:\n");
	printf("    %s player.png -w 32 -h 48 -y 40 -b -o out/PLAYER\n",
	       prog_name);
	printf("    out/PLAYER.XSB  <-- Everything\n");
}

// Free after usage. NULL on error.
static uint8_t *load_png_data(const char *fname,
                              unsigned int *png_w, unsigned int *png_h,
                              LodePNGState *state)
{
	uint8_t *png;
	uint8_t *ret;
	// First load the file into memory.
	size_t fsize;
	int error = lodepng_load_file(&png, &fsize, fname);
	if (error)
	{
		printf("LodePNG error %u: %s\n", error, lodepng_error_text(error));
		return NULL;
	}

	// The image is decoded as an 8-bit indexed color PNG; we don't want any
	// conversion to take place.
	lodepng_state_init(state);
	state->info_raw.colortype = LCT_PALETTE;
	state->info_raw.bitdepth = 8;
	error = lodepng_decode(&ret, png_w, png_h, state, png, fsize);
	if (error)
	{
		printf("LodePNG error %u: %s\n", error, lodepng_error_text(error));
		return NULL;
	}

//	printf("Loaded \"%s\": %d x %d\n", fname, *png_w, *png_h);
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
static void chop_sprite(uint8_t *imgdat, int iw, int ih, ConvMode mode,
                        int ox, int oy,
                        int sx, int sy, int sw, int sh)
{
	// Data that gets placed into the ref dat at the end.
	// frm_offs needs to point at the start of the XOBJ_FRM_DAT for this
	// sprite. s_frm_offs will be added for every hardware sprite chopped
	// out from the metasprite data.
	uint16_t sp_count = 0;  // SP count in REF dat
	const uint32_t frm_offs = record_get_frm_offs();

	ox -= PCG_TILE_PX / 2;
	oy -= PCG_TILE_PX / 2;

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
	// render_region(imgdat, iw, ih, sx, sy, sw, sh);

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
				printf("PCG area is full! Cannot record any more tiles.\n");
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
	const char *progname = argv[0];
	if (argc == 1)
	{
		show_usage(progname);
		return 0;
	}
	//
	// Parse user parameters.
	//
	const char *fname = NULL;
	const char *outname = NULL;
	int frame_w = -1;
	int frame_h = -1;
	int origin_x = -1;
	int origin_y = -1;
	bool bundle = false;

	// Parse options.
	int c;
	while ((c = getopt(argc, argv, "?o:w:h:x:y:b")) != -1)
	{
		switch (c)
		{
			case '?':
				show_usage(progname);
				return 0;
			case 'o':
				outname = optarg;
				break;
			case 'w':
				frame_w = strtoul(optarg, NULL, 0);
				break;
			case 'h':
				frame_h = strtoul(optarg, NULL, 0);
				break;
			case 'x':
				if (strcmp("left", optarg) == 0) origin_x = 0;  // min
				else if (strcmp("right", optarg) == 0) origin_x = 65535;  // max
				else origin_x = strtoul(optarg, NULL, 0);
				break;
			case 'y':
				if (strcmp("top", optarg) == 0) origin_y = 0;  // min
				else if (strcmp("bottom", optarg) == 0) origin_y = 65535;  // max
				else origin_y = strtoul(optarg, NULL, 0);
				break;
			case 'b':
				bundle = true;
				break;
		}
	}

	// Non-opt arguments
	for (int i = optind; i < argc; i++)
	{
		fname = argv[i];
		break;
	}

	//
	// Check argument sanity
	//

	if (frame_w <= 0 || frame_h <= 0)
	{
		printf("Frame width and height parameters must be >= 0 (have %d x %d)\n",
		       frame_w, frame_h);
		return -1;
	}
	if (!outname)
	{
		printf("Output file name must be specified.\n");
		return -1;
	}

	if (!fname)
	{
		printf("Input file name must be specified.\n");
		return -1;
	}

	// Default to center origin.
	if (origin_x < 0) origin_x = frame_w / 2;
	if (origin_y < 0) origin_y = frame_h / 2;
	if (origin_x > frame_w) origin_x = frame_w;
	if (origin_y > frame_w) origin_y = frame_h;

	const ConvMode mode = (frame_w <= PCG_TILE_PX && frame_h <= PCG_TILE_PX) ?
	                      CONV_MODE_SP : CONV_MODE_XOBJ;

	const char *modestr = (mode == CONV_MODE_XOBJ) ? "XSP" : "SP";
	printf("Options summary:\n");
	printf("Input: %s\n", fname);
	printf("Frame: %d x %d\n", frame_w, frame_h);
	printf("Origin: %d, %d\n", origin_x, origin_y);
	printf("Mode: %s\n", modestr);
	printf("Bundle: %s\n", bundle ? "Yes" : "No");
	printf("Output: \"%s\"\n", outname);
	if (bundle)
	{
		printf("--> %s.XSB\n", outname);
	}
	else
	{
		printf("--> %s.%s\n", outname, modestr);
		printf("--> %s.FRM\n", outname);
		printf("--> %s.REF\n", outname);
		printf("--> %s.PAL\n", outname);
	}

	//
	// Prepare the PNG image.
	//

	unsigned int png_w = 0;
	unsigned int png_h = 0;
	LodePNGState state;
	uint8_t *imgdat = load_png_data(fname, &png_w, &png_h, &state);
	if (!imgdat) return -1;
	if (frame_w > png_w || frame_h > png_h)
	{
		printf("Frame size (%d x %d) exceed source image (%d x %d)\n",
		       frame_w, frame_h, png_w, png_h);
		goto finished;
	}


	//
	// Generate XSP data.
	//
	if (!record_init(outname, mode, bundle)) goto finished;

	// Chop sprites out of the image data.
	const int sprite_rows = png_h / frame_h;
	const int sprite_columns = png_w / frame_w;
	for (int y = 0; y < sprite_rows; y++)
	{
		for (int x = 0; x < sprite_columns; x++)
		{
			chop_sprite(imgdat, png_w, png_h, mode, origin_x, origin_y,
			            x * frame_w, y * frame_h, frame_w, frame_h);
		}
	}

	printf("\n");
	printf("Conversion complete.\n");
	printf("--------------------\n");
	if (mode == CONV_MODE_SP)
	{
		printf("SP:\t%d\n", record_get_pcg_count());
	}
	else
	{
		printf("XSP:\t%d\n", record_get_pcg_count());
		printf("FRM:\t%d\n", record_get_frm_offs() / 8);
		printf("REF:\t%d\n", record_get_ref_count());
	}
	printf("--------------------\n");

	//
	// Extract the palette.
	//

	// The first index is always transparent, so we just set it to 0.
	record_pal_dat(0, 0);
	for (int i = 1; i < 16; i++)
	{
		// LodePNG palette data is sets of four bytes in RGBA order.
		const int offs = i * 4;
		const uint8_t r = state.info_png.color.palette[offs + 0];
		const uint8_t g = state.info_png.color.palette[offs + 1];
		const uint8_t b = state.info_png.color.palette[offs + 2];
		// Conversion to X68000 RGB555.
		const uint16_t entry = (((r >> 3) & 0x1F) << 6) |
		                       (((g >> 3) & 0x1F) << 11) |
		                       (((b >> 3) & 0x1F) << 1);
		record_pal_dat(i, entry);
	}

	record_complete();

finished:
	free(imgdat);

	return 0;
}
