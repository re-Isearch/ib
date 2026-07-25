// irset_type.hxx
#ifndef IB_IRSET_TYPE_HXX
#define IB_IRSET_TYPE_HXX

#if 0

/*
 * Forward declarations are sufficient for aliases and pointer aliases.
 */
class atomicIRSET;
class _IRSET;

#ifdef IB_USE_COW_IRSET

using IRSET  = _IRSET;
using PIRSET = _IRSET*;

#else

using IRSET  = atomicIRSET;
using PIRSET = atomicIRSET*;

#endif

/*
 * Explicit implementation-type pointers.
 *
 * Use these only when code genuinely requires one particular
 * implementation rather than the configured public IRSET type.
 */
using PatomicIRSET = atomicIRSET*;
using P_IRSET      = _IRSET*;


#else


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

#endif

