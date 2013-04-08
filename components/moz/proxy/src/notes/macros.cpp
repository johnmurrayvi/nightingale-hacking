/* macros */


/*********************
 * NS_DECL_ISUPPORTS *
 *********************/

/*
 * Declare the reference count variable and the implementations of the
 * AddRef and QueryInterface methods.
 */

#define NS_DECL_ISUPPORTS                                                     \
public:                                                                       \
  NS_IMETHOD QueryInterface(REFNSIID aIID,                                    \
                            void** aInstancePtr);                             \
  NS_IMETHOD_(nsrefcnt) AddRef(void);                                         \
  NS_IMETHOD_(nsrefcnt) Release(void);                                        \
protected:                                                                    \
  nsAutoRefCnt mRefCnt;                                                       \
  NS_DECL_OWNINGTHREAD                                                        \
public:


/***********************
 * NS_DECL_NSIRUNNABLE *
 ***********************/

/* Use this macro when declaring classes that implement this interface. */
#define NS_DECL_NSIRUNNABLE \
  NS_SCRIPTABLE NS_IMETHOD Run(void); 


/*
 * Generic API modifiers which return the standard XPCOM nsresult type
 */
#define NS_IMETHOD          NS_IMETHOD_(nsresult)
#define NS_IMETHODIMP       NS_IMETHODIMP_(nsresult)

#define NS_IMETHOD_(type) virtual type
#define NS_IMETHODIMP_(type) type



/******************************************
 ***** BIGGEST ISSUE IS THE FOLLOWING *****
 ******************************************/

#ifdef MOZILLA_INTERNAL_API
#  define NS_COM_GLUE NS_COM
   /*
     The frozen string API has different definitions of nsAC?String
     classes than the internal API. On systems that explicitly declare
     dllexport symbols this is not a problem, but on ELF systems
     internal symbols can accidentally "shine through"; we rename the
     internal classes to avoid symbol conflicts.
   */
#  define nsAString nsAString_internal
#  define nsACString nsACString_internal
#else
#  ifdef HAVE_VISIBILITY_ATTRIBUTE
#    define NS_COM_GLUE NS_VISIBILITY_HIDDEN
#  else
#    define NS_COM_GLUE
#  endif
#endif


/******************************
 ***** COULD BE USEFUL... *****
 ******************************/

/*
 * Often you have to cast an implementation pointer, e.g., |this|, to an
 * |nsISupports*|, but because you have multiple inheritance, a simple cast
 * is ambiguous.  One could simply say, e.g., (given a base |nsIBase|),
 * |static_cast<nsIBase*>(this)|; but that disguises the fact that what
 * you are really doing is disambiguating the |nsISupports|.  You could make
 * that more obvious with a double cast, e.g., 
 * |static_cast<nsISupports*> (* static_cast<nsIBase*>(this))|, 
 * but that is bulky and harder to read...
 *
 * The following macro is clean, short, and obvious.  In the example above,
 * you would use it like this: |NS_ISUPPORTS_CAST(nsIBase*, this)|.
 */

#define NS_ISUPPORTS_CAST(__unambiguousBase, __expr) \
  static_cast<nsISupports*>(static_cast<__unambiguousBase>(__expr))



/**************
 * NS_RELEASE *
 **************/

/**
 * Macro for releasing a reference to an interface.
 * @param _ptr The interface pointer.
 */
#define NS_RELEASE(_ptr)                                                      \
  PR_BEGIN_MACRO                                                              \
    (_ptr)->Release();                                                        \
    (_ptr) = 0;                                                               \
  PR_END_MACRO



/************************
 * NS_DECL_OWNINGTHREAD *
 ************************/

////////////////////////////////////////////////////////////////////////////////
// Macros to help detect thread-safety:

#if defined(NS_DEBUG) && !defined(XPCOM_GLUE_AVOID_NSPR)

class nsAutoOwningThread {
public:
    nsAutoOwningThread() { mThread = PR_GetCurrentThread(); }
    void *GetThread() const { return mThread; }

private:
    void *mThread;
};

#define NS_DECL_OWNINGTHREAD            nsAutoOwningThread _mOwningThread;
#define NS_ASSERT_OWNINGTHREAD(_class) \
  NS_CheckThreadSafe(_mOwningThread.GetThread(), #_class " not thread-safe")

#else // !NS_DEBUG

#define NS_DECL_OWNINGTHREAD            /* nothing */
#define NS_ASSERT_OWNINGTHREAD(_class)  ((void)0)

#endif // NS_DEBUG


/* nsISupportsUtils.h */

/**
 * Macro for instantiating a new object that implements nsISupports.
 * Note that you can only use this if you adhere to the no arguments
 * constructor com policy (which you really should!).
 * @param _result Where the new instance pointer is stored
 * @param _type The type of object to call "new" with.
 */
#define NS_NEWXPCOM(_result,_type)                                            \
  PR_BEGIN_MACRO                                                              \
    _result = new _type();                                                    \
  PR_END_MACRO

/**
 * Macro for deleting an object that implements nsISupports.
 * @param _ptr The object to delete.
 */
#define NS_DELETEXPCOM(_ptr)                                                  \
  PR_BEGIN_MACRO                                                              \
    delete (_ptr);                                                            \
  PR_END_MACRO

/**
 * Macro for adding a reference to an interface.
 * @param _ptr The interface pointer.
 */
#define NS_ADDREF(_ptr) \
  (_ptr)->AddRef()

/**
 * Macro for adding a reference to this. This macro should be used
 * because NS_ADDREF (when tracing) may require an ambiguous cast
 * from the pointers primary type to nsISupports. This macro sidesteps
 * that entire problem.
 */
#define NS_ADDREF_THIS() \
  AddRef()



/* nsDebug.h */

/**
 * Test a precondition for truth. If the expression is not true then
 * trigger a program failure.
 */
#define NS_PRECONDITION(expr, str)                            \
  PR_BEGIN_MACRO                                              \
    if (!(expr)) {                                            \
      NS_DebugBreak(NS_DEBUG_ASSERTION, str, #expr, __FILE__, __LINE__); \
    }                                                         \
  PR_END_MACRO

/**
 * Test an assertion for truth. If the expression is not true then
 * trigger a program failure.
 */
#define NS_ASSERTION(expr, str)                               \
  PR_BEGIN_MACRO                                              \
    if (!(expr)) {                                            \
      NS_DebugBreak(NS_DEBUG_ASSERTION, str, #expr, __FILE__, __LINE__); \
    }                                                         \
  PR_END_MACRO


/* nsXPCOM.h */

/**
 * Print a runtime assertion. This function is available in both debug and
 * release builds.
 * 
 * @note Based on the value of aSeverity and the XPCOM_DEBUG_BREAK
 * environment variable, this function may cause the application to
 * print the warning, print a stacktrace, break into a debugger, or abort
 * immediately.
 *
 * @param aSeverity A NS_DEBUG_* value
 * @param aStr   A readable error message (ASCII, may be null)
 * @param aExpr  The expression evaluated (may be null)
 * @param aFile  The source file containing the assertion (may be null)
 * @param aLine  The source file line number (-1 indicates no line number)
 */
XPCOM_API(void)
NS_DebugBreak(PRUint32 aSeverity,
              const char *aStr, const char *aExpr,
              const char *aFile, PRInt32 aLine);

/**
 * Log a stacktrace when an XPCOM object's refcount is incremented or
 * decremented. Processing tools can use the stacktraces printed by these
 * functions to identify objects that were leaked due to XPCOM references.
 *
 * @param aPtr          A pointer to the concrete object
 * @param aNewRefCnt    The new reference count.
 * @param aTypeName     The class name of the type
 * @param aInstanceSize The size of the type
 */
XPCOM_API(void)
NS_LogAddRef(void *aPtr, nsrefcnt aNewRefCnt,
             const char *aTypeName, PRUint32 aInstanceSize);

XPCOM_API(void)
NS_LogRelease(void *aPtr, nsrefcnt aNewRefCnt, const char *aTypeName);


