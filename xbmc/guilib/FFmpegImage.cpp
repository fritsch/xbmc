/*
 *      Copyright (C) 2012-2015 Team Kodi
 *      http://kodi.tv
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */
#include "FFmpegImage.h"
#include "utils/log.h"
#include "cores/FFmpeg.h"
#include "guilib/Texture.h"

#include <algorithm>

extern "C"
{
#include <libavutil/imgutils.h>
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/avutil.h"
#include "libswscale/swscale.h"
}

struct MemBuffer
{
  uint8_t* data;
  size_t size;
  size_t pos;
};

struct ThumbDataManagement
{
  uint8_t* intermediateBuffer = nullptr; // gets av_alloced
  AVFrame* frame_input = nullptr;
  AVFrame* frame_temporary = nullptr;
  SwsContext* sws = nullptr;
  AVCodecContext* avOutctx = nullptr;
  AVCodec* codec = nullptr;
  ~ThumbDataManagement()
  {
    av_free(intermediateBuffer);
    intermediateBuffer = nullptr;
    av_frame_free(&frame_input);
    frame_input = nullptr;
    av_frame_free(&frame_temporary);
    frame_temporary = nullptr;
    avcodec_close(avOutctx);
    avcodec_free_context(&avOutctx);
    avOutctx = nullptr;
    sws_freeContext(sws);
    sws = nullptr;
  }
};
static size_t Clamp(int64_t newPosition, size_t bufferSize, size_t offset)
{
  size_t clampedPosition = 0;
  if (newPosition < 0 || offset + newPosition < 0)
  {
    clampedPosition = 0;
  }
  else if (offset + newPosition >= bufferSize)
  {
    clampedPosition = bufferSize;
  }
  else
    clampedPosition = (size_t)newPosition;

  return clampedPosition;
}

static int mem_file_read(void *h, uint8_t* buf, int size)
{
  if (size < 0)
    return -1;

  MemBuffer* mbuf = static_cast<MemBuffer*>(h);
  int64_t unread = mbuf->size - mbuf->pos;
  if (unread < 0)
    return 0;
  
  size_t tocopy = std::min((size_t)size, (size_t)unread);
  memcpy(buf, mbuf->data + mbuf->pos, tocopy);
  mbuf->pos += tocopy;
  return tocopy;
}

static int64_t mem_file_seek(void *h, int64_t pos, int whence)
{
  MemBuffer* mbuf = static_cast<MemBuffer*>(h);
  if (whence == AVSEEK_SIZE)
    return mbuf->size;

  // we want to ignore the AVSEEK_FORCE flag and therefore mask it away
  whence &= ~AVSEEK_FORCE;

  if (whence == SEEK_SET)
  {
    mbuf->pos = Clamp(pos, mbuf->size - 1, 0);
  }
  else if (whence == SEEK_CUR)
  {
    mbuf->pos = Clamp(((int64_t)mbuf->pos) + pos, mbuf->size - 1, mbuf->pos);
  }
  else
    CLog::LogFunction(LOGERROR, __FUNCTION__, "Unknown seek mode: %i", whence);

  return mbuf->pos;
}

CFFmpegImage::CFFmpegImage()
{
  m_hasAlpha = false;
  m_pFrame = nullptr;
  m_outputBuffer = nullptr;
}

CFFmpegImage::~CFFmpegImage()
{
  av_frame_free(&m_pFrame);
  // someone could have forgotten to call us
  CleanupLocalOutputBuffer();
}

bool CFFmpegImage::LoadImageFromMemory(unsigned char* buffer, unsigned int bufSize,
                                      unsigned int width, unsigned int height)
{
  
  uint8_t* fbuffer = (uint8_t*)av_malloc(FFMPEG_FILE_BUFFER_SIZE);
  MemBuffer buf;
  buf.data = buffer;
  buf.size = bufSize;
  buf.pos = 0;

  AVIOContext* ioctx = avio_alloc_context(fbuffer, FFMPEG_FILE_BUFFER_SIZE, 0, &buf,
                                          mem_file_read, NULL, mem_file_seek);

  if (!ioctx)
  {
    av_free(fbuffer);
    CLog::LogFunction(LOGERROR, __FUNCTION__, "Could not allocate AVIOContext");
    return false;
  }

  AVFormatContext* fctx = avformat_alloc_context();
  if (!fctx)
  {
    av_free(ioctx->buffer);
    av_free(ioctx);
    CLog::LogFunction(LOGERROR, __FUNCTION__, "Could not allocate AVFormatContext");
    return false;
  }

  fctx->pb = ioctx;
  ioctx->max_packet_size = FFMPEG_FILE_BUFFER_SIZE;

  if (avformat_open_input(&fctx, "", NULL, NULL) < 0)
  {
    avformat_close_input(&fctx);
    FreeIOCtx(ioctx);
    return false;
  }

  AVCodecContext* codec_ctx = fctx->streams[0]->codec;
  AVCodec* codec = avcodec_find_decoder(codec_ctx->codec_id);
  if (avcodec_open2(codec_ctx, codec, NULL) < 0)
  {
    avformat_close_input(&fctx);
    FreeIOCtx(ioctx);
    return false;
  }

  AVPacket pkt;
  AVFrame* frame = av_frame_alloc();
  av_read_frame(fctx, &pkt);
  int frame_decoded;
  int ret = avcodec_decode_video2(codec_ctx, frame, &frame_decoded, &pkt);
  if (ret < 0)
    CLog::Log(LOGDEBUG, "Error [%d] while decoding frame: %s\n", ret, strerror(AVERROR(ret)));

  if (frame_decoded != 0)
  {
    av_frame_free(&m_pFrame);
    m_pFrame = av_frame_clone(frame);

    if (m_pFrame)
    {
      m_height = m_pFrame->height;
      m_width = m_pFrame->width;
    }    
    else
    {
      CLog::LogFunction(LOGERROR, __FUNCTION__, "Could not allocate a picture data buffer");
      frame_decoded = 0;
    }
  }
  else
    CLog::LogFunction(LOGERROR, __FUNCTION__, "Could not decode a frame");

  av_frame_free(&frame);
  av_free_packet(&pkt);
  avcodec_close(codec_ctx);
  avformat_close_input(&fctx);
  FreeIOCtx(ioctx);

  return (frame_decoded != 0);
}

AVPixelFormat CFFmpegImage::ConvertFormats(AVFrame* frame)
{
  switch (frame->format) {
  case AV_PIX_FMT_YUVJ420P:
    return AV_PIX_FMT_YUV420P;
    break;
  case AV_PIX_FMT_YUVJ422P:
    return AV_PIX_FMT_YUV422P;
    break;
  case AV_PIX_FMT_YUVJ444P:
    return AV_PIX_FMT_YUV444P;
    break;
  case AV_PIX_FMT_YUVJ440P:
    return AV_PIX_FMT_YUV440P;
  default:
    return static_cast<AVPixelFormat>(frame->format);
    break;
  }
}

void CFFmpegImage::FreeIOCtx(AVIOContext* ioctx)
{
  av_free(ioctx->buffer);
  av_free(ioctx);
}

bool CFFmpegImage::Decode(unsigned char * const pixels, unsigned int width, unsigned int height,
                          unsigned int pitch, unsigned int format)
{
  if (m_width == 0 || m_height == 0 || format != XB_FMT_A8R8G8B8)
    return false;

  if (!m_pFrame || !m_pFrame->data)
  {
    CLog::LogFunction(LOGERROR, __FUNCTION__, "AVFrame member not allocated");
    return false;
  }

  AVPicture* pictureRGB = static_cast<AVPicture*>(av_mallocz(sizeof(AVPicture)));

  int size = avpicture_fill(pictureRGB, NULL, AV_PIX_FMT_RGB32, width, height);
  if (size < 0)
  {
    CLog::LogFunction(LOGERROR, __FUNCTION__, "Could not allocate AVPicture member with %i x %i pixes", width, height);
    av_free(pictureRGB);
    return false;
  }

  bool needsCopy = false;
  int pixelsSize = pitch * height;
  if (size == pixelsSize && (int) pitch == pictureRGB->linesize[0])
  {
    // We can use the pixels buffer directly
    pictureRGB->data[0] = pixels;
  }
  else
  {
    // We need an extra buffer and copy it manually afterwards
    if (avpicture_alloc(pictureRGB, AV_PIX_FMT_RGB32, width, height) < 0)
    {
      CLog::LogFunction(LOGERROR, __FUNCTION__, "Could not allocate temp buffer of size %i bytes", size);
      av_free(pictureRGB);
      return false;
    }
    needsCopy = true;
  }

  AVPixelFormat pixFormat = ConvertFormats(m_pFrame);

  struct SwsContext* context = sws_getContext(m_width, m_height, pixFormat,
    width, height, AV_PIX_FMT_RGB32, SWS_BICUBIC, NULL, NULL, NULL);

  sws_scale(context, m_pFrame->data, m_pFrame->linesize, 0, m_height,
    pictureRGB->data, pictureRGB->linesize);
  sws_freeContext(context);
  

  if (needsCopy)
  {
    int minPitch = std::min((int)pitch, pictureRGB->linesize[0]);
    int minHeight = std::min((int)height, (size / pictureRGB->linesize[0]));
    if (minPitch < 0 || minHeight < 0)
    {
      CLog::LogFunction(LOGERROR, __FUNCTION__, "negative pitch or height");
      av_free(pictureRGB);
      return false;
    }

    const unsigned char *src = pictureRGB->data[0];
    unsigned char* dst = pixels;

    for (int y = 0; y < minHeight; y++)
    {
      memcpy(dst, src, minPitch);
      src += pictureRGB->linesize[0];
      dst += pitch;
    }

    avpicture_free(pictureRGB);
  }
  pictureRGB->data[0] = nullptr;
  avpicture_free(pictureRGB);

  m_originalHeight = m_height = height;
  m_originalWidth = m_width = width;

  return true;
}

bool CFFmpegImage::CreateThumbnailFromSurface(unsigned char* bufferin, unsigned int width,
                                             unsigned int height, unsigned int format,
                                             unsigned int pitch,
                                             const std::string& destFile,
                                             unsigned char* &bufferout,
                                             unsigned int &bufferoutSize)
{
  // It seems XB_FMT_A8R8G8B8 mean RGBA and not ARGB
  if (format != XB_FMT_A8R8G8B8)
  {
    CLog::Log(LOGERROR, "Supplied format: %d is not supported.", format);
    return false;
  }

  ThumbDataManagement tdm;

  tdm.codec = avcodec_find_encoder(CODEC_ID_MJPEG);
  if (!tdm.codec)
  {
    CLog::Log(LOGERROR, "Your are missing a working MJPEG encoder");
    return false;
  }

  tdm.avOutctx = avcodec_alloc_context3(tdm.codec);
  if (!tdm.avOutctx)
  {
    CLog::Log(LOGERROR, "Could not allocate context for thumbnail: %s", destFile.c_str());
    return false;
  }

  tdm.avOutctx->height = height;
  tdm.avOutctx->width = width;
  tdm.avOutctx->time_base.num = 1;
  tdm.avOutctx->time_base.den = 1;
  tdm.avOutctx->pix_fmt = AV_PIX_FMT_YUVJ420P;
  tdm.avOutctx->flags = CODEC_FLAG_QSCALE;
  tdm.avOutctx->mb_lmin = tdm.avOutctx->qmin * FF_QP2LAMBDA;
  tdm.avOutctx->mb_lmax = tdm.avOutctx->qmax * FF_QP2LAMBDA;
  tdm.avOutctx->global_quality = tdm.avOutctx->qmin * FF_QP2LAMBDA;

  unsigned int internalBufOutSize = 0;

  int size = avpicture_get_size(tdm.avOutctx->pix_fmt, tdm.avOutctx->width, tdm.avOutctx->height);
  if (size < 0)
  {
    CLog::Log(LOGERROR, "Could not compute picture size for thumbnail: %s", destFile.c_str());
    CleanupLocalOutputBuffer();
    return false;
  }
  internalBufOutSize = (unsigned int) size;

  m_outputBuffer = (uint8_t*) av_malloc(internalBufOutSize);

  if (!m_outputBuffer)
  {
    CLog::Log(LOGERROR, "Could not generate allocate memory for thumbnail: %s", destFile.c_str());
    CleanupLocalOutputBuffer();
    return false;
  }

  tdm.intermediateBuffer = (uint8_t*) av_malloc(internalBufOutSize);
  if (!tdm.intermediateBuffer)
  {
    CLog::Log(LOGERROR, "Could not allocate memory for thumbnail: %s", destFile.c_str());
    CleanupLocalOutputBuffer();
    return false;
  }

  if (avcodec_open2(tdm.avOutctx, tdm.codec, NULL) < 0)
  {
    CLog::Log(LOGERROR, "Could not open avcodec context thumbnail: %s", destFile.c_str());
    CleanupLocalOutputBuffer();
    return false;
  }

  tdm.frame_input = av_frame_alloc();
  if (!tdm.frame_input)
  {
    CLog::Log(LOGERROR, "Could not allocate frame for thumbnail: %s", destFile.c_str());
    CleanupLocalOutputBuffer();
    return false;
  }

  // convert the RGB32 frame to AV_PIX_FMT_YUV420P - we use this later on as AV_PIX_FMT_YUVJ420P
  tdm.frame_temporary = av_frame_alloc();
  if (!tdm.frame_temporary)
  {
    CLog::Log(LOGERROR, "Could not allocate frame for thumbnail: %s", destFile.c_str());
    CleanupLocalOutputBuffer();
    return false;
  }

  if (avpicture_fill((AVPicture*)tdm.frame_temporary, tdm.intermediateBuffer, AV_PIX_FMT_YUV420P, width, height) < 0)
  {
    CLog::Log(LOGERROR, "Could not fill picture for thumbnail: %s", destFile.c_str());
    CleanupLocalOutputBuffer();
    return false;
  }
  //input size == output size which means only pix_fmt conversion
  tdm.sws = sws_getContext(width, height, AV_PIX_FMT_RGB32, width, height, AV_PIX_FMT_YUV420P, 0, 0, 0, 0);
  if (!tdm.sws)
  {
    CLog::Log(LOGERROR, "Could not setup scaling context for thumbnail: %s", destFile.c_str());
    CleanupLocalOutputBuffer();
    return false;
  }

  // Setup jpeg range for sws
  int* inv_table = nullptr;
  int* table = nullptr;
  int srcRange, dstRange, brightness, contrast, saturation;

  if (sws_getColorspaceDetails(tdm.sws, &inv_table, &srcRange, &table, &dstRange, &brightness, &contrast, &saturation) < 0)
  {
    CLog::Log(LOGERROR, "SWS_SCALE failed to get ColorSpaceDetails for thumbnail: %s", destFile.c_str());
    CleanupLocalOutputBuffer();
    return false;
  }
  dstRange = 1; // jpeg full range yuv420p output
  srcRange = 0; // full range RGB32 input
  if (sws_setColorspaceDetails(tdm.sws, inv_table, srcRange, table, dstRange, brightness, contrast, saturation) < 0)
  {
    CLog::Log(LOGERROR, "SWS_SCALE failed to set ColorSpace Details for thumbnail: %s", destFile.c_str());
    CleanupLocalOutputBuffer();
    return false;
  }

  uint8_t* src[] = { bufferin, NULL, NULL, NULL };
  int srcStride[] = { (int) pitch, 0, 0, 0};

  if (sws_scale(tdm.sws, src, srcStride, 0, height, tdm.frame_temporary->data, tdm.frame_temporary->linesize) < 0)
  {
    CLog::Log(LOGERROR, "SWS_SCALE failed for thumbnail: %s", destFile.c_str());
    CleanupLocalOutputBuffer();
    return false;
  }

  tdm.frame_input->pts = 1;
  tdm.frame_input->quality = tdm.avOutctx->global_quality;
  tdm.frame_input->data[0] = (uint8_t*) tdm.frame_temporary->data[0];
  tdm.frame_input->data[1] = (uint8_t*) tdm.frame_temporary->data[1];
  tdm.frame_input->data[2] = (uint8_t*) tdm.frame_temporary->data[2];
  tdm.frame_input->height = height;
  tdm.frame_input->width = width;
  tdm.frame_input->linesize[0] = tdm.frame_temporary->linesize[0];
  tdm.frame_input->linesize[1] = tdm.frame_temporary->linesize[1];
  tdm.frame_input->linesize[2] = tdm.frame_temporary->linesize[2];
  // this is deprecated but mjpeg is not yet transitioned
  tdm.frame_input->format = AV_PIX_FMT_YUVJ420P;

  int got_package = 0;
  AVPacket avpkt;
  av_init_packet(&avpkt);
  avpkt.data = m_outputBuffer;
  avpkt.size = internalBufOutSize;

  if ((avcodec_encode_video2(tdm.avOutctx, &avpkt, tdm.frame_input, &got_package) < 0) || (got_package == 0))
  {
    CLog::Log(LOGERROR, "Could not encode thumbnail: %s", destFile.c_str());
    CleanupLocalOutputBuffer();
    return false;
  }

  bufferoutSize = avpkt.size;
  bufferout = m_outputBuffer;

  return true;
}

void CFFmpegImage::ReleaseThumbnailBuffer()
{
  CleanupLocalOutputBuffer();
}

void CFFmpegImage::CleanupLocalOutputBuffer()
{
  av_free(m_outputBuffer);
  m_outputBuffer = nullptr;
}
