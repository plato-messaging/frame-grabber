#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include "frame_grabber.h"

/**
 * @brief Grabs the first frame of video data provided in a byte array
 * Return the frame in JPEG format. The data is returned in a byte array along with
 * any rotate hints
 *
 * Memory tests:
 * - 100 000 grabs -> memory capped at 21MB (seems to stabilize after the 50 000 first grabs)
 * Performance:
 * ~ 138 grabs per second
 */

const int FG_OK = 200;
const int FG_ERROR_INVALID_INPUT = 422;
const int FG_NOT_FOUND = 404;
const int FG_ERROR_INTERNAL = 500;

/**
 * @brief Dynamic buffer that fills / reads incrementally
 *
 * available size = size - pos
 */
typedef struct DynBuffer
{
  int pos;            // current position in buffer
  int allocated_size; // current allocated size in buffer
  int size;           // buffer target size (e.g.: file size)
  uint8_t *buffer;
} DynBuffer;

static int64_t seek(void *opaque, int64_t offset, int whence)
{
  DynBuffer *c = opaque;

  if (whence == SEEK_CUR)
  {
    if (offset > INT64_MAX - c->pos)
      return -1;
    offset += c->pos;
  }
  else if (whence == SEEK_END)
  {
    if (offset > INT64_MAX - c->size)
      return -1;
    offset += c->size;
  }
  else if (whence == AVSEEK_SIZE)
  {
    return c->size;
  }
  if (offset < 0 || offset > c->size)
    return -1;
  c->pos = offset;
  return 0;
}

// We do not want default header that add multipart boundary "--ffmpeg"
static int ofmt_write_header(AVFormatContext *s)
{
  return 0;
}

// We do not want default trailer that add multipart boundary "--ffmpeg"
static int ofmt_write_trailer(AVFormatContext *s)
{
  return 0;
}

// Simply write data to avio
// Otherwise, multipart headers (Content_Type & Content-Length) are added
static int ofmt_write_packet(AVFormatContext *s, AVPacket *packet)
{
  avio_write(s->pb, packet->data, packet->size);
  avio_flush(s->pb);
  return 0;
}

static ResponseStatus result_from(int code, char *description)
{
  ResponseStatus res = {0};
  res.code = code;
  res.description = description;
  return res;
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
  // position should never exceed size
  if (d->pos > d->size)
  {
    d->size = d->pos;
  }
  return buf_size;
}

static int read_packet(void *opaque, uint8_t *buf, int buf_size)
{
  DynBuffer *d = opaque;
  buf_size = FFMIN(buf_size, d->size - d->pos);

  if (!buf_size)
    return AVERROR_EOF;

  /* copy internal buffer data to buf */
  memcpy(buf, d->buffer + d->pos, buf_size);
  d->pos += buf_size;

  return buf_size;
}

static int read_packet_from_input_stream(void *opaque, uint8_t *buf, int buf_size)
{
  ReadNBytes *pf = opaque;
  buf_size = (*pf)(buf, buf_size);

  if (!buf_size)
    return AVERROR_EOF;

  return buf_size;
}

static ResponseStatus init_encoder(AVFormatContext *out_format_ctx, AVCodecContext *decoder,
                                   AVCodecContext **encoder, AVIOContext *avio_ctx)
{
  int ret;
  ResponseStatus res = {0};
  AVStream *out_stream;
  enum AVCodecID codec_id = av_guess_codec(out_format_ctx->oformat, "mpjpeg", NULL, NULL, AVMEDIA_TYPE_VIDEO);
  AVCodec *encoding_codec = avcodec_find_encoder(codec_id);

  // Init stram
  out_stream = avformat_new_stream(out_format_ctx, NULL);
  if (!out_stream)
  {
    av_log(NULL, AV_LOG_ERROR, "Failed allocating output stream\n");
    return result_from(FG_ERROR_INTERNAL, "Failed allocating output stream");
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

  if (avcodec_parameters_from_context(out_stream->codecpar, *encoder) < 0)
  {
    fprintf(stderr, "Failed to copy parameters to stream");
    return result_from(FG_ERROR_INTERNAL, "Failed to copy parameters to stream");
  }
  if (avcodec_open2(*encoder, encoding_codec, NULL) < 0)
  {
    fprintf(stderr, "Failed to open encoding context\n");
    return result_from(FG_ERROR_INTERNAL, "Failed to open encoding context\n");
  }
  // Set IO context in OUT format context
  // and override header, trailer and content default functions
  out_format_ctx->pb = avio_ctx;

  /* init muxer, write output file header */
  ret = avformat_write_header(out_format_ctx, NULL);
  if (ret < 0)
  {
    return result_from(FG_ERROR_INTERNAL, "Error occurred while writing header to output");
  }
  return result_from(FG_OK, "");
}

static ResponseStatus encode(AVFormatContext *out_format_ctx, AVCodecContext *encoder, AVFrame *src_frame,
                             int width, int height)
{
  int ret = 0;
  int data_size = 0;
  int cursor = 0;
  AVPacket *packet = av_packet_alloc();

  ret = avcodec_send_frame(encoder, src_frame);
  if (ret < 0)
  {
    return result_from(FG_ERROR_INTERNAL, "Error sending a src_frame for encoding");
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
      return result_from(FG_ERROR_INTERNAL, "Error while encoding packet");
    }
    av_write_frame(out_format_ctx, packet);
  };
  av_write_frame(out_format_ctx, NULL);
  av_packet_unref(packet);
  av_packet_free(&packet);
  return result_from(FG_OK, "");
}

static ResponseStatus decode_packet(AVCodecContext *dec, const AVPacket *pkt, AVFrame *frame)
{
  int ret = 0;

  // submit the packet to the decoder
  ret = avcodec_send_packet(dec, pkt);
  if (ret < 0)
  {
    return result_from(FG_ERROR_INTERNAL, "Error submitting a packet for decoding");
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
        return result_from(FG_NOT_FOUND, "No frame found");

      return result_from(FG_ERROR_INTERNAL, "Error while decoding packet");
    }

    if (dec->codec->type == AVMEDIA_TYPE_VIDEO)
      return result_from(FG_OK, "");

    av_frame_unref(frame);
    if (ret < 0)
    {
      return result_from(FG_ERROR_INTERNAL, av_err2str(ret));
    }
  }

  return result_from(FG_NOT_FOUND, "No frame found");
}

static ResponseStatus open_codec_context(int *stream_idx, AVCodecContext **dec_ctx,
                                         AVFormatContext *fmt_ctx, enum AVMediaType type)
{
  int ret, stream_index;
  AVStream *st;
  AVCodec *dec = NULL;
  AVDictionary *opts = NULL;

  ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
  if (ret < 0)
  {
    return result_from(FG_ERROR_INVALID_INPUT, "Could not find video stream");
  }

  stream_index = ret;
  st = fmt_ctx->streams[stream_index];

  /* find decoder for the stream */
  dec = avcodec_find_decoder(st->codecpar->codec_id);
  if (!dec)
  {
    return result_from(FG_ERROR_INVALID_INPUT, "Failed to find codec");
  }

  *dec_ctx = avcodec_alloc_context3(dec);
  if (!*dec_ctx)
  {
    return result_from(FG_ERROR_INTERNAL, "Failed to allocate the video codec");
  }

  /* Copy codec parameters from input stream to output codec context */
  if ((ret = avcodec_parameters_to_context(*dec_ctx, st->codecpar)) < 0)
  {
    return result_from(FG_ERROR_INTERNAL, "Failed to copy video codec parameters to decoder context");
  }

  /* Init the decoders */
  if ((ret = avcodec_open2(*dec_ctx, dec, &opts)) < 0)
  {
    return result_from(FG_ERROR_INTERNAL, "Failed to open video codec");
  }
  *stream_idx = stream_index;

  return result_from(FG_OK, "");
}

ResponseStatus grab_frame(uint8_t *in_data, size_t in_size, ReadNBytes read_n_bytes,
                          uint8_t **out_data, size_t *out_size, char **rotate)
{
  int ret = 0;
  ResponseStatus res = {0};
  size_t buffer_initial_size = 32768;

  AVFormatContext *in_format_ctx = NULL;
  AVCodecContext *decoder = NULL;
  AVStream *video_stream = NULL;

  AVIOContext *read_avio_ctx = NULL;
  uint8_t *read_avio_ctx_buffer = NULL;

  AVFormatContext *out_format_ctx = NULL;
  AVCodecContext *encoder = NULL;

  AVIOContext *write_avio_ctx = NULL;
  uint8_t *write_avio_ctx_buffer = NULL;

  AVFrame *frame = NULL;
  AVPacket *pkt = NULL;
  int video_stream_idx = -1;

  read_avio_ctx_buffer = av_malloc(buffer_initial_size);

  if (!(in_format_ctx = avformat_alloc_context()))
  {
    ret = AVERROR(ENOMEM);
    goto end;
  }

  if (!read_avio_ctx_buffer)
  {
    ret = AVERROR(ENOMEM);
    goto end;
  }

  if (read_n_bytes)
  {
    read_avio_ctx = avio_alloc_context(read_avio_ctx_buffer, buffer_initial_size,
                                       0, &read_n_bytes, &read_packet_from_input_stream, NULL, NULL);
  }
  else
  {
    if (!in_data || in_size == 0)
    {
      res = result_from(FG_ERROR_INVALID_INPUT, "NULL input data or size is 0");
      goto end;
    }
    DynBuffer in_byte_buffer = {0};
    in_byte_buffer.size = in_size;
    in_byte_buffer.buffer = in_data;
    read_avio_ctx = avio_alloc_context(read_avio_ctx_buffer, buffer_initial_size,
                                       0, &in_byte_buffer, &read_packet, NULL, &seek);
  }

  if (!read_avio_ctx)
  {
    ret = AVERROR(ENOMEM);
    goto end;
  }
  in_format_ctx->pb = read_avio_ctx;

  /* open input file, and allocate format context */
  ret = avformat_open_input(&in_format_ctx, NULL, NULL, NULL);
  if (ret < 0)
  {
    res = result_from(FG_ERROR_INVALID_INPUT, "Could not read bytes");
    goto end;
  }

  /* retrieve stream information */
  ret = avformat_find_stream_info(in_format_ctx, NULL);
  if (ret < 0)
  {
    res = result_from(FG_ERROR_INVALID_INPUT, "Could not find stream information");
    goto end;
  }

  res = open_codec_context(&video_stream_idx, &decoder, in_format_ctx, AVMEDIA_TYPE_VIDEO);
  if (res.code != FG_OK)
  {
    goto end;
  }
  video_stream = in_format_ctx->streams[video_stream_idx];
  if (!video_stream)
  {
    res = result_from(FG_NOT_FOUND, "Could not find audio or video stream in the input");
    goto end;
  }

  AVOutputFormat *output_fmt = av_guess_format("mpjpeg", NULL, NULL);
  output_fmt->write_header = ofmt_write_header;
  output_fmt->write_trailer = ofmt_write_trailer;
  output_fmt->write_packet = ofmt_write_packet;
  ret = avformat_alloc_output_context2(&out_format_ctx, output_fmt, "mpjpeg", NULL);
  if (ret < 0)
  {
    res = result_from(FG_ERROR_INTERNAL, "Could not allocate output format context");
    goto end;
  }

  // Output in memory buffer
  DynBuffer out_byte_buffer = {0};
  out_byte_buffer.size = 4096;
  out_byte_buffer.buffer = av_malloc(out_byte_buffer.size);
  write_avio_ctx_buffer = av_malloc(buffer_initial_size);
  write_avio_ctx = avio_alloc_context(write_avio_ctx_buffer, buffer_initial_size, 1,
                                      (void *)&out_byte_buffer, NULL, write_packet, NULL);
  if (!write_avio_ctx)
  {
    res = result_from(FG_ERROR_INTERNAL, "Could not allocate AV I/O context");
    goto end;
  }

  res = init_encoder(out_format_ctx, decoder, &encoder, write_avio_ctx);
  if (res.code != 200)
    goto end;

  frame = av_frame_alloc();
  if (!frame)
  {
    res = result_from(FG_ERROR_INTERNAL, "Could not allocate frame");
    goto end;
  }

  /* initialize packet, set data to NULL, let the demuxer fill it */
  pkt = av_packet_alloc();
  av_init_packet(pkt);
  pkt->data = NULL;
  pkt->size = 0;

  /* read frames from decoder */
  res.code = 0;
  while (av_read_frame(in_format_ctx, pkt) >= 0)
  {
    // check if the packet belongs to a stream we are interested in, otherwise
    // skip it
    if (pkt->stream_index == video_stream_idx)
      res = decode_packet(decoder, pkt, frame);
    av_packet_unref(pkt);
    if (res.code == FG_ERROR_INTERNAL)
      break;
    if (res.code == FG_OK)
      break;
  }

  /* flush the decoders */
  if (decoder)
    avcodec_send_packet(decoder, NULL);

  if (res.code != FG_OK)
  {
    goto end;
  }

  res = encode(out_format_ctx, encoder, frame, decoder->width, decoder->height);
  if (res.code != FG_OK)
  {
    goto end;
  }

  // get rotation if any
  AVDictionaryEntry *rotate_entry = av_dict_get(in_format_ctx->streams[video_stream_idx]->metadata, "rotate", NULL, AV_DICT_MATCH_CASE);
  if (rotate_entry && rotate_entry->value)
  {
    // last +1 is for nul ASCII code for the string
    *rotate = malloc(strlen(rotate_entry->value) + 1);
    *rotate[0] = 0;
    strcpy(*rotate, rotate_entry->value);
  }

end:
  av_packet_free(&pkt);
  avcodec_free_context(&decoder);
  avformat_close_input(&in_format_ctx);
  avformat_free_context(in_format_ctx);
  av_frame_free(&frame);
  if (read_avio_ctx)
    av_freep(&read_avio_ctx->buffer);
  avio_context_free(&read_avio_ctx);

  if (write_avio_ctx)
    av_freep(&write_avio_ctx->buffer);
  avio_context_free(&write_avio_ctx);
  avcodec_free_context(&encoder);
  avformat_free_context(out_format_ctx);

  *out_data = out_byte_buffer.buffer;
  *out_size = out_byte_buffer.size;
  return res;
}

ResponseStatus grab_frame_from_input_stream(ReadNBytes read_n_bytes,
                                            uint8_t **out_data, size_t *out_size,
                                            char **rotate)
{
  return grab_frame(NULL, 0, read_n_bytes, out_data, out_size, rotate);
}

ResponseStatus grab_frame_from_byte_buffer(uint8_t *in_data, size_t in_size,
                                           uint8_t **out_data, size_t *out_size, char **rotate)
{
  return grab_frame(in_data, in_size, NULL, out_data, out_size, rotate);
}
