template <class T>
class nsProxyEventKey
{
public:
    nsProxyEventKey(void *rootObjectKey, void *targetKey, PRInt32 proxyType);


protected:
    void *mRootObjectKey;
    void *mTargetKey;
    PRInt32 mProxyType;
}