/* vim: set sw=2 :miv */
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

#ifndef __SBBASEDEVICE__H__
#define __SBBASEDEVICE__H__

#include <deque>
#include <map>

#include "sbIDevice.h"
#include "sbBaseDeviceEventTarget.h"
#include "sbDeviceLibrary.h"

#include <nsAutoPtr.h>
#include <nsCOMPtr.h>
#include <nsCOMArray.h>
#include <nsDataHashtable.h>
#include <nsISupportsImpl.h>
#include <prlock.h>

#include <sbILibraryChangeset.h>
#include "sbIMediaItem.h"
#include "sbIMediaList.h"
#include "sbDeviceStatistics.h"

class nsITimer;
class nsIPrefBranch;

class sbBaseDeviceLibraryListener;
class sbDeviceBaseLibraryCopyListener;
class sbBaseDeviceMediaListListener;
class sbIDeviceLibrary;

/* Property used to force a sync diff. */
#define DEVICE_PROPERTY_SYNC_FORCE_DIFF \
          "http://songbirdnest.com/device/1.0#forceDiff"

/**
 * Base class for implementing a device
 *
 * Derived classes should
 *  - QI to sbIDevice, sbIDeviceEventTarget
 *  - call sbBaseDevice::Init() at some early point
 *  - Set the hidden property to false on the library when done mounting.
 *  - Set the hidden property on all medialists in the library when
 *    done mounting.
 */
class NS_HIDDEN sbBaseDevice : public sbIDevice,
                               public sbBaseDeviceEventTarget
{
public:
  struct TransferRequest : public nsISupports {
    /* types of requests. not all types necessarily apply to all types of
     * devices, and devices may have custom types.
     * Request types that should result in delaying shutdown (e.g. writing to
     * the device) should have the write flag set.
     * The specific device may internally define more request types.
     */

    static const int REQUEST_FLAG_USER  = 0x80000000;
    static const int REQUEST_FLAG_WRITE = 0x40000000;

    /* note that type 0 is reserved */
    enum {
      /* read requests */
      REQUEST_MOUNT         = sbIDevice::REQUEST_MOUNT,
      REQUEST_READ          = sbIDevice::REQUEST_READ,
      REQUEST_EJECT         = sbIDevice::REQUEST_EJECT,
      REQUEST_SUSPEND       = sbIDevice::REQUEST_SUSPEND,
      
      /* not in sbIDevice, internal use */
      REQUEST_BATCH_BEGIN,
      REQUEST_BATCH_END,
      
      /* write requests */
      REQUEST_WRITE         = sbIDevice::REQUEST_WRITE,
      REQUEST_DELETE        = sbIDevice::REQUEST_DELETE,
      REQUEST_SYNC          = sbIDevice::REQUEST_SYNC,
      /* delete all files */
      REQUEST_WIPE          = sbIDevice::REQUEST_WIPE,
      /* move an item in one playlist */
      REQUEST_MOVE          = sbIDevice::REQUEST_MOVE,
      REQUEST_UPDATE        = sbIDevice::REQUEST_UPDATE,
      REQUEST_NEW_PLAYLIST  = sbIDevice::REQUEST_NEW_PLAYLIST
    };
    
    int type;                        /* one of the REQUEST_* constants,
                                          or a custom type */
    nsCOMPtr<sbIMediaItem> item;     /* the item this request pertains to */
    nsCOMPtr<sbIMediaList> list;     /* the list this request is to act on */
    nsCOMPtr<nsISupports>  data;     /* optional data that may or may not be used by a type of request */
    PRUint32 index;                  /* the index in the list for this action */
    PRUint32 otherIndex;             /* any secondary index needed */
    PRUint32 batchCount;             /* the number of items in this batch
                                          (batch = run of requests of the same
                                          type) */
    PRUint32 batchIndex;             /* index of item in the batch to process */

    PRUint32 itemTransferID;         /* id for this item transfer */
    
    static const PRInt32 PRIORITY_DEFAULT = 0x1000000;
    PRInt32 priority;                /* priority for the request (lower first) */

    NS_DECL_ISUPPORTS
    /**
     * Returns PR_TRUE if the request is for a playlist and PR_FALSE otherwise
     */
    PRBool IsPlaylist() const;
    /**
     * Returns PR_TRUE if the request should be counted as part of he batch
     * otherwise returns PR_FALSE
     */
    PRBool IsCountable() const;
    static TransferRequest * New();

    /* Don't allow manual construction/destruction, but allow sub-classing. */
  protected:
    TransferRequest() {}
    ~TransferRequest(){} /* we're refcounted, no manual deleting! */
  };
  
public:
  /* selected methods from sbIDevice */
  NS_IMETHOD GetPreference(const nsAString & aPrefName, nsIVariant **_retval);
  NS_IMETHOD SetPreference(const nsAString & aPrefName, nsIVariant *aPrefValue);
  NS_IMETHOD GetIsBusy(PRBool *aIsBusy);
  NS_IMETHOD GetCanDisconnect(PRBool *aCanDisconnect);
  NS_IMETHOD GetState(PRUint32 *aState);
  NS_IMETHOD SyncLibraries(void);
  
public:
  sbBaseDevice();
  ~sbBaseDevice();

  /* add a transfer/action request to the request queue */
  nsresult  PushRequest(const int aType,
                        sbIMediaItem* aItem = nsnull,
                        sbIMediaList* aList = nsnull,
                        PRUint32 aIndex = PR_UINT32_MAX,
                        PRUint32 aOtherIndex = PR_UINT32_MAX,
                        PRInt32 aPriority = TransferRequest::PRIORITY_DEFAULT);

  virtual nsresult PushRequest(TransferRequest *aRequest);

  /* remove the next request to be processed; note that _retval will be null
     while returning NS_OK if there are no requests left */
  nsresult PopRequest(TransferRequest** _retval);

  /* get a reference to the next request without removing it; note that _retval
   * will be null while returning NS_OK if there are no requests left
   * once a request has been peeked, it will be returned for further PeekRequest
   * and PopRequest calls until after it has been popped.
   */
  nsresult PeekRequest(TransferRequest** _retval);
  
  /* remove a given request from the transfer queue
     note: returns NS_OK on sucessful removal,
           and NS_SUCCESS_LOSS_OF_INSIGNIFICANT_DATA if no match was found.
     if the request is not unique, removes the first matching one
     (note that it is not possible to filter by priority) */
  nsresult RemoveRequest(const int aType,
                         sbIMediaItem* aItem = nsnull,
                         sbIMediaList* aList = nsnull);
  
  /* clear the request queue */
  nsresult ClearRequests(const nsAString &aDeviceID);

  /**
   * Internally set preference specified by aPrefName to the value specified by
   * aPrefValue.  If the new preference value is different than that old, return
   * true in aHasChanged unless aHasChanged is null.
   * This method may be used for loading preferences from device storage.  It
   * will not send preference changed events.
   *
   * @param aPrefName Name of the preference to set.
   * @param aPrefValue Value to set preference.
   * @param aHasChanged True if preference value changed.
   */

  nsresult SetPreferenceInternal(const nsAString& aPrefName,
                                 nsIVariant*      aPrefValue,
                                 PRBool*          aHasChanged);

  /* A request has been added, process the request
     (or schedule it to be processed) */
  virtual nsresult ProcessRequest() = 0;

  /**
   * Set the device state
   * @param aState new device state
   */
  nsresult SetState(PRUint32 aState);
  
  /**
   * Create a local database library for the device.  The library must be
   * finalized by calling FinalizeDeviceLibrary.
   * @param aId the ID for the library
   * @param aLibraryLocation the file to name the library, or null to use some default
   * @return the library created (or re-used if it exists)
   */
  nsresult CreateDeviceLibrary(const nsAString& aId,
                               nsIURI* aLibraryLocation,
                               sbIDeviceLibrary** _retval);

  /**
   * Initialize a library for the device.  The library must be finalized by
   * calling FinalizeDeviceLibrary.
   * @param aDevLib the device library to initialize.
   * @param aId the ID for the library
   * @param aLibraryLocation the file to name the library, or null to use some default
   */
  nsresult InitializeDeviceLibrary(sbDeviceLibrary* aDevLib,
                                   const nsAString& aId,
                                   nsIURI*          aLibraryLocation);

  /**
   * Finalize a library for the device
   * @param aDevLib the device library to finalize.
   */
  void FinalizeDeviceLibrary(sbIDeviceLibrary* aDevLib);

  /**
   * Called when a media list has been added to the device library
   * @param aList the media list to listen for modifications
   */
  nsresult ListenToList(sbIMediaList* aList);

  typedef struct {
    sbBaseDevice*        device;
    nsCOMPtr<sbILibrary> library;
  } EnumerateFinalizeMediaListListenersInfo;

  static NS_HIDDEN_(PLDHashOperator)
           EnumerateFinalizeMediaListListeners
            (nsISupports* aKey,
             nsRefPtr<sbBaseDeviceMediaListListener>& aData,
             void* aClosure);

  static NS_HIDDEN_(PLDHashOperator) EnumerateIgnoreMediaListListeners(nsISupports* aKey,
                                                                       nsRefPtr<sbBaseDeviceMediaListListener> aData,
                                                                       void* aClosure);

  /**
  * Set the ignore flag on all media list listeners registered for
  * the library for this device.
  * \param aIgnoreListener Ignore flag value.
  */
  nsresult SetIgnoreMediaListListeners(PRBool aIgnoreListener);

  /**
   * Set the ignore flag on all library listeners registered for the
   * library for this device
   * \param aIgnoreListener Ignore flag value.
   */
  nsresult SetIgnoreLibraryListener(PRBool aIgnoreListener);

  /**
   * Set all media lists in the library hidden. This is useful
   * for hiding the lists during mounting operations.
   * \param aLibrary The library containing the media lists you wish to hide.
   * \param aHidden True to hide, false to show.
   */
  nsresult SetMediaListsHidden(sbIMediaList *aLibrary, PRBool aHidden);

  /**
   * Ignores events for the given media item
   */
  nsresult IgnoreMediaItem(sbIMediaItem * aItem);
  
  /**
   * Restores listening to events for the given item
   */
  nsresult UnignoreMediaItem(sbIMediaItem * aItem);

  /**
   * Delete an item from the library and suppress notifications during
   * delete. This is to avoid having the listeners watching the library
   * attempt to double delete an item.
   */
  nsresult DeleteItem(sbIMediaList *aLibrary, sbIMediaItem *aItem);

  /**
   * Return our statistics collector
   */
  sbDeviceStatistics & DeviceStatistics()
  {
    return mDeviceStatistics;
  }

  nsresult CreateTransferRequest(PRUint32 aRequest, 
                                 nsIPropertyBag2 *aRequestParameters,
                                 TransferRequest **aTransferRequest);

  /**
   * Create an event for the device and dispatch it
   * @param aType type of event
   * @param aData event data
   */
  nsresult CreateAndDispatchEvent(PRUint32 aType,
                                  nsIVariant *aData,
                                  PRBool aAsync = PR_TRUE);

  /**
   * Returns true if the current request should be aborted. This subsequently
   * resets the flags
   */
  PRBool IsRequestAborted()
  {
    NS_ASSERTION(mRequestMonitor, "mRequestMonitor must be initialized");
    nsAutoMonitor mon(mRequestMonitor);
    PRBool const abort = mAbortCurrentRequest;
    mAbortCurrentRequest = PR_FALSE;
    return abort;
  }

  NS_SCRIPTABLE NS_IMETHOD SetWarningDialogEnabled(const nsAString & aWarning, PRBool aEnabled);
  NS_SCRIPTABLE NS_IMETHOD GetWarningDialogEnabled(const nsAString & aWarning, PRBool *_retval);
  NS_SCRIPTABLE NS_IMETHOD ResetWarningDialogs(void);

protected:
  friend class sbBaseDeviceInitHelper;
  /**
   * Base class initialization this will call the InitDevice first then
   * do the intialization needed by the sbDeviceBase
   */
  nsresult Init();
  /**
   * This allows the derived classes to be initialized if needed
   */
  virtual nsresult InitDevice() { return NS_OK; }
private:
  /**
   * Helper for PopRequest / PeekRequest
   */
  nsresult GetFirstRequest(PRBool aRemove, TransferRequest** _retval);

protected:
  /* to block the background thread from transferring files while the batch
     has not completed; if a delay is active, either the request monitor OR
     the request removal lock needs to be held.  No requests may be removed
     while the removal lock is behind held.  No actions (including pushing
     additional requests) may be performed while the request monitor is held. */
  PRMonitor * mRequestMonitor;
  PRLock * mRequestRemovalLock;
  nsRefPtr<TransferRequest> mRequestBatchStart;
  nsCOMPtr<nsITimer> mRequestBatchTimer;

  typedef std::deque<nsRefPtr<TransferRequest> > TransferRequestQueue;
  typedef std::map<PRInt32, TransferRequestQueue> TransferRequestQueueMap;
  TransferRequestQueueMap mRequests;
  PRUint32 mLastTransferID;
  PRInt32 mLastRequestPriority; // to make sure peek returns the same
  PRLock *mStateLock;
  PRUint32 mState;
  sbDeviceStatistics mDeviceStatistics;
  PRBool mAbortCurrentRequest;
  PRInt32 mIgnoreMediaListCount; // Allows us to know if we're ignoring lists
  
  nsRefPtr<sbBaseDeviceLibraryListener> mLibraryListener;
  nsRefPtr<sbDeviceBaseLibraryCopyListener> mLibraryCopyListener;
  nsDataHashtableMT<nsISupportsHashKey, nsRefPtr<sbBaseDeviceMediaListListener> > mMediaListListeners;
  nsCOMArray<nsISupports> mPlaylistSyncListeners;

protected:
 /**
   * Go through the given queue to make sure that there is enough free space
   * to complete the write requests.  If not, and the user agrees, attempt to
   * transfer a subset.
   *
   * \param aQueue the queue to check size for
   * \param aRequestsRemoved [out, optional] true if at least one request had
   *                         to be removed
   * \param aItemsToCopy [out, optional] the items that will be copied
   *                     note this won't be set if all items will be copied or
   *                     if the device library is in manual mode
   */
  virtual nsresult EnsureSpaceForWrite(TransferRequestQueue& aQueue,
                                       PRBool * aRequetsRemoved = nsnull,
                                       nsIArray** aItemsToWrite = nsnull);

  /* get a prefbranch for this device */
  nsresult GetPrefBranch(nsIPrefBranch** aPrefBranch);

  /**
   * Check if device is linked to the local sync partner.  If it is, return true
   * in aIsLinkedLocally; otherwise, return false.  If aRequestPartnerChange is
   * true and the device is not linked locally, make a request to the user to
   * change the device sync partner to the local sync partner.
   *
   * \param aRequestPartnerChange   Request that the sync partner be changed to
   *                                the local sync partner.
   * \param aIsLinkedLocally        Returned true if the device is linked to the
   *                                local sync partner.
   */
  nsresult SyncCheckLinkedPartner(PRBool  aRequestPartnerChange,
                                  PRBool* aIsLinkedLocally);

  /**
   * Make a request to the user to change the device sync partner to the local
   * sync partner.  If the user grants the request, return true in
   * aPartnerChangeGranted; otherwise, return true.
   *
   * \param aPartnerChangeGranted   Returned true if the user granted the
   *                                request.
   */
  nsresult SyncRequestPartnerChange(PRBool* aPartnerChangeGranted);

  /**
   * Handle the sync request specified by aRequest.
   *
   * \param aRequest              Request data record.
   */
  nsresult HandleSyncRequest(TransferRequest* aRequest);

  /**
   * Produce the sync change set for the sync request specified by aRequest and
   * return the change set in aChangeset.
   *
   * \param aRequest              Sync request data record.
   * \param aChangeset            Sync request change set.
   */
  nsresult SyncProduceChangeset(TransferRequest*      aRequest,
                                sbILibraryChangeset** aChangeset);

  /**
   * Apply the sync change set specified by aChangeset to the media list
   * specified by aDstMediaList.
   *
   * \param aDstMediaList         Media list to which to apply sync change set.
   * \param aChangeset            Set of sync changes.
   */
  nsresult SyncApplyChanges(sbIMediaList*        aDstMediaList,
                            sbILibraryChangeset* aChangeset);

  /**
   * Synchronize all the media lists in the list of media list changes specified
   * by aMediaListChangeList.
   *
   * \param aMediaListChangeList  List of media list changes.
   */
  nsresult SyncMediaLists(nsCOMArray<sbILibraryChange>& aMediaListChangeList);

  /**
   * Update the properties of a media item as specified by the sync change
   * specified by aChange.
   *
   * \param aChange               Changes to make to media item.
   */
  nsresult SyncUpdateProperties(sbILibraryChange* aChange);

  /**
   * Set up all media lists within the media list specified by aMediaList to
   * trigger a difference when syncing.  The media list aMediaList is not set up
   * to force a difference.
   *
   * This function triggers a difference by setting a property that should not
   * be set on any source media lists.
   *
   * \param aMediaList            Media list containing media lists to force
   *                              differences.
   */
  nsresult SyncForceDiffMediaLists(sbIMediaList* aMediaList);

  /**
   * Determine if we should listen to a playlist
   * \param aMainList this is the playlist on the main library
   * \param aDeviceLibrary The device library
   * \param aShoudListen out parameter that denotes whether the
   *        playlist should be listened to
   */
  nsresult ShouldListenToPlaylist(sbIMediaList * aMainList,
                                  sbIDeviceLibrary * aDeviceLibrary,
                                  PRBool & aShoudlListen);

  /**
   * This function listens to the playlist aMainMediaList
   * \param aMainmediaList The media list to listen to
   * \param aDeviceLibrary the device library
   */
  nsresult ListenToPlaylist(sbIMediaList * aMainMediaList,
                            sbIDeviceLibrary * aDeviceLibrary);

  /**
   * Show a dialog box to ask the user if they would like the device ejected
   * even though Songbird is currently playing from the device.
   *
   * \param aEject [out, retval]  Should the device be ejected?
   */
  nsresult PromptForEjectDuringPlayback(PRBool* aEject);
};

#endif /* __SBBASEDEVICE__H__ */

