#ifndef FRAME_GRABBER_H_
#define FRAME_GRABBER_H_

#include <stdint.h>
#include <stddef.h>

int grab_frame(uint8_t *in_data, size_t in_size, uint8_t **out_data, size_t *out_size, char **rotate);

#endif