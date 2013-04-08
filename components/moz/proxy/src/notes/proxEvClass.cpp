


// LIFETIME_CACHE will cache class for the entire cyle of the application.
#define LIFETIME_CACHE

static uint32 zero_methods_descriptor;

nsProxyEventClass::nsProxyEventClass(REFNSIID aIID, nsIInterfaceInfo* aInfo)
                              : mIID(aIID), mInfo(aInfo), mDescriptors(NULL)
{
    uint16 methodCount;
    if(NS_SUCCEEDED(mInfo->GetMethodCount(&methodCount))) {
        if (methodCount) {
            int wordCount = (methodCount/32)+1;
            if (NULL != (mDescriptors = new uint32[wordCount])) {
                memset(mDescriptors, 0, wordCount * sizeof(uint32));
            }
        } else {
            mDescriptors = &zero_methods_descriptor;
        }
    }
}

nsProxyEventClass::~nsProxyEventClass()
{
    if (mDescriptors && mDescriptors != &zero_methods_descriptor)
        delete [] mDescriptors;
}
