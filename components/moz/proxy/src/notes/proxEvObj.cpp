
/*
 * Object representing a single interface implemented on a proxied object.
 * This object is maintained in a singly-linked list from the associated
 * "parent" nsProxyObject.
 */
class nsProxyEventObject : protected nsAutoXPTCStub
{
// public:
    // NS_DECL_ISUPPORTS
public:
  virtual nsresult QueryInterface(REFNSIID aIID, void** aInstancePtr);
  virtual nsrefcnt AddRef(void);
  virtual nsrefcnt Release(void);
protected:
  nsAutoRefCnt mRefCnt;
  nsAutoOwningThread _mOwningThread;
public:

    // call this method and return result
    virtual nsresult CallMethod(PRUint16 methodIndex,
                                const XPTMethodDescriptor* info,
                                nsXPTCMiniVariant* params);

    nsProxyEventClass* GetClass() const { return mClass; }
    nsISomeInterface* GetProxiedInterface() const { return mRealInterface; }
    nsIEventTarget* GetTarget() const { return mProxyObject->GetTarget(); }
    PRInt32 GetProxyType() const { return mProxyObject->GetProxyType(); } 

    nsresult convertMiniVariantToVariant(const XPTMethodDescriptor *methodInfo,
                                         nsXPTCMiniVariant *params,
                                         nsXPTCVariant **fullParam,
                                         uint8 *outParamCount);

    nsProxyEventObject(nsProxyObject *aParent,
                       nsProxyEventClass *aClass,
                       already_AddRefed<nsISomeInterface> aRealInterface,
                       nsresult *rv);

    // AddRef, but you must be holding the global POM lock
    nsrefcnt LockedAddRef();
    friend class nsProxyObject;

private:
    ~nsProxyEventObject();
    // Member ordering is important: See note in the destructor.
    nsProxyEventClass          *mClass;
    nsCOMPtr<nsProxyObject>     mProxyObject;
    nsCOMPtr<nsISomeInterface>  mRealInterface;
    // Weak reference, maintained by the parent nsProxyObject
    nsProxyEventObject         *mNext;
};


nsProxyEventObject::nsProxyEventObject(nsProxyObject *aParent,
                            nsProxyEventClass* aClass,
                            already_AddRefed<nsISomeInterface> aRealInterface,
                            nsresult *rv)
                            : mClass(aClass),
                              mProxyObject(aParent),
                              mRealInterface(aRealInterface),
                              mNext(nsnull)
{
    *rv = InitStub(aClass->GetProxiedIID());
}

nsProxyEventObject::~nsProxyEventObject()
{
    // This destructor must *not* be called within the POM lock
    // XXX assert this!

    // mRealInterface must be released before mProxyObject so that the last
    // release of the proxied object is proxied to the correct thread.
    // See bug 337492.
    mRealInterface = nsnull;
}


//
// nsISupports implementation...
//

nsrefcnt
nsProxyEventObject::AddRef()
{
    nsAutoLock lock(nsProxyObjectManager::GetInstance()->GetLock());
    return LockedAddRef();
}

nsrefcnt
nsProxyEventObject::LockedAddRef()
{
    ++mRefCnt;
    NS_LOG_ADDREF(this, mRefCnt, "nsProxyEventObject", sizeof(nsProxyEventObject));
    return mRefCnt;
}

nsrefcnt
nsProxyEventObject::Release(void)
{
    {
        nsAutoLock lock(nsProxyObjectManager::GetInstance()->GetLock());
        NS_PRECONDITION(0 != mRefCnt, "dup release");
/*
        PR_BEGIN_MACRO
        if (!(0 != mRefCnt)) {
            NS_DebugBreak(NS_DEBUG_ASSERTION, "dup release", 
                            #0 != mRefCnt, __FILE__, __LINE__);
        }
        PR_END_MACRO
 */
        --mRefCnt;
        NS_LOG_RELEASE(this, mRefCnt, "nsProxyEventObject");

        if (mRefCnt)
            return mRefCnt;

        mProxyObject->LockedRemove(this);
    }

    // call the destructor outside of the lock so that we aren't holding the
    // lock when we release the object
    PR_BEGIN_MACRO
        delete (this);
    PR_END_MACRO
    return 0;
}

nsresult
nsProxyEventObject::QueryInterface(REFNSIID aIID, void** aInstancePtr)
{
    if( aIID.Equals(GetClass()->GetProxiedIID()) )
    {
        *aInstancePtr = static_cast<nsISupports*>(mXPTCStub);
        AddRef();
        return NS_OK;
    }
        
    return mProxyObject->QueryInterface(aIID, aInstancePtr);
}

