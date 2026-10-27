// The single translation unit that compiles miniaudio's implementation. Kept separate so
// TVE's strict warning flags don't apply to third-party code.
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include <miniaudio.h>
