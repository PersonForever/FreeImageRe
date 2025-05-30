// ==========================================================
// JPEG2000 J2K codestream Loader and Writer
//
// Design and implementation by
// - Herve Drolon (drolon@infonie.fr)
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
#include "openjp2/openjpeg.h"
#include "J2KHelper.h"

// ==========================================================
// Plugin Interface
// ==========================================================

static int s_format_id;

// ==========================================================
// Internal functions
// ==========================================================

/**
OpenJPEG Error callback 
*/
static void j2k_error_callback(const char *msg, void *client_data) {
	FreeImage_OutputMessageProc(s_format_id, "Error: %s", msg);
}
/**
OpenJPEG Warning callback 
*/
static void j2k_warning_callback(const char *msg, void *client_data) {
	FreeImage_OutputMessageProc(s_format_id, "Warning: %s", msg);
}

// ==========================================================
// Plugin Implementation
// ==========================================================

static const char * DLL_CALLCONV
Format() {
	return "J2K";
}

static const char * DLL_CALLCONV
Description() {
	return "JPEG-2000 codestream";
}

static const char * DLL_CALLCONV
Extension() {
	return "j2k,j2c";
}

static const char * DLL_CALLCONV
RegExpr() {
	return nullptr;
}

static const char * DLL_CALLCONV
MimeType() {
	return "image/j2k";
}

static FIBOOL DLL_CALLCONV
Validate(FreeImageIO *io, fi_handle handle) {
	const uint8_t jpc_signature[] = { 0xFF, 0x4F };
	uint8_t signature[2] = { 0, 0 };

	long tell = io->tell_proc(handle);
	io->read_proc(signature, 1, sizeof(jpc_signature), handle);
	io->seek_proc(handle, tell, SEEK_SET);

	return (memcmp(jpc_signature, signature, sizeof(jpc_signature)) == 0);
}

static FIBOOL DLL_CALLCONV
SupportsExportDepth(int depth) {
	return (
		(depth == 8) ||
		(depth == 24) || 
		(depth == 32)
	);
}

static FIBOOL DLL_CALLCONV 
SupportsExportType(FREE_IMAGE_TYPE type) {
	return (
		(type == FIT_BITMAP)  ||
		(type == FIT_UINT16)  ||
		(type == FIT_RGB16) || 
		(type == FIT_RGBA16)
	);
}

// ----------------------------------------------------------

static void * DLL_CALLCONV
Open(FreeImageIO *io, fi_handle handle, FIBOOL read) {
	// create the stream wrapper
	J2KFIO_t *fio = opj_freeimage_stream_create(io, handle, read);
	return fio;
}

static void DLL_CALLCONV
Close(FreeImageIO *io, fi_handle handle, void *data) {
	// destroy the stream wrapper
	J2KFIO_t *fio = (J2KFIO_t*)data;
	opj_freeimage_stream_destroy(fio);
}

// ----------------------------------------------------------

static FIBITMAP * DLL_CALLCONV
Load(FreeImageIO *io, fi_handle handle, int page, int flags, void *data) {
	J2KFIO_t *fio = (J2KFIO_t*)data;
	if (handle && fio) {
		opj_dparameters_t parameters;	// decompression parameters

		// check the file format
		if (!Validate(io, handle)) {
			return nullptr;
		}

		FIBOOL header_only = (flags & FIF_LOAD_NOPIXELS) == FIF_LOAD_NOPIXELS;

		// get the OpenJPEG stream
		opj_stream_t *d_stream = fio->stream;

		// set decoding parameters to default values 
		opj_set_default_decoder_parameters(&parameters);

		try {
			// decode the JPEG-2000 codestream

			// get a decoder handle
			std::unique_ptr<opj_codec_t, decltype(&opj_destroy_codec)> d_codec(opj_create_decompress(OPJ_CODEC_J2K), &opj_destroy_codec);
			
			// configure the event callbacks
			// catch events using our callbacks (no local context needed here)
			opj_set_info_handler(d_codec.get(), nullptr, nullptr);
			opj_set_warning_handler(d_codec.get(), j2k_warning_callback, nullptr);
			opj_set_error_handler(d_codec.get(), j2k_error_callback, nullptr);

			// setup the decoder decoding parameters using user parameters
			if (!opj_setup_decoder(d_codec.get(), &parameters)) {
				throw "Failed to setup the decoder\n";
			}

			// read the main header of the codestream and if necessary the JP2 boxes
			opj_image_t *image{};		// decoded image 
			if (!opj_read_header(d_stream, d_codec.get(), &image)) {
				throw "Failed to read the header\n";
			}
			std::unique_ptr<opj_image_t, decltype(&opj_image_destroy)> safeImage(image, &opj_image_destroy);

			std::unique_ptr<FIBITMAP, decltype(&FreeImage_Unload)> dib(nullptr, &FreeImage_Unload);

			// --- header only mode
			if (header_only) {
				// create output image 
				dib.reset(J2KImageToFIBITMAP(s_format_id, image, header_only));
				if (!dib) {
					throw "Failed to import JPEG2000 image";
				}
				return dib.release();
			}

			// decode the stream and fill the image structure 
			if (!(opj_decode(d_codec.get(), d_stream, image) && opj_end_decompress(d_codec.get(), d_stream))) {
				throw "Failed to decode image!\n";
			}

			// create output image 
			dib.reset(J2KImageToFIBITMAP(s_format_id, image, header_only));
			if (!dib) {
				throw "Failed to import JPEG2000 image";
			}

			return dib.release();

		} catch (const char *text) {
			FreeImage_OutputMessageProc(s_format_id, text);
		}
	}
	return nullptr;
}

static FIBOOL DLL_CALLCONV
Save(FreeImageIO *io, FIBITMAP *dib, fi_handle handle, int page, int flags, void *data) {
	J2KFIO_t *fio = (J2KFIO_t*)data;
	if (dib && handle && fio) {
		FIBOOL bSuccess;
		opj_codec_t *c_codec{};	// handle to a compressor
		opj_cparameters_t parameters;	// compression parameters
		opj_image_t *image{};		// image to encode

		// get the OpenJPEG stream
		opj_stream_t *c_stream = fio->stream;

		// set encoding parameters to default values
		opj_set_default_encoder_parameters(&parameters);

		try {
			parameters.tcp_numlayers = 0;
			// if no rate entered, apply a 16:1 rate by default
			if (flags == J2K_DEFAULT) {
				parameters.tcp_rates[0] = (float)16;
			} else {
				// for now, the flags parameter is only used to specify the rate
				parameters.tcp_rates[0] = (float)(flags & 0x3FF);
			}
			parameters.tcp_numlayers++;
			parameters.cp_disto_alloc = 1;

			// convert the dib to a OpenJPEG image
			image = FIBITMAPToJ2KImage(s_format_id, dib, &parameters);
			if (!image) {
				return FALSE;
			}

			// decide if MCT should be used
			parameters.tcp_mct = (image->numcomps == 3) ? 1 : 0;

			// encode the destination image

			// get a J2K compressor handle
			c_codec = opj_create_compress(OPJ_CODEC_J2K);

			// configure the event callbacks
			// catch events using our callbacks (no local context needed here)
			opj_set_info_handler(c_codec, nullptr, nullptr);
			opj_set_warning_handler(c_codec, j2k_warning_callback, nullptr);
			opj_set_error_handler(c_codec, j2k_error_callback, nullptr);

			// setup the encoder parameters using the current image and using user parameters
			opj_setup_encoder(c_codec, &parameters, image);

			// encode the image
			bSuccess = opj_start_compress(c_codec, image, c_stream);
			bSuccess = bSuccess && opj_encode(c_codec, c_stream);
			bSuccess = bSuccess && opj_end_compress(c_codec, c_stream);
			if (!bSuccess) {
				throw "Failed to encode image";
			}

			// free remaining compression structures
			opj_destroy_codec(c_codec);
			
			// free image data
			opj_image_destroy(image);

			return TRUE;

		} catch (const char *text) {
			if (c_codec) opj_destroy_codec(c_codec);
			if (image) opj_image_destroy(image);
			FreeImage_OutputMessageProc(s_format_id, text);
			return FALSE;
		}
	}

	return FALSE;
}

// ==========================================================
//   Init
// ==========================================================

void DLL_CALLCONV
InitJ2K(Plugin *plugin, int format_id) {
	s_format_id = format_id;

	plugin->format_proc = Format;
	plugin->description_proc = Description;
	plugin->extension_proc = Extension;
	plugin->regexpr_proc = RegExpr;
	plugin->open_proc = Open;
	plugin->close_proc = Close;
	plugin->pagecount_proc = nullptr;
	plugin->pagecapability_proc = nullptr;
	plugin->load_proc = Load;
	plugin->save_proc = Save;
	plugin->validate_proc = Validate;
	plugin->mime_proc = MimeType;
	plugin->supports_export_bpp_proc = SupportsExportDepth;
	plugin->supports_export_type_proc = SupportsExportType;
	plugin->supports_icc_profiles_proc = nullptr;
}
