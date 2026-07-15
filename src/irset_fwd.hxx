#ifndef IB_IRSET_FWD_HXX
#define IB_IRSET_FWD_HXX

class atomicIRSET;
class _IRSET;

#if defined(IB_USE_COW_IRSET)

using IRSET_TYPE = _IRSET;
using PIRSET = _IRSET*;

#else

using IRSET_TYPE = atomicIRSET;
using PIRSET = atomicIRSET*;

#endif

#endif // IB_IRSET_FWD_HXX

