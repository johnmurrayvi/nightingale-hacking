

class nsProxyCallCompletedEvent : public nsRunnable
{
public:
    nsProxyCallCompletedEvent(nsProxyObjectCallInfo *info);

// NS_DECL_NSIRUNNABLE = NS_SCRIPTABLE NS_IMETHOD Run(void);
// /* Use this macro when declaring classes that implement this interface. */
// #define NS_DECL_NSIRUNNABLE \
//         NS_SCRIPTABLE NS_IMETHOD Run(void); 

// #define NS_IMETHOD          NS_IMETHOD_(nsresult)
// #define NS_IMETHOD_(type) virtual type
// NS_IMETHOD = virtual nsresult

// nsIRunnable.h: NS_DECL_NSIRUNNABLE
// nscore.h: NS_IMETHOD, NS_IMETHOD_(type), NS_SCRITPABLE, NS_IMETHODIMP

    // NS_DECL_NSIRUNNABLE
    virtual nsresult Run(void);

    // NS_IMETHOD QueryInterface(REFNSIID aIID, void **aResult);
    virtual nsresult QueryInterface(REFNSIID aIID, void **aResult);

private:
        nsProxyObjectCallInfo *mInfo;
};


// NS_IMETHODIMP = NS_IMETHODIMP_(nsresult)
// #define NS_IMETHODIMP_(type) type __stdcall
nsresult
nsProxyCallCompletedEvent::Run()
{
    NS_ASSERTION(mInfo, "no info");
    mInfo->SetCompleted();
    return NS_OK;
}


//-----------------------------------------------------------------------------
nsresult
nsProxyObject::nsProxyObjectDestructorEvent::Run()
{
    delete mDoomed;
    return NS_OK;
}
//-----------------------------------------------------------------------------
