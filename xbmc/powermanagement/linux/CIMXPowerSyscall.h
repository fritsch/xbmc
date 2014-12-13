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
#include "powermanagement/IPowerSyscall.h"
#include "system.h"
#if defined(TARGET_POSIX) && defined(HAS_DBUS)
#include "powermanagement/linux/LogindUPowerSyscall.h"
#endif

class CIMXPowerSyscall : public CPowerSyscallWithoutEvents
{
public:
  CIMXPowerSyscall()
  {
      m_instance = NULL;
      #if defined(TARGET_POSIX) && defined(HAS_DBUS)
      if (CLogindUPowerSyscall::HasLogind())
      {
        m_instance = new CLogindUPowerSyscall();
      }
      #endif
  }
  ~CIMXPowerSyscall()
  {
    delete m_instance;
  }
  virtual bool Powerdown() {return false; }
  virtual bool Suspend() {return false; }
  virtual bool Hibernate() {return false; }
  virtual bool Reboot()
  {
    if (m_instance)
      return m_instance->Reboot();
    else
      return false;
  }

  virtual bool CanPowerdown() {return false; }
  virtual bool CanSuspend() {return false; }
  virtual bool CanHibernate() {return false;}
  virtual bool CanReboot()
  {
   if (m_instance)
     return m_instance->CanReboot();
   else
     return false;
  }
  virtual int BatteryLevel() {return 0; }

private:
  IPowerSyscall * m_instance;
};
