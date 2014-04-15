/*
 *=BEGIN NIGHTINGALE GPL
 *
 * This file is part of the Nightingale Media Player.
 *
 * Copyright(c) 2014
 * http://www.getnightingale.com
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
 *=END NIGHTINGALE GPL
 */

#ifndef __SB_PROXIEDCOMPONENTMANAGER_H__
#define __SB_PROXIEDCOMPONENTMANAGER_H__

#include <nsIRunnable.h>
#include <nsCOMPtr.h>
#include <nsComponentManagerUtils.h>
#include <nsServiceManagerUtils.h>
#include <nsThreadUtils.h>
#include <nsXPCOMCIDInternal.h>
#include <nsProxyRelease.h>



/**
 * Class used by do_MainThreadQueryInterface.
 */

class NS_COM_GLUE sbMainThreadQueryInterface : public nsCOMPtr_helper
{
public:
  sbMainThreadQueryInterface(nsISupports* aSupports,
                             nsresult*    aResult) :
    mSupports(aSupports),
    mResult(aResult)
  {
  }

  virtual nsresult NS_FASTCALL operator()(const nsIID&, void**) const;

private:
  nsISupports* mSupports;
  nsresult*    mResult;
};


/**
 * Return a QI'd version of the object specified by aSupports whose methods will
 * execute on the main thread.  If this function is called on the main thread,
 * the returned object will simply be the QI'd version of aSupports.  Otherwise,
 * the returned object will by a main thread synchronous proxy QI'd version of
 * aSupports.  The result code may optionally be returned in aResult.
 *
 * \param aSupports             Object for which to get QI'd main thread object.
 * \param aResult               Optional returned result.
 *
 * \return                      QI'd main thread object.
 *
 * Example:
 *
 *   nsresult rv;
 *   nsCOMPtr<nsIURI> mainThreadURI = do_MainThreadQueryInterface(uri, &rv);
 *   NS_ENSURE_SUCCESS(rv, rv);
 */

inline sbMainThreadQueryInterface
do_MainThreadQueryInterface(nsISupports* aSupports,
                            nsresult*    aResult = nsnull)
{
  return sbMainThreadQueryInterface(aSupports, aResult);
}


template <class T> inline void
do_MainThreadQueryInterface(already_AddRefed<T>& aSupports,
                            nsresult*            aResult = nsnull)
{
  // This signature exists solely to _stop_ you from doing the bad thing.
  // Saying |do_MainThreadQueryInterface()| on a pointer that is not otherwise
  // owned by someone else is an automatic leak.  See
  // <http://bugzilla.mozilla.org/show_bug.cgi?id=8221>.
}


#endif /* __SB_PROXIEDCOMPONENTMANAGER_H__ */
