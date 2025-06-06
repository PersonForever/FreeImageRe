// ==========================================================
// GIF Loader and Writer
//
// Design and implementation by
// - Ryan Rubley <ryan@lostreality.org>
// - Raphael Gaquer <raphael.gaquer@alcer.com>
// - Aaron Shumate <aaron@shumate.us>
//
// References
// http://www.w3.org/Graphics/GIF/spec-gif87.txt
// http://www.w3.org/Graphics/GIF/spec-gif89a.txt
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

#ifdef _MSC_VER 
#pragma warning (disable : 4786) // identifier was truncated to 'number' characters
#endif

#include "FreeImage.h"
#include "Utilities.h"
#include "../Metadata/FreeImageTag.h"

// ==========================================================
//   Metadata declarations
// ==========================================================

#define GIF_DISPOSAL_UNSPECIFIED	0
#define GIF_DISPOSAL_LEAVE			1
#define GIF_DISPOSAL_BACKGROUND		2
#define GIF_DISPOSAL_PREVIOUS		3

// ==========================================================
//   Constant/Typedef declarations
// ==========================================================


struct GIFinfo {
	FIBOOL read;
	//only really used when reading
	size_t global_color_table_offset;
	int global_color_table_size;
	uint8_t background_color;
	std::vector<size_t> application_extension_offsets;
	std::vector<size_t> comment_extension_offsets;
	std::vector<size_t> graphic_control_extension_offsets;
	std::vector<size_t> image_descriptor_offsets;

	GIFinfo() : read(0), global_color_table_offset(0), global_color_table_size(0), background_color(0)
	{
	}
};

struct PageInfo {
	PageInfo(int d, int l, int t, int w, int h) { 
		disposal_method = d; left = (uint16_t)l; top = (uint16_t)t; width = (uint16_t)w; height = (uint16_t)h; 
	}
	int disposal_method;
	uint16_t left, top, width, height;
};

//GIF defines a max of 12 bits per code
#define MAX_LZW_CODE			4096

class StringTable
{
public:
	StringTable();
	~StringTable();
	void Initialize(int minCodeSize);
	uint8_t *FillInputBuffer(int len);
	void CompressStart(int bpp, int width);
	int CompressEnd(uint8_t *buf); //0-4 bytes
	bool Compress(uint8_t *buf, int *len);
	bool Decompress(uint8_t *buf, int *len);
	void Done(void);

protected:
	bool m_done;

	int m_minCodeSize, m_clearCode, m_endCode, m_nextCode;

	int m_bpp, m_slack; //Compressor information

	int m_prefix; //Compressor state variable
	int m_codeSize, m_codeMask; //Compressor/Decompressor state variables
	int m_oldCode; //Decompressor state variable
	int m_partial, m_partialSize; //Compressor/Decompressor bit buffer

	int firstPixelPassed; // A specific flag that indicates if the first pixel
	                      // of the whole image had already been read

	std::string m_strings[MAX_LZW_CODE]; //This is what is really the "string table" data for the Decompressor
	int* m_strmap;

	//input buffer
	uint8_t *m_buffer;
	int m_bufferSize, m_bufferRealSize, m_bufferPos, m_bufferShift;

	void ClearCompressorTable(void);
	void ClearDecompressorTable(void);
};

#define GIF_PACKED_LSD_HAVEGCT		0x80
#define GIF_PACKED_LSD_COLORRES		0x70
#define GIF_PACKED_LSD_GCTSORTED	0x08
#define GIF_PACKED_LSD_GCTSIZE		0x07
#define GIF_PACKED_ID_HAVELCT		0x80
#define GIF_PACKED_ID_INTERLACED	0x40
#define GIF_PACKED_ID_LCTSORTED		0x20
#define GIF_PACKED_ID_RESERVED		0x18
#define GIF_PACKED_ID_LCTSIZE		0x07
#define GIF_PACKED_GCE_RESERVED		0xE0
#define GIF_PACKED_GCE_DISPOSAL		0x1C
#define GIF_PACKED_GCE_WAITINPUT	0x02
#define GIF_PACKED_GCE_HAVETRANS	0x01

#define GIF_BLOCK_IMAGE_DESCRIPTOR	0x2C
#define GIF_BLOCK_EXTENSION			0x21
#define GIF_BLOCK_TRAILER			0x3B

#define GIF_EXT_PLAINTEXT			0x01
#define GIF_EXT_GRAPHIC_CONTROL		0xF9
#define GIF_EXT_COMMENT				0xFE
#define GIF_EXT_APPLICATION			0xFF

#define GIF_INTERLACE_PASSES		4
static int g_GifInterlaceOffset[GIF_INTERLACE_PASSES] = {0, 4, 2, 1};
static int g_GifInterlaceIncrement[GIF_INTERLACE_PASSES] = {8, 8, 4, 2};

// ==========================================================
// Helpers Functions
// ==========================================================

static FIBOOL 
FreeImage_SetMetadataEx(FREE_IMAGE_MDMODEL model, FIBITMAP *dib, const char *key, uint16_t id, FREE_IMAGE_MDTYPE type, uint32_t count, uint32_t length, const void *value)
{
	bool bSuccess{};
	if (std::unique_ptr<FITAG, decltype(&FreeImage_DeleteTag)> tag(FreeImage_CreateTag(), &FreeImage_DeleteTag); tag) {
		bSuccess = FreeImage_SetTagKey(tag.get(), key);
		bSuccess = bSuccess && FreeImage_SetTagID(tag.get(), id);
		bSuccess = bSuccess && FreeImage_SetTagType(tag.get(), type);
		bSuccess = bSuccess && FreeImage_SetTagCount(tag.get(), count);
		bSuccess = bSuccess && FreeImage_SetTagLength(tag.get(), length);
		bSuccess = bSuccess && FreeImage_SetTagValue(tag.get(), value);
		if (model == FIMD_ANIMATION) {
			const TagLib& s = TagLib::instance();
			// get the tag description
			const char *description = s.getTagDescription(TagLib::ANIMATION, id);
			bSuccess = bSuccess && FreeImage_SetTagDescription(tag.get(), description);
		}
		// store the tag
		bSuccess = bSuccess && FreeImage_SetMetadata(model, dib, key, tag.get());
	}
	return bSuccess ? TRUE : FALSE;
}

static FIBOOL 
FreeImage_GetMetadataEx(FREE_IMAGE_MDMODEL model, FIBITMAP *dib, const char *key, FREE_IMAGE_MDTYPE type, FITAG **tag)
{
	if (FreeImage_GetMetadata(model, dib, key, tag)) {
		if (FreeImage_GetTagType(*tag) == type) {
			return TRUE;
		}
	}
	return FALSE;
}

StringTable::StringTable()
{
	m_buffer = nullptr;
	firstPixelPassed = 0; // Still no pixel read
	// Maximum number of entries in the map is MAX_LZW_CODE * 256 
	// (aka 2**12 * 2**8 => a 20 bits key)
	// This Map could be optmized to only handle MAX_LZW_CODE * 2**(m_bpp)
	m_strmap = new(std::nothrow) int[1<<20];
}

StringTable::~StringTable()
{
	if (m_buffer) {
		delete [] m_buffer;
	}
	if (m_strmap) {
		delete [] m_strmap;
		m_strmap = nullptr;
	}
}

void StringTable::Initialize(int minCodeSize)
{
	m_done = false;

	m_bpp = 8;
	m_minCodeSize = minCodeSize;
	m_clearCode = 1 << m_minCodeSize;
	if (m_clearCode > MAX_LZW_CODE) {
		m_clearCode = MAX_LZW_CODE;
	}
	m_endCode = m_clearCode + 1;

	m_partial = 0;
	m_partialSize = 0;

	m_bufferSize = 0;
	ClearCompressorTable();
	ClearDecompressorTable();
}

uint8_t *StringTable::FillInputBuffer(int len)
{
	if (!m_buffer) {
		m_buffer = new(std::nothrow) uint8_t[len];
		m_bufferRealSize = len;
	} else if (len > m_bufferRealSize) {
		delete [] m_buffer;
		m_buffer = new(std::nothrow) uint8_t[len];
		m_bufferRealSize = len;
	}
	m_bufferSize = len;
	m_bufferPos = 0;
	m_bufferShift = 8 - m_bpp;
	return m_buffer;
}

void StringTable::CompressStart(int bpp, int width)
{
	m_bpp = bpp;
	m_slack = (8 - ((width * bpp) % 8)) % 8;

	m_partial |= m_clearCode << m_partialSize;
	m_partialSize += m_codeSize;
	ClearCompressorTable();
}

int StringTable::CompressEnd(uint8_t *buf)
{
	int len = 0;

	//output code for remaining prefix
	m_partial |= m_prefix << m_partialSize;
	m_partialSize += m_codeSize;
	while (m_partialSize >= 8) {
		*buf++ = (uint8_t)m_partial;
		m_partial >>= 8;
		m_partialSize -= 8;
		len++;
	}

	//add the end of information code and flush the entire buffer out
	m_partial |= m_endCode << m_partialSize;
	m_partialSize += m_codeSize;
	while (m_partialSize > 0) {
		*buf++ = (uint8_t)m_partial;
		m_partial >>= 8;
		m_partialSize -= 8;
		len++;
	}

	//most this can be is 4 bytes.  7 bits in m_partial to start + 12 for the
	//last code + 12 for the end code = 31 bits total.
	return len;
}

bool StringTable::Compress(uint8_t *buf, int *len)
{
	if (m_bufferSize == 0 || m_done) {
		return false;
	}

	int mask = (1 << m_bpp) - 1;
	uint8_t *bufpos = buf;
	while (m_bufferPos < m_bufferSize) {
		//get the current pixel value
		char ch = (char)((m_buffer[m_bufferPos] >> m_bufferShift) & mask);

		// The next prefix is : 
		// <the previous LZW code (on 12 bits << 8)> | <the code of the current pixel (on 8 bits)>
		int nextprefix = (((m_prefix)<<8)&0xFFF00) + (ch & 0x000FF);
		if (firstPixelPassed) {
			
			if (m_strmap[nextprefix] > 0) {
				m_prefix = m_strmap[nextprefix];
			} else {
				m_partial |= m_prefix << m_partialSize;
				m_partialSize += m_codeSize;
				//grab full bytes for the output buffer
				while (m_partialSize >= 8 && bufpos - buf < *len) {
					*bufpos++ = (uint8_t)m_partial;
					m_partial >>= 8;
					m_partialSize -= 8;
				}

				//add the code to the "table map"
				m_strmap[nextprefix] = m_nextCode;

				//increment the next highest valid code, increase the code size
				if (m_nextCode == (1 << m_codeSize)) {
					m_codeSize++;
				}
				m_nextCode++;

				//if we're out of codes, restart the string table
				if (m_nextCode == MAX_LZW_CODE) {
					m_partial |= m_clearCode << m_partialSize;
					m_partialSize += m_codeSize;
					ClearCompressorTable();
				}

				// Only keep the 8 lowest bits (prevent problems with "negative chars")
				m_prefix = ch & 0x000FF;
			}

			//increment to the next pixel
			if (m_bufferShift > 0 && !(m_bufferPos + 1 == m_bufferSize && m_bufferShift <= m_slack)) {
				m_bufferShift -= m_bpp;
			} else {
				m_bufferPos++;
				m_bufferShift = 8 - m_bpp;
			}

			//jump out here if the output buffer is full
			if (bufpos - buf == *len) {
				return true;
			}
		
		} else {
			// Specific behavior for the first pixel of the whole image

			firstPixelPassed=1;
			// Only keep the 8 lowest bits (prevent problems with "negative chars")
			m_prefix = ch & 0x000FF;

			//increment to the next pixel
			if (m_bufferShift > 0 && !(m_bufferPos + 1 == m_bufferSize && m_bufferShift <= m_slack)) {
				m_bufferShift -= m_bpp;
			} else {
				m_bufferPos++;
				m_bufferShift = 8 - m_bpp;
			}

			//jump out here if the output buffer is full
			if (bufpos - buf == *len) {
				return true;
			}
		}
	}

	m_bufferSize = 0;
	*len = (int)(bufpos - buf);

	return true;
}

bool StringTable::Decompress(uint8_t *buf, int *len)
{
	if (m_bufferSize == 0 || m_done) {
		return false;
	}

	uint8_t *bufpos = buf;
	for ( ; m_bufferPos < m_bufferSize; m_bufferPos++ ) {
		m_partial |= (int)m_buffer[m_bufferPos] << m_partialSize;
		m_partialSize += 8;
		while (m_partialSize >= m_codeSize) {
			int code = m_partial & m_codeMask;
			m_partial >>= m_codeSize;
			m_partialSize -= m_codeSize;

			if (code > m_nextCode || /*(m_nextCode == MAX_LZW_CODE && code != m_clearCode) || */code == m_endCode) {
				m_done = true;
				*len = (int)(bufpos - buf);
				return true;
			}
			if (code == m_clearCode) {
				ClearDecompressorTable();
				continue;
			}

			//add new string to string table, if not the first pass since a clear code
			if (m_oldCode != MAX_LZW_CODE && m_nextCode < MAX_LZW_CODE) {
				m_strings[m_nextCode] = m_strings[m_oldCode] + m_strings[code == m_nextCode ? m_oldCode : code][0];
			}

			if ((int)m_strings[code].size() > *len - (bufpos - buf)) {
				//out of space, stuff the code back in for next time
				m_partial <<= m_codeSize;
				m_partialSize += m_codeSize;
				m_partial |= code;
				m_bufferPos++;
				*len = (int)(bufpos - buf);
				return true;
			}

			//output the string into the buffer
			memcpy(bufpos, m_strings[code].data(), m_strings[code].size());
			bufpos += m_strings[code].size();

			//increment the next highest valid code, add a bit to the mask if we need to increase the code size
			if (m_oldCode != MAX_LZW_CODE && m_nextCode < MAX_LZW_CODE) {
				if (++m_nextCode < MAX_LZW_CODE) {
					if ((m_nextCode & m_codeMask) == 0) {
						m_codeSize++;
						m_codeMask |= m_nextCode;
					}
				}
			}

			m_oldCode = code;
		}
	}

	m_bufferSize = 0;
	*len = (int)(bufpos - buf);

	return true;
}

void StringTable::Done(void)
{
	m_done = true;
}

void StringTable::ClearCompressorTable(void)
{
	if (m_strmap) {
		memset(m_strmap, 0xFF, sizeof(unsigned int)*(1<<20));
	}
	m_nextCode = m_endCode + 1;

	m_prefix = 0;
	m_codeSize = m_minCodeSize + 1;
}

void StringTable::ClearDecompressorTable(void)
{
	for (int i = 0; i < m_clearCode; i++) {
		m_strings[i].resize(1);
		m_strings[i][0] = (char)i;
	}
	m_nextCode = m_endCode + 1;

	m_codeSize = m_minCodeSize + 1;
	m_codeMask = (1 << m_codeSize) - 1;
	m_oldCode = MAX_LZW_CODE;
}

// ==========================================================
// Plugin Interface
// ==========================================================

static int s_format_id;

// ==========================================================
// Plugin Implementation
// ==========================================================

static const char * DLL_CALLCONV 
Format() {
	return "GIF";
}

static const char * DLL_CALLCONV 
Description() {
	return "Graphics Interchange Format";
}

static const char * DLL_CALLCONV 
Extension() {
	return "gif";
}

static const char * DLL_CALLCONV 
RegExpr() {
	return "^GIF";
}

static const char * DLL_CALLCONV 
MimeType() {
	return "image/gif";
}

static FIBOOL DLL_CALLCONV
Validate(FreeImageIO *io, fi_handle handle) {
	const uint8_t GIF89a[] = { 0x47, 0x49, 0x46, 0x38, 0x39, 0x61 };	// ASCII code for "GIF89a"
	const uint8_t GIF87a[] = { 0x47, 0x49, 0x46, 0x38, 0x37, 0x61 };	// ASCII code for "GIF87a"
	uint8_t signature[6] = { 0, 0, 0, 0, 0, 0 };

	io->read_proc(signature, 1, 6, handle);

	if (memcmp(GIF89a, signature, 6) == 0)
		return TRUE;
	if (memcmp(GIF87a, signature, 6) == 0)
		return TRUE;

	return FALSE;
}

static FIBOOL DLL_CALLCONV 
SupportsExportDepth(int depth) {
	return	(depth == 1) ||
			(depth == 4) ||
			(depth == 8);
}

static FIBOOL DLL_CALLCONV 
SupportsExportType(FREE_IMAGE_TYPE type) {
	return (type == FIT_BITMAP) ? TRUE : FALSE;
}

// ----------------------------------------------------------

static void *DLL_CALLCONV 
Open(FreeImageIO *io, fi_handle handle, FIBOOL read) {
	GIFinfo *info = new(std::nothrow) GIFinfo;
	if (!info) {
		return nullptr;
	}

	// set Read/Write mode
	info->read = read;

	if (read) {
		try {
			// read Header (6 bytes)
			if (!Validate(io, handle)) {
				throw FI_MSG_ERROR_MAGIC_NUMBER;
			}

			//Logical Screen Descriptor
			io->seek_proc(handle, 4, SEEK_CUR);
			uint8_t packed;
			if (io->read_proc(&packed, 1, 1, handle) < 1) {
				throw "EOF reading Logical Screen Descriptor";
			}
			if (io->read_proc(&info->background_color, 1, 1, handle) < 1) {
				throw "EOF reading Logical Screen Descriptor";
			}
			io->seek_proc(handle, 1, SEEK_CUR);

			//Global Color Table
			if (packed & GIF_PACKED_LSD_HAVEGCT) {
				info->global_color_table_offset = io->tell_proc(handle);
				info->global_color_table_size = 2 << (packed & GIF_PACKED_LSD_GCTSIZE);
				io->seek_proc(handle, 3 * info->global_color_table_size, SEEK_CUR);
			}

			//Scan through all the rest of the blocks, saving offsets
			size_t gce_offset = 0;
			uint8_t block = 0;
			while (block != GIF_BLOCK_TRAILER) {
				if (io->read_proc(&block, 1, 1, handle) < 1) {
					throw "EOF reading blocks";
				}
				if (block == GIF_BLOCK_IMAGE_DESCRIPTOR) {
					info->image_descriptor_offsets.push_back(io->tell_proc(handle));
					//GCE may be 0, meaning no GCE preceded this ID
					info->graphic_control_extension_offsets.push_back(gce_offset);
					gce_offset = 0;

					io->seek_proc(handle, 8, SEEK_CUR);
					if (io->read_proc(&packed, 1, 1, handle) < 1) {
						throw "EOF reading Image Descriptor";
					}

					//Local Color Table
					if (packed & GIF_PACKED_ID_HAVELCT) {
						io->seek_proc(handle, 3 * (2 << (packed & GIF_PACKED_ID_LCTSIZE)), SEEK_CUR);
					}

					//LZW Minimum Code Size
					io->seek_proc(handle, 1, SEEK_CUR);
				} else if (block == GIF_BLOCK_EXTENSION) {
					uint8_t ext;
					if (io->read_proc(&ext, 1, 1, handle) < 1) {
						throw "EOF reading extension";
					}

					if (ext == GIF_EXT_GRAPHIC_CONTROL) {
						//overwrite previous offset if more than one GCE found before an ID
						gce_offset = io->tell_proc(handle);
					} else if (ext == GIF_EXT_COMMENT) {
						info->comment_extension_offsets.push_back(io->tell_proc(handle));
					} else if (ext == GIF_EXT_APPLICATION) {
						info->application_extension_offsets.push_back(io->tell_proc(handle));
					}
				} else if (block == GIF_BLOCK_TRAILER) {
					continue;
				} else {
					throw "Invalid GIF block found";
				}

				//Data Sub-blocks
				uint8_t len;
				if (io->read_proc(&len, 1, 1, handle) < 1) {
					throw "EOF reading sub-block";
				}
				while (len != 0) {
					io->seek_proc(handle, len, SEEK_CUR);
					if (io->read_proc(&len, 1, 1, handle) < 1) {
						throw "EOF reading sub-block";
					}
				}
			}
		} catch (const char *msg) {
			FreeImage_OutputMessageProc(s_format_id, msg);
			delete info;
			return nullptr;
		}
	} else {
		//Header
		io->write_proc((void *)"GIF89a", 6, 1, handle);
	}

	return info;
}

static void DLL_CALLCONV 
Close(FreeImageIO *io, fi_handle handle, void *data) {
	if (!data) {
		return;
	}
	GIFinfo *info = (GIFinfo *)data;

	if (!info->read) {
		//Trailer
		uint8_t b = GIF_BLOCK_TRAILER;
		io->write_proc(&b, 1, 1, handle);
	}

	delete info;
}

static int DLL_CALLCONV
PageCount(FreeImageIO *io, fi_handle handle, void *data) {
	if (!data) {
		return 0;
	}
	auto *info = (GIFinfo *)data;

	return (int) info->image_descriptor_offsets.size();
}

static FIBITMAP * DLL_CALLCONV 
Load(FreeImageIO *io, fi_handle handle, int page, int flags, void *data) {
	if (!data) {
		return nullptr;
	}
	GIFinfo *info = (GIFinfo *)data;

	if (page == -1) {
		page = 0;
	}
	if (page < 0 || page >= (int)info->image_descriptor_offsets.size()) {
		return nullptr;
	}

	try {
		bool have_transparent = false, no_local_palette = false, interlaced = false;
		int disposal_method = GIF_DISPOSAL_LEAVE, delay_time = 0, transparent_color = 0;
		uint16_t left, top, width, height;
		uint8_t packed, b;
		uint16_t w;

		//playback pages to generate what the user would see for this frame
		if ((flags & GIF_PLAYBACK) == GIF_PLAYBACK) {
			//Logical Screen Descriptor
			io->seek_proc(handle, 6, SEEK_SET);
			uint16_t logicalwidth, logicalheight;
			io->read_proc(&logicalwidth, 2, 1, handle);
			io->read_proc(&logicalheight, 2, 1, handle);
#ifdef FREEIMAGE_BIGENDIAN
			SwapShort(&logicalwidth);
			SwapShort(&logicalheight);
#endif
			//set the background color with 0 alpha
			FIRGBA8 background;
			if (info->global_color_table_offset != 0 && info->background_color < info->global_color_table_size) {
				io->seek_proc(handle, (long)(info->global_color_table_offset + (info->background_color * 3)), SEEK_SET);
				io->read_proc(&background.red, 1, 1, handle);
				io->read_proc(&background.green, 1, 1, handle);
				io->read_proc(&background.blue, 1, 1, handle);
			} else {
				background.red = 0;
				background.green = 0;
				background.blue = 0;
			}
			background.alpha = 0;

			//allocate entire logical area
			std::unique_ptr<FIBITMAP, decltype(&FreeImage_Unload)> dib(FreeImage_Allocate(logicalwidth, logicalheight, 32), &FreeImage_Unload);
			if (!dib) {
				throw FI_MSG_ERROR_DIB_MEMORY;
			}

			//fill with background color to start
			int x, y;
			FIRGBA8 *scanline;
			for (y = 0; y < logicalheight; y++) {
				scanline = (FIRGBA8 *)FreeImage_GetScanLine(dib.get(), y);
				for (x = 0; x < logicalwidth; x++) {
					*scanline++ = background;
				}
			}

			//cache some info about each of the pages so we can avoid decoding as many of them as possible
			std::vector<PageInfo> pageinfo;
			int start = page, end = page;
			while (start >= 0) {
				//Graphic Control Extension
				io->seek_proc(handle, (long)(info->graphic_control_extension_offsets[start] + 1), SEEK_SET);
				io->read_proc(&packed, 1, 1, handle);
				have_transparent = (packed & GIF_PACKED_GCE_HAVETRANS) ? true : false;
				disposal_method = (packed & GIF_PACKED_GCE_DISPOSAL) >> 2;
				//Image Descriptor
				io->seek_proc(handle, (long)(info->image_descriptor_offsets[start]), SEEK_SET);
				io->read_proc(&left, 2, 1, handle);
				io->read_proc(&top, 2, 1, handle);
				io->read_proc(&width, 2, 1, handle);
				io->read_proc(&height, 2, 1, handle);
#ifdef FREEIMAGE_BIGENDIAN
				SwapShort(&left);
				SwapShort(&top);
				SwapShort(&width);
				SwapShort(&height);
#endif

				pageinfo.push_back(PageInfo(disposal_method, left, top, width, height));

				if (start != end) {
					if (left == 0 && top == 0 && width == logicalwidth && height == logicalheight) {
						if (disposal_method == GIF_DISPOSAL_BACKGROUND) {
							pageinfo.pop_back();
							start++;
							break;
						} else if (disposal_method != GIF_DISPOSAL_PREVIOUS) {
							if (!have_transparent) {
								break;
							}
						}
					}
				}
				start--;
			}
			if (start < 0) {
				start = 0;
			}

			//draw each page into the logical area
			delay_time = 0;
			for (page = start; page <= end; page++) {
				const PageInfo &info = pageinfo[end - page];
				//things we can skip having to decode
				if (page != end) {
					if (info.disposal_method == GIF_DISPOSAL_PREVIOUS) {
						continue;
					}
					if (info.disposal_method == GIF_DISPOSAL_BACKGROUND) {
						for (y = 0; y < info.height; y++) {
							const int scanidx = logicalheight - (y + info.top) - 1;
							if (scanidx < 0) {
								break;  // If data is corrupt, don't calculate in invalid scanline
							}
							scanline = (FIRGBA8 *)FreeImage_GetScanLine(dib.get(), scanidx) + info.left;
							for (x = 0; x < info.width; x++) {
								*scanline++ = background;
							}
						}
						continue;
					}
				}

				//decode page
				FIBITMAP *pagedib = Load(io, handle, page, GIF_LOAD256, data);
				if (pagedib) {
					std::unique_ptr<FIBITMAP, decltype(&FreeImage_Unload)> safePagedib(pagedib, &FreeImage_Unload);
					const FIRGBA8 *pal = FreeImage_GetPalette(pagedib);
					have_transparent = false;
					if (FreeImage_IsTransparent(pagedib)) {
						int count = FreeImage_GetTransparencyCount(pagedib);
						const uint8_t *table = FreeImage_GetTransparencyTable(pagedib);
						for (int i = 0; i < count; i++) {
							if (table[i] == 0) {
								have_transparent = true;
								transparent_color = i;
								break;
							}
						}
					}
					//copy page data into logical buffer, with full alpha opaqueness
					for (y = 0; y < info.height; y++) {
						const int scanidx = logicalheight - (y + info.top) - 1;
						if (scanidx < 0) {
							break;  // If data is corrupt, don't calculate in invalid scanline
						}
						scanline = (FIRGBA8 *)FreeImage_GetScanLine(dib.get(), scanidx) + info.left;
						uint8_t *pageline = FreeImage_GetScanLine(pagedib, info.height - y - 1);
						for (x = 0; x < info.width; x++) {
							if (!have_transparent || *pageline != transparent_color) {
								*scanline = pal[*pageline];
								scanline->alpha = 255;
							}
							scanline++;
							pageline++;
						}
					}
					//copy frame time
					if (page == end) {
						FITAG *tag;
						if (FreeImage_GetMetadataEx(FIMD_ANIMATION, pagedib, "FrameTime", FIDT_LONG, &tag)) {
							delay_time = *(int32_t *)FreeImage_GetTagValue(tag);
						}
					}
				}
			}

			//setup frame time
			FreeImage_SetMetadataEx(FIMD_ANIMATION, dib.get(), "FrameTime", ANIMTAG_FRAMETIME, FIDT_LONG, 1, 4, &delay_time);
			return dib.release();
		}

		//get the actual frame image data for a single frame

		//Image Descriptor
		io->seek_proc(handle, (long)info->image_descriptor_offsets[page], SEEK_SET);
		io->read_proc(&left, 2, 1, handle);
		io->read_proc(&top, 2, 1, handle);
		io->read_proc(&width, 2, 1, handle);
		io->read_proc(&height, 2, 1, handle);
#ifdef FREEIMAGE_BIGENDIAN
		SwapShort(&left);
		SwapShort(&top);
		SwapShort(&width);
		SwapShort(&height);
#endif
		io->read_proc(&packed, 1, 1, handle);
		interlaced = (packed & GIF_PACKED_ID_INTERLACED) ? true : false;
		no_local_palette = (packed & GIF_PACKED_ID_HAVELCT) ? false : true;

		int bpp = 8;
		if ((flags & GIF_LOAD256) == 0) {
			if (!no_local_palette) {
				int size = 2 << (packed & GIF_PACKED_ID_LCTSIZE);
				if (size <= 2) bpp = 1;
				else if (size <= 16) bpp = 4;
			} else if (info->global_color_table_offset != 0) {
				if (info->global_color_table_size <= 2) bpp = 1;
				else if (info->global_color_table_size <= 16) bpp = 4;
			}
		}
		std::unique_ptr<FIBITMAP, decltype(&FreeImage_Unload)> dib(FreeImage_Allocate(width, height, bpp), &FreeImage_Unload);
		if (!dib) {
			throw FI_MSG_ERROR_DIB_MEMORY;
		}

		FreeImage_SetMetadataEx(FIMD_ANIMATION, dib.get(), "FrameLeft", ANIMTAG_FRAMELEFT, FIDT_SHORT, 1, 2, &left);
		FreeImage_SetMetadataEx(FIMD_ANIMATION, dib.get(), "FrameTop", ANIMTAG_FRAMETOP, FIDT_SHORT, 1, 2, &top);
		b = no_local_palette ? 1 : 0;
		FreeImage_SetMetadataEx(FIMD_ANIMATION, dib.get(), "NoLocalPalette", ANIMTAG_NOLOCALPALETTE, FIDT_BYTE, 1, 1, &b);
		b = interlaced ? 1 : 0;
		FreeImage_SetMetadataEx(FIMD_ANIMATION, dib.get(), "Interlaced", ANIMTAG_INTERLACED, FIDT_BYTE, 1, 1, &b);

		//Palette
		FIRGBA8 *pal = FreeImage_GetPalette(dib.get());
		if (!no_local_palette) {
			int size = 2 << (packed & GIF_PACKED_ID_LCTSIZE);

			int i = 0;
			while (i < size) {
				io->read_proc(&pal[i].red, 1, 1, handle);
				io->read_proc(&pal[i].green, 1, 1, handle);
				io->read_proc(&pal[i].blue, 1, 1, handle);
				i++;
			}
		} else if (info->global_color_table_offset != 0) {
			long pos = io->tell_proc(handle);
			io->seek_proc(handle, (long)info->global_color_table_offset, SEEK_SET);

			int i = 0;
			while (i < info->global_color_table_size) {
				io->read_proc(&pal[i].red, 1, 1, handle);
				io->read_proc(&pal[i].green, 1, 1, handle);
				io->read_proc(&pal[i].blue, 1, 1, handle);
				i++;
			}

			io->seek_proc(handle, pos, SEEK_SET);
		} else {
			//its legal to have no palette, but we're going to generate *something*
			for (int i = 0; i < 256; i++) {
				pal[i].red   = (uint8_t)i;
				pal[i].green = (uint8_t)i;
				pal[i].blue  = (uint8_t)i;
			}
		}

		//LZW Minimum Code Size
		io->read_proc(&b, 1, 1, handle);
		StringTable *stringtable = new(std::nothrow) StringTable;
		stringtable->Initialize(b);

		//Image Data Sub-blocks
		int x = 0, xpos = 0, y = 0, shift = 8 - bpp, mask = (1 << bpp) - 1, interlacepass = 0;
		uint8_t *scanline = FreeImage_GetScanLine(dib.get(), height - 1);
		uint8_t buf[4096];
		io->read_proc(&b, 1, 1, handle);
		while (b) {
			io->read_proc(stringtable->FillInputBuffer(b), b, 1, handle);
			int size = sizeof(buf);
			while (stringtable->Decompress(buf, &size)) {
				for ( int i = 0; i < size; i++ ) {
					scanline[xpos] |= (buf[i] & mask) << shift;
					if (shift > 0) {
						shift -= bpp;
					} else {
						xpos++;
						shift = 8 - bpp;
					}
					if (++x >= width) {
						if (interlaced) {
							y += g_GifInterlaceIncrement[interlacepass];
							if (y >= height && ++interlacepass < GIF_INTERLACE_PASSES) {
								y = g_GifInterlaceOffset[interlacepass];
							}
						} else {
							y++;
						}
						if (y >= height) {
							stringtable->Done();
							break;
						}
						x = xpos = 0;
						shift = 8 - bpp;
						scanline = FreeImage_GetScanLine(dib.get(), height - y - 1);
					}
				}
				size = sizeof(buf);
			}
			io->read_proc(&b, 1, 1, handle);
		}

		if (page == 0) {
			size_t idx;

			//Logical Screen Descriptor
			io->seek_proc(handle, 6, SEEK_SET);
			uint16_t logicalwidth, logicalheight;
			io->read_proc(&logicalwidth, 2, 1, handle);
			io->read_proc(&logicalheight, 2, 1, handle);
#ifdef FREEIMAGE_BIGENDIAN
			SwapShort(&logicalwidth);
			SwapShort(&logicalheight);
#endif
			FreeImage_SetMetadataEx(FIMD_ANIMATION, dib.get(), "LogicalWidth", ANIMTAG_LOGICALWIDTH, FIDT_SHORT, 1, 2, &logicalwidth);
			FreeImage_SetMetadataEx(FIMD_ANIMATION, dib.get(), "LogicalHeight", ANIMTAG_LOGICALHEIGHT, FIDT_SHORT, 1, 2, &logicalheight);

			//Global Color Table
			if (info->global_color_table_offset != 0) {
				FIRGBA8 globalpalette[256];
				io->seek_proc(handle, (long)info->global_color_table_offset, SEEK_SET);
				int i = 0;
				while (i < info->global_color_table_size) {
					io->read_proc(&globalpalette[i].red, 1, 1, handle);
					io->read_proc(&globalpalette[i].green, 1, 1, handle);
					io->read_proc(&globalpalette[i].blue, 1, 1, handle);
					globalpalette[i].alpha = 0;
					i++;
				}
				FreeImage_SetMetadataEx(FIMD_ANIMATION, dib.get(), "GlobalPalette", ANIMTAG_GLOBALPALETTE, FIDT_PALETTE, info->global_color_table_size, info->global_color_table_size * 4, globalpalette);
				//background color
				if (info->background_color < info->global_color_table_size) {
					FreeImage_SetBackgroundColor(dib.get(), &globalpalette[info->background_color]);
				}
			}

			//Application Extension
			int32_t loop = 1; //If no AE with a loop count is found, the default must be 1
			for (idx = 0; idx < info->application_extension_offsets.size(); idx++) {
				io->seek_proc(handle, (long)info->application_extension_offsets[idx], SEEK_SET);
				io->read_proc(&b, 1, 1, handle);
				if (b == 11) { //All AEs start with an 11 byte sub-block to determine what type of AE it is
					char buf[11];
					io->read_proc(buf, 11, 1, handle);
					if (!memcmp(buf, "NETSCAPE2.0", 11) || !memcmp(buf, "ANIMEXTS1.0", 11)) { //Not everybody recognizes ANIMEXTS1.0 but it is valid
						io->read_proc(&b, 1, 1, handle);
						if (b == 3) { //we're supposed to have a 3 byte sub-block now
							io->read_proc(&b, 1, 1, handle); //this should be 0x01 but isn't really important
							io->read_proc(&w, 2, 1, handle);
#ifdef FREEIMAGE_BIGENDIAN
							SwapShort(&w);
#endif
							loop = w;
							if (loop > 0) loop++;
							break;
						}
					}
				}
			}
			FreeImage_SetMetadataEx(FIMD_ANIMATION, dib.get(), "Loop", ANIMTAG_LOOP, FIDT_LONG, 1, 4, &loop);

			//Comment Extension
			for (idx = 0; idx < info->comment_extension_offsets.size(); idx++) {
				io->seek_proc(handle, (long)info->comment_extension_offsets[idx], SEEK_SET);
				std::string comment;
				char buf[255];
				io->read_proc(&b, 1, 1, handle);
				while (b) {
					io->read_proc(buf, b, 1, handle);
					comment.append(buf, b);
					io->read_proc(&b, 1, 1, handle);
				}
				comment.append(1, '\0');
				snprintf(buf, std::size(buf), "Comment%zd", idx);
				uint32_t comment_size = (uint32_t)comment.size();
				FreeImage_SetMetadataEx(FIMD_COMMENTS, dib.get(), buf, 1, FIDT_ASCII, comment_size, comment_size, comment.c_str());
			}
		}

		//Graphic Control Extension
		if (info->graphic_control_extension_offsets[page] != 0) {
			io->seek_proc(handle, (long)(info->graphic_control_extension_offsets[page] + 1), SEEK_SET);
			io->read_proc(&packed, 1, 1, handle);
			io->read_proc(&w, 2, 1, handle);
#ifdef FREEIMAGE_BIGENDIAN
			SwapShort(&w);
#endif
			io->read_proc(&b, 1, 1, handle);
			have_transparent = (packed & GIF_PACKED_GCE_HAVETRANS) ? true : false;
			disposal_method = (packed & GIF_PACKED_GCE_DISPOSAL) >> 2;
			delay_time = w * 10; //convert cs to ms
			transparent_color = b;
			if (have_transparent) {
				int size = 1 << bpp;
				if (transparent_color <= size) {
					uint8_t table[256];
					memset(table, 0xFF, size);
					table[transparent_color] = 0;
					FreeImage_SetTransparencyTable(dib.get(), table, size);
				}
			}
		}
		FreeImage_SetMetadataEx(FIMD_ANIMATION, dib.get(), "FrameTime", ANIMTAG_FRAMETIME, FIDT_LONG, 1, 4, &delay_time);
		b = (uint8_t)disposal_method;
		FreeImage_SetMetadataEx(FIMD_ANIMATION, dib.get(), "DisposalMethod", ANIMTAG_DISPOSALMETHOD, FIDT_BYTE, 1, 1, &b);

		delete stringtable;

		return dib.release();

	} catch (const char *msg) {
		FreeImage_OutputMessageProc(s_format_id, msg);
	}

	return nullptr;
}

static FIBOOL DLL_CALLCONV 
Save(FreeImageIO *io, FIBITMAP *dib, fi_handle handle, int page, int flags, void *data) {
	if (!data) {
		return FALSE;
	}
	//GIFinfo *info = (GIFinfo *)data;

	if (page == -1) {
		page = 0;
	}

	try {
		uint8_t packed, b;
		uint16_t w;
		FITAG *tag;

		int bpp = FreeImage_GetBPP(dib);
		if (bpp != 1 && bpp != 4 && bpp != 8) {
			throw "Only 1, 4, or 8 bpp images supported";
		}

		bool have_transparent = false, no_local_palette = false, interlaced = false;
		int disposal_method = GIF_DISPOSAL_BACKGROUND, delay_time = 100, transparent_color = 0;
		uint16_t left = 0, top = 0, width = (uint16_t)FreeImage_GetWidth(dib), height = (uint16_t)FreeImage_GetHeight(dib);
		uint16_t output_height = height;
		if (FreeImage_GetMetadataEx(FIMD_ANIMATION, dib, "FrameLeft", FIDT_SHORT, &tag)) {
			left = *(uint16_t *)FreeImage_GetTagValue(tag);
		}
		if (FreeImage_GetMetadataEx(FIMD_ANIMATION, dib, "FrameTop", FIDT_SHORT, &tag)) {
			top = *(uint16_t *)FreeImage_GetTagValue(tag);
		}
		if (FreeImage_GetMetadataEx(FIMD_ANIMATION, dib, "NoLocalPalette", FIDT_BYTE, &tag)) {
			no_local_palette = *(uint8_t *)FreeImage_GetTagValue(tag) ? true : false;
		}
		if (FreeImage_GetMetadataEx(FIMD_ANIMATION, dib, "Interlaced", FIDT_BYTE, &tag)) {
			interlaced = *(uint8_t *)FreeImage_GetTagValue(tag) ? true : false;
		}
		if (FreeImage_GetMetadataEx(FIMD_ANIMATION, dib, "FrameTime", FIDT_LONG, &tag)) {
			delay_time = *(int32_t *)FreeImage_GetTagValue(tag);
		}
		if (FreeImage_GetMetadataEx(FIMD_ANIMATION, dib, "DisposalMethod", FIDT_BYTE, &tag)) {
			disposal_method = *(uint8_t *)FreeImage_GetTagValue(tag);
		}

		FIRGBA8 *pal = FreeImage_GetPalette(dib);
#ifdef FREEIMAGE_BIGENDIAN
		SwapShort(&left);
		SwapShort(&top);
		SwapShort(&width);
		SwapShort(&height);
#endif

		if (page == 0) {
			//gather some info
			uint16_t logicalwidth = width; // width has already been swapped...
			if (FreeImage_GetMetadataEx(FIMD_ANIMATION, dib, "LogicalWidth", FIDT_SHORT, &tag)) {
				logicalwidth = *(uint16_t *)FreeImage_GetTagValue(tag);
#ifdef FREEIMAGE_BIGENDIAN
				SwapShort(&logicalwidth);
#endif
			}
			uint16_t logicalheight = height; // height has already been swapped...
			if (FreeImage_GetMetadataEx(FIMD_ANIMATION, dib, "LogicalHeight", FIDT_SHORT, &tag)) {
				logicalheight = *(uint16_t *)FreeImage_GetTagValue(tag);
#ifdef FREEIMAGE_BIGENDIAN
				SwapShort(&logicalheight);
#endif
			}
			FIRGBA8 *globalpalette{};
			int globalpalette_size = 0;
			if (FreeImage_GetMetadataEx(FIMD_ANIMATION, dib, "GlobalPalette", FIDT_PALETTE, &tag)) {
				globalpalette_size = FreeImage_GetTagCount(tag);
				if (globalpalette_size >= 2) {
					globalpalette = (FIRGBA8 *)FreeImage_GetTagValue(tag);
				}
			}

			//Logical Screen Descriptor
			io->write_proc(&logicalwidth, 2, 1, handle);
			io->write_proc(&logicalheight, 2, 1, handle);
			packed = GIF_PACKED_LSD_COLORRES;
			b = 0;
			FIRGBA8 background_color;
			if (globalpalette) {
				packed |= GIF_PACKED_LSD_HAVEGCT;
				if (globalpalette_size < 4) {
					globalpalette_size = 2;
					packed |= 0 & GIF_PACKED_LSD_GCTSIZE;
				} else if (globalpalette_size < 8) {
					globalpalette_size = 4;
					packed |= 1 & GIF_PACKED_LSD_GCTSIZE;
				} else if (globalpalette_size < 16) {
					globalpalette_size = 8;
					packed |= 2 & GIF_PACKED_LSD_GCTSIZE;
				} else if (globalpalette_size < 32) {
					globalpalette_size = 16;
					packed |= 3 & GIF_PACKED_LSD_GCTSIZE;
				} else if (globalpalette_size < 64) {
					globalpalette_size = 32;
					packed |= 4 & GIF_PACKED_LSD_GCTSIZE;
				} else if (globalpalette_size < 128) {
					globalpalette_size = 64;
					packed |= 5 & GIF_PACKED_LSD_GCTSIZE;
				} else if (globalpalette_size < 256) {
					globalpalette_size = 128;
					packed |= 6 & GIF_PACKED_LSD_GCTSIZE;
				} else {
					globalpalette_size = 256;
					packed |= 7 & GIF_PACKED_LSD_GCTSIZE;
				}
				if (FreeImage_GetBackgroundColor(dib, &background_color)) {
					for (int i = 0; i < globalpalette_size; i++) {
						if (background_color.red == globalpalette[i].red &&
							background_color.green == globalpalette[i].green &&
							background_color.blue == globalpalette[i].blue) {

							b = (uint8_t)i;
							break;
						}
					}
				}
			} else {
				packed |= (bpp - 1) & GIF_PACKED_LSD_GCTSIZE;
			}
			io->write_proc(&packed, 1, 1, handle);
			io->write_proc(&b, 1, 1, handle);
			b = 0;
			io->write_proc(&b, 1, 1, handle);

			//Global Color Table
			if (globalpalette) {
				int i = 0;
				while (i < globalpalette_size) {
					io->write_proc(&globalpalette[i].red, 1, 1, handle);
					io->write_proc(&globalpalette[i].green, 1, 1, handle);
					io->write_proc(&globalpalette[i].blue, 1, 1, handle);
					i++;
				}
			}

			//Application Extension
			int32_t loop = 0;
			if (FreeImage_GetMetadataEx(FIMD_ANIMATION, dib, "Loop", FIDT_LONG, &tag)) {
				loop = *(int32_t *)FreeImage_GetTagValue(tag);
			}
			if (loop != 1) {
				//the Netscape extension is really "repeats" not "loops"
				if (loop > 1) loop--;
				if (loop > 0xFFFF) loop = 0xFFFF;
				w = (uint16_t)loop;
#ifdef FREEIMAGE_BIGENDIAN
				SwapShort(&w);
#endif
				io->write_proc((void *)"\x21\xFF\x0BNETSCAPE2.0\x03\x01", 16, 1, handle);
				io->write_proc(&w, 2, 1, handle);
				b = 0;
				io->write_proc(&b, 1, 1, handle);
			}

			//Comment Extension
			FITAG *tag{};
			FIMETADATA *mdhandle = FreeImage_FindFirstMetadata(FIMD_COMMENTS, dib, &tag);
			if (mdhandle) {
				do {
					if (FreeImage_GetTagType(tag) == FIDT_ASCII) {
						int length = FreeImage_GetTagLength(tag) - 1;
						char *value = (char *)FreeImage_GetTagValue(tag);
						io->write_proc((void *)"\x21\xFE", 2, 1, handle);
						while (length > 0) {
							b = (uint8_t)(length >= 255 ? 255 : length);
							io->write_proc(&b, 1, 1, handle);
							io->write_proc(value, b, 1, handle);
							value += b;
							length -= b;
						}
						b = 0;
						io->write_proc(&b, 1, 1, handle);
					}
				} while (FreeImage_FindNextMetadata(mdhandle, &tag));

				FreeImage_FindCloseMetadata(mdhandle);
			}
		}

		//Graphic Control Extension
		if (FreeImage_IsTransparent(dib)) {
			const int count = FreeImage_GetTransparencyCount(dib);
			uint8_t *table = FreeImage_GetTransparencyTable(dib);
			for (int i = 0; i < count; i++) {
				if (table[i] == 0) {
					have_transparent = true;
					transparent_color = i;
					break;
				}
			}
		}
		io->write_proc((void *)"\x21\xF9\x04", 3, 1, handle);
		b = (uint8_t)((disposal_method << 2) & GIF_PACKED_GCE_DISPOSAL);
		if (have_transparent) b |= GIF_PACKED_GCE_HAVETRANS;
		io->write_proc(&b, 1, 1, handle);
		//Notes about delay time for GIFs:
		//IE5/IE6 have a minimum and default of 100ms
		//Mozilla/Firefox/Netscape 6+/Opera have a minimum of 20ms and a default of 100ms if <20ms is specified or the GCE is absent
		//Netscape 4 has a minimum of 10ms if 0ms is specified, but will use 0ms if the GCE is absent
		w = (uint16_t)(delay_time / 10); //convert ms to cs
#ifdef FREEIMAGE_BIGENDIAN
		SwapShort(&w);
#endif
		io->write_proc(&w, 2, 1, handle);
		b = (uint8_t)transparent_color;
		io->write_proc(&b, 1, 1, handle);
		b = 0;
		io->write_proc(&b, 1, 1, handle);

		//Image Descriptor
		b = GIF_BLOCK_IMAGE_DESCRIPTOR;
		io->write_proc(&b, 1, 1, handle);
		io->write_proc(&left, 2, 1, handle);
		io->write_proc(&top, 2, 1, handle);
		io->write_proc(&width, 2, 1, handle);
		io->write_proc(&height, 2, 1, handle);
		packed = 0;
		if (!no_local_palette) packed |= GIF_PACKED_ID_HAVELCT | ((bpp - 1) & GIF_PACKED_ID_LCTSIZE);
		if (interlaced) packed |= GIF_PACKED_ID_INTERLACED;
		io->write_proc(&packed, 1, 1, handle);

		//Local Color Table
		if (!no_local_palette) {
			int palsize = 1 << bpp;
			for (int i = 0; i < palsize; i++) {
				io->write_proc(&pal[i].red, 1, 1, handle);
				io->write_proc(&pal[i].green, 1, 1, handle);
				io->write_proc(&pal[i].blue, 1, 1, handle);
			}
		}


		//LZW Minimum Code Size
		b = (uint8_t)(bpp == 1 ? 2 : bpp);
		io->write_proc(&b, 1, 1, handle);
		StringTable *stringtable = new(std::nothrow) StringTable;
		stringtable->Initialize(b);
		stringtable->CompressStart(bpp, width);

		//Image Data Sub-blocks
		int y = 0, interlacepass = 0, line = FreeImage_GetLine(dib);
		uint8_t buf[255], *bufptr = buf; //255 is the max sub-block length
		int size = sizeof(buf);
		b = sizeof(buf);
		while (y < output_height) {
			memcpy(stringtable->FillInputBuffer(line), FreeImage_GetScanLine(dib, output_height - y - 1), line);
			while (stringtable->Compress(bufptr, &size)) {
				bufptr += size;
				if ( bufptr - buf == sizeof(buf)) {
					io->write_proc(&b, 1, 1, handle);
					io->write_proc(buf, sizeof(buf), 1, handle);
					size = sizeof(buf);
					bufptr = buf;
				} else {
					size = (int)(sizeof(buf) - (bufptr - buf));
				}
			}
			if (interlaced) {
				y += g_GifInterlaceIncrement[interlacepass];
				if (y >= output_height && ++interlacepass < GIF_INTERLACE_PASSES) {
					y = g_GifInterlaceOffset[interlacepass];
				}		
			} else {
				y++;
			}
		}
		size = (int)(bufptr - buf);
		uint8_t last[4];
		w = (uint16_t)stringtable->CompressEnd(last);
		if (size + w >= sizeof(buf)) {
			//one last full size sub-block
			io->write_proc(&b, 1, 1, handle);
			io->write_proc(buf, size, 1, handle);
			io->write_proc(last, sizeof(buf) - size, 1, handle);
			//and possibly a tiny additional sub-block
			b = (uint8_t)(w - (sizeof(buf) - size));
			if (b > 0) {
				io->write_proc(&b, 1, 1, handle);
				io->write_proc(last + w - b, b, 1, handle);
			}
		} else {
			//last sub-block less than full size
			b = (uint8_t)(size + w);
			io->write_proc(&b, 1, 1, handle);
			io->write_proc(buf, size, 1, handle);
			io->write_proc(last, w, 1, handle);
		}

		//Block Terminator
		b = 0;
		io->write_proc(&b, 1, 1, handle);

		delete stringtable;

	} catch (const char *msg) {
		FreeImage_OutputMessageProc(s_format_id, msg);
		return FALSE;
	}

	return TRUE;
}

// ==========================================================
//   Init
// ==========================================================

void DLL_CALLCONV 
InitGIF(Plugin *plugin, int format_id) {
	s_format_id = format_id;

	plugin->format_proc = Format;
	plugin->description_proc = Description;
	plugin->extension_proc = Extension;
	plugin->regexpr_proc = RegExpr;
	plugin->open_proc = Open;
	plugin->close_proc = Close;
	plugin->pagecount_proc = PageCount;
	plugin->pagecapability_proc = nullptr;
	plugin->load_proc = Load;
	plugin->save_proc = Save;
	plugin->validate_proc = Validate;
	plugin->mime_proc = MimeType;
	plugin->supports_export_bpp_proc = SupportsExportDepth;
	plugin->supports_export_type_proc = SupportsExportType;
	plugin->supports_icc_profiles_proc = nullptr;
}
