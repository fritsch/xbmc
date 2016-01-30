#pragma once
/*
 *      Copyright (C) 2010-2013 Team XBMC
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

#include "cores/AudioEngine/Interfaces/AESink.h"
#include "cores/AudioEngine/Utils/AEDeviceInfo.h"
#include "threads/CriticalSection.h"
#include "threads/Thread.h"

#include <vector>
#include <set>
#include <queue>
namespace jni
{
class CJNIAudioTrack;
};

class CAESinkAUDIOTRACK : public IAESink
{
public:
  virtual const char *GetName() { return "AUDIOTRACK"; }

  CAESinkAUDIOTRACK();
  virtual ~CAESinkAUDIOTRACK();

  virtual bool Initialize(AEAudioFormat &format, std::string &device);
  virtual void Deinitialize();
  bool IsInitialized();

  virtual void         GetDelay        (AEDelayStatus& status);
  virtual double       GetLatency      ();
  virtual double       GetCacheTotal   ();
  virtual unsigned int AddPackets      (uint8_t **data, unsigned int frames, unsigned int offset);
  virtual void         AddPause        (unsigned int millis);
  virtual void         Drain           ();
  static void          EnumerateDevicesEx(AEDeviceInfoList &list, bool force = false);

protected:
  static bool IsSupported(int sampleRateInHz, int channelConfig, int audioFormat);

private:
  jni::CJNIAudioTrack  *m_at_jni;
  double                m_duration_written;
  unsigned int          m_min_buffer_size;
  unsigned int          m_lastPlaybackHeadPosition;
  int64_t               m_offset;
  double                GetMovingAverageDelay(double newestdelay);
  unsigned int          m_pause_counter;

  // while warming up make sure we don't cache more than the number of
  // bytes we can write out - we can on paused buffer
  unsigned int          m_raw_buffer_count_bytes;
  // AT's head position does not change immediately
  // track the packages it did not account for and
  // calculate manually
  // Currently only used in passthrough mode
  unsigned int          m_packages_not_counted;

  std::vector<double>   m_linearmovingaverage;

  static CAEDeviceInfo m_info;
  static std::set<unsigned int>       m_sink_sampleRates;

  AEAudioFormat      m_format;
  double             m_volume;
  int16_t           *m_alignedS16;
  unsigned int       m_sink_frameSize;
  unsigned int       m_sink_sampleRate;
  bool               m_passthrough;
  double             m_audiotrackbuffer_sec;
  int                m_encoding;
};
