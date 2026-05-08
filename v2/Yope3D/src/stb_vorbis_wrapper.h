#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations for stb_vorbis functions
// These are implemented in stb_impl.cpp via stb_vorbis.c

int stb_vorbis_decode_filename(const char *filename, int *channels, int *sample_rate, short **output);
int stb_vorbis_decode_memory(const unsigned char *mem, int len, int *channels, int *sample_rate, short **output);

#ifdef __cplusplus
}
#endif
