/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://xbmc.org
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
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#include "system_gl.h"

#include "DVDVideoCodec.h"
#include "DVDVideoCodecFFmpeg.h"
#include "DVDVideoCodec.h"
#include "DVDVideoCodecFFmpeg.h"
#include "settings/VideoSettings.h"
#include "threads/CriticalSection.h"
#include "threads/SharedSection.h"
#include "threads/Event.h"
#include "threads/Thread.h"
#include "utils/ActorProtocol.h"
#include <list>
#include <map>
#include <va/va.h>

extern "C" {
#include "libavutil/avutil.h"
#include "libavcodec/vaapi.h"
}

using namespace Actor;


#define FULLHD_WIDTH                       1920

namespace VAAPI
{

//-----------------------------------------------------------------------------
// VAAPI data structs
//-----------------------------------------------------------------------------

class CDecoder;

/**
 * Buffer statistics used to control number of frames in queue
 */

class CVaapiBufferStats
{
public:
  uint16_t decodedPics;
  uint16_t processedPics;
  uint16_t renderPics;
  uint64_t latency;         // time decoder has waited for a frame, ideally there is no latency
  int codecFlags;
  bool canSkipDeint;
  int processCmd;

  void IncDecoded() { CSingleLock l(m_sec); decodedPics++;}
  void DecDecoded() { CSingleLock l(m_sec); decodedPics--;}
  void IncProcessed() { CSingleLock l(m_sec); processedPics++;}
  void DecProcessed() { CSingleLock l(m_sec); processedPics--;}
  void IncRender() { CSingleLock l(m_sec); renderPics++;}
  void DecRender() { CSingleLock l(m_sec); renderPics--;}
  void Reset() { CSingleLock l(m_sec); decodedPics=0; processedPics=0;renderPics=0;latency=0;}
  void Get(uint16_t &decoded, uint16_t &processed, uint16_t &render) {CSingleLock l(m_sec); decoded = decodedPics, processed=processedPics, render=renderPics;}
  void SetParams(uint64_t time, int flags) { CSingleLock l(m_sec); latency = time; codecFlags = flags; }
  void GetParams(uint64_t &lat, int &flags) { CSingleLock l(m_sec); lat = latency; flags = codecFlags; }
  void SetCmd(int cmd) { CSingleLock l(m_sec); processCmd = cmd; }
  void GetCmd(int &cmd) { CSingleLock l(m_sec); cmd = processCmd; processCmd = 0; }
  void SetCanSkipDeint(bool canSkip) { CSingleLock l(m_sec); canSkipDeint = canSkip; }
  bool CanSkipDeint() { CSingleLock l(m_sec); if (canSkipDeint) return true; else return false;}
private:
  CCriticalSection m_sec;
};

/**
 *  CVaapiConfig holds all configuration parameters needed by vaapi
 *  The structure is sent to the internal classes CMixer and COutput
 *  for init.
 */

class CVideoSurfaces;
class CVAAPIContext;

struct CVaapiConfig
{
  int surfaceWidth;
  int surfaceHeight;
  int vidWidth;
  int vidHeight;
  int outWidth;
  int outHeight;
  VAConfigID configId;
  VAContextID contextId;
  CVaapiBufferStats *stats;
  CDecoder *vaapi;
  int upscale;
  CVideoSurfaces *videoSurfaces;
  int numRenderBuffers;
  uint32_t maxReferences;
  bool useInteropYuv;
  CVAAPIContext *context;
  VADisplay dpy;
  VAProfile profile;
  VAConfigAttrib attrib;
};

/**
 * Holds a decoded frame
 * Input to COutput for further processing
 */
struct CVaapiDecodedPicture
{
  DVDVideoPicture DVDPic;
  VASurfaceID videoSurface;
};

/**
 * Frame after having been processed by vpp
 */
struct CVaapiProcessedPicture
{
  DVDVideoPicture DVDPic;
  VASurfaceID videoSurface;
  VASurfaceID outputSurface;
  bool crop;
};

/**
 * Ready to render textures
 * Sent from COutput back to CDecoder
 * Objects are referenced by DVDVideoPicture and are sent
 * to renderer
 */
class CVaapiRenderPicture
{
  friend class CDecoder;
  friend class COutput;
public:
  CVaapiRenderPicture(CCriticalSection &section)
    : texture(None), refCount(0), surface(NULL), renderPicSection(section) { fence = None; }
  void Sync();
  DVDVideoPicture DVDPic;
  int texWidth, texHeight;
  CRect crop;
  GLuint texture;
  bool valid;
  CDecoder *vaapi;
  CVaapiRenderPicture* Acquire();
  long Release();
private:
  void ReturnUnused();
  bool usefence;
  GLsync fence;
  int refCount;
  void *surface;
  CCriticalSection &renderPicSection;
};

//-----------------------------------------------------------------------------
// Output
//-----------------------------------------------------------------------------

/**
 * Buffer pool holds allocated vaapi and gl resources
 * Embedded in COutput
 */
struct VaapiBufferPool
{
  VaapiBufferPool();
  virtual ~VaapiBufferPool();
  std::vector<CVaapiRenderPicture*> allRenderPics;
  std::deque<int> usedRenderPics;
  std::deque<int> freeRenderPics;
  std::deque<int> syncRenderPics;
  std::deque<CVaapiProcessedPicture> processedPics;
  std::deque<CVaapiDecodedPicture> decodedPics;
  CCriticalSection renderPicSec;
};

class COutputControlProtocol : public Protocol
{
public:
  COutputControlProtocol(std::string name, CEvent* inEvent, CEvent *outEvent) : Protocol(name, inEvent, outEvent) {};
  enum OutSignal
  {
    INIT,
    FLUSH,
    PRECLEANUP,
    TIMEOUT,
  };
  enum InSignal
  {
    ACC,
    ERROR,
    STATS,
  };
};

class COutputDataProtocol : public Protocol
{
public:
  COutputDataProtocol(std::string name, CEvent* inEvent, CEvent *outEvent) : Protocol(name, inEvent, outEvent) {};
  enum OutSignal
  {
    NEWFRAME = 0,
    RETURNPIC,
  };
  enum InSignal
  {
    PICTURE,
  };
};

/**
 * COutput is embedded in CDecoder and embeds vpp
 * The class has its own OpenGl context which is shared with render thread
 * COuput generated ready to render textures and passes them back to
 * CDecoder
 */
class COutput : private CThread
{
public:
  COutput(CEvent *inMsgEvent);
  virtual ~COutput();
  void Start();
  void Dispose();
  COutputControlProtocol m_controlPort;
  COutputDataProtocol m_dataPort;
protected:
  void OnStartup();
  void OnExit();
  void Process();
  void StateMachine(int signal, Protocol *port, Message *msg);
  bool HasWork();
  void InitCycle();
  CVaapiRenderPicture* ProcessPicture();
  void FiniCycle();
  void QueueReturnPicture(CVaapiRenderPicture *pic);
  void ProcessReturnPicture(CVaapiRenderPicture *pic);
  bool ProcessSyncPicture();
  bool Init();
  bool Uninit();
  void Flush();
  bool CreateGlxContext();
  bool DestroyGlxContext();
  bool EnsureBufferPool();
  void ReleaseBufferPool();
  void PreCleanup();
  bool GLInit();
  bool CheckSuccess(VAStatus status);
  CEvent m_outMsgEvent;
  CEvent *m_inMsgEvent;
  int m_state;
  bool m_bStateMachineSelfTrigger;

  // extended state variables for state machine
  int m_extTimeout;
  bool m_vaError;
  CVaapiConfig m_config;
  VaapiBufferPool m_bufferPool;
  Display *m_Display;
  Window m_Window;
  GLXContext m_glContext;
  GLXWindow m_glWindow;
  Pixmap    m_pixmap;
  GLXPixmap m_glPixmap;
  CVaapiDecodedPicture m_processPicture;
  GLenum m_textureTarget;
};

//-----------------------------------------------------------------------------
// VAAPI Video Surface states
//-----------------------------------------------------------------------------

class CVideoSurfaces
{
public:
  void AddSurface(VASurfaceID surf);
  void ClearReference(VASurfaceID surf);
  bool MarkRender(VASurfaceID surf);
  void ClearRender(VASurfaceID surf);
  bool IsValid(VASurfaceID surf);
  VASurfaceID GetFree(VASurfaceID surf);
  VASurfaceID GetAtIndex(int idx);
  VASurfaceID RemoveNext(bool skiprender = false);
  void Reset();
  int Size();
protected:
  std::map<VASurfaceID, int> m_state;
  std::list<VASurfaceID> m_freeSurfaces;
  CCriticalSection m_section;
};

//-----------------------------------------------------------------------------
// VAAPI decoder
//-----------------------------------------------------------------------------

class CVAAPIContext
{
public:
  static bool EnsureContext(CVAAPIContext **ctx);
  void Release();
  VADisplay GetDisplay();
  bool SupportsProfile(VAProfile profile);
  VAConfigAttrib GetAttrib(VAProfile profile);
  VAConfigID CreateConfig(VAProfile profile, VAConfigAttrib attrib);
private:
  CVAAPIContext();
  void Close();
  bool CreateContext();
  void DestroyContext();
  void QueryCaps();
  bool CheckSuccess(VAStatus status);
  static CVAAPIContext *m_context;
  static CCriticalSection m_section;
  static Display *m_X11dpy;
  VADisplay m_display;
  int m_refCount;
  int m_attributeCount;
  VADisplayAttribute *m_attributes;
  int m_profileCount;
  VAProfile *m_profiles;
};

/**
 *  VAAPI main class
 */
class CDecoder
 : public CDVDVideoCodecFFmpeg::IHardwareDecoder
{
   friend class CVaapiRenderPicture;

public:

  CDecoder();
  virtual ~CDecoder();

  virtual bool Open      (AVCodecContext* avctx, const enum PixelFormat, unsigned int surfaces = 0);
  virtual int  Decode    (AVCodecContext* avctx, AVFrame* frame);
  virtual bool GetPicture(AVCodecContext* avctx, AVFrame* frame, DVDVideoPicture* picture);
  virtual void Reset();
  virtual void Close();
  virtual long Release();
  virtual bool CanSkipDeint();
  virtual unsigned GetAllowedReferences() { return 5; }

  virtual int  Check(AVCodecContext* avctx);
  virtual const std::string Name() { return "vaapi"; }

  bool Supports(EINTERLACEMETHOD method);
  EINTERLACEMETHOD AutoInterlaceMethod();

  static void FFReleaseBuffer(void *opaque, uint8_t *data);
  static int FFGetBuffer(AVCodecContext *avctx, AVFrame *pic, int flags);

protected:
  void SetWidthHeight(int width, int height);
  bool ConfigVAAPI();
  bool CheckStatus(VAStatus vdp_st, int line);
  void FiniVAAPIOutput();
  void ReturnRenderPicture(CVaapiRenderPicture *renderPic);
  long ReleasePicReference();
  bool CheckSuccess(VAStatus status);

  enum EDisplayState
  { VAAPI_OPEN
  , VAAPI_RESET
  , VAAPI_LOST
  , VAAPI_ERROR
  } m_DisplayState;
  CCriticalSection m_DecoderSection;
  CEvent m_DisplayEvent;
  int m_ErrorCount;

  ThreadIdentifier m_decoderThread;
  bool m_vaapiConfigured;
  CVaapiConfig  m_vaapiConfig;
  CVideoSurfaces m_videoSurfaces;
  vaapi_context m_hwContext;

  COutput m_vaapiOutput;
  CVaapiBufferStats m_bufferStats;
  CEvent m_inMsgEvent;
  CVaapiRenderPicture *m_presentPicture;

  int m_codecControl;
};

}
