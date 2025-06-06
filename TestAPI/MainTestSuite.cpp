// ==========================================================
// FreeImage 3 Test Script
//
// Design and implementation by
// - Herv� Drolon (drolon@infonie.fr)
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


#include "TestSuite.h"
#include "FreeImage.hpp"

// ----------------------------------------------------------

/**
	FreeImage error handler
	@param fif Format / Plugin responsible for the error 
	@param message Error message
*/
void FreeImageErrorHandler(FREE_IMAGE_FORMAT fif, const char *message) {
	printf("\n*** "); 
	if(fif != FIF_UNKNOWN) {
		printf("%s Format\n", FreeImage_GetFormatFromFIF(fif));
	}
	printf("%s", message);
	printf(" ***\n");
}

// ----------------------------------------------------------

int main(int argc, char *argv[]) {
	unsigned width  = 512;
	unsigned height = 512;

#if defined(_DEBUG) && defined(WIN32)
	// check for memory leaks at program exit (after the 'return 0')
	// through a call to _CrtDumpMemoryLeaks 
	// note that in debug mode, objects allocated with the new operator 
	// may be destroyed *after* the end of the main function. 
	// OpenEXR
	// note that OpenEXR produce so called "false memory leaks"
	// see http://lists.nongnu.org/archive/html/openexr-devel/2013-11/msg00000.html
	_CrtSetDbgFlag(_CRTDBG_LEAK_CHECK_DF|_CRTDBG_ALLOC_MEM_DF);
#endif

#if defined(FREEIMAGE_LIB) || !defined(WIN32)
	FreeImage_Initialise();
#endif

	// initialize our own FreeImage error handler
	FreeImage_SetOutputMessage(FreeImageErrorHandler);

	// test plugins capabilities
	showPlugins();

	// test internal image types
	testImageType(width, height);

#if FREEIMAGE_WITH_LIBJPEG
	// test the clone function
	testAllocateCloneUnload("exif.jpg");

	// test JPEG lossless transform & cropping
	testJPEG();

	// test Exif raw metadata loading & saving
	testExifRaw();

	// test thumbnail functions
	testThumbnail("exif.jpg", 0);

	// test wrapped user buffer
	testWrappedBuffer("exif.jpg", 0);

	// test views
	testCreateView("exif.jpg", 0);
#endif

#if FREEIMAGE_WITH_LIBTIFF
	// test loading / saving / converting image types using the TIFF plugin
	testImageTypeTIFF(width, height);

	// test multipage streaming
	testStreamMultiPage("sample.tif");

	// test multipage streaming with memory IO
	testMultiPageMemory("sample.tif");
#endif

#if FREEIMAGE_WITH_LIBPNG
	// test memory IO
	testMemIO("sample.png");

	// test multipage functions
	testMultiPage("sample.png");

	// test get/set channel
	testImageChannels(width, height);
#endif

#if FREEIMAGE_WITH_LIBJXR
	// test memory IO
	testMemIO("exif.jxr");
#endif

#if FREEIMAGE_WITH_LIBHEIF
	testHeif(FIF_HEIF, "exif.heic", "heif_out.heic");
	testHeif(FIF_AVIF, "exif.avif", "avif_out.avif");
#endif

#if FREEIMAGE_WITH_LIBPNG && FREEIMAGE_WITH_LIBJPEG
	// test loading header only
	testHeaderOnly();
#endif
	

#if defined(FREEIMAGE_LIB) || !defined(WIN32)
	FreeImage_DeInitialise();
#endif

	// other tests
	testConvertToFloat();
	testConvertToColor();
	testFindMinMax();
	testTmoClamp();
	testTmoLinear();
	testHistogram();

	return 0;
}

