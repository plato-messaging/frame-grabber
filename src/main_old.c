// instructions to setup XCode
// https://airejie.medium.com/setting-up-xcode-for-c-projects-17531c3c3941

#include <stdio.h>
#include <time.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <jpeglib.h>

/**
 * TODO:
 * - Read packet and set them directly in jpeglib writer
 * - Wrap with Rust
 * - Rotate by rotate angle
 * 
 * For reference: how to use JPEGLIB here https://gist.github.com/PhirePhly/3080633
 */

typedef struct ByteBuffer
{
  uint8_t *buf;
  int capacity;
} ByteBuffer;

uint8_t *avio_ctx_buffer;
ssize_t buffer_size;
struct ByteBuffer *buffer_stream;

static void init_format_with_input(AVFormatContext **format_ctx, const char *url)
{
  int error = avformat_open_input(format_ctx, url, NULL, NULL);
  if (error != 0)
  {
    printf("Opening file %s failed with error %i\n", url, error);
    exit(1);
  }
  return;
}

static int find_video_stream_decoder(AVFormatContext *format_ctx, AVCodecContext **decoder)
{
  AVCodec *codec = NULL;
  unsigned int stream_index = av_find_best_stream(format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
  *decoder = format_ctx->streams[stream_index]->codec;
  avcodec_open2(*decoder, codec, NULL);
  return stream_index;
}

static int init_video_decoder(AVFormatContext *format_ctx, AVCodecContext **decoder)
{
  int error = avformat_find_stream_info(format_ctx, NULL);
  if (error != 0)
  {
    printf("Could not find stream info\n");
    exit(1);
  }
  int stream_index = find_video_stream_decoder(format_ctx, decoder);
  printf("Index of video streams: %i\n", stream_index);
  printf("Video codec name: %s\n", (*decoder)->codec->long_name);
  printf("Pixel format: %s\n", av_get_pix_fmt_name((*decoder)->pix_fmt));
  printf("Video dimension are w: %i, h: %i\n", (*decoder)->width, (*decoder)->height);
  return stream_index;
}

static int write_packet(void *opaque, uint8_t *buf, int buf_size)
{
  struct ByteBuffer *out = (struct ByteBuffer *)opaque;
  memcpy(out->buf + out->capacity, buf, buf_size);
  out->capacity += buf_size;
  return buf_size;
}

static int init_encoder(AVFormatContext *format_ctx, AVCodecContext *decoder,
                        AVCodecContext **encoder, char *rotate_value, AVIOContext *avio_ctx)
{
  int ret;
  AVStream *out_stream;
  AVOutputFormat *output_fmt = av_guess_format(NULL, "image.jpg", NULL);
  enum AVCodecID codec_id = av_guess_codec(output_fmt, NULL, "image.jpg", NULL, AVMEDIA_TYPE_VIDEO);
  AVCodec *encoding_codec = avcodec_find_encoder(codec_id);

  // Init stram
  out_stream = avformat_new_stream(format_ctx, NULL);
  if (!out_stream)
  {
    av_log(NULL, AV_LOG_ERROR, "Failed allocating output stream\n");
    return AVERROR_UNKNOWN;
  }
  *encoder = avcodec_alloc_context3(encoding_codec);
  (*encoder)->height = decoder->height;
  (*encoder)->width = decoder->width;
  (*encoder)->sample_aspect_ratio = decoder->sample_aspect_ratio;
  (*encoder)->pix_fmt = encoding_codec->pix_fmts[0];
  (*encoder)->time_base = av_inv_q(decoder->framerate);
  if (rotate_value)
  {
    av_dict_set(&format_ctx->metadata, "rotate", rotate_value, AV_DICT_MATCH_CASE);
    av_dict_set(&format_ctx->streams[0]->metadata, "rotate", rotate_value, AV_DICT_MATCH_CASE);
  }
  ret = avcodec_parameters_from_context(out_stream->codecpar, *encoder);
  if (ret < 0)
  {
    fprintf(stderr, "Failed to copy parameters to stream");
    return ret;
  }
  avcodec_open2(*encoder, encoding_codec, NULL);
  format_ctx->pb = avio_ctx;

  /* init muxer, write output file header */
  ret = avformat_write_header(format_ctx, NULL);
  if (ret < 0)
  {
    av_log(NULL, AV_LOG_ERROR, "Error occurred when opening output file\n");
    return ret;
  }
  return 0;
}

static int send_video_packet(AVFormatContext *format_ctx, AVCodecContext *decoder, unsigned int stream_index)
{
  AVPacket *packet = av_packet_alloc();

  // Consider only packet with the appropriate stream index
  do
  {
    int read_frame_result = av_read_frame(format_ctx, packet);
    if (read_frame_result != 0)
    {
      fprintf(stderr, "Could not read first src_frame");
      goto end;
    }
  } while (packet->stream_index != stream_index);

  int send_packet_result = avcodec_send_packet(decoder, packet);
  if (send_packet_result != 0)
  {
    fprintf(stderr, "Error while sending a packet for decoding\n");
    goto end;
  }
end:
  av_packet_free(&packet);
  return 0;
}

static int read_first_frame(AVFormatContext *format_ctx, AVCodecContext *decoder,
                            AVFrame *frame, unsigned int stream_index)
{
  int ret;
  int receive_frame_result;
  do
  {
    ret = send_video_packet(format_ctx, decoder, stream_index);

    receive_frame_result = avcodec_receive_frame(decoder, frame);
    if (receive_frame_result == 0)
      break;
    if (receive_frame_result == AVERROR_EOF)
    {
      fprintf(stderr, "Reached end of file before a src_frame was returned");
      exit(1);
    }
    if (receive_frame_result != AVERROR(EAGAIN))
    {
      fprintf(stderr, "Could not process src_frame");
      exit(1);
    }
  } while (receive_frame_result == AVERROR(EAGAIN));

end:
  return 0;
}

static int encode(AVFormatContext *out_format_ctx, AVCodecContext *encoder, AVFrame *src_frame,
                  int width, int height)
{
  int ret;
  int data_size = 0;
  int cursor = 0;
  AVPacket *packet = av_packet_alloc();
  AVFrame *dst_frame = av_frame_alloc();

  ret = avformat_write_header(out_format_ctx, NULL);
  if (ret < 0)
  {
    fprintf(stderr, "Error writing header\n");
    exit(1);
  }
  ret = avcodec_send_frame(encoder, src_frame);
  if (ret < 0)
  {
    fprintf(stderr, "Error sending a src_frame for encoding\n");
    exit(1);
  }

  do
  {
    ret = avcodec_receive_packet(encoder, packet);
    if (ret != AVERROR(EAGAIN))
    {
      if (ret == AVERROR_EOF)
      {
        fprintf(stderr, "Reached EOF");
        exit(1);
      }
      else if (ret < 0)
      {
        fprintf(stderr, "Error during encoding\n");
        exit(1);
      }
    }
    av_write_frame(out_format_ctx, packet);
  } while (ret != AVERROR(EAGAIN));
  av_packet_unref(packet);
  av_write_trailer(out_format_ctx);
  return ret;
}

int main(int argc, const char *argv[])
{
  int ret;
  time_t start = clock();
  AVFormatContext *in_format_ctx = NULL;
  AVCodecContext *decoder = NULL;

  // initialize decoder
  init_format_with_input(&in_format_ctx, argv[1]);
  int stream_index = init_video_decoder(in_format_ctx, &decoder);

  AVFormatContext *out_format_ctx = NULL;
  AVCodecContext *encoder = NULL;
  ret = avformat_alloc_output_context2(&out_format_ctx, NULL, NULL, "image.jpg");
  if (ret < 0)
  {
    fprintf(stderr, "Could not allocate output format context");
    goto end;
  }

  // get rotation if any
  AVDictionaryEntry *rotate_entry = av_dict_get(in_format_ctx->streams[stream_index]->metadata, "rotate", NULL, AV_DICT_MATCH_CASE);
  char *rotate_value = NULL;
  if (rotate_entry && rotate_entry->value)
  {
    // last +1 is for nul ASCII code for the string
    rotate_value = (char *)malloc(strlen(rotate_entry->value) + 1);
    strcpy(rotate_value, rotate_entry->value);
  }

  // Output file in memory buffer
  ssize_t buffer_initial_size = 4096;
  uint8_t *avio_ctx_buffer = av_malloc(buffer_initial_size);
  ByteBuffer byte_buffer = {0};
  byte_buffer.capacity = 1024;
  byte_buffer.buf = av_malloc(byte_buffer.capacity);
  AVIOContext *avio_ctx = avio_alloc_context(avio_ctx_buffer, buffer_initial_size, 1, (void *)&byte_buffer, NULL, write_packet, NULL);

  if (!avio_ctx)
  {
    fprintf(stderr, "Could not allocate AV I/O context");
    goto end;
  }

  // initialize encoder
  init_encoder(out_format_ctx, decoder, &encoder, rotate_value, avio_ctx);

  AVFrame *src_frame = av_frame_alloc();
  int result = read_first_frame(in_format_ctx, decoder, src_frame, stream_index);
  if (result != 0)
  {
    goto end;
  }

  uint8_t *data = malloc(0);
  encode(out_format_ctx, encoder, src_frame, decoder->width, decoder->height);

end:
  // Free
  free(avio_ctx_buffer);
  av_frame_free(&src_frame);
  avformat_close_input(&in_format_ctx);
  avformat_free_context(in_format_ctx);
  avformat_free_context(out_format_ctx);
  free(rotate_value);
  avcodec_free_context(&encoder);
  // av_freep(&avio_ctx->buffer);
  av_free(avio_ctx);

  /* write buffer to file */
  {
    FILE *out_file = fopen("in_memory_file.jpeg", "w");
    if (!out_file)
    {
      fprintf(stderr, "Could not open file '%s'\n", "in_memory_file.jpeg");
      ret = AVERROR(errno);
    }
    else
    {
      fwrite(byte_buffer.buf, byte_buffer.capacity, 1, out_file);
      fclose(out_file);
    }
  }

  av_free(byte_buffer.buf);
  unsigned long millis = (clock() - start) * 1000 / CLOCKS_PER_SEC;
  printf("Took %ldms\n", millis);
  // read_thumbnail();
}