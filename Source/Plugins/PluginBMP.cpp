// ==========================================================
// BMP Loader and Writer
//
// Design and implementation by
// - Floris van den Berg (flvdberg@wxs.nl)
// - Markus Loibl (markus.loibl@epost.de)
// - Martin Weber (martweb@gmx.net)
// - Herve Drolon (drolon@infonie.fr)
// - Michal Novotny (michal@etc.cz)
// - Mihail Naydenov (mnaydenov@users.sourceforge.net)
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

static const uint8_t RLE_COMMAND     = 0;
static const uint8_t RLE_ENDOFLINE   = 0;
static const uint8_t RLE_ENDOFBITMAP = 1;
static const uint8_t RLE_DELTA       = 2;

static const uint8_t BI_RGB            = 0;	// compression: none
static const uint8_t BI_RLE8           = 1;	// compression: RLE 8-bit/pixel
static const uint8_t BI_RLE4           = 2;	// compression: RLE 4-bit/pixel
static const uint8_t BI_BITFIELDS      = 3;	// compression: Bit field or Huffman 1D compression for BITMAPCOREHEADER2
static const uint8_t BI_JPEG           = 4;	// compression: JPEG or RLE-24 compression for BITMAPCOREHEADER2
static const uint8_t BI_PNG            = 5;	// compression: PNG
static const uint8_t BI_ALPHABITFIELDS = 6;	// compression: Bit field (this value is valid in Windows CE .NET 4.0 and later)

// ----------------------------------------------------------

#ifdef _WIN32
#pragma pack(push, 1)
#else
#pragma pack(1)
#endif

typedef struct tagBITMAPCOREHEADER {
  uint32_t   bcSize;
  uint16_t    bcWidth;
  uint16_t    bcHeight;
  uint16_t    bcPlanes;
  uint16_t    bcBitCnt;
} BITMAPCOREHEADER, *PBITMAPCOREHEADER; 

typedef struct tagBITMAPINFOOS2_1X_HEADER {
  uint32_t  biSize;
  uint16_t   biWidth;
  uint16_t   biHeight; 
  uint16_t   biPlanes; 
  uint16_t   biBitCount;
} BITMAPINFOOS2_1X_HEADER, *PBITMAPINFOOS2_1X_HEADER; 

typedef struct tagBITMAPFILEHEADER {
  uint16_t    bfType;		//! The file type
  uint32_t   bfSize;		//! The size, in bytes, of the bitmap file
  uint16_t    bfReserved1;	//! Reserved; must be zero
  uint16_t    bfReserved2;	//! Reserved; must be zero
  uint32_t   bfOffBits;	//! The offset, in bytes, from the beginning of the BITMAPFILEHEADER structure to the bitmap bits
} BITMAPFILEHEADER, *PBITMAPFILEHEADER;

#ifdef _WIN32
#pragma pack(pop)
#else
#pragma pack()
#endif

// ==========================================================
// Plugin Interface
// ==========================================================

static int s_format_id;

// ==========================================================
// Internal functions
// ==========================================================

#ifdef FREEIMAGE_BIGENDIAN
static void
SwapInfoHeader(FIBITMAPINFOHEADER *header) {
	SwapLong(&header->biSize);
	SwapLong((uint32_t *)&header->biWidth);
	SwapLong((uint32_t *)&header->biHeight);
	SwapShort(&header->biPlanes);
	SwapShort(&header->biBitCount);
	SwapLong(&header->biCompression);
	SwapLong(&header->biSizeImage);
	SwapLong((uint32_t *)&header->biXPelsPerMeter);
	SwapLong((uint32_t *)&header->biYPelsPerMeter);
	SwapLong(&header->biClrUsed);
	SwapLong(&header->biClrImportant);
}

static void
SwapCoreHeader(BITMAPCOREHEADER *header) {
	SwapLong(&header->bcSize);
	SwapShort(&header->bcWidth);
	SwapShort(&header->bcHeight);
	SwapShort(&header->bcPlanes);
	SwapShort(&header->bcBitCnt);
}

static void
SwapOS21XHeader(BITMAPINFOOS2_1X_HEADER *header) {
	SwapLong(&header->biSize);
	SwapShort(&header->biWidth);
	SwapShort(&header->biHeight);
	SwapShort(&header->biPlanes);
	SwapShort(&header->biBitCount);
}

static void
SwapFileHeader(BITMAPFILEHEADER *header) {
	SwapShort(&header->bfType);
  	SwapLong(&header->bfSize);
  	SwapShort(&header->bfReserved1);
  	SwapShort(&header->bfReserved2);
	SwapLong(&header->bfOffBits);
}
#endif

// --------------------------------------------------------------------------

/**
Load uncompressed image pixels for 1-, 4-, 8-, 16-, 24- and 32-bit dib
@param io FreeImage IO
@param handle FreeImage IO handle
@param dib Image to be loaded 
@param height Image height
@param pitch Image pitch
@param bit_count Image bit-depth (1-, 4-, 8-, 16-, 24- or 32-bit)
@return Returns TRUE if successful, returns FALSE otherwise
*/
static FIBOOL 
LoadPixelData(FreeImageIO *io, fi_handle handle, FIBITMAP *dib, int height, unsigned pitch, unsigned bit_count) {
	unsigned count = 0;

	// Load pixel data
	// NB: height can be < 0 for BMP data
	if (height > 0) {
		count = io->read_proc((void *)FreeImage_GetBits(dib), height * pitch, 1, handle);
		if (count != 1) {
			return FALSE;
		}
	} else {
		int positiveHeight = abs(height);
		for (int c = 0; c < positiveHeight; ++c) {
			count = io->read_proc((void *)FreeImage_GetScanLine(dib, positiveHeight - c - 1), pitch, 1, handle);
			if (count != 1) {
				return FALSE;
			}
		}
	}

	// swap as needed
#ifdef FREEIMAGE_BIGENDIAN
	if (bit_count == 16) {
		for (unsigned y = 0; y < FreeImage_GetHeight(dib); y++) {
			auto *pixel = (uint16_t *)FreeImage_GetScanLine(dib, y);
			for (unsigned x = 0; x < FreeImage_GetWidth(dib); x++) {
				SwapShort(pixel);
				pixel++;
			}
		}
	}
#endif
#if FREEIMAGE_COLORORDER == FREEIMAGE_COLORORDER_RGB
	if (bit_count == 24 || bit_count == 32) {
		for (unsigned y = 0; y < FreeImage_GetHeight(dib); y++) {
			auto *pixel = FreeImage_GetScanLine(dib, y);
			for (unsigned x = 0; x < FreeImage_GetWidth(dib); x++) {
				INPLACESWAP(pixel[0], pixel[2]);
				pixel += (bit_count >> 3);
			}
		}
	}
#endif

	return TRUE;
}

/**
Load image pixels for 4-bit RLE compressed dib
@param io FreeImage IO
@param handle FreeImage IO handle
@param width Image width
@param height Image height
@param dib Image to be loaded 
@return Returns TRUE if successful, returns FALSE otherwise
*/
static FIBOOL 
LoadPixelDataRLE4(FreeImageIO *io, fi_handle handle, int width, int height, FIBITMAP *dib) {
	int status_byte = 0;
	uint8_t second_byte = 0;
	int bits = 0;

	try {
		height = abs(height);

		std::unique_ptr<void, decltype(&free)> safePixels(calloc(static_cast<size_t>(width) * height, sizeof(uint8_t)), &free);
		if (!safePixels) throw(1);
		auto *pixels = static_cast<uint8_t*>(safePixels.get());

		uint8_t *q = pixels;
		uint8_t *end = pixels + static_cast<size_t>(height) * width;

		for (size_t scanline = 0; scanline < height; ) {
			if (q < pixels || q  >= end) {
				break;
			}
			if (io->read_proc(&status_byte, sizeof(uint8_t), 1, handle) != 1) {
				throw(1);
			}
			if (status_byte != 0)	{
				status_byte = (int)MIN((size_t)status_byte, (size_t)(end - q));
				// Encoded mode
				if (io->read_proc(&second_byte, sizeof(uint8_t), 1, handle) != 1) {
					throw(1);
				}
				for (int i = 0; i < status_byte; i++)	{
					*q++=(uint8_t)((i & 0x01) ? (second_byte & 0x0f) : ((second_byte >> 4) & 0x0f));
				}
				bits += status_byte;
			}
			else {
				// Escape mode
				if (io->read_proc(&status_byte, sizeof(uint8_t), 1, handle) != 1) {
					throw(1);
				}
				switch (status_byte) {
					case RLE_ENDOFLINE:
					{
						// End of line
						bits = 0;
						scanline++;
						q = pixels + scanline*width;
					}
					break;

					case RLE_ENDOFBITMAP:
						// End of bitmap
						q = end;
						break;

					case RLE_DELTA:
					{
						// read the delta values

						uint8_t delta_x = 0;
						uint8_t delta_y = 0;

						if (io->read_proc(&delta_x, sizeof(uint8_t), 1, handle) != 1) {
							throw(1);
						}
						if (io->read_proc(&delta_y, sizeof(uint8_t), 1, handle) != 1) {
							throw(1);
						}

						// apply them

						bits += delta_x;
						scanline += delta_y;
						q = pixels + scanline*width+bits;
					}
					break;

					default:
					{
						// Absolute mode
						status_byte = (int)MIN((size_t)status_byte, (size_t)(end - q));
						for (int i = 0; i < status_byte; i++) {
							if ((i & 0x01) == 0) {
								if (io->read_proc(&second_byte, sizeof(uint8_t), 1, handle) != 1) {
									throw(1);
								}
							}
							*q++=(uint8_t)((i & 0x01) ? (second_byte & 0x0f) : ((second_byte >> 4) & 0x0f));
						}
						bits += status_byte;
						// Read pad byte
						if (((status_byte & 0x03) == 1) || ((status_byte & 0x03) == 2)) {
							uint8_t padding = 0;
							if (io->read_proc(&padding, sizeof(uint8_t), 1, handle) != 1) {
								throw(1);
							}
						}
					}
					break;
				}
			}
		}
		
		{
			// Convert to 4-bit
			auto *src = (const uint8_t*)pixels;
			for (int y = 0; y < height; y++) {
				uint8_t *dst = FreeImage_GetScanLine(dib, y);

				FIBOOL hinibble = TRUE;

				for (int cols = 0; cols < width; cols++){
					if (hinibble) {
						dst[cols >> 1] = (src[cols] << 4);
					} else {
						dst[cols >> 1] |= src[cols];
					}

					hinibble = !hinibble;
				}
				src += width;
			}
		}

		return TRUE;

	} catch(int) {
		return FALSE;
	}
}

/**
Load image pixels for 8-bit RLE compressed dib
@param io FreeImage IO
@param handle FreeImage IO handle
@param width Image width
@param height Image height
@param dib Image to be loaded 
@return Returns TRUE if successful, returns FALSE otherwise
*/
static FIBOOL 
LoadPixelDataRLE8(FreeImageIO *io, fi_handle handle, int width, int height, FIBITMAP *dib) {
	uint8_t status_byte = 0;
	uint8_t second_byte = 0;
	int scanline = 0;
	int bits = 0;

	for (;;) {
		if ( io->read_proc(&status_byte, sizeof(uint8_t), 1, handle) != 1) {
			return FALSE;
		}

		switch (status_byte) {
			case RLE_COMMAND :
				if (io->read_proc(&status_byte, sizeof(uint8_t), 1, handle) != 1) {
					return FALSE;
				}

				switch (status_byte) {
					case RLE_ENDOFLINE :
						bits = 0;
						scanline++;
						break;

					case RLE_ENDOFBITMAP :
						return TRUE;

					case RLE_DELTA :
					{
						// read the delta values

						uint8_t delta_x = 0;
						uint8_t delta_y = 0;

						if (io->read_proc(&delta_x, sizeof(uint8_t), 1, handle) != 1) {
							return FALSE;
						}
						if (io->read_proc(&delta_y, sizeof(uint8_t), 1, handle) != 1) {
							return FALSE;
						}

						// apply them

						bits     += delta_x;
						scanline += delta_y;

						break;
					}

					default :
					{
						if (scanline >= abs(height)) {
							return TRUE;
						}

						int count = MIN((int)status_byte, width - bits);

						uint8_t *sline = FreeImage_GetScanLine(dib, scanline);

						if (io->read_proc((void *)(sline + bits), sizeof(uint8_t) * count, 1, handle) != 1) {
							return FALSE;
						}
						
						// align run length to even number of bytes 

						if ((status_byte & 1) == 1) {
							if (io->read_proc(&second_byte, sizeof(uint8_t), 1, handle) != 1) {
								return FALSE;
							}
						}

						bits += status_byte;													

						break;	
					}
				}

				break;

			default :
			{
				if (scanline >= abs(height)) {
					return TRUE;
				}

				int count = MIN((int)status_byte, width - bits);

				uint8_t *sline = FreeImage_GetScanLine(dib, scanline);

				if (io->read_proc(&second_byte, sizeof(uint8_t), 1, handle) != 1) {
					return FALSE;
				}

				for (int i = 0; i < count; i++) {
					*(sline + bits) = second_byte;

					bits++;					
				}

				break;
			}
		}
	}
}

// --------------------------------------------------------------------------

static FIBITMAP *
LoadWindowsBMP(FreeImageIO *io, fi_handle handle, int flags, unsigned bitmap_bits_offset, int type) {
	try {
		FIBOOL header_only = (flags & FIF_LOAD_NOPIXELS) == FIF_LOAD_NOPIXELS;

		// load the info header

		FIBITMAPINFOHEADER bih;

		io->read_proc(&bih, sizeof(FIBITMAPINFOHEADER), 1, handle);
#ifdef FREEIMAGE_BIGENDIAN
		SwapInfoHeader(&bih);
#endif

		// keep some general information about the bitmap

		unsigned used_colors	= bih.biClrUsed;
		const int width				= bih.biWidth;
		const int height				= bih.biHeight;		// WARNING: height can be < 0 => check each call using 'height' as a parameter
		if (width < 0) {
			throw "BMP width is negative";
		}

		const unsigned bit_count		= bih.biBitCount;
		const unsigned compression	= bih.biCompression;
		const unsigned pitch			= CalculatePitch(CalculateLine(width, bit_count));
		std::unique_ptr<FIBITMAP, decltype(&FreeImage_Unload)> dib(nullptr, &FreeImage_Unload);

		switch (bit_count) {
			case 1 :
			case 4 :
			case 8 :
			{
				if ((used_colors == 0) || (used_colors > CalculateUsedPaletteEntries(bit_count))) {
					used_colors = CalculateUsedPaletteEntries(bit_count);
				}
				
				// allocate enough memory to hold the bitmap (header, palette, pixels) and read the palette

				dib.reset(FreeImage_AllocateHeader(header_only, width, height, bit_count));
				if (!dib) {
					throw FI_MSG_ERROR_DIB_MEMORY;
				}

				// set resolution information
				FreeImage_SetDotsPerMeterX(dib.get(), bih.biXPelsPerMeter);
				FreeImage_SetDotsPerMeterY(dib.get(), bih.biYPelsPerMeter);

				// seek to the end of the header (depending on the BMP header version)
				// type == sizeof(BITMAPVxINFOHEADER)
				switch (type) {
					case 40:	// sizeof(BITMAPINFOHEADER) - all Windows versions since Windows 3.0
						break;
					case 52:	// sizeof(BITMAPV2INFOHEADER) (undocumented)
					case 56:	// sizeof(BITMAPV3INFOHEADER) (undocumented)
					case 108:	// sizeof(BITMAPV4HEADER) - all Windows versions since Windows 95/NT4 (not supported)
					case 124:	// sizeof(BITMAPV5HEADER) - Windows 98/2000 and newer (not supported)
						io->seek_proc(handle, (long)(type - sizeof(FIBITMAPINFOHEADER)), SEEK_CUR);
						break;
				}
				
				// load the palette

				io->read_proc(FreeImage_GetPalette(dib.get()), used_colors * sizeof(FIRGBA8), 1, handle);
#if FREEIMAGE_COLORORDER == FREEIMAGE_COLORORDER_RGB
				FIRGBA8 *pal = FreeImage_GetPalette(dib.get());
				for (unsigned int i = 0; i < used_colors; i++) {
					INPLACESWAP(pal[i].red, pal[i].blue);
				}
#endif

				if (header_only) {
					// header only mode
					return dib.release();
				}

				// seek to the actual pixel data.
				// this is needed because sometimes the palette is larger than the entries it contains predicts
				io->seek_proc(handle, bitmap_bits_offset, SEEK_SET);

				// read the pixel data

				switch (compression) {
					case BI_RGB :
						if ( LoadPixelData(io, handle, dib.get(), height, pitch, bit_count) ) {
							return dib.release();
						} else {
							throw "Error encountered while decoding BMP data";
						}
						break;

					case BI_RLE4 :
						if ( LoadPixelDataRLE4(io, handle, width, height, dib.get()) ) {
							return dib.release();
						} else {
							throw "Error encountered while decoding RLE4 BMP data";
						}
						break;

					case BI_RLE8 :
						if ( LoadPixelDataRLE8(io, handle, width, height, dib.get()) ) {
							return dib.release();
						} else {
							throw "Error encountered while decoding RLE8 BMP data";
						}
						break;

					default :
						throw FI_MSG_ERROR_UNSUPPORTED_COMPRESSION;
				}
			}
			break; // 1-, 4-, 8-bit

			case 16 :
			{
				int use_bitfields = 0;
				if (bih.biCompression == BI_BITFIELDS) use_bitfields = 3;
				else if (bih.biCompression == BI_ALPHABITFIELDS) use_bitfields = 4;
				else if (type == 52) use_bitfields = 3;
				else if (type >= 56) use_bitfields = 4;
				
				if (use_bitfields > 0) {
 					uint32_t bitfields[4];
					io->read_proc(bitfields, use_bitfields * sizeof(uint32_t), 1, handle);
					dib.reset(FreeImage_AllocateHeader(header_only, width, height, bit_count, bitfields[0], bitfields[1], bitfields[2]));
				} else {
					dib.reset(FreeImage_AllocateHeader(header_only, width, height, bit_count, FI16_555_RED_MASK, FI16_555_GREEN_MASK, FI16_555_BLUE_MASK));
				}

				if (!dib) {
					throw FI_MSG_ERROR_DIB_MEMORY;
				}

				// set resolution information
				FreeImage_SetDotsPerMeterX(dib.get(), bih.biXPelsPerMeter);
				FreeImage_SetDotsPerMeterY(dib.get(), bih.biYPelsPerMeter);

				if (header_only) {
					// header only mode
					return dib.release();
				}
				
				// seek to the actual pixel data
				io->seek_proc(handle, bitmap_bits_offset, SEEK_SET);

				// load pixel data and swap as needed if OS is Big Endian
				LoadPixelData(io, handle, dib.get(), height, pitch, bit_count);

				return dib.release();
			}
			break; // 16-bit

			case 24 :
			case 32 :
			{
				int use_bitfields = 0;
				if (bih.biCompression == BI_BITFIELDS) use_bitfields = 3;
				else if (bih.biCompression == BI_ALPHABITFIELDS) use_bitfields = 4;
				else if (type == 52) use_bitfields = 3;
				else if (type >= 56) use_bitfields = 4;

 				if (use_bitfields > 0) {
					uint32_t bitfields[4];
					io->read_proc(bitfields, use_bitfields * sizeof(uint32_t), 1, handle);
					dib.reset(FreeImage_AllocateHeader(header_only, width, height, bit_count, bitfields[0], bitfields[1], bitfields[2]));
				} else {
					if ( bit_count == 32 ) {
						dib.reset(FreeImage_AllocateHeader(header_only, width, height, bit_count, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK));
					} else {
						dib.reset(FreeImage_AllocateHeader(header_only, width, height, bit_count, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK));
					}
				}

				if (!dib) {
					throw FI_MSG_ERROR_DIB_MEMORY;
				}

				// set resolution information
				FreeImage_SetDotsPerMeterX(dib.get(), bih.biXPelsPerMeter);
				FreeImage_SetDotsPerMeterY(dib.get(), bih.biYPelsPerMeter);

				if (header_only) {
					// header only mode
					return dib.release();
				}

				// Skip over the optional palette 
				// A 24 or 32 bit DIB may contain a palette for faster color reduction
				// i.e. you can have (FreeImage_GetColorsUsed(dib) > 0)

				// seek to the actual pixel data
				io->seek_proc(handle, bitmap_bits_offset, SEEK_SET);

				// read in the bitmap bits
				// load pixel data and swap as needed if OS is Big Endian
				LoadPixelData(io, handle, dib.get(), height, pitch, bit_count);

				// check if the bitmap contains transparency, if so enable it in the header

				FreeImage_SetTransparent(dib.get(), FreeImage_GetColorType(dib.get()) == FIC_RGBALPHA);

				return dib.release();
			}
			break; // 24-, 32-bit
		}
	} catch(const char *message) {
		if (message) {
			FreeImage_OutputMessageProc(s_format_id, message);
		}
	}

	return nullptr;
}

// --------------------------------------------------------------------------

static FIBITMAP *
LoadOS22XBMP(FreeImageIO *io, fi_handle handle, int flags, unsigned bitmap_bits_offset) {
	try {
		FIBOOL header_only = (flags & FIF_LOAD_NOPIXELS) == FIF_LOAD_NOPIXELS;

		// load the info header

		FIBITMAPINFOHEADER bih;

		io->read_proc(&bih, sizeof(FIBITMAPINFOHEADER), 1, handle);
#ifdef FREEIMAGE_BIGENDIAN
		SwapInfoHeader(&bih);
#endif

		// keep some general information about the bitmap

		unsigned used_colors	= bih.biClrUsed;
		const int width				= bih.biWidth;
		const int height				= bih.biHeight;		// WARNING: height can be < 0 => check each read_proc using 'height' as a parameter
		const unsigned bit_count		= bih.biBitCount;
		const unsigned compression	= bih.biCompression;
		const unsigned pitch			= CalculatePitch(CalculateLine(width, bit_count));
		std::unique_ptr<FIBITMAP, decltype(&FreeImage_Unload)> dib(nullptr, &FreeImage_Unload);

		switch (bit_count) {
			case 1 :
			case 4 :
			case 8 :
			{
				if ((used_colors == 0) || (used_colors > CalculateUsedPaletteEntries(bit_count)))
					used_colors = CalculateUsedPaletteEntries(bit_count);
					
				// allocate enough memory to hold the bitmap (header, palette, pixels) and read the palette

				dib.reset(FreeImage_AllocateHeader(header_only, width, height, bit_count));

				if (!dib) {
					throw FI_MSG_ERROR_DIB_MEMORY;
				}

				// set resolution information
				FreeImage_SetDotsPerMeterX(dib.get(), bih.biXPelsPerMeter);
				FreeImage_SetDotsPerMeterY(dib.get(), bih.biYPelsPerMeter);
				
				// load the palette
				// note that it may contain RGB or RGBA values : we will calculate this
				unsigned pal_size = (bitmap_bits_offset - sizeof(BITMAPFILEHEADER) - bih.biSize) / used_colors; 

				io->seek_proc(handle, sizeof(BITMAPFILEHEADER) + bih.biSize, SEEK_SET);

				FIRGBA8 *pal = FreeImage_GetPalette(dib.get());

				if (pal_size == 4) {
					for (unsigned count = 0; count < used_colors; count++) {
						FILE_BGRA bgra;

						io->read_proc(&bgra, sizeof(FILE_BGRA), 1, handle);
						
						pal[count].red	= bgra.r;
						pal[count].green = bgra.g;
						pal[count].blue	= bgra.b;
					} 
				} else if (pal_size == 3) {
					for (unsigned count = 0; count < used_colors; count++) {
						FILE_BGR bgr;

						io->read_proc(&bgr, sizeof(FILE_BGR), 1, handle);
						
						pal[count].red	= bgr.r;
						pal[count].green = bgr.g;
						pal[count].blue	= bgr.b;
					} 
				}
				
				if (header_only) {
					// header only mode
					return dib.release();
				}

				// seek to the actual pixel data.
				// this is needed because sometimes the palette is larger than the entries it contains predicts

				if (bitmap_bits_offset > (sizeof(BITMAPFILEHEADER) + sizeof(FIBITMAPINFOHEADER) + (used_colors * 3))) {
					io->seek_proc(handle, bitmap_bits_offset, SEEK_SET);
				}

				// read the pixel data

				switch (compression) {
					case BI_RGB :
						// load pixel data 
						LoadPixelData(io, handle, dib.get(), height, pitch, bit_count);						
						return dib.release();

					case BI_RLE4 :
						if ( LoadPixelDataRLE4(io, handle, width, height, dib.get()) ) {
							return dib.release();
						} else {
							throw "Error encountered while decoding RLE4 BMP data";
						}
						break;

					case BI_RLE8 :
						if ( LoadPixelDataRLE8(io, handle, width, height, dib.get()) ) {
							return dib.release();
						} else {
							throw "Error encountered while decoding RLE8 BMP data";
						}
						break;

					default :
						throw FI_MSG_ERROR_UNSUPPORTED_COMPRESSION;
				}
			}

			case 16 :
			{
				if (bih.biCompression == 3) {
					uint32_t bitfields[3];

					io->read_proc(bitfields, 3 * sizeof(uint32_t), 1, handle);

					dib.reset(FreeImage_AllocateHeader(header_only, width, height, bit_count, bitfields[0], bitfields[1], bitfields[2]));
				} else {
					dib.reset(FreeImage_AllocateHeader(header_only, width, height, bit_count, FI16_555_RED_MASK, FI16_555_GREEN_MASK, FI16_555_BLUE_MASK));
				}

				if (!dib) {
					throw FI_MSG_ERROR_DIB_MEMORY;
				}

				// set resolution information
				FreeImage_SetDotsPerMeterX(dib.get(), bih.biXPelsPerMeter);
				FreeImage_SetDotsPerMeterY(dib.get(), bih.biYPelsPerMeter);

				if (header_only) {
					// header only mode
					return dib.release();
				}

				if (bitmap_bits_offset > (sizeof(BITMAPFILEHEADER) + sizeof(FIBITMAPINFOHEADER) + (used_colors * 3))) {
					io->seek_proc(handle, bitmap_bits_offset, SEEK_SET);
				}

				// load pixel data and swap as needed if OS is Big Endian
				LoadPixelData(io, handle, dib.get(), height, pitch, bit_count);

				return dib.release();
			}

			case 24 :
			case 32 :
			{
				if ( bit_count == 32 ) {
					dib.reset(FreeImage_AllocateHeader(header_only, width, height, bit_count, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK));
				} else {
					dib.reset(FreeImage_AllocateHeader(header_only, width, height, bit_count, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK));
				}

				if (!dib) {
					throw FI_MSG_ERROR_DIB_MEMORY;
				}
				
				// set resolution information
				FreeImage_SetDotsPerMeterX(dib.get(), bih.biXPelsPerMeter);
				FreeImage_SetDotsPerMeterY(dib.get(), bih.biYPelsPerMeter);

				if (header_only) {
					// header only mode
					return dib.release();
				}

				// Skip over the optional palette 
				// A 24 or 32 bit DIB may contain a palette for faster color reduction

				if (bitmap_bits_offset > (sizeof(BITMAPFILEHEADER) + sizeof(FIBITMAPINFOHEADER) + (used_colors * 3))) {
					io->seek_proc(handle, bitmap_bits_offset, SEEK_SET);
				}
				
				// read in the bitmap bits
				// load pixel data and swap as needed if OS is Big Endian
				LoadPixelData(io, handle, dib.get(), height, pitch, bit_count);

				// check if the bitmap contains transparency, if so enable it in the header

				FreeImage_SetTransparent(dib.get(), FreeImage_GetColorType(dib.get()) == FIC_RGBALPHA);

				return dib.release();
			}
		}
	} catch(const char *message) {
		FreeImage_OutputMessageProc(s_format_id, message);
	}

	return nullptr;
}

// --------------------------------------------------------------------------

static FIBITMAP *
LoadOS21XBMP(FreeImageIO *io, fi_handle handle, int flags, unsigned bitmap_bits_offset) {
	try {
		FIBOOL header_only = (flags & FIF_LOAD_NOPIXELS) == FIF_LOAD_NOPIXELS;

		BITMAPINFOOS2_1X_HEADER bios2_1x;

		io->read_proc(&bios2_1x, sizeof(BITMAPINFOOS2_1X_HEADER), 1, handle);
#ifdef FREEIMAGE_BIGENDIAN
		SwapOS21XHeader(&bios2_1x);
#endif
		// keep some general information about the bitmap

		unsigned used_colors = 0;
		const unsigned width		= bios2_1x.biWidth;
		const unsigned height		= bios2_1x.biHeight;	// WARNING: height can be < 0 => check each read_proc using 'height' as a parameter
		const unsigned bit_count	= bios2_1x.biBitCount;
		const unsigned pitch		= CalculatePitch(CalculateLine(width, bit_count));
		std::unique_ptr<FIBITMAP, decltype(&FreeImage_Unload)> dib(nullptr, &FreeImage_Unload);

		switch (bit_count) {
			case 1 :
			case 4 :
			case 8 :
			{
				used_colors = CalculateUsedPaletteEntries(bit_count);
				
				// allocate enough memory to hold the bitmap (header, palette, pixels) and read the palette

				dib.reset(FreeImage_AllocateHeader(header_only, width, height, bit_count));

				if (!dib) {
					throw FI_MSG_ERROR_DIB_MEMORY;
				}

				// set resolution information to default values (72 dpi in english units)
				FreeImage_SetDotsPerMeterX(dib.get(), 2835);
				FreeImage_SetDotsPerMeterY(dib.get(), 2835);
				
				// load the palette

				FIRGBA8 *pal = FreeImage_GetPalette(dib.get());

				for (unsigned count = 0; count < used_colors; count++) {
					FILE_BGR bgr;

					io->read_proc(&bgr, sizeof(FILE_BGR), 1, handle);
					
					pal[count].red	= bgr.r;
					pal[count].green = bgr.g;
					pal[count].blue	= bgr.b;
				}
				
				if (header_only) {
					// header only mode
					return dib.release();
				}

				// Skip over the optional palette 
				// A 24 or 32 bit DIB may contain a palette for faster color reduction

				io->seek_proc(handle, bitmap_bits_offset, SEEK_SET);
				
				// read the pixel data

				// load pixel data 
				LoadPixelData(io, handle, dib.get(), height, pitch, bit_count);
						
				return dib.release();
			}

			case 16 :
			{
				dib.reset(FreeImage_AllocateHeader(header_only, width, height, bit_count, FI16_555_RED_MASK, FI16_555_GREEN_MASK, FI16_555_BLUE_MASK));

				if (!dib) {
					throw FI_MSG_ERROR_DIB_MEMORY;						
				}

				// set resolution information to default values (72 dpi in english units)
				FreeImage_SetDotsPerMeterX(dib.get(), 2835);
				FreeImage_SetDotsPerMeterY(dib.get(), 2835);

				if (header_only) {
					// header only mode
					return dib.release();
				}

				// load pixel data and swap as needed if OS is Big Endian
				LoadPixelData(io, handle, dib.get(), height, pitch, bit_count);

				return dib.release();
			}

			case 24 :
			case 32 :
			{
				if (bit_count == 32) {
					dib.reset(FreeImage_AllocateHeader(header_only, width, height, bit_count, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK));
				} else {
					dib.reset(FreeImage_AllocateHeader(header_only, width, height, bit_count, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK));
				}

				if (!dib) {
					throw FI_MSG_ERROR_DIB_MEMORY;						
				}

				// set resolution information to default values (72 dpi in english units)
				FreeImage_SetDotsPerMeterX(dib.get(), 2835);
				FreeImage_SetDotsPerMeterY(dib.get(), 2835);

				if (header_only) {
					// header only mode
					return dib.release();
				}

				// Skip over the optional palette 
				// A 24 or 32 bit DIB may contain a palette for faster color reduction

				// load pixel data and swap as needed if OS is Big Endian
				LoadPixelData(io, handle, dib.get(), height, pitch, bit_count);

				// check if the bitmap contains transparency, if so enable it in the header

				FreeImage_SetTransparent(dib.get(), FreeImage_GetColorType(dib.get()) == FIC_RGBALPHA);

				return dib.release();
			}
		}
	} catch(const char *message) {	
		FreeImage_OutputMessageProc(s_format_id, message);
	}

	return nullptr;
}

// ==========================================================
// Plugin Implementation
// ==========================================================

static const char * DLL_CALLCONV
Format() {
	return "BMP";
}

static const char * DLL_CALLCONV
Description() {
	return "Windows or OS/2 Bitmap";
}

static const char * DLL_CALLCONV
Extension() {
	return "bmp";
}

static const char * DLL_CALLCONV
RegExpr() {
	return "^BM";
}

static const char * DLL_CALLCONV
MimeType() {
	return "image/bmp";
}

static FIBOOL DLL_CALLCONV
Validate(FreeImageIO *io, fi_handle handle) {
	const uint8_t bmp_signature1[] = { 0x42, 0x4D };
	const uint8_t bmp_signature2[] = { 0x42, 0x41 };
	uint8_t signature[2] = { 0, 0 };

	io->read_proc(signature, 1, sizeof(bmp_signature1), handle);

	if (memcmp(bmp_signature1, signature, sizeof(bmp_signature1)) == 0)
		return TRUE;

	if (memcmp(bmp_signature2, signature, sizeof(bmp_signature2)) == 0)
		return TRUE;

	return FALSE;
}

static FIBOOL DLL_CALLCONV
SupportsExportDepth(int depth) {
	return (
			(depth == 1) ||
			(depth == 4) ||
			(depth == 8) ||
			(depth == 16) ||
			(depth == 24) ||
			(depth == 32)
		);
}

static FIBOOL DLL_CALLCONV 
SupportsExportType(FREE_IMAGE_TYPE type) {
	return (type == FIT_BITMAP) ? TRUE : FALSE;
}

static FIBOOL DLL_CALLCONV
SupportsNoPixels() {
	return TRUE;
}

// ----------------------------------------------------------

static FIBITMAP * DLL_CALLCONV
Load(FreeImageIO *io, fi_handle handle, int page, int flags, void *data) {
	if (handle) {
		BITMAPFILEHEADER bitmapfileheader;
		uint32_t type = 0;

		// we use this offset value to make seemingly absolute seeks relative in the file
		
		long offset_in_file = io->tell_proc(handle);

		// read the fileheader

		io->read_proc(&bitmapfileheader, sizeof(BITMAPFILEHEADER), 1, handle);
#ifdef FREEIMAGE_BIGENDIAN
		SwapFileHeader(&bitmapfileheader);
#endif

		// check the signature

		if ((bitmapfileheader.bfType != 0x4D42) && (bitmapfileheader.bfType != 0x4142)) {
			FreeImage_OutputMessageProc(s_format_id, FI_MSG_ERROR_MAGIC_NUMBER);
			return nullptr;
		}

		// read the first byte of the infoheader

		io->read_proc(&type, sizeof(uint32_t), 1, handle);
		io->seek_proc(handle, 0 - (long)sizeof(uint32_t), SEEK_CUR);
#ifdef FREEIMAGE_BIGENDIAN
		SwapLong(&type);
#endif

		// call the appropriate load function for the found bitmap type

		switch (type) {
			case 12:
				// OS/2 and also all Windows versions since Windows 3.0
				return LoadOS21XBMP(io, handle, flags, offset_in_file + bitmapfileheader.bfOffBits);

			case 64:
				// OS/2
				return LoadOS22XBMP(io, handle, flags, offset_in_file + bitmapfileheader.bfOffBits);

			case 40:	// BITMAPINFOHEADER - all Windows versions since Windows 3.0
			case 52:	// BITMAPV2INFOHEADER (undocumented, partially supported)
			case 56:	// BITMAPV3INFOHEADER (undocumented, partially supported)
			case 108:	// BITMAPV4HEADER - all Windows versions since Windows 95/NT4 (partially supported)
			case 124:	// BITMAPV5HEADER - Windows 98/2000 and newer (partially supported)
				return LoadWindowsBMP(io, handle, flags, offset_in_file + bitmapfileheader.bfOffBits, type);

			default:
				break;
		}

		FreeImage_OutputMessageProc(s_format_id, "unknown bmp subtype with id %d", type);
	}

	return nullptr;
}

// ----------------------------------------------------------

/**
Encode a 8-bit source buffer into a 8-bit target buffer using a RLE compression algorithm. 
The size of the target buffer must be equal to the size of the source buffer. 
On return, the function will return the real size of the target buffer, which should be less that or equal to the source buffer size. 
@param target 8-bit Target buffer
@param source 8-bit Source buffer
@param size Source/Target input buffer size
@return Returns the target buffer size
*/
static int
RLEEncodeLine(uint8_t *target, const uint8_t *source, int size) {
	uint8_t buffer[256];
	int buffer_size = 0;
	int target_pos = 0;

	for (int i = 0; i < size; ++i) {
		if ((i < size - 1) && (source[i] == source[i + 1])) {
			// find a solid block of same bytes

			int j = i + 1;
			int jmax = 254 + i;

			while ((j < size - 1) && (j < jmax) && (source[j] == source[j + 1]))
				++j;

			// if the block is larger than 3 bytes, use it
			// else put the data into the larger pool

			if (((j - i) + 1) > 3) {
				// don't forget to write what we already have in the buffer

				switch (buffer_size) {
					case 0 :
						break;

					case RLE_DELTA :
						target[target_pos++] = 1;
						target[target_pos++] = buffer[0];
						target[target_pos++] = 1;
						target[target_pos++] = buffer[1];
						break;

					case RLE_ENDOFBITMAP :
						target[target_pos++] = (uint8_t)buffer_size;
						target[target_pos++] = buffer[0];
						break;

					default :
						target[target_pos++] = RLE_COMMAND;
						target[target_pos++] = (uint8_t)buffer_size;
						memcpy(target + target_pos, buffer, buffer_size);

						// prepare for next run
						
						target_pos += buffer_size;

						if ((buffer_size & 1) == 1)
							target_pos++;

						break;
				}

				// write the continuous data

				target[target_pos++] = (uint8_t)((j - i) + 1);
				target[target_pos++] = source[i];

				buffer_size = 0;
			} else {
				for (int k = 0; k < (j - i) + 1; ++k) {
					buffer[buffer_size++] = source[i + k];

					if (buffer_size == 254) {
						// write what we have

						target[target_pos++] = RLE_COMMAND;
						target[target_pos++] = (uint8_t)buffer_size;
						memcpy(target + target_pos, buffer, buffer_size);

						// prepare for next run

						target_pos += buffer_size;
						buffer_size = 0;
					}
				}
			}

			i = j;
		} else {
			buffer[buffer_size++] = source[i];
		}

		// write the buffer if it's full

		if (buffer_size == 254) {
			target[target_pos++] = RLE_COMMAND;
			target[target_pos++] = (uint8_t)buffer_size;
			memcpy(target + target_pos, buffer, buffer_size);

			// prepare for next run

			target_pos += buffer_size;
			buffer_size = 0;
		}
	}

	// write the last bytes

	switch (buffer_size) {
		case 0 :
			break;

		case RLE_DELTA :
			target[target_pos++] = 1;
			target[target_pos++] = buffer[0];
			target[target_pos++] = 1;
			target[target_pos++] = buffer[1];
			break;

		case RLE_ENDOFBITMAP :
			target[target_pos++] = (uint8_t)buffer_size;
			target[target_pos++] = buffer[0];
			break;

		default :
			target[target_pos++] = RLE_COMMAND;
			target[target_pos++] = (uint8_t)buffer_size;
			memcpy(target + target_pos, buffer, buffer_size);

			// prepare for next run
			
			target_pos += buffer_size;

			if ((buffer_size & 1) == 1)
				target_pos++;

			break;			
	}

	// write the END_OF_LINE marker

	target[target_pos++] = RLE_COMMAND;
	target[target_pos++] = RLE_ENDOFLINE;

	// return the written size

	return target_pos;
}

static FIBOOL DLL_CALLCONV
Save(FreeImageIO *io, FIBITMAP *dib, fi_handle handle, int page, int flags, void *data) {
	if (dib && handle) {
		// write the file header

		const unsigned dst_width = FreeImage_GetWidth(dib);
		const unsigned dst_height = FreeImage_GetHeight(dib);

		// note that the dib may have been created using FreeImage_CreateView
		// we need to recalculate the dst pitch here
		const unsigned dst_bpp = FreeImage_GetBPP(dib);
		const unsigned dst_pitch = CalculatePitch(CalculateLine(dst_width, dst_bpp));

		BITMAPFILEHEADER bitmapfileheader;
		bitmapfileheader.bfType = 0x4D42;
		bitmapfileheader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(FIBITMAPINFOHEADER) + FreeImage_GetColorsUsed(dib) * sizeof(FIRGBA8);
		bitmapfileheader.bfSize = bitmapfileheader.bfOffBits + dst_height * dst_pitch;
		bitmapfileheader.bfReserved1 = 0;
		bitmapfileheader.bfReserved2 = 0;

		// take care of the bit fields data of any

		bool bit_fields = (dst_bpp == 16) ? true : false;

		if (bit_fields) {
			bitmapfileheader.bfSize += 3 * sizeof(uint32_t);
			bitmapfileheader.bfOffBits += 3 * sizeof(uint32_t);
		}

#ifdef FREEIMAGE_BIGENDIAN
		SwapFileHeader(&bitmapfileheader);
#endif
		if (io->write_proc(&bitmapfileheader, sizeof(BITMAPFILEHEADER), 1, handle) != 1) {
			return FALSE;
		}

		// update the bitmap info header

		FIBITMAPINFOHEADER bih;
		memcpy(&bih, FreeImage_GetInfoHeader(dib), sizeof(FIBITMAPINFOHEADER));

		if (bit_fields) {
			bih.biCompression = BI_BITFIELDS;
		}
		else if ((bih.biBitCount == 8) && ((flags & BMP_SAVE_RLE) == BMP_SAVE_RLE)) {
			bih.biCompression = BI_RLE8;
		}
		else {
			bih.biCompression = BI_RGB;
		}

		// write the bitmap info header

#ifdef FREEIMAGE_BIGENDIAN
		SwapInfoHeader(&bih);
#endif
		if (io->write_proc(&bih, sizeof(FIBITMAPINFOHEADER), 1, handle) != 1) {
			return FALSE;
		}

		// write the bit fields when we are dealing with a 16 bit BMP

		if (bit_fields) {
			uint32_t d;

			d = FreeImage_GetRedMask(dib);

			if (io->write_proc(&d, sizeof(uint32_t), 1, handle) != 1) {
				return FALSE;
			}

			d = FreeImage_GetGreenMask(dib);

			if (io->write_proc(&d, sizeof(uint32_t), 1, handle) != 1) {
				return FALSE;
			}

			d = FreeImage_GetBlueMask(dib);

			if (io->write_proc(&d, sizeof(uint32_t), 1, handle) != 1) {
				return FALSE;
			}
		}

		// write the palette

		if (FreeImage_GetPalette(dib)) {
			FIRGBA8 *pal = FreeImage_GetPalette(dib);
			FILE_BGRA bgra;
			for (unsigned i = 0; i < FreeImage_GetColorsUsed(dib); i++ ) {
				bgra.b = pal[i].blue;
				bgra.g = pal[i].green;
				bgra.r = pal[i].red;
				bgra.a = pal[i].alpha;
				if (io->write_proc(&bgra, sizeof(FILE_BGRA), 1, handle) != 1) {
					return FALSE;
				}
			}
		}

		// write the bitmap data... if RLE compression is enable, use it

		if ((dst_bpp == 8) && ((flags & BMP_SAVE_RLE) == BMP_SAVE_RLE)) {
			auto buffer(std::make_unique<uint8_t[]>(dst_pitch * 2));

			for (unsigned i = 0; i < dst_height; ++i) {
				int size = RLEEncodeLine(buffer.get(), FreeImage_GetScanLine(dib, i), FreeImage_GetLine(dib));

				if (io->write_proc(buffer.get(), size, 1, handle) != 1) {
					return FALSE;
				}
			}

			buffer[0] = RLE_COMMAND;
			buffer[1] = RLE_ENDOFBITMAP;

			if (io->write_proc(buffer.get(), 2, 1, handle) != 1) {
				return FALSE;
			}
#ifdef FREEIMAGE_BIGENDIAN
		} else if (bpp == 16) {
			int padding = dst_pitch - dst_width * sizeof(uint16_t);
			uint16_t pad = 0;
			uint16_t pixel;
			for (unsigned y = 0; y < dst_height; y++) {
				uint8_t *line = FreeImage_GetScanLine(dib, y);
				for (unsigned x = 0; x < dst_width; x++) {
					pixel = ((uint16_t *)line)[x];
					SwapShort(&pixel);
					if (io->write_proc(&pixel, sizeof(uint16_t), 1, handle) != 1) {
						return FALSE;
					}
				}
				if (padding != 0) {
					if (io->write_proc(&pad, padding, 1, handle) != 1) {
						return FALSE;				
					}
				}
			}
#endif
#if FREEIMAGE_COLORORDER == FREEIMAGE_COLORORDER_RGB
		} else if (dst_bpp == 24) {
			int padding = dst_pitch - dst_width * sizeof(FILE_BGR);
			uint32_t pad = 0;
			FILE_BGR bgr;
			for (unsigned y = 0; y < dst_height; y++) {
				uint8_t *line = FreeImage_GetScanLine(dib, y);
				for (unsigned x = 0; x < dst_width; x++) {
					const FIRGB8 *triple = ((FIRGB8 *)line)+x;
					bgr.b = triple->blue;
					bgr.g = triple->green;
					bgr.r = triple->red;
					if (io->write_proc(&bgr, sizeof(FILE_BGR), 1, handle) != 1) {
						return FALSE;
					}
				}
				if (padding != 0) {
					if (io->write_proc(&pad, padding, 1, handle) != 1) {
						return FALSE;
					}
				}
			}
		} else if (dst_bpp == 32) {
			FILE_BGRA bgra;
			for (unsigned y = 0; y < dst_height; y++) {
				uint8_t *line = FreeImage_GetScanLine(dib, y);
				for (unsigned x = 0; x < dst_width; x++) {
					const FIRGBA8 *quad = ((FIRGBA8 *)line)+x;
					bgra.b = quad->blue;
					bgra.g = quad->green;
					bgra.r = quad->red;
					bgra.a = quad->alpha;
					if (io->write_proc(&bgra, sizeof(FILE_BGRA), 1, handle) != 1) {
						return FALSE;
					}
				}
			}
#endif
		} 
		else if (FreeImage_GetPitch(dib) == dst_pitch) {
			return (io->write_proc(FreeImage_GetBits(dib), dst_height * dst_pitch, 1, handle) != 1) ? FALSE : TRUE;
		}
		else {
			for (unsigned y = 0; y < dst_height; y++) {
				uint8_t *line = (uint8_t*)FreeImage_GetScanLine(dib, y);
				
				if (io->write_proc(line, dst_pitch, 1, handle) != 1) {
					return FALSE;
				}
			}
		}

		return TRUE;

	} else {
		return FALSE;
	}
}

// ==========================================================
//   Init
// ==========================================================

void DLL_CALLCONV
InitBMP(Plugin *plugin, int format_id) {
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
	plugin->save_proc = Save;
	plugin->validate_proc = Validate;
	plugin->mime_proc = MimeType;
	plugin->supports_export_bpp_proc = SupportsExportDepth;
	plugin->supports_export_type_proc = SupportsExportType;
	plugin->supports_icc_profiles_proc = nullptr;	// not implemented yet;
	plugin->supports_no_pixels_proc = SupportsNoPixels;
}
