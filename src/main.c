#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <libavutil/file.h>
#include "frame_grabber.h"

int main(int argc, char **argv)
{
  int ret;
  const char *src_filename;
  uint8_t *buffer = NULL;
  size_t buffer_size;
  uint8_t *jpeg_data = NULL;
  size_t jpeg_size;
  char *rotate = NULL;
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s  input_file\n",
            argv[0]);
    exit(1);
  }

  src_filename = argv[1];

  /* slurp file content into buffer */
  ret = av_file_map(src_filename, &buffer, &buffer_size, 0, NULL);
  if (ret < 0)
  {
    fprintf(stderr, "Could slurp bytes into buffers");
    exit(1);
  }

  grab_frame(buffer, buffer_size, &jpeg_data, &jpeg_size, &rotate);

  /* write buffer to file */
  FILE *out_file = fopen("in_memory_file.jpeg", "w");
  fwrite(jpeg_data, jpeg_size, 1, out_file);
  fclose(out_file);

  // Cleanup
  av_file_unmap(buffer, buffer_size);
  av_free(jpeg_data);
  return 0;
}