/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://xbmc.org
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */
#include "system.h"
#ifdef HAVE_LIBVA
#include "windowing/WindowingFactory.h"
#include "VAAPI.h"
#include "DVDVideoCodec.h"
#include "utils/log.h"
#include "threads/SingleLock.h"
#include "settings/Settings.h"
#include "guilib/GraphicContext.h"
#include "settings/MediaSettings.h"
#include <va/va_glx.h>

extern "C" {
#include "libavutil/avutil.h"
}

using namespace VAAPI;
#define NUM_RENDER_PICS 7

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

CVAAPIContext *CVAAPIContext::m_context = 0;
CCriticalSection CVAAPIContext::m_section;
Display *CVAAPIContext::m_X11dpy = 0;

CVAAPIContext::CVAAPIContext()
{
  m_context = 0;
  m_refCount = 0;
  m_attributes = NULL;
  m_profiles = NULL;
}

void CVAAPIContext::Release()
{
  CSingleLock lock(m_section);

  m_refCount--;
  if (m_refCount <= 0)
  {
    Close();
    delete this;
    m_context = 0;
  }
}

void CVAAPIContext::Close()
{
  CLog::Log(LOGNOTICE, "VAAPI::Close - closing decoder context");
  DestroyContext();
}

bool CVAAPIContext::EnsureContext(CVAAPIContext **ctx)
{
  CSingleLock lock(m_section);

  if (m_context)
  {
    m_context->m_refCount++;
    *ctx = m_context;
    return true;
  }

  m_context = new CVAAPIContext();
  *ctx = m_context;
  {
    CSingleLock gLock(g_graphicsContext);
    if (!m_context->CreateContext())
    {
      delete m_context;
      m_context = 0;
      *ctx = NULL;
      return false;
    }
  }

  m_context->m_refCount++;

  *ctx = m_context;
  return true;
}

bool CVAAPIContext::CreateContext()
{
  { CSingleLock lock(g_graphicsContext);
    if (!m_X11dpy)
      m_X11dpy = XOpenDisplay(NULL);
  }

  m_display = vaGetDisplayGLX(m_X11dpy);

  int major_version, minor_version;
  if (!CheckSuccess(vaInitialize(m_display, &major_version, &minor_version)))
  {
    return false;
  }

  CLog::Log(LOGDEBUG, "VAAPI - initialize version %d.%d", major_version, minor_version);


  QueryCaps();
  if (!m_profileCount || !m_attributeCount)
    return false;

  return true;
}

void CVAAPIContext::DestroyContext()
{
  delete[] m_attributes;
  delete[] m_profiles;
}

void CVAAPIContext::QueryCaps()
{
  m_attributeCount = 0;
  m_profileCount = 0;

  int max_attributes = vaMaxNumDisplayAttributes(m_display);
  m_attributes = new VADisplayAttribute[max_attributes];

  if (!CheckSuccess(vaQueryDisplayAttributes(m_display, m_attributes, &m_attributeCount)))
    return;

  for(int i = 0; i < m_attributeCount; i++)
  {
    VADisplayAttribute * const display_attr = &m_attributes[i];
    CLog::Log(LOGDEBUG, "VAAPI - attrib %d (%s/%s) min %d max %d value 0x%x\n"
                       , display_attr->type
                       ,(display_attr->flags & VA_DISPLAY_ATTRIB_GETTABLE) ? "get" : "---"
                       ,(display_attr->flags & VA_DISPLAY_ATTRIB_SETTABLE) ? "set" : "---"
                       , display_attr->min_value
                       , display_attr->max_value
                       , display_attr->value);
  }

  int max_profiles = vaMaxNumProfiles(m_display);
  m_profiles = new VAProfile[max_profiles];

  if (!CheckSuccess(vaQueryConfigProfiles(m_display, m_profiles, &m_profileCount)))
    return;

  for(int i = 0; i < m_profileCount; i++)
    CLog::Log(LOGDEBUG, "VAAPI - profile %d", m_profiles[i]);
}

VAConfigAttrib CVAAPIContext::GetAttrib(VAProfile profile)
{
  CSingleLock lock(m_section);

  VAConfigAttrib attrib;
  attrib.type = VAConfigAttribRTFormat;
  CheckSuccess(vaGetConfigAttributes(m_display, profile, VAEntrypointVLD, &attrib, 1));

  return attrib;
}

bool CVAAPIContext::SupportsProfile(VAProfile profile)
{
  CSingleLock lock(m_section);

  for (int i=0; i<m_profileCount; i++)
  {
    if (m_profiles[i] == profile)
      return true;
  }
  return false;
}

VAConfigID CVAAPIContext::CreateConfig(VAProfile profile, VAConfigAttrib attrib)
{
  CSingleLock lock(m_section);

  VAConfigID config = VA_INVALID_ID;
  CheckSuccess(vaCreateConfig(m_display, profile, VAEntrypointVLD, &attrib, 1, &config));

  return config;
}

bool CVAAPIContext::CheckSuccess(VAStatus status)
{
  if (status != VA_STATUS_SUCCESS)
  {
    CLog::Log(LOGERROR, "VAAPI error: %s", vaErrorStr(status));
    return false;
  }
  return true;
}

VADisplay CVAAPIContext::GetDisplay()
{
  return m_display;
}

//-----------------------------------------------------------------------------
// VAAPI Video Surface states
//-----------------------------------------------------------------------------

#define SURFACE_USED_FOR_REFERENCE 0x01
#define SURFACE_USED_FOR_RENDER    0x02

void CVideoSurfaces::AddSurface(VASurfaceID surf)
{
  CSingleLock lock(m_section);
  m_state[surf] = 0;
  m_freeSurfaces.push_back(surf);
}

void CVideoSurfaces::ClearReference(VASurfaceID surf)
{
  CSingleLock lock(m_section);
  if (m_state.find(surf) == m_state.end())
  {
    CLog::Log(LOGWARNING, "CVideoSurfaces::ClearReference - surface invalid");
    return;
  }
  m_state[surf] &= ~SURFACE_USED_FOR_REFERENCE;
  if (m_state[surf] == 0)
  {
    m_freeSurfaces.push_back(surf);
  }
}

bool CVideoSurfaces::MarkRender(VASurfaceID surf)
{
  CSingleLock lock(m_section);
  if (m_state.find(surf) == m_state.end())
  {
    CLog::Log(LOGWARNING, "CVideoSurfaces::MarkRender - surface invalid");
    return false;
  }
  std::list<VASurfaceID>::iterator it;
  it = std::find(m_freeSurfaces.begin(), m_freeSurfaces.end(), surf);
  if (it != m_freeSurfaces.end())
  {
    m_freeSurfaces.erase(it);
  }
  m_state[surf] |= SURFACE_USED_FOR_RENDER;
  return true;
}

void CVideoSurfaces::ClearRender(VASurfaceID surf)
{
  CSingleLock lock(m_section);
  if (m_state.find(surf) == m_state.end())
  {
    CLog::Log(LOGWARNING, "CVideoSurfaces::ClearRender - surface invalid");
    return;
  }
  m_state[surf] &= ~SURFACE_USED_FOR_RENDER;
  if (m_state[surf] == 0)
  {
    m_freeSurfaces.push_back(surf);
  }
}

bool CVideoSurfaces::IsValid(VASurfaceID surf)
{
  CSingleLock lock(m_section);
  if (m_state.find(surf) != m_state.end())
    return true;
  else
    return false;
}

VASurfaceID CVideoSurfaces::GetFree(VASurfaceID surf)
{
  CSingleLock lock(m_section);
  if (m_state.find(surf) != m_state.end())
  {
    std::list<VASurfaceID>::iterator it;
    it = std::find(m_freeSurfaces.begin(), m_freeSurfaces.end(), surf);
    if (it == m_freeSurfaces.end())
    {
      CLog::Log(LOGWARNING, "CVideoSurfaces::GetFree - surface not free");
    }
    else
    {
      m_freeSurfaces.erase(it);
      m_state[surf] = SURFACE_USED_FOR_REFERENCE;
      return surf;
    }
  }

  if (!m_freeSurfaces.empty())
  {
    VASurfaceID freeSurf = m_freeSurfaces.front();
    m_freeSurfaces.pop_front();
    m_state[freeSurf] = SURFACE_USED_FOR_REFERENCE;
    return freeSurf;
  }

  return VA_INVALID_SURFACE;
}

VASurfaceID CVideoSurfaces::GetAtIndex(int idx)
{
  if (idx >= m_state.size())
    return VA_INVALID_SURFACE;

  std::map<VASurfaceID, int>::iterator it = m_state.begin();
  for(int i = 0; i < idx; i++)
    ++it;
  return it->first;
}

VASurfaceID CVideoSurfaces::RemoveNext(bool skiprender)
{
  CSingleLock lock(m_section);
  VASurfaceID surf;
  std::map<VASurfaceID, int>::iterator it;
  for(it = m_state.begin(); it != m_state.end(); ++it)
  {
    if (skiprender && it->second & SURFACE_USED_FOR_RENDER)
      continue;
    surf = it->first;
    m_state.erase(surf);

    std::list<VASurfaceID>::iterator it2;
    it2 = std::find(m_freeSurfaces.begin(), m_freeSurfaces.end(), surf);
    if (it2 != m_freeSurfaces.end())
      m_freeSurfaces.erase(it2);
    return surf;
  }
  return VA_INVALID_SURFACE;
}

void CVideoSurfaces::Reset()
{
  CSingleLock lock(m_section);
  m_freeSurfaces.clear();
  m_state.clear();
}

int CVideoSurfaces::Size()
{
  CSingleLock lock(m_section);
  return m_state.size();
}

//-----------------------------------------------------------------------------
// VAAPI
//-----------------------------------------------------------------------------

// settings codecs mapping
DVDCodecAvailableType g_vaapi_available[] = {
  { AV_CODEC_ID_H263, "videoplayer.usevaapimpeg4" },
  { AV_CODEC_ID_MPEG4, "videoplayer.usevaapimpeg4" },
  { AV_CODEC_ID_WMV3, "videoplayer.usevaapivc1" },
  { AV_CODEC_ID_VC1, "videoplayer.usevaapivc1" },
  { AV_CODEC_ID_MPEG2VIDEO, "videoplayer.usevaapimpeg2" },
};
const size_t settings_count = sizeof(g_vaapi_available) / sizeof(DVDCodecAvailableType);

CDecoder::CDecoder() : m_vaapiOutput(&m_inMsgEvent)
{
  m_vaapiConfig.videoSurfaces = &m_videoSurfaces;

  m_vaapiConfigured = false;
  m_DisplayState = VAAPI_OPEN;
  m_vaapiConfig.context = 0;
  m_vaapiConfig.contextId = VA_INVALID_ID;
  m_vaapiConfig.configId = VA_INVALID_ID;
}

CDecoder::~CDecoder()
{
  Close();
}

bool CDecoder::Open(AVCodecContext* avctx, const enum PixelFormat fmt, unsigned int surfaces)
{
  // check if user wants to decode this format with VAAPI
  if (CDVDVideoCodec::IsCodecDisabled(g_vaapi_available, settings_count, avctx->codec_id))
    return false;

  CLog::Log(LOGDEBUG,"VAAPI - open decoder");

  if (!CVAAPIContext::EnsureContext(&m_vaapiConfig.context))
    return false;

  m_vaapiConfig.vidWidth = avctx->width;
  m_vaapiConfig.vidHeight = avctx->height;
  m_vaapiConfig.outWidth = avctx->width;
  m_vaapiConfig.outHeight = avctx->height;
  m_vaapiConfig.surfaceWidth = avctx->width;
  m_vaapiConfig.surfaceHeight = avctx->height;
  m_vaapiConfig.numRenderBuffers = surfaces;
  m_vaapiConfig.dpy = m_vaapiConfig.context->GetDisplay();
  m_decoderThread = CThread::GetCurrentThreadId();
  m_DisplayState = VAAPI_OPEN;
  m_vaapiConfigured = false;
  m_presentPicture = 0;

  VAProfile profile;
  switch (avctx->codec_id)
  {
    case AV_CODEC_ID_MPEG2VIDEO:
      profile = VAProfileMPEG2Main;
      if (!m_vaapiConfig.context->SupportsProfile(profile))
        return false;
      break;
    case AV_CODEC_ID_MPEG4:
    case AV_CODEC_ID_H263:
      profile = VAProfileMPEG4AdvancedSimple;
      if (!m_vaapiConfig.context->SupportsProfile(profile))
        return false;
      break;
    case AV_CODEC_ID_H264:
    {
      if (avctx->profile == FF_PROFILE_H264_BASELINE)
      {
        profile = VAProfileH264Baseline;
        if (!m_vaapiConfig.context->SupportsProfile(profile))
          return false;
      }
      else
      {
        if(avctx->profile == FF_PROFILE_H264_MAIN)
        {
          profile = VAProfileH264Main;
          if (m_vaapiConfig.context->SupportsProfile(profile))
            break;
        }
        profile = VAProfileH264High;
        if (!m_vaapiConfig.context->SupportsProfile(profile))
          return false;
      }
      break;
    }
    case AV_CODEC_ID_WMV3:
      profile = VAProfileVC1Main;
      if (!m_vaapiConfig.context->SupportsProfile(profile))
        return false;
      break;
    case AV_CODEC_ID_VC1:
      profile = VAProfileVC1Advanced;
      if (!m_vaapiConfig.context->SupportsProfile(profile))
        return false;
      break;
    default:
      return false;
  }

  m_vaapiConfig.profile = profile;
  m_vaapiConfig.attrib = m_vaapiConfig.context->GetAttrib(profile);
  if ((m_vaapiConfig.attrib.value & VA_RT_FORMAT_YUV420) == 0)
  {
    CLog::Log(LOGERROR, "VAAPI - invalid yuv format %x", m_vaapiConfig.attrib.value);
    return false;
  }

  if(avctx->codec_id == AV_CODEC_ID_H264)
  {
    m_vaapiConfig.maxReferences = avctx->refs;
    if (m_vaapiConfig.maxReferences > 16)
      m_vaapiConfig.maxReferences = 16;
    if (m_vaapiConfig.maxReferences < 5)
      m_vaapiConfig.maxReferences = 5;
  }
  else
    m_vaapiConfig.maxReferences = 2;

  if (!ConfigVAAPI())
  {
    return false;
  }

  m_hwContext.config_id = m_vaapiConfig.configId;
  m_hwContext.display = m_vaapiConfig.dpy;

  avctx->hwaccel_context = &m_hwContext;
  avctx->thread_count = 1;
  avctx->get_buffer2 = CDecoder::FFGetBuffer;
  avctx->slice_flags = SLICE_FLAG_CODED_ORDER|SLICE_FLAG_ALLOW_FIELD;
  return true;
}

void CDecoder::Close()
{
  CLog::Log(LOGNOTICE, "VAAPI::%s", __FUNCTION__);

  CSingleLock lock(m_DecoderSection);

  FiniVAAPIOutput();

  if (m_vaapiConfig.context)
    m_vaapiConfig.context->Release();
  m_vaapiConfig.context = 0;
}

long CDecoder::Release()
{
  // check if we should do some pre-cleanup here
  // a second decoder might need resources
  if (m_vaapiConfigured == true)
  {
    CSingleLock lock(m_DecoderSection);
    CLog::Log(LOGNOTICE,"VAAPI::Release pre-cleanup");

    Message *reply;
    if (m_vaapiOutput.m_controlPort.SendOutMessageSync(COutputControlProtocol::PRECLEANUP,
                                                   &reply,
                                                   2000))
    {
      bool success = reply->signal == COutputControlProtocol::ACC ? true : false;
      reply->Release();
      if (!success)
      {
        CLog::Log(LOGERROR, "VAAPI::%s - pre-cleanup returned error", __FUNCTION__);
        m_DisplayState = VAAPI_ERROR;
      }
    }
    else
    {
      CLog::Log(LOGERROR, "VAAPI::%s - pre-cleanup timed out", __FUNCTION__);
      m_DisplayState = VAAPI_ERROR;
    }

    VASurfaceID surf;
    while((surf = m_videoSurfaces.RemoveNext(true)) != VA_INVALID_SURFACE)
    {
      CheckSuccess(vaDestroySurfaces(m_vaapiConfig.dpy, &surf, 1));
    }
  }
  return IHardwareDecoder::Release();
}

long CDecoder::ReleasePicReference()
{
  return IHardwareDecoder::Release();
}

int CDecoder::FFGetBuffer(AVCodecContext *avctx, AVFrame *pic, int flags)
{
  CDVDVideoCodecFFmpeg* ctx = (CDVDVideoCodecFFmpeg*)avctx->opaque;
  CDecoder*             va  = (CDecoder*)ctx->GetHardware();

  // while we are waiting to recover we can't do anything
  CSingleLock lock(va->m_DecoderSection);

  if(va->m_DisplayState != VAAPI_OPEN)
  {
    CLog::Log(LOGWARNING, "VAAPI::FFGetBuffer - returning due to awaiting recovery");
    return -1;
  }

  VASurfaceID surf = (VASurfaceID)(uintptr_t)pic->data[3];
  surf = va->m_videoSurfaces.GetFree(surf != 0 ? surf : VA_INVALID_SURFACE);

  if (surf == VA_INVALID_SURFACE)
  {
    CLog::Log(LOGERROR, "VAAPI::FFGetBuffer - no surface available");
    return -1;
  }

  pic->data[1] = pic->data[2] = NULL;
  pic->data[0] = (uint8_t*)(uintptr_t)surf;
  pic->data[3] = (uint8_t*)(uintptr_t)surf;
  pic->linesize[0] = pic->linesize[1] =  pic->linesize[2] = 0;
  AVBufferRef *buffer = av_buffer_create(pic->data[3], 0, FFReleaseBuffer, ctx, 0);
  if (!buffer)
  {
    CLog::Log(LOGERROR, "VAAPI::%s - error creating buffer", __FUNCTION__);
    return -1;
  }
  pic->buf[0] = buffer;

  pic->reordered_opaque= avctx->reordered_opaque;
  return 0;
}

void CDecoder::FFReleaseBuffer(void *opaque, uint8_t *data)
{
  CDecoder *va = (CDecoder*)((CDVDVideoCodecFFmpeg*)opaque)->GetHardware();

  VASurfaceID surf;
  unsigned int i;

  CSingleLock lock(va->m_DecoderSection);

  surf = (VASurfaceID)(uintptr_t)data;
  va->m_videoSurfaces.ClearReference(surf);
}

int CDecoder::Decode(AVCodecContext* avctx, AVFrame* pFrame)
{
  int result = Check(avctx);
  if (result)
    return result;

  CSingleLock lock(m_DecoderSection);

  if (!m_vaapiConfigured)
    return VC_ERROR;

  if(pFrame)
  { // we have a new frame from decoder

    VASurfaceID surf = (VASurfaceID)(uintptr_t)pFrame->data[3];
    // ffmpeg vc-1 decoder does not flush, make sure the data buffer is still valid
    if (!m_videoSurfaces.IsValid(surf))
    {
      CLog::Log(LOGWARNING, "VAAPI::Decode - ignoring invalid buffer");
      return VC_BUFFER;
    }
    m_videoSurfaces.MarkRender(surf);

    // send frame to output for processing
    CVaapiDecodedPicture pic;
    memset(&pic.DVDPic, 0, sizeof(pic.DVDPic));
    ((CDVDVideoCodecFFmpeg*)avctx->opaque)->GetPictureCommon(&pic.DVDPic);
    pic.videoSurface = surf;
    pic.DVDPic.color_matrix = avctx->colorspace;
    m_bufferStats.IncDecoded();
    m_vaapiOutput.m_dataPort.SendOutMessage(COutputDataProtocol::NEWFRAME, &pic, sizeof(pic));

//    m_codecControl = pic.DVDPic.iFlags & (DVP_FLAG_DRAIN | DVP_FLAG_NO_POSTPROC);
  }

  int retval = 0;
  uint16_t decoded, processed, render;
  Message *msg;
  while (m_vaapiOutput.m_controlPort.ReceiveInMessage(&msg))
  {
    if (msg->signal == COutputControlProtocol::ERROR)
    {
      m_DisplayState = VAAPI_ERROR;
      retval |= VC_ERROR;
    }
    msg->Release();
  }

  m_bufferStats.Get(decoded, processed, render);

  while (!retval)
  {
    // first fill the buffers to keep vdpau busy
    // mixer will run with decoded >= 2. output is limited by number of output surfaces
    // In case mixer is bypassed we limit by looking at processed
    if (decoded < 3 && processed < 3)
    {
      retval |= VC_BUFFER;
    }
    else if (m_vaapiOutput.m_dataPort.ReceiveInMessage(&msg))
    {
      if (msg->signal == COutputDataProtocol::PICTURE)
      {
        if (m_presentPicture)
        {
          m_presentPicture->ReturnUnused();
          m_presentPicture = 0;
        }

        m_presentPicture = *(CVaapiRenderPicture**)msg->data;
        m_presentPicture->vaapi = this;
        m_bufferStats.DecRender();
        m_bufferStats.Get(decoded, processed, render);
        retval |= VC_PICTURE;
        msg->Release();
        break;
      }
      msg->Release();
    }
    else if (m_vaapiOutput.m_controlPort.ReceiveInMessage(&msg))
    {
      if (msg->signal == COutputControlProtocol::STATS)
      {
        m_bufferStats.Get(decoded, processed, render);
      }
      else
      {
        m_DisplayState = VAAPI_ERROR;
        retval |= VC_ERROR;
      }
      msg->Release();
    }

    if (decoded < 3 && processed < 3)
    {
      retval |= VC_BUFFER;
    }

    if (!retval && !m_inMsgEvent.WaitMSec(2000))
      break;
  }
  if (retval & VC_PICTURE)
  {
    m_bufferStats.SetParams(0, m_codecControl);
  }

  if (!retval)
  {
    CLog::Log(LOGERROR, "VAAPI::%s - timed out waiting for output message", __FUNCTION__);
    m_DisplayState = VAAPI_ERROR;
    retval |= VC_ERROR;
  }

  return retval;

}

int CDecoder::Check(AVCodecContext* avctx)
{
  EDisplayState state;

  { CSingleLock lock(m_DecoderSection);
    state = m_DisplayState;
  }

  if (state == VAAPI_LOST)
  {
    CLog::Log(LOGNOTICE,"VAAPI::Check waiting for display reset event");
    if (!m_DisplayEvent.WaitMSec(4000))
    {
      CLog::Log(LOGERROR, "VAAPI::Check - device didn't reset in reasonable time");
      state = VAAPI_RESET;
    }
    else
    {
      CSingleLock lock(m_DecoderSection);
      state = m_DisplayState;
    }
  }
  if (state == VAAPI_RESET || state == VAAPI_ERROR)
  {
    CSingleLock lock(m_DecoderSection);

    FiniVAAPIOutput();
    if (m_vaapiConfig.context)
      m_vaapiConfig.context->Release();
    m_vaapiConfig.context = 0;

    if (CVAAPIContext::EnsureContext(&m_vaapiConfig.context) && ConfigVAAPI())
    {
      m_DisplayState = VAAPI_OPEN;
    }

    if (state == VAAPI_RESET)
      return VC_FLUSHED;
    else
      return VC_ERROR;
  }
  return 0;
}

bool CDecoder::GetPicture(AVCodecContext* avctx, AVFrame* frame, DVDVideoPicture* picture)
{
  CSingleLock lock(m_DecoderSection);

  if (m_DisplayState != VAAPI_OPEN)
    return false;

  *picture = m_presentPicture->DVDPic;
  picture->vaapi = m_presentPicture;

  return true;
}

void CDecoder::Reset()
{
  CSingleLock lock(m_DecoderSection);

  if (!m_vaapiConfigured)
    return;

  Message *reply;
  if (m_vaapiOutput.m_controlPort.SendOutMessageSync(COutputControlProtocol::FLUSH,
                                                 &reply,
                                                 2000))
  {
    bool success = reply->signal == COutputControlProtocol::ACC ? true : false;
    reply->Release();
    if (!success)
    {
      CLog::Log(LOGERROR, "VAAPI::%s - flush returned error", __FUNCTION__);
      m_DisplayState = VAAPI_ERROR;
    }
    else
      m_bufferStats.Reset();
  }
  else
  {
    CLog::Log(LOGERROR, "VAAPI::%s - flush timed out", __FUNCTION__);
    m_DisplayState = VAAPI_ERROR;
  }
}

bool CDecoder::CanSkipDeint()
{
  return m_bufferStats.CanSkipDeint();
}

bool CDecoder::CheckSuccess(VAStatus status)
{
  if (status != VA_STATUS_SUCCESS)
  {
    CLog::Log(LOGERROR, "VAAPI - error: %s", vaErrorStr(status));
    m_ErrorCount++;

    if(m_DisplayState == VAAPI_OPEN)
    {
      if (m_ErrorCount > 2)
        m_DisplayState = VAAPI_ERROR;
    }
    return false;
  }
  m_ErrorCount = 0;
  return true;
}

bool CDecoder::Supports(EINTERLACEMETHOD method)
{
  if(method == VS_INTERLACEMETHOD_AUTO)
    return true;

  return false;
}

EINTERLACEMETHOD CDecoder::AutoInterlaceMethod()
{
  return VS_INTERLACEMETHOD_RENDER_BOB;
}

bool CDecoder::ConfigVAAPI()
{
  m_vaapiConfig.configId = m_vaapiConfig.context->CreateConfig(m_vaapiConfig.profile,
                                                               m_vaapiConfig.attrib);
  if (m_vaapiConfig.configId == VA_INVALID_ID)
    return false;

  // create surfaces
  VASurfaceID surfaces[32];
  int nb_surfaces = m_vaapiConfig.maxReferences+5;
  if (!CheckSuccess(vaCreateSurfaces(m_vaapiConfig.dpy,
                                     VA_RT_FORMAT_YUV420,
                                     m_vaapiConfig.surfaceWidth,
                                     m_vaapiConfig.surfaceHeight,
                                     surfaces,
                                     nb_surfaces,
                                     NULL, 0)))
  {
    return false;
  }
  for (int i=0; i<nb_surfaces; i++)
  {
    m_videoSurfaces.AddSurface(surfaces[i]);
  }


  // create vaapi decoder context
  if (!CheckSuccess(vaCreateContext(m_vaapiConfig.dpy,
                                    m_vaapiConfig.configId,
                                    m_vaapiConfig.surfaceWidth,
                                    m_vaapiConfig.surfaceHeight,
                                    VA_PROGRESSIVE,
                                    surfaces,
                                    nb_surfaces,
                                    &m_vaapiConfig.contextId)))
  {
    m_vaapiConfig.contextId = VA_INVALID_ID;
    return false;
  }
  m_hwContext.context_id = m_vaapiConfig.contextId;

  // initialize output
  CSingleLock lock(g_graphicsContext);
  m_vaapiConfig.stats = &m_bufferStats;
  m_vaapiConfig.vaapi = this;
  m_bufferStats.Reset();
  m_vaapiOutput.Start();
  Message *reply;
  if (m_vaapiOutput.m_controlPort.SendOutMessageSync(COutputControlProtocol::INIT,
                                                     &reply,
                                                     2000,
                                                     &m_vaapiConfig,
                                                     sizeof(m_vaapiConfig)))
  {
    bool success = reply->signal == COutputControlProtocol::ACC ? true : false;
    reply->Release();
    if (!success)
    {
      CLog::Log(LOGERROR, "VAAPI::%s - vaapi output returned error", __FUNCTION__);
      m_vaapiOutput.Dispose();
      return false;
    }
  }
  else
  {
    CLog::Log(LOGERROR, "VAAPI::%s - failed to init output", __FUNCTION__);
    m_vaapiOutput.Dispose();
    return false;
  }

  m_inMsgEvent.Reset();
  m_vaapiConfigured = true;
  m_ErrorCount = 0;

  return true;
}

void CDecoder::FiniVAAPIOutput()
{
  // destroy vaapi config
  if (m_vaapiConfig.configId != VA_INVALID_ID)
    CheckSuccess(vaDestroyConfig(m_vaapiConfig.dpy, m_vaapiConfig.configId));

  if (!m_vaapiConfigured)
    return;

  // uninit output
  m_vaapiOutput.Dispose();
  m_vaapiConfigured = false;

  VAStatus status;

  // destroy decoder context
  if (m_vaapiConfig.contextId != VA_INVALID_ID)
    CheckSuccess(vaDestroyContext(m_vaapiConfig.dpy, m_vaapiConfig.contextId));

  // detroy surfaces
  CLog::Log(LOGDEBUG, "VAAPI::FiniVDPAUOutput destroying %d video surfaces", m_videoSurfaces.Size());
  VASurfaceID surf;
  while((surf = m_videoSurfaces.RemoveNext()) != VA_INVALID_SURFACE)
  {
    CheckSuccess(vaDestroySurfaces(m_vaapiConfig.dpy, &surf, 1));
  }
  m_videoSurfaces.Reset();
}

void CDecoder::ReturnRenderPicture(CVaapiRenderPicture *renderPic)
{
  m_vaapiOutput.m_dataPort.SendOutMessage(COutputDataProtocol::RETURNPIC, &renderPic, sizeof(renderPic));
}


//-----------------------------------------------------------------------------
// RenderPicture
//-----------------------------------------------------------------------------

CVaapiRenderPicture* CVaapiRenderPicture::Acquire()
{
  CSingleLock lock(renderPicSection);

  if (refCount == 0)
    vaapi->Acquire();

  refCount++;
  return this;
}

long CVaapiRenderPicture::Release()
{
  CSingleLock lock(renderPicSection);

  refCount--;
  if (refCount > 0)
    return refCount;

  lock.Leave();
  vaapi->ReturnRenderPicture(this);
  vaapi->ReleasePicReference();

  return refCount;
}

void CVaapiRenderPicture::ReturnUnused()
{
  { CSingleLock lock(renderPicSection);
    if (refCount > 0)
      return;
  }
  if (vaapi)
    vaapi->ReturnRenderPicture(this);
}

void CVaapiRenderPicture::Sync()
{
#ifdef GL_ARB_sync
  CSingleLock lock(renderPicSection);
  if (usefence)
  {
    if(glIsSync(fence))
    {
      glDeleteSync(fence);
      fence = None;
    }
    fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  }
#endif
}

//-----------------------------------------------------------------------------
// Buffer Pool
//-----------------------------------------------------------------------------

VaapiBufferPool::VaapiBufferPool()
{
  CVaapiRenderPicture *pic;
  for (unsigned int i = 0; i < NUM_RENDER_PICS; i++)
  {
    pic = new CVaapiRenderPicture(renderPicSec);
    allRenderPics.push_back(pic);
  }
}

VaapiBufferPool::~VaapiBufferPool()
{
  CVaapiRenderPicture *pic;
  for (unsigned int i = 0; i < NUM_RENDER_PICS; i++)
  {
    pic = allRenderPics[i];
    delete pic;
  }
  allRenderPics.clear();
}

//-----------------------------------------------------------------------------
// Output
//-----------------------------------------------------------------------------
COutput::COutput(CEvent *inMsgEvent) :
  CThread("Vaapi-Output"),
  m_controlPort("OutputControlPort", inMsgEvent, &m_outMsgEvent),
  m_dataPort("OutputDataPort", inMsgEvent, &m_outMsgEvent)
{
  m_inMsgEvent = inMsgEvent;

  for (unsigned int i = 0; i < m_bufferPool.allRenderPics.size(); ++i)
  {
    m_bufferPool.freeRenderPics.push_back(i);
  }
}

void COutput::Start()
{
  Create();
}

COutput::~COutput()
{
  Dispose();

  m_bufferPool.freeRenderPics.clear();
  m_bufferPool.usedRenderPics.clear();
}

void COutput::Dispose()
{
  CSingleLock lock(g_graphicsContext);
  m_bStop = true;
  m_outMsgEvent.Set();
  StopThread();
  m_controlPort.Purge();
  m_dataPort.Purge();
}

void COutput::OnStartup()
{
  CLog::Log(LOGNOTICE, "COutput::OnStartup: Output Thread created");
}

void COutput::OnExit()
{
  CLog::Log(LOGNOTICE, "COutput::OnExit: Output Thread terminated");
}

enum OUTPUT_STATES
{
  O_TOP = 0,                      // 0
  O_TOP_ERROR,                    // 1
  O_TOP_UNCONFIGURED,             // 2
  O_TOP_CONFIGURED,               // 3
  O_TOP_CONFIGURED_IDLE,          // 4
  O_TOP_CONFIGURED_STEP1,         // 5
};

int VAAPI_OUTPUT_parentStates[] = {
    -1,
    0, //TOP_ERROR
    0, //TOP_UNCONFIGURED
    0, //TOP_CONFIGURED
    3, //TOP_CONFIGURED_IDLE
    3, //TOP_CONFIGURED_STEP1
};

void COutput::StateMachine(int signal, Protocol *port, Message *msg)
{
  for (int state = m_state; ; state = VAAPI_OUTPUT_parentStates[state])
  {
    switch (state)
    {
    case O_TOP: // TOP
      if (port == &m_controlPort)
      {
        switch (signal)
        {
        case COutputControlProtocol::FLUSH:
          msg->Reply(COutputControlProtocol::ACC);
          return;
        case COutputControlProtocol::PRECLEANUP:
          msg->Reply(COutputControlProtocol::ACC);
          return;
        default:
          break;
        }
      }
      else if (port == &m_dataPort)
      {
        switch (signal)
        {
        case COutputDataProtocol::RETURNPIC:
          CVaapiRenderPicture *pic;
          pic = *((CVaapiRenderPicture**)msg->data);
          QueueReturnPicture(pic);
          return;
        default:
          break;
        }
      }
      {
        std::string portName = port == NULL ? "timer" : port->portName;
        CLog::Log(LOGWARNING, "COutput::%s - signal: %d form port: %s not handled for state: %d", __FUNCTION__, signal, portName.c_str(), m_state);
      }
      return;

    case O_TOP_ERROR:
      break;

    case O_TOP_UNCONFIGURED:
      if (port == &m_controlPort)
      {
        switch (signal)
        {
        case COutputControlProtocol::INIT:
          CVaapiConfig *data;
          data = (CVaapiConfig*)msg->data;
          if (data)
          {
            m_config = *data;
          }
          Init();

          // set initial number of
          EnsureBufferPool();
          if (!m_vaError)
          {
            m_state = O_TOP_CONFIGURED_IDLE;
            msg->Reply(COutputControlProtocol::ACC, &m_config, sizeof(m_config));
          }
          else
          {
            m_state = O_TOP_ERROR;
            msg->Reply(COutputControlProtocol::ERROR);
          }
          return;
        default:
          break;
        }
      }
      break;

    case O_TOP_CONFIGURED:
      if (port == &m_controlPort)
      {
        switch (signal)
        {
        case COutputControlProtocol::FLUSH:
          Flush();
          msg->Reply(COutputControlProtocol::ACC);
          return;
        case COutputControlProtocol::PRECLEANUP:
          Flush();
          PreCleanup();
          msg->Reply(COutputControlProtocol::ACC);
          return;
        default:
          break;
        }
      }
      else if (port == &m_dataPort)
      {
        switch (signal)
        {
        case COutputDataProtocol::NEWFRAME:
          CVaapiDecodedPicture *frame;
          frame = (CVaapiDecodedPicture*)msg->data;
          if (frame)
          {
            m_bufferPool.decodedPics.push_back(*frame);
            m_extTimeout = 0;
          }
          return;
        case COutputDataProtocol::RETURNPIC:
          CVaapiRenderPicture *pic;
          pic = *((CVaapiRenderPicture**)msg->data);
          QueueReturnPicture(pic);
          m_controlPort.SendInMessage(COutputControlProtocol::STATS);
          m_state = O_TOP_CONFIGURED_IDLE;
          m_extTimeout = 0;
          return;
        default:
          break;
        }
      }

    case O_TOP_CONFIGURED_IDLE:
      if (port == NULL) // timeout
      {
        switch (signal)
        {
        case COutputControlProtocol::TIMEOUT:
          if (ProcessSyncPicture())
            m_extTimeout = 10;
          else
            m_extTimeout = 100;
          if (HasWork())
          {
            m_state = O_TOP_CONFIGURED_STEP1;
            m_extTimeout = 0;
          }
          return;
        default:
          break;
        }
      }
      break;

    case O_TOP_CONFIGURED_STEP1:
      if (port == NULL) // timeout
      {
        switch (signal)
        {
        case COutputControlProtocol::TIMEOUT:
          m_processPicture = m_bufferPool.decodedPics.front();
          m_bufferPool.decodedPics.pop_front();
          InitCycle();
          m_config.stats->IncProcessed();
          m_config.stats->DecDecoded();
          CVaapiRenderPicture *pic;
          pic = ProcessPicture();
          if (pic)
          {
            m_config.stats->DecProcessed();
            m_config.stats->IncRender();
            m_dataPort.SendInMessage(COutputDataProtocol::PICTURE, &pic, sizeof(pic));
          }
          if (m_vaError)
          {
            m_state = O_TOP_ERROR;
            return;
          }
          FiniCycle();
          m_state = O_TOP_CONFIGURED_IDLE;
          m_extTimeout = 0;
          return;
        default:
          break;
        }
      }
      break;

    default: // we are in no state, should not happen
      CLog::Log(LOGERROR, "COutput::%s - no valid state: %d", __FUNCTION__, m_state);
      return;
    }
  } // for
}

void COutput::Process()
{
  Message *msg = NULL;
  Protocol *port = NULL;
  bool gotMsg;

  m_state = O_TOP_UNCONFIGURED;
  m_extTimeout = 1000;
  m_bStateMachineSelfTrigger = false;

  while (!m_bStop)
  {
    gotMsg = false;

    if (m_bStateMachineSelfTrigger)
    {
      m_bStateMachineSelfTrigger = false;
      // self trigger state machine
      StateMachine(msg->signal, port, msg);
      if (!m_bStateMachineSelfTrigger)
      {
        msg->Release();
        msg = NULL;
      }
      continue;
    }
    // check control port
    else if (m_controlPort.ReceiveOutMessage(&msg))
    {
      gotMsg = true;
      port = &m_controlPort;
    }
    // check data port
    else if (m_dataPort.ReceiveOutMessage(&msg))
    {
      gotMsg = true;
      port = &m_dataPort;
    }
    if (gotMsg)
    {
      StateMachine(msg->signal, port, msg);
      if (!m_bStateMachineSelfTrigger)
      {
        msg->Release();
        msg = NULL;
      }
      continue;
    }

    // wait for message
    else if (m_outMsgEvent.WaitMSec(m_extTimeout))
    {
      continue;
    }
    // time out
    else
    {
      msg = m_controlPort.GetMessage();
      msg->signal = COutputControlProtocol::TIMEOUT;
      port = 0;
      // signal timeout to state machine
      StateMachine(msg->signal, port, msg);
      if (!m_bStateMachineSelfTrigger)
      {
        msg->Release();
        msg = NULL;
      }
    }
  }
  Flush();
  Uninit();
}

bool COutput::Init()
{
  if (!CreateGlxContext())
    return false;

  if (!GLInit())
    return false;

  m_vaError = false;

  return true;
}

bool COutput::Uninit()
{
  glFlush();
  while(ProcessSyncPicture())
  {
    Sleep(10);
  }
  ReleaseBufferPool();
  DestroyGlxContext();
  return true;
}

void COutput::Flush()
{
  Message *msg;
  while (m_dataPort.ReceiveOutMessage(&msg))
  {
    if (msg->signal == COutputDataProtocol::NEWFRAME)
    {
      CVaapiDecodedPicture pic = *(CVaapiDecodedPicture*)msg->data;
      m_config.videoSurfaces->ClearRender(pic.videoSurface);
    }
    else if (msg->signal == COutputDataProtocol::RETURNPIC)
    {
      CVaapiRenderPicture *pic;
      pic = *((CVaapiRenderPicture**)msg->data);
      QueueReturnPicture(pic);
    }
    msg->Release();
  }

  while (m_dataPort.ReceiveInMessage(&msg))
  {
    if (msg->signal == COutputDataProtocol::PICTURE)
    {
      CVaapiRenderPicture *pic;
      pic = *((CVaapiRenderPicture**)msg->data);
      QueueReturnPicture(pic);
    }
  }
}

bool COutput::HasWork()
{
  int render = m_bufferPool.freeRenderPics.size();
  int dec = m_bufferPool.decodedPics.size();
  if (!m_bufferPool.freeRenderPics.empty() &&
      !m_bufferPool.decodedPics.empty())
    return true;
  return false;
}

void COutput::InitCycle()
{
  uint64_t latency;
  int speed;
  m_config.stats->GetParams(latency, speed);

  m_config.stats->SetCanSkipDeint(false);

  EDEINTERLACEMODE mode = CMediaSettings::Get().GetCurrentVideoSettings().m_DeinterlaceMode;
  EINTERLACEMETHOD method = CMediaSettings::Get().GetCurrentVideoSettings().m_InterlaceMethod;
  bool interlaced = m_processPicture.DVDPic.iFlags & DVP_FLAG_INTERLACED;

  m_processPicture.DVDPic.format = RENDER_FMT_VAAPI;
  m_processPicture.DVDPic.iFlags &= ~(DVP_FLAG_TOP_FIELD_FIRST |
                                      DVP_FLAG_REPEAT_TOP_FIELD |
                                      DVP_FLAG_INTERLACED);
  m_processPicture.DVDPic.iWidth = m_config.vidWidth;
  m_processPicture.DVDPic.iHeight = m_config.vidHeight;
}

CVaapiRenderPicture* COutput::ProcessPicture()
{
  CVaapiRenderPicture *retPic;
  int idx = m_bufferPool.freeRenderPics.front();
  retPic = m_bufferPool.allRenderPics[idx];

  if (!CheckSuccess(vaCopySurfaceGLX(m_config.dpy,
                   retPic->surface,
                   m_processPicture.videoSurface,
                   VA_FRAME_PICTURE | VA_SRC_BT709)))
  {
    return NULL;
  }

  m_config.videoSurfaces->ClearRender(m_processPicture.videoSurface);

  m_bufferPool.freeRenderPics.pop_front();
  m_bufferPool.usedRenderPics.push_back(idx);

  retPic->DVDPic = m_processPicture.DVDPic;
  retPic->valid = true;
  retPic->texWidth = m_config.outWidth;
  retPic->texHeight = m_config.outHeight;
  retPic->crop.x1 = 0;
  retPic->crop.y1 = 0;
  retPic->crop.x2 = m_config.outWidth;
  retPic->crop.y2 = m_config.outHeight;
  return retPic;
}

void COutput::FiniCycle()
{
//  m_config.stats->DecDecoded();
}

void COutput::QueueReturnPicture(CVaapiRenderPicture *pic)
{
  std::deque<int>::iterator it;
  for (it = m_bufferPool.usedRenderPics.begin(); it != m_bufferPool.usedRenderPics.end(); ++it)
  {
    if (m_bufferPool.allRenderPics[*it] == pic)
    {
      break;
    }
  }

  if (it == m_bufferPool.usedRenderPics.end())
  {
    CLog::Log(LOGWARNING, "COutput::QueueReturnPicture - pic not found");
    return;
  }

  // check if already queued
  std::deque<int>::iterator it2 = find(m_bufferPool.syncRenderPics.begin(),
                                       m_bufferPool.syncRenderPics.end(),
                                       *it);
  if (it2 == m_bufferPool.syncRenderPics.end())
  {
    m_bufferPool.syncRenderPics.push_back(*it);
  }

  ProcessSyncPicture();
}

bool COutput::ProcessSyncPicture()
{
  CVaapiRenderPicture *pic;
  bool busy = false;

  std::deque<int>::iterator it;
  for (it = m_bufferPool.syncRenderPics.begin(); it != m_bufferPool.syncRenderPics.end(); )
  {
    pic = m_bufferPool.allRenderPics[*it];

#ifdef GL_ARB_sync
    if (pic->usefence)
    {
      if (glIsSync(pic->fence))
      {
        GLint state;
        GLsizei length;
        glGetSynciv(pic->fence, GL_SYNC_STATUS, 1, &length, &state);
        if(state == GL_SIGNALED)
        {
          glDeleteSync(pic->fence);
          pic->fence = None;
        }
        else
        {
          busy = true;
          ++it;
          continue;
        }
      }
    }
#endif

    m_bufferPool.freeRenderPics.push_back(*it);

    std::deque<int>::iterator it2 = find(m_bufferPool.usedRenderPics.begin(),
                                         m_bufferPool.usedRenderPics.end(),
                                         *it);
    if (it2 == m_bufferPool.usedRenderPics.end())
    {
      CLog::Log(LOGERROR, "COutput::ProcessSyncPicture - pic not found in queue");
    }
    else
    {
      m_bufferPool.usedRenderPics.erase(it2);
    }
    it = m_bufferPool.syncRenderPics.erase(it);

    if (pic->valid)
    {
      ProcessReturnPicture(pic);
    }
    else
    {
      CLog::Log(LOGDEBUG, "COutput::%s - return of invalid render pic", __FUNCTION__);
    }
  }
  return busy;
}

void COutput::ProcessReturnPicture(CVaapiRenderPicture *pic)
{
}

bool COutput::EnsureBufferPool()
{
  VAStatus status;

  // create glx surfaces
  CVaapiRenderPicture *pic;
  glEnable(m_textureTarget);
  for (unsigned int i = 0; i < m_bufferPool.allRenderPics.size(); i++)
  {
    pic = m_bufferPool.allRenderPics[i];
    glGenTextures(1, &pic->texture);
    glBindTexture(m_textureTarget, pic->texture);
    glTexParameteri(m_textureTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(m_textureTarget, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(m_textureTarget, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(m_textureTarget, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(m_textureTarget, 0, GL_RGBA, m_config.surfaceWidth, m_config.surfaceHeight, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);

    if (!CheckSuccess(vaCreateSurfaceGLX(m_config.dpy , m_textureTarget, pic->texture, &pic->surface)))
    {
      return false;
    }
  }
  glDisable(m_textureTarget);

  CLog::Log(LOGNOTICE, "VAAPI::COutput::InitBufferPool - Surfaces created");
  return true;
}

void COutput::ReleaseBufferPool()
{
  VAStatus status;
  CVaapiRenderPicture *pic;

  CSingleLock lock(m_bufferPool.renderPicSec);

  // wait for all fences
  XbmcThreads::EndTime timeout(1000);
  for (unsigned int i = 0; i < m_bufferPool.allRenderPics.size(); i++)
  {
    pic = m_bufferPool.allRenderPics[i];
    if (pic->usefence)
    {
#ifdef GL_ARB_sync
      while (glIsSync(pic->fence))
      {
        GLint state;
        GLsizei length;
        glGetSynciv(pic->fence, GL_SYNC_STATUS, 1, &length, &state);
        if(state == GL_SIGNALED || timeout.IsTimePast())
        {
          glDeleteSync(pic->fence);
        }
        else
        {
          Sleep(5);
        }
      }
      pic->fence = None;
#endif
    }
  }
  if (timeout.IsTimePast())
  {
    CLog::Log(LOGERROR, "COutput::%s - timeout waiting for fence", __FUNCTION__);
  }
  ProcessSyncPicture();

  // invalidate all used render pictures
  for (unsigned int i = 0; i < m_bufferPool.usedRenderPics.size(); ++i)
  {
    CVaapiRenderPicture *pic = m_bufferPool.allRenderPics[m_bufferPool.usedRenderPics[i]];
    pic->valid = false;
  }

  for (unsigned int i = 0; i < m_bufferPool.allRenderPics.size(); i++)
  {
    pic = m_bufferPool.allRenderPics[i];
    if (pic->surface == NULL)
      continue;
    CheckSuccess(vaDestroySurfaceGLX(m_config.dpy, pic->surface));
    glDeleteTextures(1, &pic->texture);
    pic->surface = NULL;
  }
}

void COutput::PreCleanup()
{

  VAStatus status;

  ProcessSyncPicture();

  CSingleLock lock(m_bufferPool.renderPicSec);
//  for (unsigned int i = 0; i < m_bufferPool.outputSurfaces.size(); ++i)
  {
//    if (m_bufferPool.outputSurfaces[i] == VDP_INVALID_HANDLE)
//      continue;

    // check if output surface is in use
    bool used = false;
    std::deque<int>::iterator it;
    CVaapiRenderPicture *pic;
    for (it = m_bufferPool.usedRenderPics.begin(); it != m_bufferPool.usedRenderPics.end(); ++it)
    {
      pic = m_bufferPool.allRenderPics[*it];
//      if ((pic->sourceIdx == m_bufferPool.outputSurfaces[i]) && pic->valid)
      {
        used = true;
        break;
      }
    }
//    if (used)
//      continue;

//#ifdef GL_NV_vdpau_interop
//    // unmap surface
//    std::map<VdpOutputSurface, VdpauBufferPool::GLVideoSurface>::iterator it_map;
//    it_map = m_bufferPool.glOutputSurfaceMap.find(m_bufferPool.outputSurfaces[i]);
//    if (it_map == m_bufferPool.glOutputSurfaceMap.end())
//    {
//      CLog::Log(LOGERROR, "%s - could not find gl surface", __FUNCTION__);
//      continue;
//    }
//    glVDPAUUnregisterSurfaceNV(it_map->second.glVdpauSurface);
//    glDeleteTextures(1, it_map->second.texture);
//    m_bufferPool.glOutputSurfaceMap.erase(it_map);
//#endif

//    vdp_st = m_config.context->GetProcs().vdp_output_surface_destroy(m_bufferPool.outputSurfaces[i]);
//    CheckStatus(vdp_st, __LINE__);
//
//    m_bufferPool.outputSurfaces[i] = VDP_INVALID_HANDLE;

    CLog::Log(LOGDEBUG, "VDPAU::PreCleanup - released output surface");
  }

}

bool COutput::GLInit()
{
#ifdef GL_ARB_sync
  bool hasfence = glewIsSupported("GL_ARB_sync");
  for (unsigned int i = 0; i < m_bufferPool.allRenderPics.size(); i++)
  {
    m_bufferPool.allRenderPics[i]->usefence = hasfence;
  }
#endif

  if (!glewIsSupported("GL_ARB_texture_non_power_of_two") && glewIsSupported("GL_ARB_texture_rectangle"))
  {
    m_textureTarget = GL_TEXTURE_RECTANGLE_ARB;
  }
  else
    m_textureTarget = GL_TEXTURE_2D;

  return true;
}

bool COutput::CheckSuccess(VAStatus status)
{
  if (status != VA_STATUS_SUCCESS)
  {
    CLog::Log(LOGERROR, "VAAPI - Error: %s(%d)", vaErrorStr(status), status);
    m_vaError = true;
    return false;
  }
  return true;
}

bool COutput::CreateGlxContext()
{
  GLXContext   glContext;

  m_Display = g_Windowing.GetDisplay();
  glContext = g_Windowing.GetGlxContext();
  m_Window = g_Windowing.GetWindow();

  // Get our window attribs.
  XWindowAttributes wndattribs;
  XGetWindowAttributes(m_Display, m_Window, &wndattribs);

  // Get visual Info
  XVisualInfo visInfo;
  visInfo.visualid = wndattribs.visual->visualid;
  int nvisuals = 0;
  XVisualInfo* visuals = XGetVisualInfo(m_Display, VisualIDMask, &visInfo, &nvisuals);
  if (nvisuals != 1)
  {
    CLog::Log(LOGERROR, "VDPAU::COutput::CreateGlxContext - could not find visual");
    return false;
  }
  visInfo = visuals[0];
  XFree(visuals);

  m_pixmap = XCreatePixmap(m_Display,
                           m_Window,
                           192,
                           108,
                           visInfo.depth);
  if (!m_pixmap)
  {
    CLog::Log(LOGERROR, "VAAPI::COutput::CreateGlxContext - Unable to create XPixmap");
    return false;
  }

  // create gl pixmap
  m_glPixmap = glXCreateGLXPixmap(m_Display, &visInfo, m_pixmap);

  if (!m_glPixmap)
  {
    CLog::Log(LOGINFO, "VAAPI::COutput::CreateGlxContext - Could not create glPixmap");
    return false;
  }

  m_glContext = glXCreateContext(m_Display, &visInfo, glContext, True);

  if (!glXMakeCurrent(m_Display, m_glPixmap, m_glContext))
  {
    CLog::Log(LOGINFO, "VAAPI::COutput::CreateGlxContext - Could not make Pixmap current");
    return false;
  }

  CLog::Log(LOGNOTICE, "VAAPI::COutput::CreateGlxContext - created context");
  return true;
}

bool COutput::DestroyGlxContext()
{
  if (m_glContext)
  {
    glXMakeCurrent(m_Display, None, NULL);
    glXDestroyContext(m_Display, m_glContext);
  }
  m_glContext = 0;

  if (m_glPixmap)
    glXDestroyPixmap(m_Display, m_glPixmap);
  m_glPixmap = 0;

  if (m_pixmap)
    XFreePixmap(m_Display, m_pixmap);
  m_pixmap = 0;

  return true;
}

#endif
