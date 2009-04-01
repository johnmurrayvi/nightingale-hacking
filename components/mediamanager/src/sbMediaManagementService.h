/* vim: set sw=2 :*/
/*
//
// BEGIN SONGBIRD GPL
//
// This file is part of the Songbird web player.
//
// Copyright(c) 2005-2009 POTI, Inc.
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

#include "sbIMediaManagementService.h"

#include <sbIMediaListListener.h>

#include <nsIObserver.h>
#include <nsITimer.h>

#include <nsCOMPtr.h>
#include <nsHashKeys.h>
#include <nsTHashtable.h>

class nsIComponentManager;

struct nsModuleComponentInfo;

class sbILibrary;

class sbMediaManagementService : public sbIMediaManagementService,
                                 public sbIMediaListListener,
                                 public nsITimerCallback,
                                 public nsIObserver
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_SBIMEDIAMANAGEMENTSERVICE
  NS_DECL_SBIMEDIALISTLISTENER
  NS_DECL_NSIOBSERVER
  NS_DECL_NSITIMERCALLBACK

  sbMediaManagementService();

public:
  /**
   * component registration callback
   * @see nsIGenericFactory NSRegisterSelfProcPtr
   */
  static NS_METHOD RegisterSelf(nsIComponentManager *aCompMgr,
                                nsIFile *aPath,
                                const char *aLoaderStr,
                                const char *aType,
                                const nsModuleComponentInfo *aInfo);

public:
  /**
   * Initialize the media management service
   */
  NS_METHOD Init();

protected:
  /**
   * Scan mLibrary for things that need updating and dispatch jobs
   */
  NS_METHOD ScanLibrary();
  
  /**
   * Queue an item for processing
   */
  NS_METHOD OnItemChanged(/*in*/ sbIMediaItem* aItem);

  /**
   * do anything else needed to start listening for changes
   */
  NS_METHOD StartListening();
  
  /**
   * do anything else needed to stop listening
   */
  NS_METHOD StopListening();
  
  /**
   * helper for submitting individual items
   */
  static PLDHashOperator ProcessItem(nsISupportsHashKey* aKey, void* aClosure);

private:
  virtual ~sbMediaManagementService();

protected:
  /**
   * the library this service is monitoring
   */
  nsCOMPtr<sbILibrary> mLibrary;
  
  /**
   * whether the management service is enabled
   * (this is a necessary but not sufficient indication that this is actively
   * monitoring the library; it is also possible that it is waiting to start.)
   */
  PRBool mEnabled;
  
  /**
   * a timer used to delay startup after the initial application start
   */
  nsCOMPtr<nsITimer> mDelayedStartupTimer;
  
  /**
   * timer used to delay actually doing anything with the files
   */
  nsCOMPtr<nsITimer> mPerformActionTimer;
  
  /**
   * list of media items that need updating
   */
  nsTHashtable<nsISupportsHashKey> mDirtyItems;
  
  /**
   * which operations we will do for media management
   */
  PRUint32 mManageMode;
};


#define SB_MEDIAMANAGEMENTSERVICE_CLASSNAME \
  "Songbird Media Management Service"
#define SB_MEDIAMANAGEMENTSERVICE_CID \
  /* {f991c1da-1dd1-11b2-a66f-b491ce4f7e09} */ \
  {0xf991c1da, 0x1dd1, 0x11b2, {0xa6, 0x6f, 0xb4, 0x91, 0xce, 0x4f, 0x7e, 0x09}}
