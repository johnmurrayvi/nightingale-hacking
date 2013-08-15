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

/**
 * \file FileScan.cpp
 * \brief
 */

// INCLUDES ===================================================================
#include "sbFileScan.h"
#include <nspr.h>
#include <prmem.h>
#include <prmon.h>
#include <prlog.h>

#include <nsMemory.h>
#include <mozilla/Mutex.h>
#include <mozilla/ReentrantMonitor.h>
#include <nsIIOService.h>
#include <nsIURI.h>
#include <nsUnicharUtils.h>
#include <nsISupportsPrimitives.h>
#include <nsArrayUtils.h>
#include <nsThreadUtils.h>
#include <nsStringGlue.h>
#include <nsIMutableArray.h>
#include <nsIThreadPool.h>
#include <nsNetUtil.h>
#include <sbILibraryUtils.h>
#include <sbIDirectoryEnumerator.h>
#include <sbLockUtils.h>
#include <sbDebugUtils.h>


/**
 * To log this module, set the following environment variable:
 *   NSPR_LOG_MODULES=sbMediaImportFileScan:5
 */

// CLASSES ====================================================================
//*****************************************************************************
//  sbFileScanQuery Class
//*****************************************************************************
/* Implementation file */
NS_IMPL_THREADSAFE_ISUPPORTS1(sbFileScanQuery, sbIFileScanQuery)

//-----------------------------------------------------------------------------
sbFileScanQuery::sbFileScanQuery()
  : m_pDirectoryLock("sbFileScanQuery::m_pDirectoryLock")
  , m_pCurrentPathLock("sbFileScanQuery::m_pCurrentPathLock")
  , m_bSearchHidden(PR_FALSE)
  , m_bRecurse(PR_FALSE)
  , m_bWantLibraryContentURIs(PR_TRUE)
  , m_pScanningLock("sbFileScanQuery::m_pScanningLock")
  , m_bIsScanning(PR_FALSE)
  , m_pCallbackLock("sbFileScanQuery::m_pCallbackLock")
  , m_pExtensionsLock("sbFileScanQuery::m_pExtensionsLock")
  , m_pFlaggedFileExtensionsLock("sbFileScanQuery::m_pFlaggedFileExtensionsLock")
  , m_pCancelLock("sbFileScanQuery::m_pCancelLock")
  , m_bCancel(PR_FALSE)
{
  MOZ_COUNT_CTOR(sbFileScanQuery);
  init();
} //ctor

//-----------------------------------------------------------------------------
sbFileScanQuery::sbFileScanQuery(const nsString & strDirectory,
                                 const bool & bRecurse,
                                 sbIFileScanCallback *pCallback)
  : m_pDirectoryLock("sbFileScanQuery::m_pDirectoryLock")
  , m_strDirectory(strDirectory)
  , m_pCurrentPathLock("sbFileScanQuery::m_pCurrentPathLock")
  , m_bSearchHidden(PR_FALSE)
  , m_bRecurse(bRecurse)
  , m_bWantLibraryContentURIs(PR_TRUE)
  , m_pScanningLock("sbFileScanQuery::m_pScanningLock")
  , m_bIsScanning(PR_FALSE)
  , m_pCallbackLock("sbFileScanQuery::m_pCallbackLock")
  , m_pCallback(pCallback)
  , m_pExtensionsLock("sbFileScanQuery::m_pExtensionsLock")
  , m_pFlaggedFileExtensionsLock("sbFileScanQuery::m_pFlaggedFileExtensionsLock")
  , m_pCancelLock("sbFileScanQuery::m_pCancelLock")
  , m_bCancel(PR_FALSE)
{
  MOZ_COUNT_CTOR(sbFileScanQuery);
  init();
} //ctor

void sbFileScanQuery::init()
{
  m_pFileStack = nsnull;
  m_pFlaggedFileStack = nsnull;
  m_lastSeenExtension = EmptyString();

  {
    mozilla::MutexAutoLock lock(m_pExtensionsLock);
    bool SB_UNUSED_IN_RELEASE(success) = m_Extensions.Init();
    NS_ASSERTION(success, "FileScanQuery.m_Extensions failed to be initialized");
  }

  {
    mozilla::MutexAutoLock lock(m_pFlaggedFileExtensionsLock);

    bool SB_UNUSED_IN_RELEASE(success) = m_FlaggedExtensions.Init();
    NS_ASSERTION(success,
        "FileScanQuery.m_FlaggedExtensions failed to be initialized!");
  }

  SB_PRLOG_SETUP(sbMediaImportFileScan);
}

//-----------------------------------------------------------------------------
/*virtual*/ sbFileScanQuery::~sbFileScanQuery()
{
  MOZ_COUNT_DTOR(sbFileScanQuery);
} //dtor

//-----------------------------------------------------------------------------
/* attribute boolean searchHidden; */
NS_IMETHODIMP sbFileScanQuery::GetSearchHidden(bool *aSearchHidden)
{
  NS_ENSURE_ARG_POINTER(aSearchHidden);
  *aSearchHidden = m_bSearchHidden;
  return NS_OK;
} //GetSearchHidden

//-----------------------------------------------------------------------------
NS_IMETHODIMP sbFileScanQuery::SetSearchHidden(bool aSearchHidden)
{
  m_bSearchHidden = aSearchHidden;
  return NS_OK;
} //SetSearchHidden

//-----------------------------------------------------------------------------
/* void SetDirectory (in wstring strDirectory); */
NS_IMETHODIMP sbFileScanQuery::SetDirectory(const nsAString &strDirectory)
{
  mozilla::MutexAutoLock lock(m_pDirectoryLock);

  nsresult rv;
  if (!m_pFileStack) {
    m_pFileStack =
      do_CreateInstance("@songbirdnest.com/moz/xpcom/threadsafe-array;1", &rv);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  if (!m_pFlaggedFileStack) {
    m_pFlaggedFileStack =
      do_CreateInstance("@songbirdnest.com/moz/xpcom/threadsafe-array;1", &rv);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  m_strDirectory = strDirectory;
  return NS_OK;
} //SetDirectory

//-----------------------------------------------------------------------------
/* wstring GetDirectory (); */
NS_IMETHODIMP sbFileScanQuery::GetDirectory(nsAString &_retval)
{
  {
    mozilla::MutexAutoLock lock(m_pDirectoryLock);
    _retval = m_strDirectory;
  }
  return NS_OK;
} //GetDirectory

//-----------------------------------------------------------------------------
/* void SetRecurse (in bool bRecurse); */
NS_IMETHODIMP sbFileScanQuery::SetRecurse(bool bRecurse)
{
  m_bRecurse = bRecurse;
  return NS_OK;
} //SetRecurse

//-----------------------------------------------------------------------------
/* bool GetRecurse (); */
NS_IMETHODIMP sbFileScanQuery::GetRecurse(bool *_retval)
{
  NS_ENSURE_ARG_POINTER(_retval);
  *_retval = m_bRecurse;
  return NS_OK;
} //GetRecurse

//-----------------------------------------------------------------------------
NS_IMETHODIMP sbFileScanQuery::AddFileExtension(const nsAString &strExtension)
{
  mozilla::MutexAutoLock lock(m_pExtensionsLock);

  nsAutoString extStr(strExtension);
  ToLowerCase(extStr);
  if (!m_Extensions.GetEntry(extStr)) {
    nsStringHashKey* hashKey = m_Extensions.PutEntry(extStr);
    NS_ENSURE_TRUE(hashKey, NS_ERROR_OUT_OF_MEMORY);
  }

  return NS_OK;
} //AddFileExtension

//-----------------------------------------------------------------------------
NS_IMETHODIMP
sbFileScanQuery::AddFlaggedFileExtension(const nsAString & strExtension)
{
  mozilla::MutexAutoLock lock(m_pFlaggedFileExtensionsLock);

  nsAutoString extStr(strExtension);
  ToLowerCase(extStr);
  if (!m_FlaggedExtensions.GetEntry(extStr)) {
    nsStringHashKey *hashKey = m_FlaggedExtensions.PutEntry(extStr);
    NS_ENSURE_TRUE(hashKey, NS_ERROR_OUT_OF_MEMORY);
  }

  return NS_OK;
} //AddFlaggedFileExtension

//-----------------------------------------------------------------------------
NS_IMETHODIMP
sbFileScanQuery::GetFlaggedExtensionsFound(bool *aOutIsFound)
{
  NS_ENSURE_ARG_POINTER(aOutIsFound);

  *aOutIsFound = PR_FALSE;

  if (m_pFlaggedFileStack) {
    PRUint32 length;
    nsresult rv = m_pFlaggedFileStack->GetLength(&length);
    NS_ENSURE_SUCCESS(rv, rv);

    *aOutIsFound = length > 0;
  }

  return NS_OK;
}

//-----------------------------------------------------------------------------
/* void SetCallback (in sbIFileScanCallback pCallback); */
NS_IMETHODIMP sbFileScanQuery::SetCallback(sbIFileScanCallback *pCallback)
{
  NS_ENSURE_ARG_POINTER(pCallback);

  {
    mozilla::MutexAutoLock lock(m_pCallbackLock);

    m_pCallback = pCallback;
  }

  return NS_OK;
} //SetCallback

//-----------------------------------------------------------------------------
/* sbIFileScanCallback GetCallback (); */
NS_IMETHODIMP sbFileScanQuery::GetCallback(sbIFileScanCallback **_retval)
{
  NS_ENSURE_ARG_POINTER(_retval);

  {
    mozilla::MutexAutoLock lock(m_pCallbackLock);
    NS_IF_ADDREF(*_retval = m_pCallback);
  }

  return NS_OK;
} //GetCallback

//-----------------------------------------------------------------------------
/* PRInt32 GetFileCount (); */
NS_IMETHODIMP sbFileScanQuery::GetFileCount(PRUint32 *_retval)
{
  NS_ENSURE_ARG_POINTER(_retval);
  if (m_pFileStack) {
    m_pFileStack->GetLength(_retval);
  } else {
    // no stack, scanning never started
    *_retval = 0;
  }
  LOG("sbFileScanQuery: reporting %d files\n", *_retval);
  return NS_OK;
} //GetFileCount

//-----------------------------------------------------------------------------
/* PRInt32 GetFlaggedFileCount (); */
NS_IMETHODIMP sbFileScanQuery::GetFlaggedFileCount(PRUint32 *_retval)
{
  NS_ENSURE_ARG_POINTER(_retval);
  if (m_pFlaggedFileStack) {
    m_pFlaggedFileStack->GetLength(_retval);
  }
  else {
    // no stack, scanning never started
    *_retval = 0;
  }

  LOG("sbFileScanQuery: reporting %d flagged files\n", *_retval);
  return NS_OK;
} //GetFileCount

//-----------------------------------------------------------------------------
/* void AddFilePath (in wstring strFilePath); */
NS_IMETHODIMP sbFileScanQuery::AddFilePath(const nsAString &strFilePath)
{
  bool isFlagged = PR_FALSE;
  const nsAutoString strExtension = GetExtensionFromFilename(strFilePath);
  if (m_lastSeenExtension.IsEmpty() ||
      !m_lastSeenExtension.Equals(strExtension, CaseInsensitiveCompare)) {
    // m_lastSeenExtension could be set multiple times without lock guarded
    // in theory. However, the race is benign and in practice, it is rare.
    bool isValidExtension = VerifyFileExtension(strExtension, &isFlagged);
    if (isValidExtension) {
      m_lastSeenExtension = strExtension;
    } else if (!isValidExtension && !isFlagged) {
      LOG("sbFileScanQuery::AddFilePath, unrecognized extension: (%s) is seen\n",
           NS_LossyConvertUTF16toASCII(strExtension).get());
      return NS_OK;
    }
  }

  nsresult rv;
  nsCOMPtr<nsISupportsString> string =
    do_CreateInstance("@mozilla.org/supports-string;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = string->SetData(strFilePath);
  NS_ENSURE_SUCCESS(rv, rv);

  if (isFlagged) {
    rv = m_pFlaggedFileStack->AppendElement(string, PR_FALSE);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  else {
    rv = m_pFileStack->AppendElement(string, PR_FALSE);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  LOG("sbFileScanQuery::AddFilePath(%s)\n",
       NS_LossyConvertUTF16toASCII(strFilePath).get());
  return NS_OK;
} //AddFilePath

//-----------------------------------------------------------------------------
/* wstring GetFilePath (in PRInt32 nIndex); */
NS_IMETHODIMP sbFileScanQuery::GetFilePath(PRUint32 nIndex, nsAString &_retval)
{
  _retval = EmptyString();
  NS_ENSURE_ARG_MIN(nIndex, 0);

  PRUint32 length;
  m_pFileStack->GetLength(&length);
  if (nIndex < length) {
    nsresult rv;
    nsCOMPtr<nsISupportsString> path = do_QueryElementAt(m_pFileStack, nIndex, &rv);
    NS_ENSURE_SUCCESS(rv, rv);
    path->GetData(_retval);
  }

  return NS_OK;
} //GetFilePath

//------------------------------------------------------------------------------
/* AString getFlaggedFilePath(in PRUint32 nIndex); */
NS_IMETHODIMP
sbFileScanQuery::GetFlaggedFilePath(PRUint32 nIndex, nsAString &retVal)
{
  retVal = EmptyString();
  NS_ENSURE_ARG_MIN(nIndex, 0);

  PRUint32 length;
  m_pFlaggedFileStack->GetLength(&length);
  if (nIndex < length) {
    nsresult rv;
    nsCOMPtr<nsISupportsString> path =
      do_QueryElementAt(m_pFlaggedFileStack, nIndex, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    path->GetData(retVal);
  }

  return NS_OK;
} //getFlaggedFilePath

//-----------------------------------------------------------------------------
/* bool IsScanning (); */
NS_IMETHODIMP sbFileScanQuery::IsScanning(bool *_retval)
{
  NS_ENSURE_ARG_POINTER(_retval);

  {
    mozilla::MutexAutoLock lock(m_pScanningLock);
    *_retval = m_bIsScanning;
  }

  return NS_OK;
} //IsScanning

//-----------------------------------------------------------------------------
/* void SetIsScanning (in bool bIsScanning); */
NS_IMETHODIMP sbFileScanQuery::SetIsScanning(bool bIsScanning)
{
  {
    mozilla::MutexAutoLock lock(m_pScanningLock);
    m_bIsScanning = bIsScanning;
  }

  return NS_OK;
} //SetIsScanning

//-----------------------------------------------------------------------------
/* wstring GetLastFileFound (); */
NS_IMETHODIMP sbFileScanQuery::GetLastFileFound(nsAString &_retval)
{
  PRUint32 length;
  m_pFileStack->GetLength(&length);
  if (length > 0) {
    nsresult rv;
    nsCOMPtr<nsISupportsString> path =
      do_QueryElementAt(m_pFileStack, length - 1, &rv);
    NS_ENSURE_SUCCESS(rv, rv);
    path->GetData(_retval);
  }
  else {
    _retval.Truncate();
  }
  return NS_OK;
} //GetLastFileFound

//-----------------------------------------------------------------------------
/* wstring GetCurrentScanPath (); */
NS_IMETHODIMP sbFileScanQuery::GetCurrentScanPath(nsAString &_retval)
{
  {
    mozilla::MutexAutoLock lock(m_pCurrentPathLock);
    _retval = m_strCurrentPath;
  }
  return NS_OK;
} //GetCurrentScanPath

//-----------------------------------------------------------------------------
/* void SetCurrentScanPath (in wstring strScanPath); */
NS_IMETHODIMP sbFileScanQuery::SetCurrentScanPath(const nsAString &strScanPath)
{
  {
    mozilla::MutexAutoLock lock(m_pCurrentPathLock);
    m_strCurrentPath = strScanPath;
  }
  return NS_OK;
} //SetCurrentScanPath

//-----------------------------------------------------------------------------
/* void Cancel (); */
NS_IMETHODIMP sbFileScanQuery::Cancel()
{
  {
    mozilla::MutexAutoLock lock(m_pCancelLock);
    m_bCancel = PR_TRUE;
  }
  return NS_OK;
} //Cancel

//-----------------------------------------------------------------------------
NS_IMETHODIMP sbFileScanQuery::GetResultRangeAsURIStrings(PRUint32 aStartIndex,
                                                          PRUint32 aEndIndex,
                                                          nsIArray** _retval)
{
  PRUint32 length;
  m_pFileStack->GetLength(&length);
  NS_ENSURE_TRUE(aStartIndex < length, NS_ERROR_INVALID_ARG);
  NS_ENSURE_TRUE(aEndIndex < length, NS_ERROR_INVALID_ARG);

  if (aStartIndex == 0 && aEndIndex == length - 1) {
    NS_ADDREF(*_retval = m_pFileStack);
  } else {
    nsresult rv;
    nsCOMPtr<nsIMutableArray> array =
      do_CreateInstance("@songbirdnest.com/moz/xpcom/threadsafe-array;1", &rv);

    for (PRUint32 i = aStartIndex; i <= aEndIndex; i++) {
      nsCOMPtr<nsISupportsString> uriSpec = do_QueryElementAt(m_pFileStack, i);
      if (!uriSpec) {
        continue;
      }
      rv = array->AppendElement(uriSpec, PR_FALSE);
      NS_ENSURE_SUCCESS(rv, rv);
#if PR_LOGGING
      nsAutoString s;
      if (NS_SUCCEEDED(uriSpec->GetData(s)) && !s.IsEmpty()) {
        LOG("sbFileScanQuery:: fetched URI %s\n", NS_LossyConvertUTF16toASCII(s).get());
      }
#endif /* PR_LOGGING */
    }
    NS_ADDREF(*_retval = array);
  }
  LOG("sbFileScanQuery:: fetched URIs %d through %d\n", aStartIndex, aEndIndex);

  return NS_OK;
}

//-----------------------------------------------------------------------------
/* bool IsCancelled (); */
NS_IMETHODIMP sbFileScanQuery::IsCancelled(bool *_retval)
{
  NS_ENSURE_ARG_POINTER(_retval);
  {
    mozilla::MutexAutoLock lock(m_pCancelLock);
    *_retval = m_bCancel;
  }
  return NS_OK;
} //IsCancelled

//-----------------------------------------------------------------------------
nsString sbFileScanQuery::GetExtensionFromFilename(const nsAString &strFilename)
{
  nsAutoString str(strFilename);

  PRInt32 index = str.RFindChar(NS_L('.'));
  if (index > -1)
    return nsString(Substring(str, index + 1, str.Length() - index));

  return EmptyString();
} //GetExtensionFromFilename

//-----------------------------------------------------------------------------
bool sbFileScanQuery::VerifyFileExtension(const nsAString &strExtension,
                                            bool *aOutIsFlaggedExtension)
{
  NS_ENSURE_ARG_POINTER(aOutIsFlaggedExtension);

  *aOutIsFlaggedExtension = PR_FALSE;
  bool isValid = PR_FALSE;
  nsAutoString extString;

  { // scoped lock
    mozilla::MutexAutoLock lock(m_pExtensionsLock);

    extString = PromiseFlatString(strExtension);
    ToLowerCase(extString);
    isValid = m_Extensions.GetEntry(extString) != nsnull;
  }

  // If the extension isn't valid, check to see if it is a flagged file
  // extension and set the out param.
  if (!isValid) {
    { // scoped lock
      mozilla::MutexAutoLock lock(m_pFlaggedFileExtensionsLock);

      *aOutIsFlaggedExtension =
        m_FlaggedExtensions.GetEntry(extString) != nsnull;
    }
  }

  return isValid;
} //VerifyFileExtension

//-----------------------------------------------------------------------------
/* attribute boolean wantContentURLs; */
NS_IMETHODIMP sbFileScanQuery::
  GetWantLibraryContentURIs(bool *aWantLibraryContentURIs)
{
  NS_ENSURE_ARG_POINTER(aWantLibraryContentURIs);
  *aWantLibraryContentURIs = m_bWantLibraryContentURIs;
  return NS_OK;
} //GetWantLibraryContentURIs

//-----------------------------------------------------------------------------
NS_IMETHODIMP sbFileScanQuery::
  SetWantLibraryContentURIs(bool aWantLibraryContentURIs)
{
  m_bWantLibraryContentURIs = aWantLibraryContentURIs;
  return NS_OK;
} //SetWantLibraryContentURIs


//*****************************************************************************
//  sbFileScan Class
//*****************************************************************************
NS_IMPL_ISUPPORTS1(sbFileScan, sbIFileScan)

//------------------------------------------------------------------------------
sbFileScan::sbFileScan()
: m_ScanQueryQueueLock("sbFileScan::m_ScanQueryQueueLock")
, m_ScanQueryProcessorIsRunning(0)
, m_ThreadShouldShutdown(PR_FALSE)
, m_Finalized(PR_FALSE)
{
  MOZ_COUNT_CTOR(sbFileScan);
} //ctor

//-----------------------------------------------------------------------------
sbFileScan::~sbFileScan()
{
  MOZ_COUNT_DTOR(sbFileScan);
  nsresult rv = NS_OK;
  if (!m_Finalized) {
    rv = Finalize();
    NS_WARN_IF_FALSE(NS_SUCCEEDED(rv), "sbFileScan::Finalize failed");
  }
  NS_ASSERTION(NS_SUCCEEDED(rv),
      "Unable to shut down the query processor thread");

} //dtor

nsresult
sbFileScan::Shutdown()
{
  // Shutdown the query processor thread if it is running.
  if (m_ScanQueryProcessorIsRunning) {
    m_ThreadShouldShutdown = PR_TRUE;

    nsCOMPtr<nsIThread> currentThread = do_GetCurrentThread();
    NS_ENSURE_TRUE(currentThread, NS_ERROR_FAILURE);

    // Wait for the thread to get to a safe spot before shutting down. Shutting
    // down the thread will cause problems if it's about to proxy.
    while (m_ScanQueryProcessorIsRunning) {
      NS_ProcessPendingEvents(currentThread);
      if (m_ScanQueryProcessorIsRunning) {
        PR_Sleep(PR_MillisecondsToInterval(100));
      }
    }
  }

  return NS_OK;
}

//-----------------------------------------------------------------------------
/* void SubmitQuery (in sbIFileScanQuery pQuery); */
NS_IMETHODIMP sbFileScan::SubmitQuery(sbIFileScanQuery *pQuery)
{
  NS_ENSURE_ARG_POINTER(pQuery);
  pQuery->AddRef();

  {
    mozilla::MutexAutoLock autoQueryQueueLock(m_ScanQueryQueueLock);

    pQuery->SetIsScanning(PR_TRUE);
    m_ScanQueryQueue.push_back(pQuery);
  }

  // Start the query processor thread if needed.
  if (!m_ScanQueryProcessorIsRunning) {
    nsresult SB_UNUSED_IN_RELEASE(rv) = StartProcessScanQueriesProcessor();
    NS_WARN_IF_FALSE(NS_SUCCEEDED(rv),
        "ERROR: Could not start the query processor thread!");
  }

  return NS_OK;
} //SubmitQuery

//-----------------------------------------------------------------------------
/* void finalize(); */
NS_IMETHODIMP sbFileScan::Finalize()
{
  nsresult rv;
  if (m_Finalized) {
    NS_WARNING("sbFileScan::Finalize called more than once");
    return NS_OK;
  }
  m_Finalized = PR_TRUE;
  rv = Shutdown();
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

//-----------------------------------------------------------------------------
nsresult
sbFileScan::StartProcessScanQueriesProcessor()
{
  // Start the query processor thread if it isn't already running and if the
  // file scan service isn't shutting down.
  if (!m_ScanQueryProcessorIsRunning && !m_ThreadShouldShutdown) {
    nsresult rv;
    nsCOMPtr<nsIThreadPool> threadPoolService =
      do_GetService("@songbirdnest.com/Songbird/ThreadPoolService;1", &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIRunnable> runnable = new nsRunnableMethod_RunProcessScanQueries(this);
    NS_ENSURE_TRUE(runnable, NS_ERROR_FAILURE);

    rv = threadPoolService->Dispatch(runnable, NS_DISPATCH_NORMAL);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

//-----------------------------------------------------------------------------
void
sbFileScan::RunProcessScanQueries()
{
  PR_AtomicSet(&m_ScanQueryProcessorIsRunning, 1);

  // This method will be run on a background thread and will process any scan
  // queries as they are pushed into the process queue.
  while (PR_TRUE) {
    nsCOMPtr<sbIFileScanQuery> curScanQuery;
    {
      mozilla::MutexAutoLock queriesLock(m_ScanQueryQueueLock);

      // Ensure that there is at least one query to run and that the thread
      // should be running.
      if (m_ScanQueryQueue.size() == 0 || m_ThreadShouldShutdown) {
        // Nothing in the query queue to process, break out of the thread.
        break;
      }

      // Now get the next scan query.
      // NOTE: This query was already addref'd when it was pushed into the
      //       the query queue.
      curScanQuery = dont_AddRef(m_ScanQueryQueue.front());
      m_ScanQueryQueue.pop_front();
    }

    if (!curScanQuery) {
      // Fail safe.
      continue;
    }

    // Process this current scan query now.
    nsresult rv;
    rv = curScanQuery->SetIsScanning(PR_TRUE);
    NS_WARN_IF_FALSE(NS_SUCCEEDED(rv),
        "WARNING: Could not inform the scan query to isScanning=true!");

    rv = ScanDirectory(curScanQuery);
    NS_WARN_IF_FALSE(NS_SUCCEEDED(rv),
        "WARNING: Could not scan the current directory!");

    rv = curScanQuery->SetIsScanning(PR_FALSE);
    NS_WARN_IF_FALSE(NS_SUCCEEDED(rv),
        "WARNING: Could not inform the scan query to isScanning=false!");
  }

  PR_AtomicSet(&m_ScanQueryProcessorIsRunning, 0);
}

//-----------------------------------------------------------------------------
nsresult
sbFileScan::ScanDirectory(sbIFileScanQuery *pQuery)
{
  dirstack_t dirStack;
  fileentrystack_t fileEntryStack;
  entrystack_t entryStack;
  nsresult rv;

  PRInt32 nFoundCount = 0;

  nsCOMPtr<nsILocalFile> pFile = do_CreateInstance("@mozilla.org/file/local;1");
  nsCOMPtr<sbILibraryUtils> pLibraryUtils =
    do_GetService("@songbirdnest.com/Songbird/library/Manager;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  sbIFileScanCallback *pCallback = nsnull;
  pQuery->GetCallback(&pCallback);

  bool bSearchHidden = PR_FALSE;
  pQuery->GetSearchHidden(&bSearchHidden);

  bool bRecurse = PR_FALSE;
  pQuery->GetRecurse(&bRecurse);

  nsString strTheDirectory;
  pQuery->GetDirectory(strTheDirectory);

  bool bWantLibraryContentURIs = PR_TRUE;
  pQuery->GetWantLibraryContentURIs(&bWantLibraryContentURIs);

  rv = pFile->InitWithPath(strTheDirectory);
  if(NS_FAILED(rv)) return rv;

  bool bFlag = PR_FALSE;
  pFile->IsDirectory(&bFlag);

  if(pCallback)
  {
    pCallback->OnFileScanStart();
  }

  if(bFlag)
  {
    sbIDirectoryEnumerator * pDirEntries;
    rv = sbGetDirectoryEntries(pFile, &pDirEntries);
    NS_ENSURE_SUCCESS(rv, rv);

    bool keepRunning = !m_ThreadShouldShutdown;

    while(keepRunning)
    {
      // Allow us to get the hell out of here.
      bool cancel = PR_FALSE;
      pQuery->IsCancelled(&cancel);

      if (cancel) {
        break;
      }

      bool bHasMore = PR_FALSE;
      rv = pDirEntries->HasMoreElements(&bHasMore);
      nsCOMPtr<nsIFile> pEntry;
      if(NS_SUCCEEDED(rv) && bHasMore) {
        rv = pDirEntries->GetNext(getter_AddRefs(pEntry));
      }

      if(NS_SUCCEEDED(rv) && pEntry)
      {
        bool bIsFile = PR_FALSE, bIsDirectory = PR_FALSE, bIsHidden = PR_FALSE;
        pEntry->IsFile(&bIsFile);
        pEntry->IsDirectory(&bIsDirectory);
        pEntry->IsHidden(&bIsHidden);

        // If it's special, we always want to skip it. This causes files with a
        // dot prefix to be treated as hidden which they are on Mac and Linux.
        // Windows will include dot prefixed file as it did before the
        // file scan reimplementation in bug 24478
        bool isSpecial = PR_FALSE;
        pEntry->IsSpecial(&isSpecial);

#if XP_MACOSX
        // If the path begins with a ._ we need to ignore on Mac. The Mac
        // API's filter out these files as that designates that they are
        // temporary files.
        nsString fileName;
        rv = pEntry->GetLeafName(fileName);
        NS_ENSURE_SUCCESS(rv, rv);
        isSpecial |= (fileName.Length() >= 2 &&
                      fileName[0] == PRUnichar('.') &&
                      fileName[1] == PRUnichar('_'));
#endif
        if(!isSpecial && (!bIsHidden || bSearchHidden))
        {
          if(bIsFile)
          {
            // Get a library content URI for the file.
            nsCOMPtr<nsIURI> pURI;
            if (bWantLibraryContentURIs) {
              rv = pLibraryUtils->GetFileContentURI(pEntry,
                                                    getter_AddRefs(pURI));
            } else {
              rv = NS_NewFileURI(getter_AddRefs(pURI), pEntry);
            }

            // Get the file URI spec.
            nsCAutoString spec;
            if (NS_SUCCEEDED(rv)) {
              rv = pURI->GetSpec(spec);
              LOG("sbFileScan::ScanDirectory (C++) found spec: %s\n",
                   spec.get());
            }

            // Add the file path to the query.
            if (NS_SUCCEEDED(rv)) {
              nsString strPath = NS_ConvertUTF8toUTF16(spec);
              pQuery->AddFilePath(strPath);
              nFoundCount += 1;

              if(pCallback)
              {
                pCallback->OnFileScanFile(strPath, nFoundCount);
              }
            }
          }
          else if(bIsDirectory && bRecurse)
          {
            sbIDirectoryEnumerator * pMoreEntries;
            rv = CallCreateInstance(SB_DIRECTORYENUMERATOR_CONTRACTID,
                                    &pMoreEntries);
            NS_ENSURE_SUCCESS(rv, rv);

            rv = pMoreEntries->SetFilesOnly(PR_FALSE);
            NS_ENSURE_SUCCESS(rv, rv);
            rv = pMoreEntries->SetMaxDepth(1);
            NS_ENSURE_SUCCESS(rv, rv);
            rv = pMoreEntries->Enumerate(pEntry);
            NS_ENSURE_SUCCESS(rv, rv);

            if(pEntry)
            {
              dirStack.push_back(pDirEntries);
              fileEntryStack.push_back(pEntry);
              entryStack.push_back(pEntry);

              pDirEntries = pMoreEntries;
            }
          }
        }
      }
      else
      {
        if(dirStack.size())
        {
          NS_IF_RELEASE(pDirEntries);

          pDirEntries = dirStack.back();

          dirStack.pop_back();
          fileEntryStack.pop_back();
          entryStack.pop_back();
        }
        else
        {
          if(pCallback)
          {
            pCallback->OnFileScanEnd();
          }

          NS_IF_RELEASE(pCallback);
          NS_IF_RELEASE(pDirEntries);

          return NS_OK;
        }
      }

      // Yield.
      PR_Sleep(PR_MillisecondsToInterval(0));

      // Check thread shutdown flag since it's possible for our thread
      // to be in shutdown without the query being cancelled.
      keepRunning = !m_ThreadShouldShutdown;
    }
    NS_IF_RELEASE(pDirEntries);

  }
  else if(NS_SUCCEEDED(pFile->IsFile(&bFlag)) && bFlag)
  {
    pQuery->AddFilePath(strTheDirectory);
  }

  if(pCallback)
  {
    pCallback->OnFileScanEnd();
  }

  NS_IF_RELEASE(pCallback);

  dirstack_t::iterator it = dirStack.begin();
  for(; it != dirStack.end(); ++it) {
    NS_IF_RELEASE(*it);
  }

  dirStack.clear();
  fileEntryStack.clear();
  entryStack.clear();

  return NS_OK;
} //ScanDirectory
