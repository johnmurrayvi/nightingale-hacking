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

#include <nsIRunnable.h>
#include "sbIDeviceEventListener.h"

#include <a11yGeneric.h>
#include <mozilla/Monitor.h>
#include <nsCOMArray.h>
#include <nsIThread.h>

/*
 * event listener stress testing class
 */

class sbDeviceEventTesterStressThreads: public nsIRunnable,
                                        public sbIDeviceEventListener
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIRUNNABLE
  NS_DECL_SBIDEVICEEVENTLISTENER
  
  sbDeviceEventTesterStressThreads();

private:
  ~sbDeviceEventTesterStressThreads();
  void OnEvent();

protected:
  mozilla::Monitor mMonitor;
  PRInt32 mCounter;
  nsCOMArray<nsIThread> mThreads;

  NS_DECL_RUNNABLEMETHOD(sbDeviceEventTesterStressThreads, OnEvent);
};
