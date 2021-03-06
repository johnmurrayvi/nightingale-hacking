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

#ifndef __SBDEVICECONTENT_H__
#define __SBDEVICECONTENT_H__

#include <sbIDeviceContent.h>

#include <mozilla/ReentrantMonitor.h>
#include <nsCOMPtr.h>
#include <nsIArray.h>
#include <nsIMutableArray.h>

class sbDeviceContent : public sbIDeviceContent
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_SBIDEVICECONTENT

  static sbDeviceContent * New();
private:
  sbDeviceContent();
  virtual ~sbDeviceContent();
  nsresult FindLibrary(sbIDeviceLibrary *aLibrary, PRUint32* _retval);

  mozilla::ReentrantMonitor mDeviceLibrariesMonitor;
  nsCOMPtr<nsIMutableArray> mDeviceLibraries;
};

#endif /* __SBDEVICECONTENT_H__ */

