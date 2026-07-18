// This file is the single translation unit that owns the STB library
// implementations.  Any STB header that requires an IMPLEMENTATION define
// gets it here.  No other .cpp file should define these.
//
// Add further STB headers (stb_image_write, stb_vorbis, etc.) as they are
// needed by later milestones — always here, never elsewhere.

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#define STB_VORBIS_IMPLEMENTATION
#include <stb_vorbis.c>