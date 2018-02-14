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

#include "system.h"
#include "DVDFactoryInputStream.h"
#include "DVDInputStream.h"
#include "DVDInputStreamFile.h"
#include "DVDInputStreamNavigator.h"
#include "DVDInputStreamFFmpeg.h"
#include "DVDInputStreamPVRManager.h"
#include "InputStreamAddon.h"
#include "InputStreamMultiSource.h"
#ifdef HAVE_LIBBLURAY
#include "DVDInputStreamBluray.h"
#endif
#ifdef ENABLE_DVDINPUTSTREAM_STACK
#include "DVDInputStreamStack.h"
#endif
#include "FileItem.h"
#include "storage/MediaManager.h"
#include "URL.h"
#include "filesystem/CurlFile.h"
#include "filesystem/File.h"
#include "utils/URIUtils.h"
#include "ServiceBroker.h"
#include "addons/binary-addons/BinaryAddonManager.h"
#include "Util.h"


std::shared_ptr<CDVDInputStream> CDVDFactoryInputStream::CreateInputStream(IVideoPlayer* pPlayer, const CFileItem &fileitem, bool scanforextaudio)
{
  using namespace ADDON;

  std::string file = fileitem.GetDynPath();
  if (scanforextaudio)
  {
    // find any available external audio tracks
    std::vector<std::string> filenames;
    filenames.push_back(file);
    CUtil::ScanForExternalAudio(file, filenames);
    CUtil::ScanForExternalDemuxSub(file, filenames);
    if (filenames.size() >= 2)
    {
      return CreateInputStream(pPlayer, fileitem, filenames);
    }
  }

  BinaryAddonBaseList addonInfos;
  CServiceBroker::GetBinaryAddonManager().GetAddonInfos(addonInfos, true /*enabled only*/, ADDON_INPUTSTREAM);
  for (auto addonInfo : addonInfos)
  {
    if (CInputStreamAddon::Supports(addonInfo, fileitem))
      return std::shared_ptr<CInputStreamAddon>(new CInputStreamAddon(addonInfo, pPlayer, fileitem));
  }

  if (fileitem.IsDiscImage())
  {
#ifdef HAVE_LIBBLURAY
    CURL url("udf://");
    url.SetHostName(file);
    url.SetFileName("BDMV/index.bdmv");
    if(XFILE::CFile::Exists(url.Get()))
      return std::shared_ptr<CDVDInputStreamBluray>(new CDVDInputStreamBluray(pPlayer, fileitem));
#endif

    return std::shared_ptr<CDVDInputStreamNavigator>(new CDVDInputStreamNavigator(pPlayer, fileitem));
  }

#ifdef HAS_DVD_DRIVE
  if(file.compare(g_mediaManager.TranslateDevicePath("")) == 0)
  {
#ifdef HAVE_LIBBLURAY
    if(XFILE::CFile::Exists(URIUtils::AddFileToFolder(file, "BDMV", "index.bdmv")))
      return std::shared_ptr<CDVDInputStreamBluray>(new CDVDInputStreamBluray(pPlayer, fileitem));
#endif

    return std::shared_ptr<CDVDInputStreamNavigator>(new CDVDInputStreamNavigator(pPlayer, fileitem));
  }
#endif

  if (fileitem.IsDVDFile(false, true))
    return std::shared_ptr<CDVDInputStreamNavigator>(new CDVDInputStreamNavigator(pPlayer, fileitem));
  else if(file.substr(0, 6) == "pvr://")
    return std::shared_ptr<CDVDInputStreamPVRManager>(new CDVDInputStreamPVRManager(pPlayer, fileitem));
#ifdef HAVE_LIBBLURAY
  else if (fileitem.IsType(".bdmv") || fileitem.IsType(".mpls") || file.substr(0, 7) == "bluray:")
    return std::shared_ptr<CDVDInputStreamBluray>(new CDVDInputStreamBluray(pPlayer, fileitem));
#endif
  else if(file.substr(0, 6) == "rtp://"
       || file.substr(0, 7) == "rtsp://"
       || file.substr(0, 6) == "sdp://"
       || file.substr(0, 6) == "udp://"
       || file.substr(0, 6) == "tcp://"
       || file.substr(0, 6) == "mms://"
       || file.substr(0, 7) == "mmst://"
       || file.substr(0, 7) == "mmsh://")
    return std::shared_ptr<CDVDInputStreamFFmpeg>(new CDVDInputStreamFFmpeg(fileitem));
#ifdef ENABLE_DVDINPUTSTREAM_STACK
  else if(file.substr(0, 8) == "stack://")
    return std::shared_ptr<CDVDInputStreamStack>(new CDVDInputStreamStack(fileitem));
#endif
  else if(file.substr(0, 7) == "rtmp://"
       || file.substr(0, 8) == "rtmpt://"
       || file.substr(0, 8) == "rtmpe://"
       || file.substr(0, 9) == "rtmpte://"
       || file.substr(0, 8) == "rtmps://")
    return std::shared_ptr<CDVDInputStreamFFmpeg>(new CDVDInputStreamFFmpeg(fileitem));

  CFileItem finalFileitem(fileitem);

  if (finalFileitem.IsInternetStream())
  {
    if (finalFileitem.ContentLookup())
    {
      CURL origUrl(finalFileitem.GetDynURL());
      XFILE::CCurlFile curlFile;
      // try opening the url to resolve all redirects if any
      try
      {
        if (curlFile.Open(finalFileitem.GetDynURL()))
        {
          CURL finalUrl(curlFile.GetURL());
          finalUrl.SetProtocolOptions(origUrl.GetProtocolOptions());
          finalUrl.SetUserName(origUrl.GetUserName());
          finalUrl.SetPassword(origUrl.GetPassWord());
          finalFileitem.SetPath(finalUrl.Get());
        }
        curlFile.Close();
      }
      catch (XFILE::CRedirectException *pRedirectEx)
      {
        if (pRedirectEx)
        {
          delete pRedirectEx->m_pNewFileImp;
          delete pRedirectEx;
        }
      }
    }

    if (finalFileitem.IsType(".m3u8"))
      return std::shared_ptr<CDVDInputStreamFFmpeg>(new CDVDInputStreamFFmpeg(finalFileitem));

    if (finalFileitem.GetMimeType() == "application/vnd.apple.mpegurl")
      return std::shared_ptr<CDVDInputStreamFFmpeg>(new CDVDInputStreamFFmpeg(finalFileitem));

    if (URIUtils::IsProtocol(finalFileitem.GetPath(), "udp"))
      return std::shared_ptr<CDVDInputStreamFFmpeg>(new CDVDInputStreamFFmpeg(finalFileitem));
  }

  // our file interface handles all these types of streams
  return std::shared_ptr<CDVDInputStreamFile>(new CDVDInputStreamFile(finalFileitem));
}

std::shared_ptr<CDVDInputStream> CDVDFactoryInputStream::CreateInputStream(IVideoPlayer* pPlayer, const CFileItem &fileitem, const std::vector<std::string>& filenames)
{
  return std::shared_ptr<CInputStreamMultiSource>(new CInputStreamMultiSource(pPlayer, fileitem, filenames));
}
