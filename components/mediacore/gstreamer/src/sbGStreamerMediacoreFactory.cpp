/*
 * BEGIN NIGHTINGALE GPL
 *
 * This file is part of the Nightingale Media Player.
 *
 * Copyright(c) 2013
 * http://getnightingale.com
 *
 * This file may be licensed under the terms of of the
 * GNU General Public License Version 2 (the "GPL").
 *
 * Software distributed under the License is distributed
 * on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either
 * express or implied. See the GPL for the specific language
 * governing rights and limitations.
 *
 * You should have received a copy of the GPL along with this
 * program. If not, go to http://www.gnu.org/licenses/gpl.html
 * or write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * END NIGHTINGALE GPL
 */

#include "sbGStreamerMediacoreFactory.h"

#include <nsMemory.h>

#include <nsXPCOMCID.h>
#include <nsIObserverService.h>
#include <nsIPrefBranch.h>
#include <nsIPrefService.h>
#include <mozilla/ReentrantMonitor.h>

#include <sbMediacoreCapabilities.h>
#include <sbTArrayStringEnumerator.h>

#include <sbIGStreamerService.h>
#include <sbStringUtils.h>

#include "sbGStreamerMediacore.h"
#include "sbGStreamerMediacoreCID.h"

/**
 * To log this class, set the following environment variable in a debug build:
 *
 *  NSPR_LOG_MODULES=sbGStreamerMediacoreFactory:5 (or :3 for LOG messages only)
 *
 */
#ifdef PR_LOGGING

static PRLogModuleInfo* gGStreamerMediacoreFactory =
  PR_NewLogModule("sbGStreamerMediacoreFactory");

#define LOG(args)                                          \
  if (gGStreamerMediacoreFactory)                             \
    PR_LOG(gGStreamerMediacoreFactory, PR_LOG_WARNING, args)

#define TRACE(args)                                        \
  if (gGStreamerMediacoreFactory)                             \
    PR_LOG(gGStreamerMediacoreFactory, PR_LOG_DEBUG, args)

#else /* PR_LOGGING */

#define LOG(args)   /* nothing */
#define TRACE(args) /* nothing */

#endif /* PR_LOGGING */

#define BLACKLIST_EXTENSIONS_PREF "songbird.mediacore.gstreamer.blacklistExtensions"
#define BLACKLIST_AUDIO_EXTENSIONS_PREF "songbird.mediacore.gstreamer.blacklistAudioExtensions"
#define BLACKLIST_VIDEO_EXTENSIONS_PREF "songbird.mediacore.gstreamer.blacklistVideoExtensions"
#define VIDEO_EXTENSIONS_PREF "songbird.mediacore.gstreamer.videoExtensions"
#define VIDEO_DISABLED_PREF "songbird.mediacore.gstreamer.disablevideo"

NS_IMPL_ISUPPORTS_INHERITED2(sbGStreamerMediacoreFactory,
                             sbBaseMediacoreFactory,
                             sbIMediacoreFactory,
                             nsIObserver)


sbGStreamerMediacoreFactory::sbGStreamerMediacoreFactory()
{
}

sbGStreamerMediacoreFactory::~sbGStreamerMediacoreFactory()
{

}

nsresult 
sbGStreamerMediacoreFactory::Init()
{
  nsresult rv = sbBaseMediacoreFactory::InitBaseMediacoreFactory();
  NS_ENSURE_SUCCESS(rv, rv);

  /* Ensure GStreamer has been loaded by getting the gstreamer service
   * component (which loads and initialises gstreamer for us).
   */
  nsCOMPtr<sbIGStreamerService> service =
    do_GetService(SBGSTREAMERSERVICE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIObserverService> obs =
    do_GetService(NS_OBSERVERSERVICE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = obs->AddObserver(this, "quit-application", PR_FALSE);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIPrefBranch2> rootPrefBranch =
    do_GetService(NS_PREFSERVICE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = rootPrefBranch->AddObserver(BLACKLIST_EXTENSIONS_PREF, this, PR_FALSE);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = rootPrefBranch->AddObserver(BLACKLIST_AUDIO_EXTENSIONS_PREF, this, PR_FALSE);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = rootPrefBranch->AddObserver(BLACKLIST_VIDEO_EXTENSIONS_PREF, this, PR_FALSE);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = rootPrefBranch->AddObserver(VIDEO_EXTENSIONS_PREF, this, PR_FALSE);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

/*virtual*/ nsresult 
sbGStreamerMediacoreFactory::Shutdown()
{
  nsresult rv;

  nsCOMPtr<nsIObserverService> obs =
    do_GetService(NS_OBSERVERSERVICE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = obs->RemoveObserver(this, "quit-application");
  NS_ENSURE_SUCCESS(rv, rv);
  
  nsCOMPtr<nsIPrefBranch2> rootPrefBranch =
    do_GetService(NS_PREFSERVICE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = rootPrefBranch->RemoveObserver(BLACKLIST_EXTENSIONS_PREF, this);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = rootPrefBranch->RemoveObserver(BLACKLIST_AUDIO_EXTENSIONS_PREF, this);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = rootPrefBranch->RemoveObserver(BLACKLIST_VIDEO_EXTENSIONS_PREF, this);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = rootPrefBranch->RemoveObserver(VIDEO_EXTENSIONS_PREF, this);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

/*virtual*/ nsresult 
sbGStreamerMediacoreFactory::OnInitBaseMediacoreFactory()
{
  nsresult rv = 
    SetName(NS_LITERAL_STRING(SB_GSTREAMERMEDIACOREFACTORY_DESCRIPTION));
  NS_ENSURE_SUCCESS(rv, rv);
  
  rv = 
    SetContractID(NS_LITERAL_STRING(SB_GSTREAMERMEDIACOREFACTORY_CONTRACTID));
  NS_ENSURE_SUCCESS(rv, rv);
  
  return NS_OK;
}

/*virtual*/ nsresult 
sbGStreamerMediacoreFactory::OnGetCapabilities(
                             sbIMediacoreCapabilities **aCapabilities)
{
  mozilla::ReentrantMonitorAutoEnter mon(mMonitor);

  // TODO: This function is now a _huge_ mess. We should talk to product about
  // what files we want to import / etc, some time soon - e.g. the current
  // approach is to treat anything even vaguely-plausibly audio-related as
  // audio (even if we can't play it!), but to only import a small list of fixed
  // extensions for videos (excluding many things we might be able to play).

  nsresult rv;
  if (!mCapabilities) {
    nsRefPtr<sbMediacoreCapabilities> caps;
    caps = new sbMediacoreCapabilities;
    NS_ENSURE_TRUE(caps, NS_ERROR_OUT_OF_MEMORY);

    rv = caps->Init();
    NS_ENSURE_SUCCESS(rv, rv);

    // Build a big list of extensions based on everything gstreamer knows about,
    // plus some known ones, minus a few known non-media-file extensions that
    // gstreamer has typefinders for.

    nsCOMPtr<nsIPrefBranch> rootPrefBranch =
      do_GetService(NS_PREFSERVICE_CONTRACTID, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    nsTArray<nsString> audioExtensions;
    nsTArray<nsString> videoExtensions;
    
    // XXX Mook: we have a silly list of blacklisted extensions because we don't
    // support them and we're being stupid and guessing things based on them.
    // This crap should really look for a plugin that may possibly actually decode
    // these things, or something better.  Whatever the real solution is, this
    // isn't it :(
    nsCString blacklistExtensions;
    { // for scope
      const char defaultBlacklistExtensions[] =
        "txt,htm,html,xml,pdf,cpl,msstyles,scr,sys,ocx,bz2,gz,zip,Z,rar,tar,dll,"
        "exe,a,bmp,png,gif,jpeg,jpg,jpe,tif,tiff,xpm,dat,swf,swfl,stm,cgi,sf,xcf,"
        "far,wvc,mpc,mpp,mp+,ra,rm,rmvb";
      char* blacklistExtensionsPtr = nsnull;
      rv = rootPrefBranch->GetCharPref(BLACKLIST_EXTENSIONS_PREF,
                                       &blacklistExtensionsPtr);
      if (NS_SUCCEEDED(rv)) {
        blacklistExtensions.Adopt(blacklistExtensionsPtr);
      } else {
        blacklistExtensions.Assign(defaultBlacklistExtensions);
      }
      blacklistExtensions.Insert(',', 0);
      blacklistExtensions.Append(',');
      LOG(("sbGStreamerMediacoreFactory: blacklisted extensions: %s\n",
           blacklistExtensions.BeginReading()));
    }

    // check for preference defined extensions to add to audio blacklist 
    nsCString blacklistAudioExts;
    {
      const char defaultBlacklistAudioExts[] = "";
      char* blacklistAudioExtsPtr = nsnull;
      rv = rootPrefBranch->GetCharPref(BLACKLIST_AUDIO_EXTENSIONS_PREF,
                                       &blacklistAudioExtsPtr);
      if (NS_SUCCEEDED(rv)) {
        blacklistAudioExts.Adopt(blacklistAudioExtsPtr);
      } else {
        blacklistAudioExts.Assign(defaultBlacklistAudioExts);
      }
      blacklistAudioExts.Insert(',', 0);
      blacklistAudioExts.Append(',');
      LOG(("sbGStreamerMediacoreFactory: blacklisted audio extensions: %s\n",
           blacklistAudioExts.BeginReading()));
    }

    // Note: if NOT defined
#ifndef STATIC_VIDEO_EXT_LIST
    // check for preference defined extensions to add to video blacklist 
    nsCString blacklistVideoExts;
    {
      const char defaultBlacklistVideoExts[] = "wma";
      char* blacklistVideoExtsPtr = nsnull;
      rv = rootPrefBranch->GetCharPref(BLACKLIST_VIDEO_EXTENSIONS_PREF,
                                       &blacklistVideoExtsPtr);
      if (NS_SUCCEEDED(rv)) {
        blacklistVideoExts.Adopt(blacklistVideoExtsPtr);
      } else {
        blacklistVideoExts.Assign(defaultBlacklistVideoExts);
      }
      blacklistVideoExts.Insert(',', 0);
      blacklistVideoExts.Append(',');
      LOG(("sbGStreamerMediacoreFactory: blacklisted video extensions: %s\n",
           blacklistVideoExts.BeginReading()));
    }
#endif /* ! STATIC_VIDEO_EXT_LIST */

    const char *extraAudioExtensions[] = {"m4r", "m4p", "oga",
                                          "ogg", "aac", "3gp"};
#ifdef XP_WIN
    const char *extraWindowsAudioExtensions[] = {"wma" };
#endif

    // Note: if defined
#ifdef STATIC_VIDEO_EXT_LIST
    { // for scope

      // Per bug 19550 -
      // Severly limit the video extensions that are imported by default to:
      //   * ogv (all platforms)
      //   * wmv (windows only)
      //   * mp4/m4v/mov (w/ qtvideowrapper plugin)

      //   * divx/avi/mkv (w/ ewmpeg4dec plugin)

      videoExtensions.AppendElement(NS_LITERAL_STRING("ogv"));

#ifdef XP_WIN
      videoExtensions.AppendElement(NS_LITERAL_STRING("wmv"));
#endif

      char* knownVideoExtensionsPtr = nsnull;
      rv = rootPrefBranch->GetCharPref(VIDEO_EXTENSIONS_PREF,
                                       &knownVideoExtensionsPtr);
      if (NS_SUCCEEDED(rv)) {
        // The override video extension pref contains a CSV string.
        nsString_Split(NS_ConvertUTF8toUTF16(knownVideoExtensionsPtr),
                       NS_LITERAL_STRING(","),
                       videoExtensions);
      }

#ifdef PR_LOGGING
      nsString videoExtensionStr;
      for (PRUint32 i = 0; i < videoExtensions.Length(); i++) {
        videoExtensionStr.Append(videoExtensions[i]);
        if (i < videoExtensions.Length() - 1) {
          videoExtensionStr.AppendLiteral(", ");
        }
      }

      LOG(("sbGStreamerMediacoreFactory: video file extensions: %s\n",
            videoExtensionStr.get()));
#endif /* PR_LOGGING */

      // Check for the 'qtvideowrapper' plugin to add mp4/m4v extensions.
      PRBool foundQTPlugin = PR_FALSE;
      GstPlugin *plugin = gst_registry_find_plugin(gst_registry_get(),
                                                   ("qtvideowrapper"));
      if (plugin) {
        foundQTPlugin = PR_TRUE;
        videoExtensions.AppendElement(NS_LITERAL_STRING("mp4"));
        videoExtensions.AppendElement(NS_LITERAL_STRING("m4v"));
        videoExtensions.AppendElement(NS_LITERAL_STRING("mov"));
        gst_object_unref(plugin);
      }
      // Check for the 'ewmpeg4dec' plugin to add divx/avi extensions.
      plugin = gst_registry_find_plugin(gst_registry_get(),
                                        ("ewmpeg4dec"));
      if (plugin) {
        videoExtensions.AppendElement(NS_LITERAL_STRING("divx"));
        videoExtensions.AppendElement(NS_LITERAL_STRING("avi"));
        videoExtensions.AppendElement(NS_LITERAL_STRING("mkv"));

        // This plugin will also handle "mp4" and "m4v", only append those
        // extensions if they haven't been added already.
        if (!foundQTPlugin) {
          videoExtensions.AppendElement(NS_LITERAL_STRING("mp4"));
          videoExtensions.AppendElement(NS_LITERAL_STRING("m4v"));
        }

        gst_object_unref(plugin);
      }
    }
#endif /* STATIC_VIDEO_EXT_LIST */

    GList *walker, *list;

    list = gst_type_find_factory_get_list ();
    walker = list;
    while (walker) {
      GstTypeFindFactory *factory = GST_TYPE_FIND_FACTORY (walker->data);
      gboolean blacklisted = FALSE;
      gboolean blacklistedAudio = FALSE;
  // Note: if NOT defined
  #ifndef STATIC_VIDEO_EXT_LIST
      gboolean blacklistedVideo = FALSE;
  #endif
      const gchar* factoryName = gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory));

      gboolean isAudioFactory = g_str_has_prefix(factoryName, "audio/");
      gboolean isVideoFactory = g_str_has_prefix(factoryName, "video/");

      const gchar* const *factoryexts = gst_type_find_factory_get_extensions (factory);
      if (factoryexts) {
        while (*factoryexts) {
          gboolean isAudioExtension = isAudioFactory;
          gboolean isVideoExtension = isVideoFactory;

          nsCString extension(*factoryexts);
          nsCString delimitedExtension(extension);
          delimitedExtension.Insert(',', 0);
          delimitedExtension.Append(',');


          blacklisted = (blacklistExtensions.Find(delimitedExtension) != -1);
          blacklistedAudio = (blacklistAudioExts.Find(delimitedExtension) != -1);
  // Note: if NOT defined
  #ifndef STATIC_VIDEO_EXT_LIST
          blacklistedVideo = (blacklistVideoExts.Find(delimitedExtension) != -1);
  #endif /* ! STATIC_VIDEO_EXT_LIST */

#if PR_LOGGING
          if (blacklisted) {
            if (isAudioExtension) {
              LOG(("sbGStreamerMediacoreFactory: Ignoring (audio) extension '%s'", *factoryexts));
            } else if (isVideoExtension) {
              LOG(("sbGStreamerMediacoreFactory: Ignoring (video) extension '%s'", *factoryexts));
            } else {
              LOG(("sbGStreamerMediacoreFactory: Ignoring extension '%s'", *factoryexts));
            }
          }

          if (blacklistedAudio) {
            if (isAudioExtension) {
              LOG(("sbGStreamerMediacoreFactory: Ignoring pref blacklisted (audio) extension '%s'", *factoryexts));
            } else {
              LOG(("sbGStreamerMediacoreFactory: Ignoring pref blacklisted extension '%s'", *factoryexts));
            }
          }

  // Note: if NOT defined
  #ifndef STATIC_VIDEO_EXT_LIST
          if (blacklistedVideo) {
            if (isAudioExtension) {
              LOG(("sbGStreamerMediacoreFactory: Ignoring pref blacklisted (video) extension '%s'", *factoryexts));
            } else { 
              LOG(("sbGStreamerMediacoreFactory: Ignoring pref blacklisted extension '%s'", *factoryexts));
            }
          }
  #endif /* ! STATIC_VIDEO_EXT_LIST */

#endif /* PR_LOGGING */

          nsString ext = NS_ConvertUTF8toUTF16(*factoryexts);

          if (!blacklisted) {
            if (isAudioExtension && !audioExtensions.Contains(ext)
                                 && !blacklistedAudio)
            {
              audioExtensions.AppendElement(ext);

              LOG(("sbGStreamerMediacoreFactory: registering audio extension %s\n",
                    *factoryexts));

            } else {

              // Note: if NOT defined
#ifndef STATIC_VIDEO_EXT_LIST
              if (isVideoExtension && !videoExtensions.Contains(ext)
                                   && !blacklistedVideo)
              {
                videoExtensions.AppendElement(ext);

                LOG(("sbGStreamerMediacoreFactory: registering video extensions %s\n",
                      *factoryexts));
              }
#endif /* ! STATIC_VIDEO_EXT_LIST */

            }
          }

          factoryexts++;
        }
      }
      walker = g_list_next (walker);
    }
    g_list_free (list);

    for (unsigned int i = 0; i < NS_ARRAY_LENGTH(extraAudioExtensions); i++) {
      nsString ext = NS_ConvertUTF8toUTF16(extraAudioExtensions[i]);
      if(!audioExtensions.Contains(ext))
        audioExtensions.AppendElement(ext);
    }

#if XP_WIN
    for (unsigned int i = 0; i < NS_ARRAY_LENGTH(extraWindowsAudioExtensions); i++) {
      nsString ext = NS_ConvertUTF8toUTF16(extraWindowsAudioExtensions[i]);
      if(!audioExtensions.Contains(ext))
        audioExtensions.AppendElement(ext);
    }
#endif

    rv = caps->SetAudioExtensions(audioExtensions);
    NS_ENSURE_SUCCESS(rv, rv);

    // Audio playback is always allowed.
    rv = caps->SetSupportsAudioPlayback(PR_TRUE);
    NS_ENSURE_SUCCESS(rv, rv);

    PRBool videoDisabled = PR_FALSE;
    rv = rootPrefBranch->GetBoolPref(
                                    "songbird.mediacore.gstreamer.disablevideo",
                                    &videoDisabled);
    NS_ENSURE_SUCCESS(rv, rv);
    if (!videoDisabled) {
      rv = caps->SetVideoExtensions(videoExtensions);
      NS_ENSURE_SUCCESS(rv, rv);

      rv = caps->SetSupportsVideoPlayback(PR_TRUE);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    mCapabilities = caps;
  }

  rv = CallQueryInterface(mCapabilities.get(), aCapabilities);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

/*virtual*/ nsresult 
sbGStreamerMediacoreFactory::OnCreate(const nsAString &aInstanceName, 
                                    sbIMediacore **_retval)
{
  nsRefPtr<sbGStreamerMediacore> mediacore;
  mediacore = new sbGStreamerMediacore;
  NS_ENSURE_TRUE(mediacore, NS_ERROR_OUT_OF_MEMORY);

  nsresult rv = mediacore->Init();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mediacore->SetInstanceName(aInstanceName);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = CallQueryInterface(mediacore.get(), _retval);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult 
sbGStreamerMediacoreFactory::Observe(nsISupports *subject,
                                     const char* topic,
                                     const PRUnichar *aData)
{
  if (!strcmp(topic, "quit-application")) {
    return Shutdown();
  }
  else if (!strcmp(topic, NS_PREFBRANCH_PREFCHANGE_TOPIC_ID)) {
    mozilla::ReentrantMonitorAutoEnter mon(mMonitor);

    mCapabilities = nsnull;
    return NS_OK;
  }

  return NS_OK;
}
