// irset_type.hxx
#ifndef IB_IRSET_TYPE_HXX
#define IB_IRSET_TYPE_HXX

class atomicIRSET;
class _IRSET;

#undef IB_USE_COW_IRSET

#ifdef IB_USE_COW_IRSET
using IRSET_TYPE = _IRSET;
#else
using IRSET_TYPE = atomicIRSET;
#endif

using PIRSET = IRSET_TYPE*;

#endif

