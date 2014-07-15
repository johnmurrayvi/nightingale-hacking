/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 :miv */
/*
 *=BEGIN SONGBIRD GPL
 *
 * This file is part of the Songbird web player.
 *
 * Copyright(c) 2005-2010 POTI, Inc.
 * http://www.songbirdnest.com
 *
 * This file may be licensed under the terms of of the
 * GNU General Public License Version 2 (the ``GPL'').
 *
 * Software distributed under the License is distributed
 * on an ``AS IS'' basis, WITHOUT WARRANTY OF ANY KIND, either
 * express or implied. See the GPL for the specific language
 * governing rights and limitations.
 *
 * You should have received a copy of the GPL along with this
 * program. If not, go to http://www.gnu.org/licenses/gpl.html
 * or write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *=END SONGBIRD GPL
 */

#include "sbMediaListEnumeratorWrapper.h"

// Mozilla includes
#include <nsComponentManagerUtils.h>
#include <nsIClassInfoImpl.h>
#include <nsIProgrammingLanguage.h>
#include <nsMemory.h>
#include <nsStringAPI.h>
#include <nsThreadUtils.h>
#include <mozilla/ReentrantMonitor.h>

// Songbird includes
#include <sbStandardProperties.h>
#include <sbLocalDatabaseMediaItem.h>

// Songbird interfaces
#include <sbILibrary.h>
#include <sbIMediaItem.h>
#include <sbIMediaList.h>

NS_IMPL_THREADSAFE_ISUPPORTS3(sbMediaListEnumeratorWrapper, 
                              sbIMediaListEnumeratorWrapper,
                              nsISimpleEnumerator,
                              nsIClassInfo);
NS_IMPL_CI_INTERFACE_GETTER3(sbMediaListEnumeratorWrapper,
                             sbIMediaListEnumeratorWrapper,
                             nsISimpleEnumerator,
                             nsIClassInfo);
// NS_DECL_CLASSINFO(sbMediaListEnumeratorWrapper);
NS_IMPL_THREADSAFE_CI(sbMediaListEnumeratorWrapper);

sbMediaListEnumeratorWrapper::sbMediaListEnumeratorWrapper()
: mMonitor("sbMediaListEnumeratorWrapper::mMonitor")
{
}

sbMediaListEnumeratorWrapper::~sbMediaListEnumeratorWrapper()
{

}

namespace {

class EnumWrapperListenerProxy : public sbIMediaListEnumeratorWrapperListener
{
public:
  EnumWrapperListenerProxy(nsISimpleEnumerator *aEnumerator,
                           sbIMediaListEnumeratorWrapperListener *aListener)
    : mEnumerator(aEnumerator), mListener(aListener)
  { }

  NS_DECL_ISUPPORTS;
  NS_DECL_SBIMEDIALISTENUMERATORWRAPPERLISTENER;

  class OnHasMoreElementsRunnable : public nsRunnable
  {
  public:
    OnHasMoreElementsRunnable(sbIMediaListEnumeratorWrapperListener *aListener,
                              nsISimpleEnumerator *aEnumerator,
                              bool aHasMore)
      : mListener(aListener), mEnumerator(aEnumerator), mHasMore(aHasMore)
    { }

    NS_DECL_NSIRUNNABLE

  private:
    nsCOMPtr<sbIMediaListEnumeratorWrapperListener> mListener;
    nsCOMPtr<nsISimpleEnumerator> mEnumerator;
    bool mHasMore;
  };

  class OnGetNextRunnable : public nsRunnable
  {
  public:
    OnGetNextRunnable(sbIMediaListEnumeratorWrapperListener *aListener,
                      nsISimpleEnumerator *aEnumerator,
                      nsISupports *aNextElement)
      : mListener(aListener), mEnumerator(aEnumerator), mNextElement(aNextElement)
    { }

    NS_DECL_NSIRUNNABLE

  private:
    nsCOMPtr<sbIMediaListEnumeratorWrapperListener> mListener;
    nsCOMPtr<nsISimpleEnumerator> mEnumerator;
    nsCOMPtr<nsISupports> mNextElement;
  };


private:
  nsCOMPtr<nsISimpleEnumerator> mEnumerator;
  nsCOMPtr<sbIMediaListEnumeratorWrapperListener> mListener;
};

NS_IMPL_THREADSAFE_ISUPPORTS1(EnumWrapperListenerProxy,
                              sbIMediaListEnumeratorWrapperListener);

NS_IMETHODIMP
EnumWrapperListenerProxy::OnHasMoreElements(nsISimpleEnumerator *aEnumerator, bool aHasMore)
{
  nsresult rv;

  nsRefPtr<OnHasMoreElementsRunnable> r = 
    new OnHasMoreElementsRunnable(mListener, aEnumerator, aHasMore);

  if (NS_IsMainThread()) {
    rv = r->Run();
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    NS_DispatchToMainThread(r);
  }

  return NS_OK;
}

NS_IMETHODIMP
EnumWrapperListenerProxy::OnGetNext(nsISimpleEnumerator *aEnumerator, nsISupports *aNextElement)
{
  nsresult rv;

  nsRefPtr<OnGetNextRunnable> r = 
    new OnGetNextRunnable(mListener, aEnumerator, aNextElement);

  if (NS_IsMainThread()) {
    rv = r->Run();
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    NS_DispatchToMainThread(r);
  }

  return NS_OK;
}

NS_IMETHODIMP
EnumWrapperListenerProxy::OnHasMoreElementsRunnable::Run()
{
  // XXX
  return NS_OK;
}

NS_IMETHODIMP
EnumWrapperListenerProxy::OnGetNextRunnable::Run()
{
  // XXX
  return NS_OK;
}

}


NS_IMETHODIMP
sbMediaListEnumeratorWrapper::Initialize(
                                nsISimpleEnumerator * aEnumerator,
                                sbIMediaListEnumeratorWrapperListener *aListener)
{
  NS_ENSURE_ARG_POINTER(aEnumerator);
  
  mozilla::ReentrantMonitorAutoEnter autoMonitor(mMonitor);

  mEnumerator = aEnumerator;

  if(aListener) {
    mListener = new EnumWrapperListenerProxy(aEnumerator, aListener);
  }

  return NS_OK;
}

// nsISimpleEnumerator implementation
NS_IMETHODIMP
sbMediaListEnumeratorWrapper::HasMoreElements(bool *aMore)
{
  NS_ENSURE_TRUE(mEnumerator, NS_ERROR_NOT_INITIALIZED);
  
  mozilla::ReentrantMonitorAutoEnter autoMonitor(mMonitor);
  nsresult rv = mEnumerator->HasMoreElements(aMore);
  NS_ENSURE_SUCCESS(rv, rv);

  if(mListener) {
    nsCOMPtr<nsISimpleEnumerator> grip(mEnumerator);
    nsCOMPtr<sbIMediaListEnumeratorWrapperListener> listener(mListener);

    rv = listener->OnHasMoreElements(grip, *aMore);
    NS_WARN_IF_FALSE(NS_SUCCEEDED(rv), "onHasMoreElements returned an error");
  }

  return NS_OK;
}

NS_IMETHODIMP
sbMediaListEnumeratorWrapper::GetNext(nsISupports ** aItem)
{
  NS_ENSURE_ARG_POINTER(aItem);

  mozilla::ReentrantMonitorAutoEnter autoMonitor(mMonitor);

  nsCOMPtr<nsISupports> supports;
  nsresult rv = mEnumerator->GetNext(getter_AddRefs(supports));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIMediaItem> mediaItem;
  nsCOMPtr<sbIIndexedMediaItem> indexedMediaItem = do_QueryInterface(supports, &rv);

  if(NS_SUCCEEDED(rv)) {
    rv = indexedMediaItem->GetMediaItem(getter_AddRefs(mediaItem));
    NS_ENSURE_SUCCESS(rv, rv);
  }
  else if(rv == NS_ERROR_NO_INTERFACE) {
    mediaItem = do_QueryInterface(supports, &rv);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  else {
    return rv;
  }

  nsString itemGuid;
  rv = mediaItem->GetGuid(itemGuid);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbILibrary> library;
  rv = mediaItem->GetLibrary(getter_AddRefs(library));
  NS_ENSURE_SUCCESS(rv, rv);

  nsString libraryGuid;
  rv = library->GetGuid(libraryGuid);
  NS_ENSURE_SUCCESS(rv, rv);

  nsString propertyValue(libraryGuid);
  propertyValue += NS_LITERAL_STRING(",");
  propertyValue += itemGuid;

  // Do not send notification when the item is updated.
  nsCOMPtr<sbILocalDatabaseMediaItem> item =
    do_QueryInterface(mediaItem, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  item->SetSuppressNotifications(PR_TRUE);
  rv = 
    mediaItem->SetProperty(NS_LITERAL_STRING(SB_PROPERTY_DOWNLOAD_STATUS_TARGET),
                           propertyValue);
  NS_ENSURE_SUCCESS(rv, rv);
  item->SetSuppressNotifications(PR_FALSE);

  NS_ADDREF(*aItem = mediaItem);

  if(mListener) {
    nsCOMPtr<nsISimpleEnumerator> grip(mEnumerator);
    nsCOMPtr<sbIMediaListEnumeratorWrapperListener> listener(mListener);

    rv = listener->OnGetNext(grip, mediaItem);
    NS_WARN_IF_FALSE(NS_SUCCEEDED(rv), "onGetNext returned an error");
  }
  
  return NS_OK;
}
