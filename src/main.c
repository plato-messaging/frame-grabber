/**
 * TODO:
 * - Read packet and set them directly in jpeglib writer
 * - Wrap with Rust
 * - Rotate by rotate angle
 * 
 * For reference: how to use JPEGLIB here https://gist.github.com/PhirePhly/3080633
 */

#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>

typedef struct DynBuffer
{
  int pos, allocated_size, size;
  uint8_t *buffer;
} DynBuffer;

uint8_t *avio_ctx_buffer;
size_t buffer_size;
struct DynBuffer *buffer_stream;
FILE *out_file;

/**
 * We do not want default header that add multipart boudary "--ffmpeg"
 */
static int ofmt_write_header(AVFormatContext *s)
{
  return 0;
}

/**
 * We do not want default header that add multipart boudary "--ffmpeg"
 */
static int ofmt_write_trailer(AVFormatContext *s)
{
  return 0;
}

/**
 * Simple write data to avio
 * Otherwise, multipart header (Content_Type & Content-Lenght) are added
 */
static int ofmt_write_packet(AVFormatContext *s, AVPacket *packet)
{
  avio_write(s->pb, packet->data, packet->size);
  avio_flush(s->pb);
  return 0;
}

static int write_packet(void *opaque, uint8_t *buf, int buf_size)
{
  DynBuffer *d = opaque;
  unsigned new_size, new_allocated_size;

  /* reallocate buffer if needed */
  new_size = (unsigned)d->pos + buf_size;
  new_allocated_size = d->allocated_size;
  if (new_size < d->pos || new_size > INT_MAX / 2)
    return -1;
  while (new_size > new_allocated_size)
  {
    if (!new_allocated_size)
      new_allocated_size = new_size;
    else
      new_allocated_size += new_allocated_size / 2 + 1;
  }

  if (new_allocated_size > d->allocated_size)
  {
    int err;
    if ((err = av_reallocp(&d->buffer, new_allocated_size)) < 0)
    {
      d->allocated_size = 0;
      d->size = 0;
      return err;
    }
    d->allocated_size = new_allocated_size;
  }
  memcpy(d->buffer + d->pos, buf, buf_size);
  d->pos = new_size;
  if (d->pos > d->size)
    d->size = d->pos;
  return buf_size;
}

static int init_encoder(AVFormatContext *out_format_ctx, AVCodecContext *decoder,
                        AVCodecContext **encoder, AVIOContext *avio_ctx)
{
  int ret;
  AVStream *out_stream;
  AVOutputFormat *output_fmt = av_guess_format("mpjpeg", NULL, NULL);
  enum AVCodecID codec_id = av_guess_codec(output_fmt, "mpjpeg", NULL, NULL, AVMEDIA_TYPE_VIDEO);
  AVCodec *encoding_codec = avcodec_find_encoder(codec_id);

  // Init stram
  out_stream = avformat_new_stream(out_format_ctx, NULL);
  if (!out_stream)
  {
    av_log(NULL, AV_LOG_ERROR, "Failed allocating output stream\n");
    return AVERROR_UNKNOWN;
  }
  AVRational one = {0};
  one.den = 1;
  one.num = 1;
  *encoder = avcodec_alloc_context3(encoding_codec);
  (*encoder)->height = decoder->height;
  (*encoder)->width = decoder->width;
  (*encoder)->sample_aspect_ratio = decoder->sample_aspect_ratio;
  (*encoder)->pix_fmt = encoding_codec->pix_fmts[0];
  (*encoder)->time_base = one;
  ret = avcodec_parameters_from_context(out_stream->codecpar, *encoder);
  if (ret < 0)
  {
    fprintf(stderr, "Failed to copy parameters to stream");
    return ret;
  }
  avcodec_open2(*encoder, encoding_codec, NULL);
  out_format_ctx->pb = avio_ctx;
  out_format_ctx->oformat->write_header = ofmt_write_header;
  out_format_ctx->oformat->write_trailer = ofmt_write_trailer;
  out_format_ctx->oformat->write_packet = ofmt_write_packet;

  /* init muxer, write output file header */
  ret = avformat_write_header(out_format_ctx, NULL);
  if (ret < 0)
  {
    av_log(NULL, AV_LOG_ERROR, "Error occurred when opening output file\n");
    return ret;
  }
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

  while (ret >= 0)
  {
    ret = avcodec_receive_packet(encoder, packet);

    if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
    {
      av_packet_unref(packet);
      break;
    }
    else if (ret < 0)
    {
      fprintf(stderr, "Error during encoding\n");
      exit(1);
    }
    av_write_frame(out_format_ctx, packet);
  };
  av_packet_unref(packet);
  av_write_trailer(out_format_ctx);
  return ret;
}

static int decode_packet(AVCodecContext *dec, const AVPacket *pkt, AVFrame *frame)
{
  int ret = 0;

  // submit the packet to the decoder
  ret = avcodec_send_packet(dec, pkt);
  if (ret < 0)
  {
    fprintf(stderr, "Error submitting a packet for decoding (%s)\n", av_err2str(ret));
    return ret;
  }

  // get all the available frames from the decoder
  while (ret >= 0)
  {
    ret = avcodec_receive_frame(dec, frame);
    if (ret < 0)
    {
      // those two return values are special and mean there is no output
      // frame available, but there were no errors during decoding
      if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
        return 0;

      fprintf(stderr, "Error during decoding (%s)\n", av_err2str(ret));
      return ret;
    }

    // write the frame data to output file
    if (dec->codec->type == AVMEDIA_TYPE_VIDEO)
      return 200; //output_video_frame(frame);

    av_frame_unref(frame);
    if (ret < 0)
      return ret;
  }

  return ret;
}

static int open_codec_context(const char *src_filename, int *stream_idx,
                              AVCodecContext **dec_ctx, AVFormatContext *fmt_ctx, enum AVMediaType type)
{
  int ret, stream_index;
  AVStream *st;
  AVCodec *dec = NULL;
  AVDictionary *opts = NULL;

  ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
  if (ret < 0)
  {
    fprintf(stderr, "Could not find %s stream in input file '%s'\n",
            av_get_media_type_string(type), src_filename);
    return ret;
  }
  else
  {
    stream_index = ret;
    st = fmt_ctx->streams[stream_index];

    /* find decoder for the stream */
    dec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!dec)
    {
      fprintf(stderr, "Failed to find %s codec\n",
              av_get_media_type_string(type));
      return AVERROR(EINVAL);
    }

    /* Allocate a codec context for the decoder */
    *dec_ctx = avcodec_alloc_context3(dec);
    if (!*dec_ctx)
    {
      fprintf(stderr, "Failed to allocate the %s codec context\n",
              av_get_media_type_string(type));
      return AVERROR(ENOMEM);
    }

    /* Copy codec parameters from input stream to output codec context */
    if ((ret = avcodec_parameters_to_context(*dec_ctx, st->codecpar)) < 0)
    {
      fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n",
              av_get_media_type_string(type));
      return ret;
    }

    /* Init the decoders */
    if ((ret = avcodec_open2(*dec_ctx, dec, &opts)) < 0)
    {
      fprintf(stderr, "Failed to open %s codec\n",
              av_get_media_type_string(type));
      return ret;
    }
    *stream_idx = stream_index;
  }

  return 0;
}

int main(int argc, char **argv)
{
  int ret = 0;
  time_t start = clock();
  AVFormatContext *in_format_ctx = NULL;
  AVCodecContext *decoder = NULL;
  AVStream *video_stream = NULL;
  const char *src_filename = NULL;
  AVFormatContext *out_format_ctx = NULL;
  AVCodecContext *encoder = NULL;

  int video_stream_idx = -1;
  AVFrame *frame = NULL;
  AVPacket pkt;

  if (argc != 2)
  {
    fprintf(stderr, "usage: %s  input_file\n",
            argv[0]);
    exit(1);
  }
  src_filename = argv[1];

  /* open input file, and allocate format context */
  if (avformat_open_input(&in_format_ctx, src_filename, NULL, NULL) < 0)
  {
    fprintf(stderr, "Could not open source file %s\n", src_filename);
    exit(1);
  }

  /* retrieve stream information */
  if (avformat_find_stream_info(in_format_ctx, NULL) < 0)
  {
    fprintf(stderr, "Could not find stream information\n");
    exit(1);
  }

  if (open_codec_context(src_filename, &video_stream_idx, &decoder, in_format_ctx, AVMEDIA_TYPE_VIDEO) >= 0)
  {
    video_stream = in_format_ctx->streams[video_stream_idx];
  }

  /* dump input information to stderr */
  av_dump_format(in_format_ctx, 0, src_filename, 0);

  if (!video_stream)
  {
    fprintf(stderr, "Could not find audio or video stream in the input, aborting\n");
    ret = 1;
    goto end;
  }

  ret = avformat_alloc_output_context2(&out_format_ctx, NULL, "mpjpeg", NULL);
  if (ret < 0)
  {
    fprintf(stderr, "Could not allocate output format context");
    goto end;
  }

  // Output file in memory buffer
  ssize_t buffer_initial_size = 4096;
  uint8_t *avio_ctx_buffer = malloc(buffer_initial_size);
  DynBuffer byte_buffer = {0};
  byte_buffer.size = 4096;
  byte_buffer.buffer = malloc(byte_buffer.size);
  AVIOContext *avio_ctx = avio_alloc_context(avio_ctx_buffer, buffer_initial_size, 1, (void *)&byte_buffer, NULL, write_packet, NULL);
  if (!avio_ctx)
  {
    fprintf(stderr, "Could not allocate AV I/O context");
    goto end;
  }

  // initialize encoder
  out_file = fopen("in_memory_file.jpeg", "w");
  init_encoder(out_format_ctx, decoder, &encoder, avio_ctx);

  frame = av_frame_alloc();
  if (!frame)
  {
    fprintf(stderr, "Could not allocate frame\n");
    ret = AVERROR(ENOMEM);
    goto end;
  }

  /* initialize packet, set data to NULL, let the demuxer fill it */
  av_init_packet(&pkt);
  pkt.data = NULL;
  pkt.size = 0;

  if (video_stream)
    printf("Demuxing video from file '%s'\n", src_filename);

  /* read frames from the file */
  while (av_read_frame(in_format_ctx, &pkt) >= 0)
  {
    // check if the packet belongs to a stream we are interested in, otherwise
    // skip it
    if (pkt.stream_index == video_stream_idx)
      ret = decode_packet(decoder, &pkt, frame);
    av_packet_unref(&pkt);
    if (ret < 0)
      break;
    if (ret == 200)
      break;
  }

  /* flush the decoders */
  if (decoder)
    decode_packet(decoder, NULL, frame);

  printf("Demuxing succeeded.\n");

  if (ret == 200)
  {
    printf("GOT FRAME!!!!\n");
    encode(out_format_ctx, encoder, frame, decoder->width, decoder->height);
    fclose(out_file);
  }

end:
  avcodec_free_context(&decoder);
  avformat_close_input(&in_format_ctx);
  av_frame_free(&frame);
  av_free(avio_ctx_buffer);
  avformat_free_context(out_format_ctx);
  avcodec_free_context(&encoder);
  av_free(avio_ctx);

  /* write buffer to file */
  printf("Byte buffer size: %d\n", byte_buffer.size);
  FILE *out_file = fopen("in_memory_file.jpeg", "w");
  fwrite(byte_buffer.buffer, byte_buffer.size, 1, out_file);
  fclose(out_file);
  av_free(byte_buffer.buffer);
  unsigned long millis = (clock() - start) * 1000 / CLOCKS_PER_SEC;
  printf("Took %ldms\n", millis);
  return ret < 0;
}
