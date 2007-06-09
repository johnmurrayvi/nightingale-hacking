/*
//
// BEGIN SONGBIRD GPL
//
// This file is part of the Songbird web player.
//
// Copyright(c) 2005-2007 POTI, Inc.
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

#include "sbLocalDatabaseCascadeFilterSet.h"

#include <nsISimpleEnumerator.h>
#include <nsITreeView.h>
#include <sbIDatabaseQuery.h>
#include <sbIDatabaseResult.h>
#include <sbIFilterableMediaList.h>
#include <sbILocalDatabaseAsyncGUIDArray.h>
#include <sbILocalDatabaseLibrary.h>
#include <sbIMediaListView.h>
#include <sbIPropertyArray.h>
#include <sbIPropertyManager.h>
#include <sbISearchableMediaList.h>
#include <sbISQLBuilder.h>

#include <DatabaseQuery.h>
#include <nsComponentManagerUtils.h>
#include <nsServiceManagerUtils.h>
#include <prlog.h>
#include "sbLocalDatabasePropertyCache.h"
#include "sbLocalDatabaseTreeView.h"
#include <sbPropertiesCID.h>
#include <sbStandardProperties.h>
#include <sbSQLBuilderCID.h>
#include <sbTArrayStringEnumerator.h>

/**
 * To log this module, set the following environment variable:
 *   NSPR_LOG_MODULES=sbLocalDatabaseCascadeFilterSet:5
 */
#ifdef PR_LOGGING
static PRLogModuleInfo* sFilterSetLog = nsnull;
#define TRACE(args) if (sFilterSetLog) PR_LOG(sFilterSetLog, PR_LOG_DEBUG, args)
#define LOG(args)   if (sFilterSetLog) PR_LOG(sFilterSetLog, PR_LOG_WARN, args)
#else /* PR_LOGGING */
#define TRACE(args) /* nothing */
#define LOG(args)   /* nothing */
#endif /* PR_LOGGING */

sbLocalDatabaseCascadeFilterSet::sbLocalDatabaseCascadeFilterSet()
{
  MOZ_COUNT_CTOR(sbLocalDatabaseCascadeFilterSet);
#ifdef PR_LOGGING
  if (!sFilterSetLog) {
    sFilterSetLog = PR_NewLogModule("sbLocalDatabaseCascadeFilterSet");
  }
#endif
  TRACE(("sbLocalDatabaseCascadeFilterSet[0x%.8x] - Constructed", this));
}

sbLocalDatabaseCascadeFilterSet::~sbLocalDatabaseCascadeFilterSet()
{
  TRACE(("sbLocalDatabaseCascadeFilterSet[0x%.8x] - Destructed", this));
  MOZ_COUNT_DTOR(sbLocalDatabaseCascadeFilterSet);
}

NS_IMPL_ISUPPORTS1(sbLocalDatabaseCascadeFilterSet,
                   sbICascadeFilterSet);

nsresult
sbLocalDatabaseCascadeFilterSet::Init(sbILocalDatabaseLibrary* aLibrary,
                                      sbIMediaListView* aMediaListView,
                                      sbILocalDatabaseAsyncGUIDArray* aProtoArray)
{
  TRACE(("sbLocalDatabaseCascadeFilterSet[0x%.8x] - Init", this));
  NS_ENSURE_ARG_POINTER(aMediaListView);
  NS_ENSURE_ARG_POINTER(aProtoArray);

  nsresult rv;

  mLibrary = aLibrary;

  mMediaListView = aMediaListView;

  mProtoArray = aProtoArray;

  rv = mProtoArray->ClearFilters();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mProtoArray->ClearSorts();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mProtoArray->SetIsDistinct(PR_TRUE);
  NS_ENSURE_SUCCESS(rv, rv);

  PRBool success = mListeners.Init();
  NS_ENSURE_TRUE(success, NS_ERROR_OUT_OF_MEMORY);

  return NS_OK;
}

nsresult
sbLocalDatabaseCascadeFilterSet::Rebuild()
{
  TRACE(("sbLocalDatabaseCascadeFilterSet[0x%.8x] - Rebuild", this));

  // XXXsteve This is slow so I am going to disable this until we have the
  // updated property notifications
/*
  nsresult rv;
  for (PRUint32 i = 0; i < mFilters.Length(); i++) {
    sbFilterSpec& fs = mFilters[i];
    if (fs.treeView) {
      rv = fs.array->Invalidate();
      NS_ENSURE_SUCCESS(rv, rv);

      rv = fs.treeView->Rebuild();
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }
*/
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseCascadeFilterSet::GetLength(PRUint16* aLength)
{
  TRACE(("sbLocalDatabaseCascadeFilterSet[0x%.8x] - GetLength", this));
  NS_ENSURE_ARG_POINTER(aLength);

  *aLength = mFilters.Length();

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseCascadeFilterSet::GetProperty(PRUint16 aIndex,
                                             nsAString& _retval)
{
  TRACE(("sbLocalDatabaseCascadeFilterSet[0x%.8x] - GetProperty", this));
  PRUint32 filterLength = mFilters.Length();
  NS_ENSURE_TRUE(filterLength, NS_ERROR_UNEXPECTED);
  NS_ENSURE_ARG_MAX(aIndex, filterLength - 1);

  _retval = mFilters[aIndex].property;

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseCascadeFilterSet::IsSearch(PRUint16 aIndex,
                                          PRBool* _retval)
{
  TRACE(("sbLocalDatabaseCascadeFilterSet[0x%.8x] - IsSearch", this));
  NS_ENSURE_ARG_POINTER(_retval);
  NS_ENSURE_TRUE(aIndex < mFilters.Length(), NS_ERROR_INVALID_ARG);

  *_retval = mFilters[aIndex].isSearch;

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseCascadeFilterSet::AppendFilter(const nsAString& aProperty,
                                              PRUint16 *_retval)
{
  TRACE(("sbLocalDatabaseCascadeFilterSet[0x%.8x] - AppendFilter", this));
  NS_ENSURE_ARG_POINTER(_retval);

  nsresult rv;

  sbFilterSpec* fs = mFilters.AppendElement();
  NS_ENSURE_TRUE(fs, NS_ERROR_OUT_OF_MEMORY);

  fs->isSearch = PR_FALSE;
  fs->property = aProperty;

  rv = mProtoArray->CloneAsyncArray(getter_AddRefs(fs->array));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = fs->array->AddSort(aProperty, PR_TRUE);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = ConfigureArray(mFilters.Length() - 1);
  NS_ENSURE_SUCCESS(rv, rv);

  *_retval = mFilters.Length() - 1;

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseCascadeFilterSet::AppendSearch(const PRUnichar** aPropertyArray,
                                              PRUint32 aPropertyArrayCount,
                                              PRUint16 *_retval)
{
  TRACE(("sbLocalDatabaseCascadeFilterSet[0x%.8x] - AppendSearch", this));
  if (aPropertyArrayCount) {
    NS_ENSURE_ARG_POINTER(aPropertyArray);
  }
  NS_ENSURE_ARG_POINTER(_retval);

  nsresult rv;

  sbFilterSpec* fs = mFilters.AppendElement();
  NS_ENSURE_TRUE(fs, NS_ERROR_OUT_OF_MEMORY);

  fs->isSearch = PR_TRUE;

  for (PRUint32 i = 0; i < aPropertyArrayCount; i++) {
    if (aPropertyArray[i]) {
      nsString* success = fs->propertyList.AppendElement(aPropertyArray[i]);
      NS_ENSURE_TRUE(success, NS_ERROR_OUT_OF_MEMORY);
    }
    else {
      NS_WARNING("Null pointer passed in array");
    }
  }

  rv = mProtoArray->CloneAsyncArray(getter_AddRefs(fs->array));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = fs->array->AddSort(NS_LITERAL_STRING(SB_PROPERTY_CREATED),
                          PR_TRUE);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = ConfigureArray(mFilters.Length() - 1);
  NS_ENSURE_SUCCESS(rv, rv);

  *_retval = mFilters.Length() - 1;

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseCascadeFilterSet::Remove(PRUint16 aIndex)
{
  TRACE(("sbLocalDatabaseCascadeFilterSet[0x%.8x] - Remove", this));
  NS_ENSURE_TRUE(aIndex < mFilters.Length(), NS_ERROR_INVALID_ARG);

  nsresult rv;

  mFilters.RemoveElementAt(aIndex);

  // Update the filters downstream from the removed filter
  for (PRUint32 i = aIndex; i < mFilters.Length(); i++) {
    rv = ConfigureArray(i);
    NS_ENSURE_SUCCESS(rv, rv);

    // Notify listeners
    mListeners.EnumerateEntries(OnValuesChangedCallback, &i);
  }

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseCascadeFilterSet::ChangeFilter(PRUint16 aIndex,
                                              const nsAString& aProperty)
{
  TRACE(("sbLocalDatabaseCascadeFilterSet[0x%.8x] - ChangeFilter", this));
  NS_ENSURE_TRUE(aIndex < mFilters.Length(), NS_ERROR_INVALID_ARG);

  nsresult rv;

  sbFilterSpec& fs = mFilters[aIndex];
  if (fs.isSearch)
    return NS_ERROR_INVALID_ARG;

  fs.property = aProperty;

  rv = fs.array->ClearSorts();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = fs.array->AddSort(aProperty, PR_TRUE);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = ConfigureArray(aIndex);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseCascadeFilterSet::Set(PRUint16 aIndex,
                                     const PRUnichar** aValueArray,
                                     PRUint32 aValueArrayCount)
{
  TRACE(("sbLocalDatabaseCascadeFilterSet[0x%.8x] - Set", this));
  if (aValueArrayCount) {
    NS_ENSURE_ARG_POINTER(aValueArray);
  }
  NS_ENSURE_TRUE(aIndex < mFilters.Length(), NS_ERROR_INVALID_ARG);

  nsresult rv;

  nsCOMPtr<sbIPropertyArray> filter =
    do_CreateInstance(SB_PROPERTYARRAY_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  sbFilterSpec& fs = mFilters[aIndex];
  fs.values.Clear();

  for (PRUint32 i = 0; i < aValueArrayCount; i++) {
    if (aValueArray[i]) {
      nsString* value = fs.values.AppendElement(aValueArray[i]);
      NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

      // If this is a search, we need to add this search value for each
      // property being searched
      if (fs.isSearch) {
        for (PRUint32 j = 0; j < fs.propertyList.Length(); j++) {
          rv = filter->AppendProperty(fs.propertyList[j], *value);
          NS_ENSURE_SUCCESS(rv, rv);
        }
      }
      else {
        rv = filter->AppendProperty(fs.property, *value);
        NS_ENSURE_SUCCESS(rv, rv);
      }
    }
    else {
      NS_WARNING("Null pointer passed in array");
    }
  }

  nsCOMPtr<sbIPropertyArray> toClear =
    do_CreateInstance(SB_PROPERTYARRAY_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  // Update downstream filters
  for (PRUint32 i = aIndex + 1; i < mFilters.Length(); i++) {

    // We want to clear the downstream filter since the upstream filter has
    // changed
    sbFilterSpec& downstream = mFilters[i];

    rv = toClear->AppendProperty(downstream.property, EmptyString());
    NS_ENSURE_SUCCESS(rv, rv);

    rv = ConfigureArray(i);
    NS_ENSURE_SUCCESS(rv, rv);

    if (downstream.treeView) {
      rv = downstream.treeView->Rebuild();
      NS_ENSURE_SUCCESS(rv, rv);
    }

    // Notify listeners
    mListeners.EnumerateEntries(OnValuesChangedCallback, &i);
  }

  // Clear the downstream filters from the associated view
  nsCOMPtr<sbIFilterableMediaList> filterable =
    do_QueryInterface(mMediaListView, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  PRUint32 numClear;
  rv = toClear->GetLength(&numClear);
  NS_ENSURE_SUCCESS(rv, rv);

  if (numClear > 0) {
    rv = filterable->SetFilters(toClear);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Update the associated view with the new filter or search setting
  if (fs.isSearch) {
    nsCOMPtr<sbISearchableMediaList> searchable =
      do_QueryInterface(mMediaListView, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    if (aValueArrayCount == 0) {
      rv = searchable->ClearSearch();
      NS_ENSURE_SUCCESS(rv, rv);
    }
    else {
      rv = searchable->SetSearch(filter);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }
  else {
    if (aValueArrayCount == 0) {
      rv = filter->AppendProperty(fs.property, EmptyString());
      NS_ENSURE_SUCCESS(rv, rv);
    }
    rv = filterable->SetFilters(filter);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseCascadeFilterSet::GetValues(PRUint16 aIndex,
                                           nsIStringEnumerator **_retval)
{
  TRACE(("sbLocalDatabaseCascadeFilterSet[0x%.8x] - GetValues", this));
  NS_ENSURE_ARG_POINTER(_retval);
  NS_ENSURE_TRUE(aIndex < mFilters.Length(), NS_ERROR_INVALID_ARG);

  sbGUIDArrayPrimarySortEnumerator* values =
    new sbGUIDArrayPrimarySortEnumerator(mFilters[aIndex].array);
  NS_ENSURE_TRUE(values, NS_ERROR_OUT_OF_MEMORY);

  NS_ADDREF(*_retval = values);
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseCascadeFilterSet::GetValueAt(PRUint16 aIndex,
                                            PRUint32 aValueIndex,
                                            nsAString& aValue)
{
  TRACE(("sbLocalDatabaseCascadeFilterSet[0x%.8x] - GetValueAt", this));
  NS_ENSURE_TRUE(aIndex < mFilters.Length(), NS_ERROR_INVALID_ARG);

  mFilters[aIndex].array->GetSortPropertyValueByIndex(aValueIndex, aValue);

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseCascadeFilterSet::GetTreeView(PRUint16 aIndex,
                                             nsITreeView **_retval)
{
  TRACE(("sbLocalDatabaseCascadeFilterSet[0x%.8x] - GetTreeView", this));
  NS_ENSURE_ARG_POINTER(_retval);
  NS_ENSURE_TRUE(aIndex < mFilters.Length(), NS_ERROR_INVALID_ARG);

  sbFilterSpec& fs = mFilters[aIndex];

  if (fs.isSearch) {
    return NS_ERROR_INVALID_ARG;
  }

  if (!fs.treeView) {

    nsresult rv;

    nsCOMPtr<sbILocalDatabasePropertyCache> propertyCache;
    rv = mLibrary->GetPropertyCache(getter_AddRefs(propertyCache));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = fs.array->SetPropertyCache(propertyCache);
    NS_ENSURE_SUCCESS(rv, rv);

    fs.treeView = new sbLocalDatabaseTreeView();
    NS_ENSURE_TRUE(fs.treeView, NS_ERROR_OUT_OF_MEMORY);

    nsCOMPtr<sbIPropertyArray> propArray =
      do_CreateInstance(SB_PROPERTYARRAY_CONTRACTID, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = propArray->AppendProperty(fs.property, NS_LITERAL_STRING("a"));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = fs.treeView->Init(mMediaListView, fs.array, propArray);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  NS_ADDREF(*_retval = fs.treeView);

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseCascadeFilterSet::GetValueCount(PRUint16 aIndex,
                                               PRUint32 *_retval)
{
  TRACE(("sbLocalDatabaseCascadeFilterSet[0x%.8x] - GetValueCount", this));
  NS_ENSURE_ARG_POINTER(_retval);
  NS_ENSURE_TRUE(aIndex < mFilters.Length(), NS_ERROR_INVALID_ARG);

  nsresult rv = mFilters[aIndex].array->GetLength(_retval);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseCascadeFilterSet::AddListener(sbICascadeFilterSetListener* aListener)
{
  TRACE(("sbLocalDatabaseCascadeFilterSet[0x%.8x] - AddListener", this));
  nsISupportsHashKey* success = mListeners.PutEntry(aListener);
  NS_ENSURE_TRUE(success, NS_ERROR_OUT_OF_MEMORY);

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseCascadeFilterSet::RemoveListener(sbICascadeFilterSetListener* aListener)
{
  TRACE(("sbLocalDatabaseCascadeFilterSet[0x%.8x] - RemoveListener", this));

  mListeners.RemoveEntry(aListener);

  return NS_OK;
}

nsresult
sbLocalDatabaseCascadeFilterSet::ConfigureArray(PRUint32 aIndex)
{
  TRACE(("sbLocalDatabaseCascadeFilterSet[0x%.8x] - ConfigureArray", this));
  NS_ENSURE_TRUE(aIndex < mFilters.Length(), NS_ERROR_INVALID_ARG);

  nsresult rv;

  sbFilterSpec& fs = mFilters[aIndex];

  // Clear this filter since our upstream filters have changed
  rv = fs.array->ClearFilters();
  NS_ENSURE_SUCCESS(rv, rv);
  fs.values.Clear();

  nsCOMPtr<sbIPropertyManager> propMan =
    do_GetService(SB_PROPERTYMANAGER_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  // Apply the filters of each upstream filter to this filter
  for (PRUint32 i = 0; i < aIndex; i++) {
    const sbFilterSpec& upstream = mFilters[i];

    if (upstream.values.Length() > 0) {

      if(upstream.isSearch) {

        for (PRUint32 j = 0; j < upstream.propertyList.Length(); j++) {

          nsCOMPtr<sbIPropertyInfo> info;
          rv = propMan->GetPropertyInfo(upstream.propertyList[j],
                                        getter_AddRefs(info));
          NS_ENSURE_SUCCESS(rv, rv);

          sbStringArray sortableValues;
          for (PRUint32 k = 0; k < upstream.values.Length(); k++) {
            nsAutoString sortableValue;
            rv = info->MakeSortable(upstream.values[k], sortableValue);
            NS_ENSURE_SUCCESS(rv, rv);

            nsString* success = sortableValues.AppendElement(sortableValue);
            NS_ENSURE_TRUE(success, NS_ERROR_OUT_OF_MEMORY);
          }

          nsCOMPtr<nsIStringEnumerator> values =
            new sbTArrayStringEnumerator(NS_CONST_CAST(sbStringArray*,
                                                       &sortableValues));
          NS_ENSURE_TRUE(values, NS_ERROR_OUT_OF_MEMORY);

          rv = fs.array->AddFilter(upstream.propertyList[j],
                                   values,
                                   PR_TRUE);
          NS_ENSURE_SUCCESS(rv, rv);

        }

      }
      else {
        nsCOMPtr<sbIPropertyInfo> info;
        rv = propMan->GetPropertyInfo(upstream.property, getter_AddRefs(info));
        NS_ENSURE_SUCCESS(rv, rv);

        sbStringArray sortableValues;
        for (PRUint32 k = 0; k < upstream.values.Length(); k++) {
          nsAutoString sortableValue;
          rv = info->MakeSortable(upstream.values[k], sortableValue);
          NS_ENSURE_SUCCESS(rv, rv);

          nsString* success = sortableValues.AppendElement(sortableValue);
          NS_ENSURE_TRUE(success, NS_ERROR_OUT_OF_MEMORY);
        }

        nsCOMPtr<nsIStringEnumerator> values =
          new sbTArrayStringEnumerator(NS_CONST_CAST(sbStringArray*,
                                                     &sortableValues));
        NS_ENSURE_TRUE(values, NS_ERROR_OUT_OF_MEMORY);

        rv = fs.array->AddFilter(upstream.property,
                                 values,
                                 PR_FALSE);
        NS_ENSURE_SUCCESS(rv, rv);
      }
    }
  }

  return NS_OK;
}

PLDHashOperator PR_CALLBACK
sbLocalDatabaseCascadeFilterSet::OnValuesChangedCallback(nsISupportsHashKey* aKey,
                                                         void* aUserData)
{
  TRACE(("sbLocalDatabaseCascadeFilterSet[static] - OnValuesChangedCallback"));
  NS_ASSERTION(aKey && aUserData, "Args should not be null!");

  nsresult rv;
  nsCOMPtr<sbICascadeFilterSetListener> listener =
    do_QueryInterface(aKey->GetKey(), &rv);

  if (NS_SUCCEEDED(rv)) {
    PRUint32* index = NS_STATIC_CAST(PRUint32*, aUserData);
    rv = listener->OnValuesChanged(*index);
    NS_WARN_IF_FALSE(NS_SUCCEEDED(rv),
                     "OnValuesChanged returned a failure code");
  }
  return PL_DHASH_NEXT;
}

NS_IMPL_ISUPPORTS1(sbGUIDArrayPrimarySortEnumerator, nsIStringEnumerator)

sbGUIDArrayPrimarySortEnumerator::sbGUIDArrayPrimarySortEnumerator(sbILocalDatabaseAsyncGUIDArray* aArray) :
  mArray(aArray),
  mNextIndex(0)
{
}

NS_IMETHODIMP
sbGUIDArrayPrimarySortEnumerator::HasMore(PRBool *_retval)
{
  NS_ENSURE_ARG_POINTER(_retval);
  nsresult rv;

  PRUint32 length;
  rv = mArray->GetLength(&length);
  NS_ENSURE_SUCCESS(rv, rv);

  *_retval = mNextIndex < length;
  return NS_OK;
}

NS_IMETHODIMP
sbGUIDArrayPrimarySortEnumerator::GetNext(nsAString& _retval)
{
  nsresult rv;

  rv = mArray->GetSortPropertyValueByIndex(mNextIndex, _retval);
  NS_ENSURE_SUCCESS(rv, rv);

  mNextIndex++;
  return NS_OK;
}

