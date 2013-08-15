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

#ifndef __SB_SECURITY_MIXIN_H__
#define __SB_SECURITY_MIXIN_H__

#include "sbISecurityMixin.h"
#include "sbISecurityAggregator.h"

#include <nsCOMPtr.h>
#include <nsIClassInfo.h>
#include <nsISecurityCheckedComponent.h>
#include <nsIURI.h>
#include <nsStringGlue.h>
#include <nsTArray.h>
#include <nsIDOMDocument.h>

#define SONGBIRD_SECURITYMIXIN_CONTRACTID                 \
  "@songbirdnest.com/remoteapi/security-mixin;1"
#define SONGBIRD_SECURITYMIXIN_CLASSNAME                  \
  "Songbird Remote Security Mixin"
#define SONGBIRD_SECURITYMIXIN_CID                        \
{ /* aaae98ec-386e-405e-b109-cf1a872ef6dd */              \
  0xaaae98ec,                                             \
  0x386e,                                                 \
  0x405e,                                                 \
  {0xb1, 0x09, 0xcf, 0x1a, 0x87, 0x2e, 0xf6, 0xdd}        \
}

extern char* SB_CloneAllAccess();

struct Scope;

#define RAPI_EVENT_CLASS      NS_LITERAL_STRING("Events")
#define RAPI_EVENT_TYPE       NS_LITERAL_STRING("remoteapi")

#define SB_EVENT_RAPI_PERMISSION_CHANGED  NS_LITERAL_STRING("RemoteAPIPermissionChanged")
#define SB_EVENT_RAPI_PERMISSION_DENIED   NS_LITERAL_STRING("RemoteAPIPermissionDenied")

class sbSecurityMixin : public nsISecurityCheckedComponent,
                        public nsIClassInfo,
                        public sbISecurityMixin
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSICLASSINFO
  NS_DECL_NSISECURITYCHECKEDCOMPONENT
  NS_DECL_SBISECURITYMIXIN

  sbSecurityMixin();
  
  /**
   * \brief Set the permission to allow for a scoped name
   * \param aURI the URI to set permissions for
   * \param aScopedName the permission to set
   */
  static nsresult SetPermission(nsIURI *aURI, const nsACString &aScopedName);

protected:
  virtual ~sbSecurityMixin();

  // helpers for resolving the prefs and permissions
  bool GetPermission(nsIURI *aURI, const struct Scope* aScope);
  bool GetPermissionForScopedName(const nsAString &aScopedName,
                                    bool disableNotificationCheck = PR_FALSE);
  bool GetScopedName(nsTArray<nsCString> &aStringArray,
                       const nsAString &aMethodName,
                       nsAString &aScopedName);
  
  const struct Scope* GetScopeForScopedName(const nsAString &aScopedName);
  
  // helpers for dispatching notification events
  nsresult DispatchNotificationEvent(const char* aNotificationType, 
                                     const Scope* aScope,
                                     bool aHasAccess);

  // helper function for allocating IID array 
  nsresult CopyIIDArray(PRUint32 aCount, const nsIID **aSourceArray, nsIID ***aDestArray);

  // helper function for copying over a string array
  nsresult CopyStrArray(PRUint32 aCount, const char **aSourceArray, nsTArray<nsCString> *aDestArray);

  // non-binding ref, we don't want to keep our outer from going away
  sbISecurityAggregator* mOuter;  

  // holders for the lists of approved methods, properties and interfaces
  // passed in to us by the mOuter ( in our Init() method )
  nsIID **mInterfaces;
  PRUint32 mInterfacesCount;
  bool mPrivileged;
  nsTArray<nsCString> mMethods;
  nsTArray<nsCString> mRProperties;
  nsTArray<nsCString> mWProperties;
  nsCOMPtr<nsIDOMDocument> mNotificationDocument;
};

#endif // __SB_SECURITY_MIXIN_H__

