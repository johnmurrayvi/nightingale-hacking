
nsProxyObject::nsProxyObject(nsIEventTarget *target, PRInt32 proxyType,
                             nsISupports *realObject) 
                : mProxyType(proxyType),
                  mTarget(target),
                  mRealObject(realObject),
                  mFirst(nsnull)
{
    MOZ_COUNT_CTOR(nsProxyObject);

    nsProxyObjectManager *pom = nsProxyObjectManager::GetInstance();
    NS_ASSERTION(pom, "Creating a proxy without a global proxy-object-manager.");
    pom->AddRef();
}

nsProxyObject::~nsProxyObject()
{
    // Proxy the release of mRealObject to protect against it being deleted on
    // the wrong thread.
    nsISupports *doomed = nsnull;
    mRealObject.swap(doomed);
    NS_ProxyRelease(mTarget, doomed);

    MOZ_COUNT_DTOR(nsProxyObject);
}

NS_IMETHODIMP_(nsrefcnt)
nsProxyObject::AddRef()
{
    nsAutoLock lock(nsProxyObjectManager::GetInstance()->GetLock());
    return LockedAddRef();
}

NS_IMETHODIMP_(nsrefcnt)
nsProxyObject::Release()
{
    nsAutoLock lock(nsProxyObjectManager::GetInstance()->GetLock());
    return LockedRelease();
}

nsrefcnt
nsProxyObject::LockedAddRef()
{
    ++mRefCnt;
    NS_LOG_ADDREF(this, mRefCnt, "nsProxyObject", sizeof(nsProxyObject));
    return mRefCnt;
}

nsrefcnt
nsProxyObject::LockedRelease()
{
    NS_PRECONDITION(0 != mRefCnt, "dup release");
    --mRefCnt;
    NS_LOG_RELEASE(this, mRefCnt, "nsProxyObject");
    if (mRefCnt)
        return mRefCnt;

    nsProxyObjectManager *pom = nsProxyObjectManager::GetInstance();
    pom->LockedRemove(this);

    nsAutoUnlock unlock(pom->GetLock());
    delete this;
    pom->Release();

    return 0;
}

NS_IMETHODIMP
nsProxyObject::QueryInterface(REFNSIID aIID, void **aResult)
{
    if (aIID.Equals(GetIID())) {
        *aResult = this;
        AddRef();
        return NS_OK;
    }

    if (aIID.Equals(NS_GET_IID(nsISupports))) {
        *aResult = static_cast<nsISupports*>(this);
        AddRef();
        return NS_OK;
    }

    nsProxyObjectManager *pom = nsProxyObjectManager::GetInstance();
    NS_ASSERTION(pom, "Deleting a proxy without a global proxy-object-manager.");

    nsAutoLock lock(pom->GetLock());
    return LockedFind(aIID, aResult);
}

nsresult
nsProxyObject::LockedFind(REFNSIID aIID, void **aResult)
{
    // This method is only called when the global lock is held.
    // XXX assert this

    nsProxyEventObject *peo;

    for (peo = mFirst; peo; peo = peo->mNext) {
        if (peo->GetClass()->GetProxiedIID().Equals(aIID)) {
            *aResult = static_cast<nsISupports*>(peo->mXPTCStub);
            peo->LockedAddRef();
            return NS_OK;
        }
    }

    nsProxyEventObject *newpeo;

    // Both GetClass and QueryInterface call out to XPCOM, so we unlock for them
    {
        nsProxyObjectManager* pom = nsProxyObjectManager::GetInstance();
        nsAutoUnlock unlock(pom->GetLock());

        nsProxyEventClass *pec;
        nsresult rv = pom->GetClass(aIID, &pec);
        if (NS_FAILED(rv))
            return rv;

        nsISomeInterface* newInterface;
        rv = mRealObject->QueryInterface(aIID, (void**) &newInterface);
        if (NS_FAILED(rv))
            return rv;

        newpeo = new nsProxyEventObject(this, pec, 
                       already_AddRefed<nsISomeInterface>(newInterface), &rv);
        if (!newpeo) {
            NS_RELEASE(newInterface);
            return NS_ERROR_OUT_OF_MEMORY;
        }

        if (NS_FAILED(rv)) {
            delete newpeo;
            return rv;
        }
    }

    // Now that we're locked again, check for races by repeating the
    // linked-list check.
    for (peo = mFirst; peo; peo = peo->mNext) {
        if (peo->GetClass()->GetProxiedIID().Equals(aIID)) {
            delete newpeo;
            *aResult = static_cast<nsISupports*>(peo->mXPTCStub);
            peo->LockedAddRef();
            return NS_OK;
        }
    }

    newpeo->mNext = mFirst;
    mFirst = newpeo;

    newpeo->LockedAddRef();

    *aResult = static_cast<nsISupports*>(newpeo->mXPTCStub);
    return NS_OK;
}

void
nsProxyObject::LockedRemove(nsProxyEventObject *peo)
{
    nsProxyEventObject **i;
    for (i = &mFirst; *i; i = &((*i)->mNext)) {
        if (*i == peo) {
            *i = peo->mNext;
            return;
        }
    }
    NS_ERROR("Didn't find nsProxyEventObject in nsProxyObject chain!");
}
