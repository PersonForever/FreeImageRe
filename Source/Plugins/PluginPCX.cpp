// ==========================================================
// PCX Loader
//
// Design and implementation by
// - Floris van den Berg (flvdberg@wxs.nl)
// - Jani Kajala (janik@remedy.fi)
// - Markus Loibl (markus.loibl@epost.de)
// - Herve Drolon (drolon@infonie.fr)
// - Juergen Riecker (j.riecker@gmx.de)
//
// This file is part of FreeImage 3
//
// COVERED CODE IS PROVIDED UNDER THIS LICENSE ON AN "AS IS" BASIS, WITHOUT WARRANTY
// OF ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING, WITHOUT LIMITATION, WARRANTIES
// THAT THE COVERED CODE IS FREE OF DEFECTS, MERCHANTABLE, FIT FOR A PARTICULAR PURPOSE
// OR NON-INFRINGING. THE ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE OF THE COVERED
// CODE IS WITH YOU. SHOULD ANY COVERED CODE PROVE DEFECTIVE IN ANY RESPECT, YOU (NOT
// THE INITIAL DEVELOPER OR ANY OTHER CONTRIBUTOR) ASSUME THE COST OF ANY NECESSARY
// SERVICING, REPAIR OR CORRECTION. THIS DISCLAIMER OF WARRANTY CONSTITUTES AN ESSENTIAL
// PART OF THIS LICENSE. NO USE OF ANY COVERED CODE IS AUTHORIZED HEREUNDER EXCEPT UNDER
// THIS DISCLAIMER.
//
// Use at your own risk!
// ==========================================================

#include "FreeImage.h"
#include "Utilities.h"

// ----------------------------------------------------------
//   Constants + headers
// ----------------------------------------------------------

#define PCX_IO_BUF_SIZE	2048

// ----------------------------------------------------------

#ifdef _WIN32
#pragma pack(push, 1)
#else
#pragma pack(1)
#endif

/**
PCX header
*/
typedef struct tagPCXHEADER {
	uint8_t  manufacturer;		// Magic number (0x0A = ZSoft Z)
	uint8_t  version;			// Version	0 == 2.5
							//          2 == 2.8 with palette info
							//          3 == 2.8 without palette info
							//          5 == 3.0 with palette info
	uint8_t  encoding;			// Encoding: 0 = uncompressed, 1 = PCX bIsRLE compressed
	uint8_t  bpp;				// Bits per pixel per plane (only 1 or 8)
	uint16_t  window[4];		// left, upper, right,lower pixel coord.
	uint16_t  hdpi;				// Horizontal resolution
	uint16_t  vdpi;				// Vertical resolution
	uint8_t  color_map[48];	// Colormap for 16-color images
	uint8_t  reserved;
	uint8_t  planes;			// Number of planes (1, 3 or 4)
	uint16_t  bytes_per_line;	// Bytes per row (always even)
	uint16_t  palette_info;		// Palette information (1 = color or b&w; 2 = gray scale)
	uint16_t  h_screen_size;
	uint16_t  v_screen_size;
	uint8_t  filler[54];		// Reserved filler
} PCXHEADER;

#ifdef _WIN32
#pragma pack(pop)
#else
#pragma pack()
#endif

// ==========================================================
// Internal functions
// ==========================================================

/**
Try to validate a PCX signature.
Note that a PCX file cannot be trusted by its signature. 
We use other information from the PCX header to improve the trust we have with this file.
@return Returns TRUE if PCX signature is OK, returns FALSE otherwise
*/
static FIBOOL 
pcx_validate(FreeImageIO *io, fi_handle handle) {
	const uint8_t pcx_signature = 0x0A;
	uint8_t signature[4] = { 0, 0, 0, 0 };

	if (io->read_proc(&signature, 1, 4, handle) != 4) {
		return FALSE;
	}
	// magic number (0x0A = ZSoft Z)
	if (signature[0] == pcx_signature) {
		// version
		if (signature[1] <= 5) {
			// encoding
			if ((signature[2] == 0) || (signature[2] == 1)) {
				// bits per pixel per plane
				if ((signature[3] == 1) || (signature[3] == 8)) {
					return TRUE;
				}
			}
		}
	}

	return FALSE;
}

/**
Read either run-length encoded or normal image data

THIS IS HOW RUNTIME LENGTH ENCODING WORKS IN PCX:
1) If the upper 2 bits of a byte are set, the lower 6 bits specify the count for the next byte
2) If the upper 2 bits of the byte are clear, the byte is actual data with a count of 1

Note that a scanline always has an even number of bytes

@param io FreeImage IO
@param handle FreeImage handle
@param buffer
@param length
@param bIsRLE
@param ReadBuf
@param ReadPos
@return
*/
static unsigned
readLine(FreeImageIO *io, fi_handle handle, uint8_t *buffer, unsigned length, FIBOOL bIsRLE, uint8_t * ReadBuf, int &ReadPos) {
	uint8_t count = 0;
	uint8_t value = 0;
	unsigned written = 0;

	if (bIsRLE) {
		// run-length encoded read

		while (length--) {
			if (count == 0) {
				if (ReadPos >= PCX_IO_BUF_SIZE - 1 ) {
					if (ReadPos == PCX_IO_BUF_SIZE - 1) {
						// we still have one uint8_t, copy it to the start pos
						*ReadBuf = ReadBuf[PCX_IO_BUF_SIZE - 1];
						io->read_proc(ReadBuf + 1, 1, PCX_IO_BUF_SIZE - 1, handle);
					} else {
						// read the complete buffer
						io->read_proc(ReadBuf, 1, PCX_IO_BUF_SIZE, handle);
					}

					ReadPos = 0;
				}

				value = ReadBuf[ReadPos++];

				if ((value & 0xC0) == 0xC0) {
					count = value & 0x3F;
					value = ReadBuf[ReadPos++];
				} else {
					count = 1;
				}
			}

			count--;

			buffer[written++] = value;
		}

	} else {
		// normal read

		written = io->read_proc(buffer, length, 1, handle);
	}

	return written;
}

#ifdef FREEIMAGE_BIGENDIAN
static void
SwapHeader(PCXHEADER *header) {
	SwapShort(&header->window[0]);
	SwapShort(&header->window[1]);
	SwapShort(&header->window[2]);
	SwapShort(&header->window[3]);
	SwapShort(&header->hdpi);
	SwapShort(&header->vdpi);
	SwapShort(&header->bytes_per_line);
	SwapShort(&header->palette_info);
	SwapShort(&header->h_screen_size);
	SwapShort(&header->v_screen_size);
}
#endif

// ==========================================================
// Plugin Interface
// ==========================================================

static int s_format_id;

// ==========================================================
// Plugin Implementation
// ==========================================================

/*!
    Returns the format string for the plugin. Each plugin,
	both internal in the DLL and external in a .fip file, must have
	a unique format string to be addressable.
*/

static const char * DLL_CALLCONV
Format() {
	return "PCX";
}

/*!
    Returns a description string for the plugin. Though a
	description is not necessary per-se,
	it is advised to return an unique string in order to tell the
	user what type of bitmaps this plugin will read and/or write.
*/

static const char * DLL_CALLCONV
Description() {
	return "Zsoft Paintbrush";
}

/*!
    Returns a comma separated list of file
	extensions indicating what files this plugin can open. The
	list, being used by FreeImage_GetFIFFromFilename, is usually
	used as a last resort in finding the type of the bitmap we
	are dealing with. Best is to check the first few bytes on
	the low-level bits level first and compare them with a known
	signature . If this fails, FreeImage_GetFIFFromFilename can be
	used.
*/

static const char * DLL_CALLCONV
Extension() {
	return "pcx";
}

/*!
    Returns an (optional) regular expression to help
	software identifying a bitmap type. The
	expression can be applied to the first few bytes (header) of
	the bitmap. FreeImage is not capable of processing regular expression itself,
	but FreeImageQt, the FreeImage Trolltech support library, can. If RegExpr
	returns NULL, FreeImageQt will automatically bypass Trolltech's regular
	expression support and use its internal functions to find the bitmap type.
*/

static const char * DLL_CALLCONV
RegExpr() {
	return nullptr;
}

static const char * DLL_CALLCONV
MimeType() {
	return "image/x-pcx";
}

/*!
    Validates a bitmap by reading the first few bytes
	and comparing them with a known bitmap signature.
	TRUE is returned if the bytes match the signature, FALSE otherwise.
	The Validate function is used by using FreeImage_GetFileType.
	
	Note: a plugin can safely read data any data from the bitmap without seeking back
	to the original entry point; the entry point is stored prior to calling this
	function and restored after.

    Note: because of FreeImage's io redirection support, the header for the bitmap
	must be on the start of the bitmap or at least on a known part in the bitmap. It is
	forbidden to seek to the end of the bitmap or to a point relative to the end of a bitmap,
	because the end of the bitmap is not always known.
*/

static FIBOOL DLL_CALLCONV
Validate(FreeImageIO *io, fi_handle handle) {
	return pcx_validate(io, handle);
}

/*!
    This function is used to 'ask' the plugin if it can write
	a bitmap in a certain bitdepth. Different bitmap types have different
	capabilities, for example not all formats allow writing in palettized mode.
	This function is there provide an uniform interface to the plugin's
	capabilities. SupportsExportDepth returns TRUE if the plugin support writing
	in the asked bitdepth, or FALSE if it doesn't. The function also
	returns FALSE if bitmap saving is not supported by the plugin at all.
*/

static FIBOOL DLL_CALLCONV
SupportsExportDepth(int depth) {
	return FALSE;
}

static FIBOOL DLL_CALLCONV 
SupportsExportType(FREE_IMAGE_TYPE type) {
	return FALSE;
}

static FIBOOL DLL_CALLCONV
SupportsNoPixels() {
	return TRUE;
}

// ----------------------------------------------------------

/*!
    Loads a bitmap into memory. On entry it is assumed that
	the bitmap to be loaded is of the correct type. If the bitmap
	is of an incorrect type, the plugin might not gracefully fail but
	crash or enter an endless loop. It is also assumed that all
	the bitmap data is available at one time. If the bitmap is not complete,
	for example because it is being downloaded while loaded, the plugin
	might also not gracefully fail.

	The Load function has the following parameters:

    The first parameter (FreeImageIO *io) is a structure providing
	function pointers in order to make use of FreeImage's IO redirection. Using
	FreeImage's file i/o functions instead of standard ones it is garantueed
	that all bitmap types, both current and future ones, can be loaded from
	memory, file cabinets, the internet and more. The second parameter (fi_handle handle)
	is a companion of FreeImageIO and can be best compared with the standard FILE* type,
	in a generalized form.

	The third parameter (int page) indicates wether we will be loading a certain page
	in the bitmap or if we will load the default one. This parameter is only used if
	the plugin supports multi-paged bitmaps, e.g. cabinet bitmaps that contain a series
	of images or pages. If the plugin does support multi-paging, the page parameter
	can contain either a number higher or equal to 0 to load a certain page, or -1 to 
	load the default page. If the plugin does not support multi-paging,
	the page parameter is always -1.
	
	The fourth parameter (int flags) manipulates the load function to load a bitmap
	in a certain way. Every plugin has a different flag parameter with different meanings.

	The last parameter (void *data) can contain a special data block used when
	the file is read multi-paged. Because not every plugin supports multi-paging
	not every plugin will use the data parameter and it will be set to NULL.However,
	when the plugin does support multi-paging the parameter contains a pointer to a
	block of data allocated by the Open function.
*/

static FIBITMAP * DLL_CALLCONV
Load(FreeImageIO *io, fi_handle handle, int page, int flags, void *data) {
	uint8_t *bits;			  // Pointer to dib data
	FIRGBA8 *pal;		  // Pointer to dib palette
	FIBOOL bIsRLE;		  // True if the file is run-length encoded

	if (!handle) {
		return nullptr;
	}

	FIBOOL header_only = (flags & FIF_LOAD_NOPIXELS) == FIF_LOAD_NOPIXELS;

	try {
		// check PCX identifier
		// (note: should have been already validated using FreeImage_GetFileType but check again)
		{
			long start_pos = io->tell_proc(handle);
			FIBOOL bValidated = pcx_validate(io, handle);
			io->seek_proc(handle, start_pos, SEEK_SET);
			if (!bValidated) {
				throw FI_MSG_ERROR_MAGIC_NUMBER;
			}
		}
		
		PCXHEADER header;

		// process the header
		if (io->read_proc(&header, sizeof(PCXHEADER), 1, handle) != 1) {
			throw FI_MSG_ERROR_PARSING;
		}
#ifdef FREEIMAGE_BIGENDIAN
		SwapHeader(&header);
#endif

		// process the window
		const uint16_t *window = header.window;	// left, upper, right,lower pixel coord.
		const int left		= window[0];
		const int top		= window[1];
		const int right		= window[2];
		const int bottom	= window[3];

		// check image size
		if ((left >= right) || (top >= bottom)) {
			throw FI_MSG_ERROR_PARSING;
		}

		const unsigned width = right - left + 1;
		const unsigned height = bottom - top + 1;
		const unsigned bitcount = header.bpp * header.planes;

		// allocate a new dib
		std::unique_ptr<FIBITMAP, decltype(&FreeImage_Unload)> dib(nullptr, &FreeImage_Unload);
		switch (bitcount) {
			case 1:
			case 4:
			case 8:
				dib.reset(FreeImage_AllocateHeader(header_only, width, height, bitcount));
				break;
			case 24:
				dib.reset(FreeImage_AllocateHeader(header_only, width, height, bitcount, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK));
				break;
			default:
				throw FI_MSG_ERROR_DIB_MEMORY;
				break;
		}

		// if the dib couldn't be allocated, throw an error
		if (!dib) {
			throw FI_MSG_ERROR_DIB_MEMORY;
		}

		// metrics handling code

		FreeImage_SetDotsPerMeterX(dib.get(), (unsigned) (((float)header.hdpi) / 0.0254000 + 0.5));
		FreeImage_SetDotsPerMeterY(dib.get(), (unsigned) (((float)header.vdpi) / 0.0254000 + 0.5));

		// Set up the palette if needed
		// ----------------------------

		switch (bitcount) {
			case 1:
			{
				pal = FreeImage_GetPalette(dib.get());
				pal[0].red = pal[0].green = pal[0].blue = 0;
				pal[1].red = pal[1].green = pal[1].blue = 255;
				break;
			}

			case 4:
			{
				pal = FreeImage_GetPalette(dib.get());

				uint8_t *pColormap = &header.color_map[0];

				for (int i = 0; i < 16; i++) {
					pal[i].red   = pColormap[0];
					pal[i].green = pColormap[1];
					pal[i].blue  = pColormap[2];
					pColormap += 3;
				}

				break;
			}

			case 8:
			{
				uint8_t palette_id;

				io->seek_proc(handle, -769L, SEEK_END);
				io->read_proc(&palette_id, 1, 1, handle);

				if (palette_id == 0x0C) {

					if (std::unique_ptr<void, decltype(&free)> cmap(malloc(768 * sizeof(uint8_t)), &free); cmap) {
						io->read_proc(cmap.get(), 768, 1, handle);

						pal = FreeImage_GetPalette(dib.get());
						auto *pColormap = static_cast<const uint8_t *>(cmap.get());

						for (int i = 0; i < 256; i++) {
							pal[i].red   = pColormap[0];
							pal[i].green = pColormap[1];
							pal[i].blue  = pColormap[2];
							pColormap += 3;
						}
					}

				}

				// wrong palette ID, perhaps a gray scale is needed ?

				else if (header.palette_info == 2) {
					pal = FreeImage_GetPalette(dib.get());

					for (int i = 0; i < 256; i++) {
						pal[i].red   = (uint8_t)i;
						pal[i].green = (uint8_t)i;
						pal[i].blue  = (uint8_t)i;
					}
				}

				io->seek_proc(handle, (long)sizeof(PCXHEADER), SEEK_SET);
			}
			break;
		}

		if (header_only) {
			// header only mode
			return dib.release();
		}

		// calculate the line length for the PCX and the dib

		// length of raster line in bytes
		const unsigned lineLength = header.bytes_per_line * header.planes;
		// length of dib line (rounded to uint32_t) in bytes
		const unsigned pitch = FreeImage_GetPitch(dib.get());

		// run-length encoding ?

		bIsRLE = (header.encoding == 1) ? TRUE : FALSE;

		// load image data
		// ---------------

		auto line(std::make_unique<uint8_t[]>(lineLength));

		auto ReadBuf(std::make_unique<uint8_t[]>(PCX_IO_BUF_SIZE));

		bits = FreeImage_GetScanLine(dib.get(), height - 1);

		int ReadPos = PCX_IO_BUF_SIZE;

		if ((header.planes == 1) && ((header.bpp == 1) || (header.bpp == 8))) {
			uint8_t skip;
			unsigned written;

			for (unsigned y = 0; y < height; y++) {
				// do a safe copy of the scanline into 'line'
				written = readLine(io, handle, line.get(), lineLength, bIsRLE, ReadBuf.get(), ReadPos);
				// sometimes (already encountered), PCX images can have a lineLength > pitch
				memcpy(bits, line.get(), MIN(pitch, lineLength));

				// skip trailing garbage at the end of the scanline

				for (unsigned count = written; count < lineLength; count++) {
					if (ReadPos < PCX_IO_BUF_SIZE) {
						ReadPos++;
					} else {
						io->read_proc(&skip, sizeof(uint8_t), 1, handle);
					}
				}

				bits -= pitch;
			}
		} else if ((header.planes == 4) && (header.bpp == 1)) {
			uint8_t bit,  mask, skip;
			unsigned index;

			auto buffer(std::make_unique<uint8_t[]>(width));

			for (unsigned y = 0; y < height; y++) {
				unsigned written = readLine(io, handle, line.get(), lineLength, bIsRLE, ReadBuf.get(), ReadPos);

				// build a nibble using the 4 planes

				memset(buffer.get(), 0, width * sizeof(uint8_t));

				for (int plane = 0; plane < 4; plane++) {
					bit = (uint8_t)(1 << plane);

					for (unsigned x = 0; x < width; x++) {
						index = (unsigned)((x / 8) + plane * header.bytes_per_line);
						mask = (uint8_t)(0x80 >> (x & 0x07));
						buffer[x] |= (line[index] & mask) ? bit : 0;
					}
				}

				// then write the dib row

				for (unsigned x = 0; x < width / 2; x++) {
					bits[x] = (buffer[2*x] << 4) | buffer[2*x+1];
				}

				// skip trailing garbage at the end of the scanline

				for (unsigned count = written; count < lineLength; count++) {
					if (ReadPos < PCX_IO_BUF_SIZE) {
						ReadPos++;
					} else {
						io->read_proc(&skip, sizeof(uint8_t), 1, handle);
					}
				}

				bits -= pitch;
			}

		} else if ((header.planes == 3) && (header.bpp == 8)) {
			for (unsigned y = 0; y < height; y++) {
				readLine(io, handle, line.get(), lineLength, bIsRLE, ReadBuf.get(), ReadPos);

				// convert the plane stream to BGR (RRRRGGGGBBBB -> BGRBGRBGRBGR)
				// well, now with the FI_RGBA_x macros, on BIGENDIAN we convert to RGB

				const auto *pLine = line.get();
				unsigned x;

				for (x = 0; x < width; x++) {
					bits[x * 3 + FI_RGBA_RED] = pLine[x];
				}
				pLine += header.bytes_per_line;

				for (x = 0; x < width; x++) {
					bits[x * 3 + FI_RGBA_GREEN] = pLine[x];
				}
				pLine += header.bytes_per_line;

				for (x = 0; x < width; x++) {
					bits[x * 3 + FI_RGBA_BLUE] = pLine[x];
				}
				pLine += header.bytes_per_line;

				bits -= pitch;
			}
		} else {
			throw FI_MSG_ERROR_UNSUPPORTED_FORMAT;
		}

		return dib.release();

	}
	catch (const char *text) {
		FreeImage_OutputMessageProc(s_format_id, text);
	}
	catch (const std::bad_alloc &) {
		FreeImage_OutputMessageProc(s_format_id, FI_MSG_ERROR_MEMORY);
	}

	return nullptr;
}

// ==========================================================
//   Init
// ==========================================================

/*!
    Initialises the plugin. The first parameter (Plugin *plugin)
	contains a pointer to a pre-allocated Plugin structure
	wherein pointers to the available plugin functions
	has to be stored. The second parameter (int format_id) is an identification
	number that the plugin may use to show plugin specific warning messages
	or other information to the user. The plugin number
	is generated by FreeImage and can differ everytime the plugin is
	initialised.

    If you want to create your own plugin you have to take some
	rules into account. Plugin functions have to be compiled
	__stdcall using the multithreaded c runtime libraries. Throwing
	exceptions in plugin functions is allowed, as long as those exceptions
	are being caught inside the same plugin. It is forbidden for a plugin
	function to directly call FreeImage functions or to allocate memory
	and pass it to the main DLL. Exception to this rule is the special file data
	block that may be allocated the Open function. Allocating a FIBITMAP inside a
	plugin can be using the function allocate_proc in the FreeImage structure,
	which will allocate the memory using the DLL's c runtime library.
*/

void DLL_CALLCONV
InitPCX(Plugin *plugin, int format_id) {
	s_format_id = format_id;

	plugin->format_proc = Format;
	plugin->description_proc = Description;
	plugin->extension_proc = Extension;
	plugin->regexpr_proc = RegExpr;
	plugin->open_proc = nullptr;
	plugin->close_proc = nullptr;
	plugin->pagecount_proc = nullptr;
	plugin->pagecapability_proc = nullptr;
	plugin->load_proc = Load;
	plugin->save_proc = nullptr;
	plugin->validate_proc = Validate;
	plugin->mime_proc = MimeType;
	plugin->supports_export_bpp_proc = SupportsExportDepth;
	plugin->supports_export_type_proc = SupportsExportType;
	plugin->supports_icc_profiles_proc = nullptr;
	plugin->supports_no_pixels_proc = SupportsNoPixels;
}
