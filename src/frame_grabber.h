#ifndef FRAME_GRABBER_H_
#define FRAME_GRABBER_H_

#include <stdint.h>
#include <stddef.h>

/** Everything went well */
extern const int FG_OK;
/** Data is invalid for some reason (no stream found, corrupted file...) */
extern const int FG_ERROR_INVALID_INPUT;
/** No frame found */
extern const int FG_NOT_FOUND;
/** Internal problem */
extern const int FG_ERROR_INTERNAL;

typedef int (*ReadNBytes)(uint8_t *, size_t);

typedef struct ResponseStatus
{
  /** An FG_* value */
  int code;
  /** Description of the code is different from FG_OK */
  char *description;
} ResponseStatus;

ResponseStatus grab_frame_from_byte_buffer(uint8_t *in_data, size_t in_size, uint8_t **out_data, size_t *out_size, char **rotate);

ResponseStatus grab_frame_from_input_stream(ReadNBytes read_n_bytes, uint8_t **out_data, size_t *out_size, char **rotate);

#endif