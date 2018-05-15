#pragma once
/*
 *      Copyright (C) 2005-2013 Team XBMC
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
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include <string>
#include <vector>

#include "storage/IStorageProvider.h"
#include "DBusUtil.h"

class CUDisks2Drive
{
public:
  std::string m_object;
  bool m_isRemovable = false;
  std::vector<std::string> m_mediaCompatibility;

  explicit CUDisks2Drive(const char *object);
  ~CUDisks2Drive() = default;

  bool IsOptical();

  std::string toString();
};

class CUDisks2Block
{
public:
  CUDisks2Drive * m_drive = nullptr;
  std::string m_object;
  std::string m_driveobject;
  std::string m_label;
  std::string m_device;
  bool m_isSystem = false;
  u_int64_t m_size = 0;

  explicit CUDisks2Block(const char *object);
  ~CUDisks2Block() = default;

  bool IsReady();

  std::string toString();
};

class CUDisks2Filesystem
{
public:

  CUDisks2Block * m_block = nullptr;
  std::string m_object;
  std::string m_mountPoint;
  bool m_isMounted = false;

  explicit CUDisks2Filesystem(const char *object);
  ~CUDisks2Filesystem() = default;

  bool IsReady();
  bool IsOptical();

  bool Mount();
  bool Unmount();

  std::string GetDisplayName();
  CMediaSource ToMediaShare();
  bool IsApproved();

  std::string toString();
};

class CUDisks2Provider : public IStorageProvider
{
public:
  CUDisks2Provider();
  ~CUDisks2Provider() override;

  void Initialize() override;

  bool PumpDriveChangeEvents(IStorageEventsCallback *callback) override;

  static bool HasUDisks2();

  bool Eject(const std::string &mountpath) override;

  std::vector<std::string> GetDiskUsage() override;

  void GetLocalDrives(VECSOURCES &localDrives) override { GetDisks(localDrives, false); }

  void GetRemovableDrives(VECSOURCES &removableDrives) override { GetDisks(removableDrives, true); }

  void Stop() override {}

private:
  typedef std::map<std::string, CUDisks2Drive *> DriveMap;
  typedef std::map<std::string, CUDisks2Block *> BlockMap;
  typedef std::map<std::string, CUDisks2Filesystem *> FilesystemMap;

  CDBusConnection m_connection;

  DriveMap m_drives;
  BlockMap m_blocks;
  FilesystemMap m_filesystems;

  std::string m_daemonVersion;

  void GetDisks(VECSOURCES &devices, bool enumerateRemovable);

  void DriveAdded(CUDisks2Drive *drive);
  bool DriveRemoved(std::string object);
  void BlockAdded(CUDisks2Block *block, bool isNew = true);
  bool BlockRemoved(std::string object);
  void FilesystemAdded(CUDisks2Filesystem *fs, bool isNew = true);
  bool FilesystemRemoved(const char *object, IStorageEventsCallback *callback);

  bool HandleInterfacesRemoved(DBusMessage *msg, IStorageEventsCallback *callback);
  void HandleInterfacesAdded(DBusMessage *msg);
  bool HandlePropertiesChanged(DBusMessage *msg, IStorageEventsCallback *callback);

  bool DrivePropertiesChanged(const char *object, DBusMessageIter *propsIter);
  bool BlockPropertiesChanged(const char *object, DBusMessageIter *propsIter);
  bool FilesystemPropertiesChanged(const char *object, DBusMessageIter *propsIter, IStorageEventsCallback *callback);

  bool RemoveInterface(const char *path, const char *iface, IStorageEventsCallback *callback);

  template<class Object, class Function>
  void ParseProperties(Object *ref, DBusMessageIter *dictIter, Function f);
  void ParseInterfaces(DBusMessageIter *dictIter);
  void ParseDriveProperty(CUDisks2Drive *drive, const char *key, DBusMessageIter *varIter);
  void ParseBlockProperty(CUDisks2Block *block, const char *key, DBusMessageIter *varIter);
  void ParseFilesystemProperty(CUDisks2Filesystem *fs, const char *key, DBusMessageIter *varIter);
  std::string ParseByteArray(DBusMessageIter *arrIter);
  void HandleManagedObjects(DBusMessage *msg);
  void ParseInterface(const char *object, const char *iface, DBusMessageIter *propsIter);
};
