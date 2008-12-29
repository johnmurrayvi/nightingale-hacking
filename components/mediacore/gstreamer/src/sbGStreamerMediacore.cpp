/*
//
// BEGIN SONGBIRD GPL
// 
// This file is part of the Songbird web player.
//
// Copyright(c) 2005-2008 POTI, Inc.
// http://songbirdnest.com
// 
// This file may be licensed under the terms of of the
// GNU General Public License Version 2 (the "GPL").
// 
// Software distributed under the License is distributed 
// on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either 
// express or implied. See the GPL for the specific language 
// governing rights and limitations.
//
// You should have received a copy of the GPL along with this 
// program. If not, go to http://www.gnu.org/licenses/gpl.html
// or write to the Free Software Foundation, Inc., 
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
// 
// END SONGBIRD GPL
//
*/

#include "sbGStreamerMediacore.h"

#include <nsIFile.h>
#include <nsIFileURL.h>
#include <nsIInterfaceRequestorUtils.h>
#include <nsServiceManagerUtils.h>
#include <nsILocalFile.h>
#include <nsIURI.h>
#include <nsIURL.h>
#include <nsIRunnable.h>
#include <nsIIOService.h>
#include <nsIConsoleService.h>
#include <nsIScriptError.h>
#include <nsIPrefBranch2.h>
#include <nsIObserver.h>
#include <nsThreadUtils.h>
#include <nsCOMPtr.h>
#include <prlog.h>

// Required to crack open the DOM XUL Element and get a native window handle.
#include <nsIBaseWindow.h>
#include <nsIBoxObject.h>
#include <nsIDocument.h>
#include <nsIDOMAbstractView.h>
#include <nsIDOMEvent.h>
#include <nsIDocShellTreeItem.h>
#include <nsIDocShellTreeOwner.h>
#include <nsIDOMDocument.h>
#include <nsIDOMDocumentView.h>
#include <nsIDOMEventTarget.h>
#include <nsIDOMXULElement.h>
#include <nsIScriptGlobalObject.h>
#include <nsIWebNavigation.h>
#include <nsIWidget.h>

#include <sbClassInfoUtils.h>
#include <sbTArrayStringEnumerator.h>
#include <sbVariantUtils.h>
#include <sbBaseMediacoreEventTarget.h>
#include <sbMediacoreError.h>
#include <sbProxiedComponentManager.h>

#include <sbIGStreamerService.h>
#include <sbIMediaItem.h>
#include <sbStandardProperties.h>

#include "sbGStreamerMediacoreUtils.h"

#include <algorithm>

#ifdef MOZ_WIDGET_GTK2
#include "sbGStreamerPlatformGDK.h"
#endif

#ifdef XP_WIN
#include "sbGStreamerPlatformWin32.h"
#endif

#ifdef XP_MACOSX
#include "sbGStreamerPlatformOSX.h"
#endif

#ifdef CreateEvent
#undef CreateEvent
#endif

#define MAX_FILE_SIZE_FOR_ACCURATE_SEEK (20 * 1024 * 1024)

/**
 * To log this class, set the following environment variable in a debug build:
 *
 *  NSPR_LOG_MODULES=sbGStreamerMediacore:5 (or :3 for LOG messages only)
 *
 */
#ifdef PR_LOGGING

static PRLogModuleInfo* gGStreamerMediacore =
  PR_NewLogModule("sbGStreamerMediacore");

#define LOG(args)                                          \
  if (gGStreamerMediacore)                             \
    PR_LOG(gGStreamerMediacore, PR_LOG_WARNING, args)

#define TRACE(args)                                        \
  if (gGStreamerMediacore)                             \
    PR_LOG(gGStreamerMediacore, PR_LOG_DEBUG, args)

#else /* PR_LOGGING */

#define LOG(args)   /* nothing */
#define TRACE(args) /* nothing */

#endif /* PR_LOGGING */

NS_IMPL_THREADSAFE_ADDREF(sbGStreamerMediacore)
NS_IMPL_THREADSAFE_RELEASE(sbGStreamerMediacore)

NS_IMPL_QUERY_INTERFACE10_CI(sbGStreamerMediacore,
                            sbIMediacore,
                            sbIMediacorePlaybackControl,
                            sbIMediacoreVolumeControl,
                            sbIMediacoreVotingParticipant,
                            sbIMediacoreEventTarget,
                            sbIMediacoreVideoWindow,
                            sbIGStreamerMediacore,
                            nsIDOMEventListener,
                            nsIObserver,			
                            nsIClassInfo)

NS_IMPL_CI_INTERFACE_GETTER7(sbGStreamerMediacore,
                             sbIMediacore,
                             sbIMediacorePlaybackControl,
                             sbIMediacoreVideoWindow,
                             sbIMediacoreVolumeControl,
                             sbIMediacoreVotingParticipant,
                             sbIGStreamerMediacore,
                             sbIMediacoreEventTarget)

NS_DECL_CLASSINFO(sbGStreamerMediacore)
NS_IMPL_THREADSAFE_CI(sbGStreamerMediacore)

sbGStreamerMediacore::sbGStreamerMediacore() :
    mMonitor(nsnull),
    mHaveVideoWindow(PR_FALSE),
    mPipeline(nsnull),
    mPlatformInterface(nsnull),
    mBaseEventTarget(new sbBaseMediacoreEventTarget(this)),
    mPrefs(nsnull),
    mReplaygainElement(nsnull),
    mTags(NULL),
    mProperties(nsnull),
    mStopped(PR_FALSE),
    mBuffering(PR_FALSE),
    mIsLive(PR_FALSE),
    mHasSeenError(PR_FALSE),
    mTargetState(GST_STATE_NULL),
    mVideoDisabled(PR_FALSE),
    mVideoSinkDescription(),
    mAudioSinkDescription(),
    mAudioSinkBufferTime(0),
    mStreamingBufferSize(0),
    mResourceIsLocal(PR_FALSE),
    mResourceSize(-1)
{
  MOZ_COUNT_CTOR(sbGStreamerMediacore);

  NS_WARN_IF_FALSE(mBaseEventTarget, 
          "mBaseEventTarget is null, may be out of memory");

}

sbGStreamerMediacore::~sbGStreamerMediacore()
{
  MOZ_COUNT_DTOR(sbGStreamerMediacore);

  if (mTags)
    gst_tag_list_free(mTags);

  if (mReplaygainElement)
    gst_object_unref (mReplaygainElement);

  std::vector<GstElement *>::const_iterator it = mAudioFilters.begin();
  for ( ; it < mAudioFilters.end(); ++it)
    gst_object_unref (*it);

  if (mMonitor)
    nsAutoMonitor::DestroyMonitor(mMonitor);
}

nsresult
sbGStreamerMediacore::Init() 
{
  nsresult rv;

  mMonitor = nsAutoMonitor::NewMonitor("sbGStreamerMediacore::mMonitor");
  NS_ENSURE_TRUE(mMonitor, NS_ERROR_OUT_OF_MEMORY);

  rv = sbBaseMediacore::InitBaseMediacore();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = sbBaseMediacorePlaybackControl::InitBaseMediacorePlaybackControl();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = sbBaseMediacoreVolumeControl::InitBaseMediacoreVolumeControl();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = InitPreferences();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
sbGStreamerMediacore::InitPreferences()
{
  nsresult rv;
  mPrefs = do_ProxiedGetService("@mozilla.org/preferences-service;1", &rv);
  NS_ENSURE_SUCCESS (rv, rv);

  rv = mPrefs->AddObserver("songbird.mediacore", this, PR_FALSE);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = ReadPreferences();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
sbGStreamerMediacore::ReadPreferences()
{
  NS_ENSURE_STATE (mPrefs);
  nsresult rv;

  rv = mPrefs->GetBoolPref("songbird.mediacore.gstreamer.disablevideo", 
	&mVideoDisabled);
  if (rv == NS_ERROR_UNEXPECTED)
    mVideoDisabled = PR_TRUE;
  else
    NS_ENSURE_SUCCESS(rv, rv);

  PRInt32 prefType;
  const char *VIDEO_SINK_PREF = "songbird.mediacore.gstreamer.videosink";
  const char *AUDIO_SINK_PREF = "songbird.mediacore.gstreamer.audiosink";

  rv = mPrefs->GetPrefType(VIDEO_SINK_PREF, &prefType);
  NS_ENSURE_SUCCESS(rv, rv);
  if (prefType == nsIPrefBranch::PREF_STRING) {
    rv = mPrefs->GetCharPref(VIDEO_SINK_PREF, 
	getter_Copies(mVideoSinkDescription));
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = mPrefs->GetPrefType(AUDIO_SINK_PREF, &prefType);
  NS_ENSURE_SUCCESS(rv, rv);
  if (prefType == nsIPrefBranch::PREF_STRING) {
    rv = mPrefs->GetCharPref(AUDIO_SINK_PREF, 
	getter_Copies(mAudioSinkDescription));
    NS_ENSURE_SUCCESS(rv, rv);
  }

  /* In milliseconds */
  const char *AUDIO_SINK_BUFFERTIME_PREF = 
      "songbird.mediacore.output.buffertime";
  /* In kilobytes */
  const char *STREAMING_BUFFERSIZE_PREF = 
      "songbird.mediacore.streaming.buffersize";

  /* Defaults if the prefs aren't present */
  PRInt64 audioSinkBufferTime = 1000 * 1000; /* 1000 ms */
  PRInt32 streamingBufferSize = 128 * 1024; /* 128 kB */

  rv = mPrefs->GetPrefType(AUDIO_SINK_BUFFERTIME_PREF, &prefType);
  NS_ENSURE_SUCCESS(rv, rv);
  if (prefType == nsIPrefBranch::PREF_INT) {
    PRInt32 time = 0;
    rv = mPrefs->GetIntPref(AUDIO_SINK_BUFFERTIME_PREF, &time);
    NS_ENSURE_SUCCESS(rv, rv);

    // Only use the pref if it's a valid value.
    if(time >= 100 || time <= 10000) {
      // Convert to usecs
      audioSinkBufferTime = time * 1000;
    }
  }

  rv = mPrefs->GetPrefType(STREAMING_BUFFERSIZE_PREF, &prefType);
  NS_ENSURE_SUCCESS(rv, rv);
  if (prefType == nsIPrefBranch::PREF_INT) {
    rv = mPrefs->GetIntPref(STREAMING_BUFFERSIZE_PREF, &streamingBufferSize);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  mAudioSinkBufferTime = audioSinkBufferTime;
  mStreamingBufferSize = streamingBufferSize;

  const char *NORMALIZATION_ENABLED_PREF = 
      "songbird.mediacore.normalization.enabled";
  const char *NORMALIZATION_MODE_PREF = 
      "songbird.mediacore.normalization.preferredGain";
  PRBool normalizationEnabled = PR_TRUE;
  rv = mPrefs->GetPrefType(NORMALIZATION_ENABLED_PREF, &prefType);
  NS_ENSURE_SUCCESS(rv, rv);
  if (prefType == nsIPrefBranch::PREF_BOOL) {
    rv = mPrefs->GetBoolPref(NORMALIZATION_ENABLED_PREF, &normalizationEnabled);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (normalizationEnabled) {
    if (!mReplaygainElement) {
      mReplaygainElement = gst_element_factory_make ("rgvolume", NULL);

      // Ref and sink the object to take ownership; we'll keep track of it 
      // from here on.
      gst_object_ref (mReplaygainElement);
      gst_object_sink (mReplaygainElement);

      rv = AddAudioFilter(mReplaygainElement);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    /* Now check the mode; set the appropriate property on the element */
    nsCString normalizationMode;
    rv = mPrefs->GetPrefType(NORMALIZATION_MODE_PREF, &prefType);
    NS_ENSURE_SUCCESS(rv, rv);
    if (prefType == nsIPrefBranch::PREF_STRING) {
      rv = mPrefs->GetCharPref(NORMALIZATION_MODE_PREF, 
              getter_Copies(normalizationMode));
      NS_ENSURE_SUCCESS(rv, rv);
    }

    if (normalizationMode.EqualsLiteral("track")) {
      g_object_set (mReplaygainElement, "album-mode", FALSE, NULL);
    }
    else {
      g_object_set (mReplaygainElement, "album-mode", TRUE, NULL);
    }
  }
  else {
    if (mReplaygainElement) {
      rv = RemoveAudioFilter(mReplaygainElement);
      NS_ENSURE_SUCCESS(rv, rv);

      gst_object_unref (mReplaygainElement);
      mReplaygainElement = NULL;
    }
  }

  return NS_OK;
}


// Utility methods

class sbGstMessageEvent : public nsIRunnable
{
public:
  NS_DECL_ISUPPORTS

  explicit sbGstMessageEvent(GstMessage *msg, sbGStreamerMediacore *core) :
      mCore(core)
  {
    gst_message_ref(msg);
    mMessage = msg;
  }

  ~sbGstMessageEvent() {
    gst_message_unref(mMessage);
  }

  NS_IMETHOD Run()
  {
    mCore->HandleMessage(mMessage);
    return NS_OK;
  }

private:
  GstMessage *mMessage;
  sbGStreamerMediacore *mCore;
};

NS_IMPL_THREADSAFE_ISUPPORTS1(sbGstMessageEvent,
                              nsIRunnable)

/* static */ GstBusSyncReply
sbGStreamerMediacore::syncHandler(GstBus* bus, GstMessage* message, 
        gpointer data)
{
  sbGStreamerMediacore *core = static_cast<sbGStreamerMediacore*>(data);

  // Allow a sync handler to look at this first.
  // If it returns false (the default), we dispatch it asynchronously.
  PRBool handled = core->HandleSynchronousMessage(message);

  if (!handled) {
    nsCOMPtr<nsIRunnable> event = new sbGstMessageEvent(message, core);
    NS_DispatchToMainThread(event);
  }

  gst_message_unref (message);

  return GST_BUS_DROP;
}

/* static */ void
sbGStreamerMediacore::aboutToFinishHandler(GstElement *playbin, gpointer data)
{
  sbGStreamerMediacore *core = static_cast<sbGStreamerMediacore*>(data);
  core->HandleAboutToFinishSignal();
  return;
}

GstElement *
sbGStreamerMediacore::CreateSinkFromPrefs(const char *aSinkDescription)
{ 
  // Only try to create it if we have a non-null, non-zero-length description
  if (aSinkDescription && *aSinkDescription) 
  {
    GstElement *sink = gst_parse_bin_from_description (aSinkDescription,
            TRUE, NULL);
    // If parsing failed, sink is NULL; return it either way.
    return sink;
  }

  return NULL;
}

nsresult
sbGStreamerMediacore::GetFileSize(nsIURI *aURI, PRInt64 *aFileSize)
{
  nsresult rv;

  nsCOMPtr<nsIFileURL> fileUrl = do_QueryInterface(aURI, &rv);
  if (rv != NS_ERROR_NO_INTERFACE) {
    NS_ENSURE_SUCCESS (rv, rv);

    nsCOMPtr<nsIFile> file;
    rv = fileUrl->GetFile(getter_AddRefs(file));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = file->GetFileSize(aFileSize);
    NS_ENSURE_SUCCESS (rv, rv);
  }
  else {
    // That's ok, not a local file.
    return rv;
  }

  return NS_OK;
}

GstElement *
sbGStreamerMediacore::CreateVideoSink()
{
  nsAutoMonitor lock(mMonitor);

  GstElement *videosink = CreateSinkFromPrefs(mVideoSinkDescription.get());

  if (mPlatformInterface)
    videosink = mPlatformInterface->SetVideoSink(videosink);

  return videosink;
}

GstElement *
sbGStreamerMediacore::CreateAudioSink()
{
  nsAutoMonitor lock(mMonitor);

  GstElement *sinkbin = gst_bin_new ("audiosink-bin");
  GstElement *audiosink = CreateSinkFromPrefs(mAudioSinkDescription.get());
  GstPad *targetpad, *ghostpad;

  if (mPlatformInterface)
    audiosink = mPlatformInterface->SetAudioSink(audiosink);

  /* audiosink is our actual sink, or something else close enough to it.
   * Now, we build a bin looking like this, with each of the added audio
   * filters in it:
   *
   * |-------------------------------------------------------------------|
   * |audiosink-bin                                                      |
   * |                                                                   |
   * | [-------]  [------------]  [-------]  [------------]  [---------] |
   * |-[Filter1]--[audioconvert]--[Filter2]--[audioconvert]--[audiosink] |
   * | [-------]  [------------]  [-------]  [------------]  [---------] |
   * |                                                                   |
   * |-------------------------------------------------------------------|
   *
   */

  gst_bin_add ((GstBin *)sinkbin, audiosink);

  targetpad = gst_element_get_pad (audiosink, "sink");

  /* Add each filter, followed by an audioconvert. The first-added filter ends
   * last in the pipeline, so we iterate in reverse.
   */
  std::vector<GstElement *>::const_reverse_iterator it = mAudioFilters.rbegin(),
      end = mAudioFilters.rend();
  for ( ; it != end; ++it)
  {
    GstElement *audioconvert = gst_element_factory_make ("audioconvert", NULL);
    GstElement *filter = *it;
    GstPad *srcpad, *sinkpad;

    gst_bin_add_many ((GstBin *)sinkbin, filter, audioconvert, NULL);

    srcpad = gst_element_get_pad (filter, "src");
    sinkpad = gst_element_get_pad (audioconvert, "sink");
    gst_pad_link (srcpad, sinkpad);
    gst_object_unref (srcpad);
    gst_object_unref (sinkpad);

    srcpad = gst_element_get_pad (audioconvert, "src");
    gst_pad_link (srcpad, targetpad);
    gst_object_unref (targetpad);
    gst_object_unref (srcpad);

    targetpad = gst_element_get_pad (filter, "sink");
  }

  // Now, targetpad is the left-most real pad in our bin. Ghost it to provide
  // a sinkpad on our bin.
  ghostpad = gst_ghost_pad_new ("sink", targetpad);
  gst_element_add_pad (sinkbin, ghostpad);

  gst_object_unref (targetpad);

  return sinkbin;
}

/* static */ void
sbGStreamerMediacore::currentVideoSetHelper(GObject* obj, GParamSpec* pspec,
        sbGStreamerMediacore *core)
{
  int current_video;
  GstPad *pad;

  /* Which video stream has been activated? */
  g_object_get(obj, "current-video", &current_video, NULL);
  NS_ASSERTION(current_video >= 0, "current video is negative");

  /* Get the video pad for this stream number */
  g_signal_emit_by_name(obj, "get-video-pad", current_video, &pad);

  if (pad) {
    GstCaps *caps;
    caps = gst_pad_get_negotiated_caps(pad);
    if (caps) {
      core->OnVideoCapsSet(caps);
      gst_caps_unref(caps);
    }

    g_signal_connect(pad, "notify::caps",
            G_CALLBACK(videoCapsSetHelper), core);

    gst_object_unref(pad);
  }
}

/* static */ void
sbGStreamerMediacore::videoCapsSetHelper(GObject* obj, GParamSpec* pspec,
        sbGStreamerMediacore *core)
{
  GstPad *pad = GST_PAD(obj);
  GstCaps *caps = gst_pad_get_negotiated_caps(pad);

  if (caps) {
    core->OnVideoCapsSet(caps);
    gst_caps_unref (caps);
  }
}

nsresult 
sbGStreamerMediacore::DestroyPipeline()
{
  GstElement *pipeline = NULL;
  nsAutoMonitor lock(mMonitor);
  if (mPipeline)
    pipeline = (GstElement *)g_object_ref (mPipeline);
  lock.Exit();

  /* Do state-change with the lock dropped */
  if (pipeline) {
    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_object_unref (pipeline);
  }

  lock.Enter();
  if (mPipeline) {
    /* If we put any filters in the pipeline, remove them now, so that we can
     * re-add them later to a different pipeline
     */
    std::vector<GstElement *>::const_iterator it = mAudioFilters.begin();
    for ( ; it < mAudioFilters.end(); ++it)
    {
      GstElement *parent;
      GstElement *filter = *it;

      parent = (GstElement *)gst_element_get_parent (filter);
      if (parent) {
        gst_bin_remove ((GstBin *)parent, filter);
        gst_object_unref (parent);
      }
    }

    gst_object_unref (mPipeline);
    mPipeline = nsnull;
  }
  if (mTags) {
    gst_tag_list_free (mTags);
    mTags = nsnull;
  }
  mProperties = nsnull;

  mStopped = PR_FALSE;
  mBuffering = PR_FALSE;
  mIsLive = PR_FALSE;
  mHasSeenError = PR_FALSE;
  mTargetState = GST_STATE_NULL;

  return NS_OK;
}

nsresult 
sbGStreamerMediacore::SetBufferingProperties(GstElement *aPipeline)
{
  NS_ENSURE_ARG_POINTER(aPipeline);

  if (g_object_class_find_property(
              G_OBJECT_GET_CLASS (aPipeline), "buffer-size")) 
    g_object_set (aPipeline, "buffer-size", mStreamingBufferSize, NULL);

  return NS_OK;
}

nsresult 
sbGStreamerMediacore::CreatePlaybackPipeline()
{
  nsresult rv;
  gint flags;

  nsAutoMonitor lock(mMonitor);

  rv = DestroyPipeline();
  NS_ENSURE_SUCCESS (rv, rv);

  mPipeline = gst_element_factory_make ("playbin2", "player");

  if (!mPipeline)
    return NS_ERROR_FAILURE;

  if (mPlatformInterface) {
    GstElement *audiosink = CreateAudioSink();

    // Set audio sink
    g_object_set(mPipeline, "audio-sink", audiosink, NULL);

    // Set audio sink buffer time based on pref
    if (g_object_class_find_property(
        G_OBJECT_GET_CLASS (audiosink), "buffer-time"))
      g_object_set (audiosink, "buffer-time", (gint64)mAudioSinkBufferTime, NULL);

    if (!mVideoDisabled) {
      GstElement *videosink = CreateVideoSink();
      g_object_set(mPipeline, "video-sink", videosink, NULL);
    }
  }

  // Configure what to output - we want audio only, unless video
  // is turned on
  flags = 0x2 | 0x10; // audio | soft-volume
  if (mHaveVideoWindow && !mVideoDisabled) {
    // Enable video only if we're set up for it is turned off. Also enable
    // text (subtitles), which require a video window to display.
    flags |= 0x1 | 0x4; // video | text
  }
  g_object_set (G_OBJECT(mPipeline), "flags", flags, NULL);

  GstBus *bus = gst_element_get_bus (mPipeline);

  // We want to receive state-changed messages when shutting down, so we
  // need to turn off bus auto-flushing
  g_object_set(mPipeline, "auto-flush-bus", FALSE, NULL);

  rv = SetBufferingProperties(mPipeline);
  NS_ENSURE_SUCCESS (rv, rv);

  // Handle GStreamer messages synchronously, either directly or
  // dispatching to the main thread.
  gst_bus_set_sync_handler (bus, syncHandler, this);

  g_object_unref ((GObject *)bus);

  // Handle about-to-finish signal emitted by playbin2
  g_signal_connect (mPipeline, "about-to-finish", 
          G_CALLBACK (aboutToFinishHandler), this);
  // Get notified when the current video stream changes.
  // This will let us get information about the specific video stream
  // being played.
  g_signal_connect (mPipeline, "notify::current-video",
          G_CALLBACK (currentVideoSetHelper), this);

  return NS_OK;
}

PRBool sbGStreamerMediacore::HandleSynchronousMessage(GstMessage *aMessage)
{
  GstMessageType msg_type;
  msg_type = GST_MESSAGE_TYPE(aMessage);

  switch (msg_type) {
    case GST_MESSAGE_ELEMENT: {
      // Win32 and GDK use prepare-xwindow-id, OSX has its own private thing,
      // have-ns-view
      if (gst_structure_has_name(aMessage->structure, "prepare-xwindow-id") ||
          gst_structure_has_name(aMessage->structure, "have-ns-view"))
      {
        if(mPlatformInterface) 
        {
          DispatchMediacoreEvent(sbIMediacoreEvent::STREAM_HAS_VIDEO);
          mPlatformInterface->PrepareVideoWindow(aMessage);
        }
        return PR_TRUE;
      }
    }
    default:
      break;
  }

  /* Return PR_FALSE since we haven't handled the message */
  return PR_FALSE;
}

void sbGStreamerMediacore::DispatchMediacoreEvent (unsigned long type, 
        nsIVariant *aData, sbIMediacoreError *aError)
{
  nsresult rv;
  nsCOMPtr<sbIMediacoreEvent> event;
  rv = sbMediacoreEvent::CreateEvent(type,
                                     aError,
                                     aData,
                                     this,
                                     getter_AddRefs(event));
  NS_ENSURE_SUCCESS(rv, /* void */);

  rv = DispatchEvent(event, PR_TRUE, nsnull);
  NS_ENSURE_SUCCESS(rv, /* void */);
}

void sbGStreamerMediacore::HandleAboutToFinishSignal()
{
  LOG(("Handling about-to-finish signal"));

  nsAutoMonitor mon(sbBaseMediacore::mMonitor);

  // Never try to handle the next file if we've seen an error.
  if (mHasSeenError)
    return;

  nsCOMPtr<sbIMediacoreSequencer> sequencer = mSequencer;
  mon.Exit();

  if(!sequencer) {
    return;
  }

  nsCOMPtr<sbIMediaItem> item;
  nsresult rv = sequencer->GetNextItem(getter_AddRefs(item));
  NS_ENSURE_SUCCESS(rv, /*void*/ );
  NS_ENSURE_TRUE(item, /*void*/);

  nsString contentURL;
  rv = item->GetProperty(NS_LITERAL_STRING(SB_PROPERTY_CONTENTURL), 
          contentURL);
  NS_ENSURE_SUCCESS(rv, /*void*/ );

  if(StringBeginsWith(contentURL, NS_LITERAL_STRING("file:"))) {
    rv = sequencer->RequestHandleNextItem(this);
    NS_ENSURE_SUCCESS(rv, /*void*/ );

    mon.Enter();

    // Clear old tags so we don't merge them with the new ones
    if (mTags) {
      gst_tag_list_free (mTags);
      mTags = nsnull;
    }
    mProperties = nsnull;
    mResourceIsLocal = PR_TRUE;

    nsCOMPtr<nsIURI> itemuri;
    rv = item->GetContentSrc(getter_AddRefs(itemuri));
    NS_ENSURE_SUCCESS (rv, /*void*/);

    rv = GetFileSize (itemuri, &mResourceSize);
    NS_ENSURE_TRUE(mPipeline, /*void*/);

    nsCString uri = NS_ConvertUTF16toUTF8(contentURL);
    LOG(("Setting URI to \"%s\"", uri.BeginReading()));

    /* Set the URI to play */
    NS_ENSURE_TRUE(mPipeline, /*void*/);
    g_object_set (G_OBJECT (mPipeline), "uri", uri.BeginReading(), NULL);
  }

  return;
}

void sbGStreamerMediacore::HandleTagMessage(GstMessage *message)
{
  GstTagList *tag_list;
  nsresult rv;

  LOG(("Handling tag message"));
  gst_message_parse_tag(message, &tag_list);

  if (mTags) {
    GstTagList *newTags = gst_tag_list_merge (mTags, tag_list,
            GST_TAG_MERGE_REPLACE);
    gst_tag_list_free (mTags);
    mTags = newTags;
  }
  else
    mTags = gst_tag_list_copy (tag_list);

  rv = ConvertTagListToPropertyArray(mTags, getter_AddRefs(mProperties));
  gst_tag_list_free(tag_list);

  if (NS_SUCCEEDED (rv)) {
    nsCOMPtr<nsISupports> properties = do_QueryInterface(mProperties, &rv);
    NS_ENSURE_SUCCESS (rv, /* void */);
    nsCOMPtr<nsIVariant> propVariant = sbNewVariant(properties).get();
    DispatchMediacoreEvent (sbIMediacoreEvent::METADATA_CHANGE, propVariant);
  }
  else // Non-fatal, just log a message
    LOG(("Failed to convert")); 
}

void sbGStreamerMediacore::HandleStateChangedMessage(GstMessage *message)
{
  // Only listen to state-changed messages from top-level pipelines
  if (GST_IS_PIPELINE (message->src))
  {
    GstState oldstate, newstate, pendingstate;
    gst_message_parse_state_changed (message, 
            &oldstate, &newstate, &pendingstate);

    // Dispatch START, PAUSE, STOP/END (but only if it's our target state)
    if (pendingstate == GST_STATE_VOID_PENDING && newstate == mTargetState) {
      if (newstate == GST_STATE_PLAYING)
        DispatchMediacoreEvent (sbIMediacoreEvent::STREAM_START);
      else if (newstate == GST_STATE_PAUSED)
        DispatchMediacoreEvent (sbIMediacoreEvent::STREAM_PAUSE);
      else if (newstate == GST_STATE_NULL)
      {
        // Distinguish between 'stopped via API' and 'stopped due to error or
        // reaching EOS'
        if (mStopped)
          DispatchMediacoreEvent (sbIMediacoreEvent::STREAM_STOP);
        else
          DispatchMediacoreEvent (sbIMediacoreEvent::STREAM_END);
      }
    }
    // We've reached our current pending state, but not our target state.
    else if (pendingstate == GST_STATE_VOID_PENDING)
    {
      // If we're not waiting for buffering to complete (where we handle
      // the state changes differently), then continue on to PLAYING.
      if (newstate == GST_STATE_PAUSED && mTargetState == GST_STATE_PLAYING &&
          !mBuffering)
      {
        gst_element_set_state (mPipeline, GST_STATE_PLAYING);
      }
    }
  }
}

void sbGStreamerMediacore::HandleBufferingMessage (GstMessage *message)
{
  nsAutoMonitor lock(mMonitor);

  gint percent = 0;
  gst_message_parse_buffering (message, &percent);
  TRACE(("Buffering (%u percent done)", percent));

  // We don't want to handle buffering specially for live pipelines
  if (mIsLive)
    return;

  /* If we receive buffering messages, go to PAUSED.
   * Then, return to PLAYING once we have 100% buffering (which will be
   * before we actually hit PAUSED)
   */
  if (percent >= 100) {
    mBuffering = PR_FALSE;

    if (mTargetState == GST_STATE_PLAYING) {
      TRACE(("Buffering complete, setting state to playing"));
      gst_element_set_state (mPipeline, GST_STATE_PLAYING);
    }
    else if (mTargetState == GST_STATE_PAUSED) {
      TRACE(("Buffering complete, setting state to paused"));
      DispatchMediacoreEvent (sbIMediacoreEvent::STREAM_PAUSE);
    }
  }
  else if (percent < 100) {
    GstState cur_state;
    gst_element_get_state (mPipeline, &cur_state, NULL, 0);

    /* Only pause if we've already reached playing (this means we've underrun
     * the buffer and need to rebuffer) */
    if (!mBuffering && cur_state == GST_STATE_PLAYING) {
      TRACE(("Buffering... setting to paused"));
      gst_element_set_state (mPipeline, GST_STATE_PAUSED);
      mTargetState = GST_STATE_PLAYING;

      // And inform listeners that we've underrun */
      DispatchMediacoreEvent(sbIMediacoreEvent::BUFFER_UNDERRUN);
    }
    mBuffering = PR_TRUE;

    // Inform listeners of current progress
    double bufferingProgress = (double)percent / 100.;
    nsCOMPtr<nsIVariant> variant = sbNewVariant(bufferingProgress).get();
    DispatchMediacoreEvent(sbIMediacoreEvent::BUFFERING, variant);
  }
}

// Demuxers (such as qtdemux) send redirect messages when the media
// file itself redirects to another location. Handle these here.
void sbGStreamerMediacore::HandleRedirectMessage(GstMessage *message)
{
  const gchar *location;
  nsresult rv;
  nsCString uriString;

  location = gst_structure_get_string (message->structure, "new-location");

  if (location && *location) {
    if (strstr (location, "://") != NULL) {
      // Then we assume it's an absolute URL
      uriString = location;
    }
    else {
      TRACE (("Resolving redirect to '%s'", location));

      rv = mUri->Resolve(nsDependentCString(location), uriString);
      NS_ENSURE_SUCCESS (rv, /* void */ );
    }

    // Now create a URI from our string form.
    nsCOMPtr<nsIIOService> ioService = do_GetService(
            "@mozilla.org/network/io-service;1", &rv);
    NS_ENSURE_SUCCESS (rv, /* void */ );

    nsCOMPtr<nsIURI> finaluri;
    rv = ioService->NewURI(uriString, nsnull, nsnull,
            getter_AddRefs(finaluri));
    NS_ENSURE_SUCCESS (rv, /* void */ );

    PRBool isEqual;
    rv = finaluri->Equals(mUri, &isEqual);
    NS_ENSURE_SUCCESS (rv, /* void */ );

    // Don't loop forever redirecting to ourselves. If the URIs are the same,
    // then just ignore the redirect message.
    if (isEqual)
      return;

    // Ok, we have a new uri, and we're ready to use it... 
    rv = SetUri(finaluri);
    NS_ENSURE_SUCCESS (rv, /* void */ );

    // Inform listeners that we've switched URI
    nsCOMPtr<nsIVariant> propVariant = sbNewVariant(finaluri).get();
    DispatchMediacoreEvent (sbIMediacoreEvent::URI_CHANGE, propVariant);

    // And finally, attempt to play it
    rv = Play();
    NS_ENSURE_SUCCESS (rv, /* void */ );
  }
}

void sbGStreamerMediacore::HandleEOSMessage(GstMessage *message)
{
  nsAutoMonitor lock(mMonitor);
  GstElement *pipeline = (GstElement *)g_object_ref (mPipeline);
  mTargetState = GST_STATE_NULL;
  lock.Exit();

  // Shut down the pipeline. This will cause us to send a STREAM_END
  // event when we get the state-changed message to GST_STATE_NULL
  gst_element_set_state (pipeline, GST_STATE_NULL);
  g_object_unref (pipeline);
}

nsresult sbGStreamerMediacore::LogMessageToErrorConsole(
        nsString message, PRUint32 flags)
{
  nsresult rv;

  nsCOMPtr<nsIConsoleService> consoleService = 
    do_GetService("@mozilla.org/consoleservice;1", &rv);
  NS_ENSURE_SUCCESS (rv, rv);

  nsCOMPtr<nsIScriptError> scriptError = 
      do_CreateInstance(NS_SCRIPTERROR_CONTRACTID);
  if (!scriptError) {
    return NS_ERROR_FAILURE;
  }

  rv = scriptError->Init(message.get(),
                         EmptyString().get(),
                         EmptyString().get(),
                         0, // No line number
                         0, // No column number
                         flags,
                         "Mediacore:GStreamer");
  NS_ENSURE_SUCCESS(rv,rv);

  rv = consoleService->LogMessage(scriptError);
  NS_ENSURE_SUCCESS(rv,rv);

  return NS_OK;
}

void sbGStreamerMediacore::HandleErrorMessage(GstMessage *message)
{
  GError *gerror = NULL;
  nsString errormessage;
  nsCOMPtr<sbMediacoreError> error;
  nsCOMPtr<sbIMediacoreEvent> event;
  gchar *debugMessage;
  nsresult rv;

  NS_ASSERTION(NS_IsMainThread(), "not on main thread");

  gst_message_parse_error(message, &gerror, &debugMessage);
  
  if (!mHasSeenError) {
    // Create and dispatch an error event. 
    NS_NEWXPCOM(error, sbMediacoreError);
    NS_ENSURE_TRUE(error, /* void */);

    CopyUTF8toUTF16(nsDependentCString(gerror->message), errormessage);
    error->Init(0, errormessage); // XXX: Use a proper error code once they exist

    DispatchMediacoreEvent(sbIMediacoreEvent::ERROR_EVENT, nsnull, error);
    mHasSeenError = PR_TRUE;
  }

  // Build an error message to output to the console 
  // TODO: This is currently not localised (but we're probably not setting
  // things up right to get translated gstreamer messages anyway).
  nsString errmessage = NS_LITERAL_STRING("GStreamer error: ");
  errmessage.Append(NS_ConvertUTF8toUTF16(gerror->message));
  errmessage.Append(NS_LITERAL_STRING(" Additional information: "));
  errmessage.Append(NS_ConvertUTF8toUTF16(debugMessage));

  g_error_free (gerror);
  g_free (debugMessage);

  // Then, shut down the pipeline, which will cause
  // a STREAM_END event to be fired.
  nsAutoMonitor lock(mMonitor);
  mTargetState = GST_STATE_NULL;
  GstElement *pipeline = (GstElement *)g_object_ref (mPipeline);
  lock.Exit();

  gst_element_set_state (pipeline, GST_STATE_NULL);
  g_object_unref (pipeline);

  // Log the error message
  rv = LogMessageToErrorConsole(errmessage, nsIScriptError::errorFlag);
  NS_ENSURE_SUCCESS(rv, /* void */);
}

void sbGStreamerMediacore::HandleWarningMessage(GstMessage *message)
{
  GError *gerror = NULL;
  gchar *debugMessage;
  nsresult rv;

  NS_ASSERTION(NS_IsMainThread(), "not on main thread");

  gst_message_parse_warning(message, &gerror, &debugMessage);

  // TODO: This is currently not localised (but we're probably not setting
  // things up right to get translated gstreamer messages anyway).
  nsString warning = NS_LITERAL_STRING("GStreamer warning: ");
  warning.Append(NS_ConvertUTF8toUTF16(gerror->message));
  warning.Append(NS_LITERAL_STRING(" Additional information: "));
  warning.Append(NS_ConvertUTF8toUTF16(debugMessage));

  g_error_free (gerror);
  g_free (debugMessage);

  rv = LogMessageToErrorConsole(warning, nsIScriptError::warningFlag);
  NS_ENSURE_SUCCESS(rv, /* void */);
}

/* Dispatch messages based on type.
 * For ELEMENT messages, further introspect the exact meaning for
 * dispatch
 */
void sbGStreamerMediacore::HandleMessage (GstMessage *message)
{
  GstMessageType msg_type;
  msg_type = GST_MESSAGE_TYPE(message);

  LOG(("Got message: %s", gst_message_type_get_name(msg_type)));

  switch (msg_type) {
    case GST_MESSAGE_STATE_CHANGED:
      HandleStateChangedMessage(message);
      break;
    case GST_MESSAGE_TAG:
      HandleTagMessage(message);
      break;
    case GST_MESSAGE_ERROR:
      HandleErrorMessage(message);
      break;
    case GST_MESSAGE_WARNING:
      HandleWarningMessage(message);
      break;
    case GST_MESSAGE_EOS:
      HandleEOSMessage(message);
      break;
    case GST_MESSAGE_BUFFERING:
      HandleBufferingMessage(message);
    case GST_MESSAGE_ELEMENT: {
      if (gst_structure_has_name (message->structure, "redirect")) {
        HandleRedirectMessage(message);
      }
      break;
    }
    default:
      LOG(("Got message: %s", gst_message_type_get_name(msg_type)));
      break;
  }
}

void
sbGStreamerMediacore::OnVideoCapsSet(GstCaps *caps)
{
  GstStructure *s;
  gint pixelAspectRatioN, pixelAspectRatioD;
  gint videoWidth, videoHeight;

  s = gst_caps_get_structure(caps, 0);
  if(s) {
    gst_structure_get_int(s, "width", &videoWidth);
    gst_structure_get_int(s, "height", &videoHeight);

    /* pixel-aspect-ratio is optional */
    const GValue* par = gst_structure_get_value(s, "pixel-aspect-ratio");
    if (par) {
      pixelAspectRatioN = gst_value_get_fraction_numerator(par);
      pixelAspectRatioD = gst_value_get_fraction_denominator(par);
    }
    else {
      /* PAR not set; default to square pixels */
      pixelAspectRatioN = pixelAspectRatioD = 1;
    }

    if (mPlatformInterface) {
      int num = videoWidth * pixelAspectRatioN;
      int denom = videoHeight * pixelAspectRatioD;
      mPlatformInterface->SetDisplayAspectRatio(num, denom);
    }
  }
}

//-----------------------------------------------------------------------------
// sbBaseMediacore
//-----------------------------------------------------------------------------

/*virtual*/ nsresult 
sbGStreamerMediacore::OnInitBaseMediacore()
{
  nsresult rv;

  // Ensure the service component is loaded; it initialises GStreamer for us.
  nsCOMPtr<sbIGStreamerService> service =
    do_GetService(SBGSTREAMERSERVICE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

/*virtual*/ nsresult 
sbGStreamerMediacore::OnGetCapabilities()
{
  // XXXAus: Implement this when implementing the default sequencer!
  return NS_ERROR_NOT_IMPLEMENTED;
}

/*virtual*/ nsresult 
sbGStreamerMediacore::OnShutdown()
{
  nsAutoMonitor lock(mMonitor);

  if (mPipeline) {
    LOG (("Destroying pipeline on shutdown"));
    DestroyPipeline();
  }

  if (mPrefs) {
    nsresult rv = mPrefs->RemoveObserver("songbird.mediacore.gstreamer", this);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

//-----------------------------------------------------------------------------
// sbBaseMediacorePlaybackControl
//-----------------------------------------------------------------------------

/*virtual*/ nsresult 
sbGStreamerMediacore::OnInitBaseMediacorePlaybackControl()
{
  /* Need to create the platform interface stuff here once that's updated. */

  return NS_OK;
}

/*virtual*/ nsresult 
sbGStreamerMediacore::OnSetUri(nsIURI *aURI)
{
  nsCAutoString spec;
  nsresult rv;
  nsAutoMonitor lock(mMonitor);

  rv = CreatePlaybackPipeline();
  NS_ENSURE_SUCCESS (rv,rv);

  rv = aURI->GetSpec(spec);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = GetFileSize (aURI, &mResourceSize);
  if (rv == NS_ERROR_NO_INTERFACE) {
    // Not being a file is fine - that's just something non-local
    mResourceIsLocal = PR_FALSE;
    mResourceSize = -1;
  }
  else
    mResourceIsLocal = PR_TRUE;

  LOG(("Setting URI to \"%s\"", spec.get()));

  /* Set the URI to play */
  g_object_set (G_OBJECT (mPipeline), "uri", spec.get(), NULL);

  return NS_OK;
}

/*virtual*/ nsresult 
sbGStreamerMediacore::OnGetDuration(PRUint64 *aDuration)
{
  GstQuery *query;
  gboolean res;
  nsresult rv;
  nsAutoMonitor lock(mMonitor);

  if (!mPipeline)
    return NS_ERROR_NOT_AVAILABLE;

  query = gst_query_new_duration(GST_FORMAT_TIME);
  res = gst_element_query(mPipeline, query);

  if(res) {
    gint64 duration;
    gst_query_parse_duration(query, NULL, &duration);

    if (duration == GST_CLOCK_TIME_NONE) {
      /* Something erroneously returned TRUE for this query
       * despite not giving us a duration. Treat the same as
       * a failed query */
      rv = NS_ERROR_NOT_AVAILABLE;
    }
    else {
      /* Convert to milliseconds */
      *aDuration = duration / GST_MSECOND;
      rv = NS_OK;
    }
  }
  else
    rv = NS_ERROR_NOT_AVAILABLE;

  gst_query_unref (query);

  return rv;
}

/*virtual*/ nsresult
sbGStreamerMediacore::OnGetPosition(PRUint64 *aPosition)
{
  GstQuery *query;
  gboolean res;
  nsresult rv;
  nsAutoMonitor lock(mMonitor);

  if (!mPipeline)
    return NS_ERROR_NOT_AVAILABLE;

  query = gst_query_new_position(GST_FORMAT_TIME);
  res = gst_element_query(mPipeline, query);

  if(res) {
    gint64 position;
    gst_query_parse_position(query, NULL, &position);

    if (position == 0 || position == GST_CLOCK_TIME_NONE) {
      // GStreamer bugs can cause us to get a position of zero when we in fact
      // don't know the current position. A real position of zero is unlikely
      // and transient, so we just treat this as unknown.
      // A value of -1 (GST_CLOCK_TIME_NONE) is also 'not available', though 
      // really that should be reported as a a false return from the query.
      rv = NS_ERROR_NOT_AVAILABLE;
    }
    else {
      /* Convert to milliseconds */
      *aPosition = position / GST_MSECOND;
      rv = NS_OK;
    }
  }
  else
    rv = NS_ERROR_NOT_AVAILABLE;

  gst_query_unref (query);

  return rv;
}

/*virtual*/ nsresult 
sbGStreamerMediacore::OnSetPosition(PRUint64 aPosition)
{
  GstClockTime position;
  gboolean ret;
  GstSeekFlags flags;

  nsAutoMonitor lock(mMonitor);

  // Incoming position is in milliseconds, convert to GstClockTime (nanoseconds)
  position = aPosition * GST_MSECOND;

  // For gapless to work after seeking in MP3, we need to do accurate seeking.
  // However, accurate seeks on large files is very slow.
  // So, we do a KEY_UNIT seek (the fastest sort) unless it's all three of:
  //  - a local file
  //  - sufficiently small
  
  if (mResourceIsLocal &&
      mResourceSize <= MAX_FILE_SIZE_FOR_ACCURATE_SEEK) 
  {
    flags = (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE);
  }
  else {
    flags = (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT);
  }

  ret = gst_element_seek_simple (mPipeline, GST_FORMAT_TIME, 
      flags, position);
  
  if (!ret) {
    /* TODO: Is this appropriate for a non-fatal failure to seek? Should we
       fire an event? */
    return NS_ERROR_FAILURE;
  }


  // Set state to PAUSED. Leave mTargetState as it is, so if we did a seek when
  // playing, we'll return to playing (after rebuffering if required)
  ret = gst_element_set_state (mPipeline, GST_STATE_PAUSED);

  if (ret == GST_STATE_CHANGE_FAILURE)
    return NS_ERROR_FAILURE;

  nsresult rv = SendInitialBufferingEvent();
  NS_ENSURE_SUCCESS (rv, rv);

  return rv;
}

/*virtual*/ nsresult 
sbGStreamerMediacore::OnPlay()
{
  GstStateChangeReturn ret;
  GstState curstate;

  nsAutoMonitor lock(mMonitor);
  NS_ENSURE_STATE(mPipeline);

  gst_element_get_state (mPipeline, &curstate, NULL, 0);

  mTargetState = GST_STATE_PLAYING;

  if (curstate == GST_STATE_PAUSED && !mBuffering) {
    // If we're already paused, then go directly to PLAYING, unless
    // we're still waiting for buffering to complete.
    ret = gst_element_set_state (mPipeline, GST_STATE_PLAYING);
  }
  else {
    // Otherwise, we change our state to PAUSED (our target state is
    // PLAYING, though). Then, when we reach PAUSED, we'll either
    // continue on to PLAYING, or (if we're buffering) wait for buffering
    // to complete.
    ret = gst_element_set_state (mPipeline, GST_STATE_PAUSED);

    nsresult rv = SendInitialBufferingEvent();
    NS_ENSURE_SUCCESS (rv, rv);
  }

  /* Usually ret will be GST_STATE_CHANGE_ASYNC, but we could get a synchronous
   * error... */
  if (ret == GST_STATE_CHANGE_FAILURE)
    return NS_ERROR_FAILURE;
  else if (ret == GST_STATE_CHANGE_NO_PREROLL)
  {
    /* NO_PREROLL means we have a live pipeline, for which we have to 
     * handle buffering differently */
    mIsLive = PR_TRUE;
  }

  return NS_OK;
}

nsresult
sbGStreamerMediacore::SendInitialBufferingEvent()
{
  nsAutoMonitor lock(mMonitor);

  // If we're starting an HTTP stream, send an immediate buffering event,
  // since GStreamer won't do that until it's connected to the server.
  PRBool schemeIsHttp;
  nsresult rv = mUri->SchemeIs("http", &schemeIsHttp);
  NS_ENSURE_SUCCESS (rv, rv);

  if (schemeIsHttp) {
    double bufferingProgress = 0.0;
    nsCOMPtr<nsIVariant> variant = sbNewVariant(bufferingProgress).get();
    DispatchMediacoreEvent(sbIMediacoreEvent::BUFFERING, variant);
  }

  return NS_OK;
}

/*virtual*/ nsresult 
sbGStreamerMediacore::OnPause()
{
  GstStateChangeReturn ret;

  nsAutoMonitor lock(mMonitor);

  NS_ENSURE_STATE(mPipeline);

  mTargetState = GST_STATE_PAUSED;
  ret = gst_element_set_state (mPipeline, GST_STATE_PAUSED);

  if (ret == GST_STATE_CHANGE_FAILURE)
    return NS_ERROR_FAILURE;
  return NS_OK;
}

/*virtual*/ nsresult 
sbGStreamerMediacore::OnStop()
{
  nsAutoMonitor lock(mMonitor);
  mTargetState = GST_STATE_NULL;
  mStopped = PR_TRUE;
  // If we get stopped without ever starting, that's ok... 
  if (!mPipeline)
    return NS_OK;

  GstElement *pipeline = (GstElement *)g_object_ref (mPipeline);
  lock.Exit();

  gst_element_set_state (pipeline, GST_STATE_NULL);
  g_object_unref (pipeline);


  return NS_OK;
}

//-----------------------------------------------------------------------------
// sbBaseMediacoreVolumeControl
//-----------------------------------------------------------------------------

/*virtual*/ nsresult 
sbGStreamerMediacore::OnInitBaseMediacoreVolumeControl()
{
  mVolume = 1.0;
  mMute = PR_FALSE;

  return NS_OK;
}

/*virtual*/ nsresult 
sbGStreamerMediacore::OnSetMute(PRBool aMute)
{
  nsAutoMonitor lock(mMonitor);

  NS_ENSURE_STATE(mPipeline);

  if(!aMute && mMute) {
    nsAutoMonitor mon(sbBaseMediacoreVolumeControl::mMonitor);

    /* Well, this is nice and easy! */
    g_object_set(mPipeline, "volume", mVolume, NULL);
  }
  else if(aMute && !mMute){
    /* We have no explicit mute control, so just set the volume to zero, but
    * don't update our internal mVolume value */
    g_object_set(mPipeline, "volume", 0.0, NULL);
  }

  return NS_OK;
}

/*virtual*/ nsresult 
sbGStreamerMediacore::OnSetVolume(double aVolume)
{
  nsAutoMonitor lock(mMonitor);

  NS_ENSURE_STATE(mPipeline);

  /* Well, this is nice and easy! */
  g_object_set(mPipeline, "volume", aVolume, NULL);

  return NS_OK;
}

//-----------------------------------------------------------------------------
// sbIMediacoreVotingParticipant
//-----------------------------------------------------------------------------

NS_IMETHODIMP 
sbGStreamerMediacore::VoteWithURI(nsIURI *aURI, PRUint32 *_retval)
{
  NS_ENSURE_ARG_POINTER(aURI);
  NS_ENSURE_ARG_POINTER(_retval);

  // XXXAus: Run aURI through extension filtering first.
  //
  //         After that, that's as much as we can do, it's most likely
  //         playable.
  
  *_retval = 2000;

  return NS_OK;
}

NS_IMETHODIMP 
sbGStreamerMediacore::VoteWithChannel(nsIChannel *aChannel, PRUint32 *_retval)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

//-----------------------------------------------------------------------------
// sbIMediacoreVideoWindow
//-----------------------------------------------------------------------------

NS_IMETHODIMP
sbGStreamerMediacore::GetFullscreen(PRBool *aFullscreen)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
sbGStreamerMediacore::SetFullscreen(PRBool aFullscreen)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
sbGStreamerMediacore::GetVideoWindow(nsIDOMXULElement **aVideoWindow)
{
  nsAutoMonitor mon(mMonitor);
  NS_IF_ADDREF(*aVideoWindow = mVideoWindow);

  return NS_OK;
}

NS_IMETHODIMP
sbGStreamerMediacore::SetVideoWindow(nsIDOMXULElement *aVideoWindow)
{
  NS_ENSURE_ARG_POINTER(aVideoWindow);
  
  nsAutoMonitor mon(mMonitor);

  // Get the box object representing the actual display area for the video.
  nsCOMPtr<nsIBoxObject> boxObject;
  nsresult rv = aVideoWindow->GetBoxObject(getter_AddRefs(boxObject));
  NS_ENSURE_SUCCESS (rv, rv);

  nsCOMPtr<nsIDOMDocument> domDocument;
  rv = aVideoWindow->GetOwnerDocument(getter_AddRefs(domDocument));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIDOMDocumentView> domDocumentView(do_QueryInterface(domDocument));
  NS_ENSURE_TRUE(domDocumentView, NS_NOINTERFACE);

  nsCOMPtr<nsIDOMAbstractView> domAbstractView;
  rv = domDocumentView->GetDefaultView(getter_AddRefs(domAbstractView));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIWebNavigation> webNavigation(do_GetInterface(domAbstractView));
  nsCOMPtr<nsIDocShellTreeItem> docShellTreeItem(do_QueryInterface(webNavigation));
  NS_ENSURE_TRUE(docShellTreeItem, NS_NOINTERFACE);

  nsCOMPtr<nsIDocShellTreeOwner> docShellTreeOwner;
  rv = docShellTreeItem->GetTreeOwner(getter_AddRefs(docShellTreeOwner));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIBaseWindow> baseWindow = do_QueryInterface(docShellTreeOwner);
  NS_ENSURE_TRUE(baseWindow, NS_NOINTERFACE);

  nsCOMPtr<nsIWidget> widget;
  rv = baseWindow->GetMainWidget(getter_AddRefs(widget));
  NS_ENSURE_SUCCESS(rv, rv);

  // Attach event listeners
  nsCOMPtr<nsIDocument> document(do_QueryInterface(domDocument));
  NS_ENSURE_TRUE(document, NS_NOINTERFACE);

  mDOMWindow = do_QueryInterface(document->GetScriptGlobalObject());
  NS_ENSURE_TRUE(mDOMWindow, NS_NOINTERFACE);

  nsCOMPtr<nsIDOMEventTarget> target(do_QueryInterface(mDOMWindow));
  NS_ENSURE_TRUE(target, NS_NOINTERFACE);
  target->AddEventListener(NS_LITERAL_STRING("resize"), this, PR_FALSE);
  target->AddEventListener(NS_LITERAL_STRING("unload"), this, PR_FALSE);
  target->AddEventListener(NS_LITERAL_STRING("hide"), this, PR_FALSE);

  nsCOMPtr<nsIThread> eventTarget;
  rv = NS_GetMainThread(getter_AddRefs(eventTarget));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIBoxObject> proxiedBoxObject;
  rv = do_GetProxyForObject(eventTarget,
                            NS_GET_IID(nsIBoxObject),
                            boxObject,
                            NS_PROXY_SYNC | NS_PROXY_ALWAYS,
                            getter_AddRefs(proxiedBoxObject));

  mHaveVideoWindow = PR_TRUE;
  mVideoWindow = aVideoWindow;

#if defined (MOZ_WIDGET_GTK2)
  GdkWindow *native = GDK_WINDOW(widget->GetNativeData(NS_NATIVE_WIDGET));
  LOG(("Found native window %x", native));
  mPlatformInterface = new GDKPlatformInterface(proxiedBoxObject, native);
#elif defined (XP_WIN)
  HWND native = (HWND)widget->GetNativeData(NS_NATIVE_WIDGET);
  LOG(("Found native window %x", native));
  mPlatformInterface = new Win32PlatformInterface(proxiedBoxObject, native);
#elif defined (XP_MACOSX)
  void * native = (void *)widget->GetNativeData(NS_NATIVE_WIDGET);
  LOG(("Found native window %x", native));
  mPlatformInterface = new OSXPlatformInterface(proxiedBoxObject, native);
#else
  LOG(("No video backend available for this platform"));
  mHaveVideoWindow = PR_FALSE;
#endif

  return NS_OK;
}

//-----------------------------------------------------------------------------
// sbIGStreamerMediacore
//-----------------------------------------------------------------------------

NS_IMETHODIMP
sbGStreamerMediacore::GetGstreamerVersion(nsAString& aGStreamerVersion)
{
  nsString versionString;

  versionString.AppendInt(GST_VERSION_MAJOR);
  versionString.AppendLiteral(".");
  versionString.AppendInt(GST_VERSION_MINOR);
  versionString.AppendLiteral(".");
  versionString.AppendInt(GST_VERSION_MICRO);
  
  aGStreamerVersion.Assign(versionString);

  return NS_OK;
}

// Forwarding functions for sbIMediacoreEventTarget interface

NS_IMETHODIMP
sbGStreamerMediacore::DispatchEvent(sbIMediacoreEvent *aEvent,
                                    PRBool aAsync,
                                    PRBool* _retval)
{
  return mBaseEventTarget ? 
         mBaseEventTarget->DispatchEvent(aEvent, aAsync, _retval) : 
         NS_ERROR_NULL_POINTER;
}

NS_IMETHODIMP
sbGStreamerMediacore::AddListener(sbIMediacoreEventListener *aListener)
{
  return mBaseEventTarget ? 
         mBaseEventTarget->AddListener(aListener) : 
         NS_ERROR_NULL_POINTER;
}

NS_IMETHODIMP
sbGStreamerMediacore::RemoveListener(sbIMediacoreEventListener *aListener)
{
  return mBaseEventTarget ? 
         mBaseEventTarget->RemoveListener(aListener) : 
         NS_ERROR_NULL_POINTER;
}


//-----------------------------------------------------------------------------
// nsIDOMEventListener
//-----------------------------------------------------------------------------

NS_IMETHODIMP
sbGStreamerMediacore::HandleEvent(nsIDOMEvent* aEvent)
{
  nsAutoString eventType;
  aEvent->GetType(eventType);

  if(eventType.EqualsLiteral("unload") ||
     eventType.EqualsLiteral("hide")) {

    // Clean up here
    nsCOMPtr<nsIDOMEventTarget> target(do_QueryInterface(mDOMWindow));
    NS_ENSURE_TRUE(target, NS_NOINTERFACE);
    target->RemoveEventListener(NS_LITERAL_STRING("resize"), this, PR_FALSE);
    target->RemoveEventListener(NS_LITERAL_STRING("unload"), this, PR_FALSE);
    target->RemoveEventListener(NS_LITERAL_STRING("hide"), this, PR_FALSE);

    mDOMWindow = nsnull;
  }
  else if(eventType.EqualsLiteral("resize") &&
          mPlatformInterface) {
    mPlatformInterface->ResizeToWindow();  
  }

  return NS_OK;
}


//-----------------------------------------------------------------------------
// nsIObserver
//-----------------------------------------------------------------------------

NS_IMETHODIMP
sbGStreamerMediacore::Observe(nsISupports *aSubject,
                              const char *aTopic,
                              const PRUnichar *aData)
{
  if (!strcmp(NS_PREFBRANCH_PREFCHANGE_TOPIC_ID, aTopic)) {
    nsresult rv = ReadPreferences();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

//-----------------------------------------------------------------------------
// sbIGstAudioFilter
//-----------------------------------------------------------------------------
nsresult
sbGStreamerMediacore::AddAudioFilter(GstElement *aElement)
{
  // Hold a reference to the element
  gst_object_ref (aElement);

  mAudioFilters.push_back(aElement);

  return NS_OK;
}

nsresult
sbGStreamerMediacore::RemoveAudioFilter(GstElement *aElement)
{
  mAudioFilters.erase(
          std::remove(mAudioFilters.begin(), mAudioFilters.end(), aElement));

  gst_object_unref (aElement);

  return NS_OK;
}



