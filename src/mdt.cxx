/* Copyright (c) 2020-21 Project re-Isearch and its contributors: See CONTRIBUTORS.
It is made available and licensed under the Apache 2.0 license: see LICENSE */
#pragma ident  "@(#)mdt.cxx"

const int MaxMDTInstances = 20; // 100000;


/************************************************************************
************************************************************************/

/*-@@@
File:		mdt.cxx
Version:	1.00
Description:	Class MDT - Multiple Document Table
Original Idea:	Nassib Nassar, nrn@cnidr.org
Author:		Edward C. Zimmermann
@@@-*/

#include <stdlib.h>
#include <string.h>
#include <limits>

#if defined(_MSDOS) || defined(_WIN32)
#include <io.h>
#include <process.h>
#else
#include <unistd.h>
#include <sys/types.h>
#endif
#include <fcntl.h>
#include <sys/stat.h>
#include "common.hxx"
#include "mdt.hxx"

#if defined(SOLARIS)
#include <sys/byteorder.h>
#include <iomanip.h>
#elif defined (BSD) || defined(LINUX)
#include <arpa/inet.h>
#else
#undef nthol
#undef htonl
#define  ntohl(x) (x)
#define  htonl(x) (x)
/*
extern   uint32_t htonl(uint32_t);
extern   uint32_t ntohl(uint32_t);
*/
#endif
#include <errno.h>
#include "mmap.hxx"
#include "index.hxx"
#include "fpt.hxx"

#include <iomanip>

#ifdef _WIN32
# define EIDRM  82              /* Identifier removed */
#endif

#define PORTABLE_MDT 0

#include <unistd.h>

#if USE_MDTHASHTABLE
MDTHASHTABLE *_globalMDTHashTable = NULL;
#endif



#if PORTABLE_MDT
# ifdef O_BUILD_IB64
#  define NTOHL(_x) ntohll(_x)
#  define HTONL(_x) htonll(_x)
# else
#  define NTOHL(_x) ntohl(_x)
#  define HTONL(_x) htonl(_x)
# endif
#else
# undef  NTOHL
# define NTOHL(_x) (_x)
# undef  HTONL
# define HTONL(_x) (_x)
#endif


#define SIZEOF_MAGIC 16 /* See mdtrec.cxx */

static UINT8 MdtMaximumFileOffset()
{
#ifdef _WIN32
  return MAX_UINT8 >> 1;
#else
  return static_cast<UINT8>(std::numeric_limits<off_t>::max());
#endif
}

static bool GetStreamLength(FILE *fp, UINT8 *length)
{
  if (fp == NULL || length == NULL)
    return false;
#ifdef _WIN32
  const INT8 current = _ftelli64(fp);
  if (current < 0 || _fseeki64(fp, 0, SEEK_END) != 0)
    return false;
  const INT8 end = _ftelli64(fp);
  const bool restored = _fseeki64(fp, current, SEEK_SET) == 0;
#else
  const off_t current = ftello(fp);
  if (current < 0 || fseeko(fp, 0, SEEK_END) != 0)
    return false;
  const off_t end = ftello(fp);
  const bool restored = fseeko(fp, current, SEEK_SET) == 0;
#endif
  if (end < 0 || !restored)
    return false;
  *length = static_cast<UINT8>(end);
  return true;
}

static bool GetFdLength(int fd, UINT8 *length)
{
  if (fd < 0 || length == NULL)
    return false;
#ifdef _WIN32
  const INT8 current = _lseeki64(fd, 0, SEEK_CUR);
  if (current < 0)
    return false;
  const INT8 end = _lseeki64(fd, 0, SEEK_END);
  const bool restored = _lseeki64(fd, current, SEEK_SET) >= 0;
#else
  const off_t current = lseek(fd, 0, SEEK_CUR);
  if (current < 0)
    return false;
  const off_t end = lseek(fd, 0, SEEK_END);
  const bool restored = lseek(fd, current, SEEK_SET) >= 0;
#endif
  if (end < 0 || !restored)
    return false;
  *length = static_cast<UINT8>(end);
  return true;
}

static bool TruncateMdtFile(int fd, UINT8 length)
{
#ifdef _WIN32
  if (length > (MAX_UINT8 >> 1))
    return false;
  return _chsize_s(fd, static_cast<INT8>(length)) == 0;
#else
  const off_t nativeLength = static_cast<off_t>(length);
  if (nativeLength < 0 || static_cast<UINT8>(nativeLength) != length)
    return false;
  return ftruncate(fd, nativeLength) == 0;
#endif
}

static size_t CalculateMdtCapacity()
{
  UINT8 capacity = static_cast<UINT8>(MdtRecordIdCapacity);
  const UINT8 maxOffset = MdtMaximumFileOffset();
  if (maxOffset < SIZEOF_MAGIC)
    return 0;

  const UINT8 byFile =
    (maxOffset - SIZEOF_MAGIC + 1) / static_cast<UINT8>(sizeof(MDTREC));
  if (capacity > byFile)
    capacity = byFile;

  const UINT8 bySizeT = static_cast<UINT8>(std::numeric_limits<size_t>::max());
  if (capacity > bySizeT)
    capacity = bySizeT;

  return static_cast<size_t>(capacity);
}

const size_t MDT_CAPACITY = CalculateMdtCapacity();

_index_id_t MDT::GetCapacity() { return MDT_CAPACITY; }

#define _NextGlobal(_c) ((_c).GetGlobalFileStart()+(_c).GetLocalRecordEnd() + 1)

int _IB_MDT_SEED = 5039;
#define GROWTH_FACTOR(_x) ((_x)*3 + _IB_MDT_SEED)

/* The cache stores a local record id plus deletion state, never a virtual id. */
static const _index_id_t DELETED_BITS =
  static_cast<_index_id_t>(0xFULL) << (sizeof(_index_id_t) * 8U - 4U);
static const _index_id_t INDEX_BITS = _index_mask;

#define DELETED_MASK(_x)    (((_x) & DELETED_BITS) ? true : false)
#define INDEX_MASK(_x)      ((_x) & INDEX_BITS)
#define SET_DELETE_BITS(_x) ((_x) |= DELETED_BITS)
#define CLR_DELETE_BITS(_x) ((_x) &= INDEX_BITS)
#define SET_DELETE_STATE(_x, _y)  ((_y) ? SET_DELETE_BITS(_x) : CLR_DELETE_BITS(_x))

// One at a time hash:
static UINT4 KeyHash32(const char *key, size_t len=0)
{
  UINT4      hash = 0;
  if (len == 0) len = strlen(key);
  // Max. key length is DocumentKeySize
  for (size_t i = 0; i < len && i<=DocumentKeySize; ++i)
    {
      hash += (BYTE)key[i]; hash += hash << 10; hash ^= hash >> 6;
    }
  hash += hash << 3; hash ^= hash >> 11; hash += hash << 15;
  return hash;
}

static inline UINT4 KeyHash32(const STRING& Key)
{
  return KeyHash32(Key.c_str(), Key.GetLength());
}

//
// GpIndex *
// is a pointer to an array of GPRECs sorted by GpStart
//
/// NOTE: We want to move Hash to GPREC from KEYREC
//
class GPREC {
 public:
  GPREC ()       { Clear(); }
  void           Clear() {  GpStart = GpEnd = 0; Index = 0; }
  GPTYPE         GpStart;
  GPTYPE         GpEnd;
  SRCH_DATE      Date;
  _index_id_t    Index; // NOTE: DOUBLE of _index_id_t
};

// KeyIndex *
// is a pointer to an arrary of KEYRECs sorted by Key
//
class KEYREC {
 public:
  KEYREC()      { Clear(); }
  void  Clear() {  Hash = 0; Index=0; Key[0] = '\0'; }
  CHR            Key[DocumentKeySize];
  UINT4          Hash;
  _index_id_t    Index;
};

//
//
// Sorted by keys and then the list..
// This then can map Index-> Order if sorted by Key.
//
// KeySortTable
class KEYSORT {
  public:
   KEYSORT()      { Position=0;}
   _index_id_t      Position; // This is the order
};


bool MDT::BuildKeySortTable()
{
  size_t entries = 0;

  if (useIndexMap)
    {
      message_log(LOG_ERROR,
                  "MDT can't build the key sort table while tables are mapped");
      return false;
    }

  if (!KeyIndexSorted)
    SortKeyIndex();

  delete[] KeySortTable;
  KeySortTable = NULL;

  if (TotalEntries == 0)
    return true;

  try
    {
      KeySortTable = new KEYSORT[TotalEntries];
    }
  catch (...)
    {
      message_log(LOG_PANIC | LOG_ERRNO,
                  "Can't allocate key sort table for %llu records",
                  static_cast<unsigned long long>(TotalEntries));
      return false;
    }

  for (size_t i = 0; i < TotalEntries; ++i)
    {
      const size_t index = static_cast<size_t>(
        INDEX_MASK(NTOHL(KeyIndex[i].Index)));
      if (index == 0)
        message_log(LOG_WARN, "Undefined key index[%llu]",
                    static_cast<unsigned long long>(i));
      else if (index > TotalEntries)
        message_log(LOG_PANIC,
                    "MDT SortTable/KeyIndex[%llu] invalid (%llu>%llu)",
                    static_cast<unsigned long long>(i),
                    static_cast<unsigned long long>(index),
                    static_cast<unsigned long long>(TotalEntries));
      else
        {
          KeySortTable[index - 1].Position = static_cast<_index_id_t>(i);
          ++entries;
        }
    }
  return entries == TotalEntries;
}



// Uses the KeySortTable
// Takes an index and returns the position in the sort
//
//
// KeySortPosition(idx1) > KeySortPosition(idx2)
// means that
// the Key of idx1 is lexi sorted > the key of idx2
//
size_t MDT::KeySortPosition(_index_id_t Idx) const
{
  size_t index = (size_t)INDEX_MASK(Idx);
  if (index > TotalEntries || KeySortTable == NULL || index <= 0) return 0;
  return KeySortTable[index-1].Position;
}

typedef MDTREC MDTRECORD;

/*
 * This cache contains raw GPREC, KEYREC and KEYSORT objects.  Encode both
 * widths in the version so an incompatible cache is rebuilt automatically.
 */
static const GPTYPE CacheVersion =
  static_cast<GPTYPE>(0x0200U + sizeof(GPTYPE) * 0x10U + sizeof(_index_id_t));

static bool CacheFileSize(size_t entries, size_t *bytes)
{
  const size_t header = 2 * sizeof(GPTYPE);
  const size_t perEntry = sizeof(GPREC) + sizeof(KEYREC) + sizeof(KEYSORT);
  if (bytes == NULL ||
      entries > (std::numeric_limits<size_t>::max() - header) / perEntry)
    return false;
  *bytes = header + entries * perEntry;
  return true;
}

static bool CacheCountToSize(GPTYPE stored, size_t *entries)
{
  const size_t value = static_cast<size_t>(stored);
  if (entries == NULL || static_cast<GPTYPE>(value) != stored ||
      value >  MDT_CAPACITY)
    return false;
  *entries = value;
  return true;
}

static bool ReadFully(int fd, void *buffer, size_t bytes)
{
  BYTE *out = static_cast<BYTE *>(buffer);
  const size_t maxChunk = 0x40000000U;
  while (bytes != 0)
    {
      const size_t chunk = bytes < maxChunk ? bytes : maxChunk;
      const long count = static_cast<long>(_sys_read(fd, out, chunk));
      if (count <= 0)
        return false;
      out += count;
      bytes -= static_cast<size_t>(count);
    }
  return true;
}

static bool WriteFully(int fd, const void *buffer, size_t bytes)
{
  const BYTE *in = static_cast<const BYTE *>(buffer);
  const size_t maxChunk = 0x40000000U;
  while (bytes != 0)
    {
      const size_t chunk = bytes < maxChunk ? bytes : maxChunk;
      const long count = static_cast<long>(write(fd, in, chunk));
      if (count <= 0)
        return false;
      in += count;
      bytes -= static_cast<size_t>(count);
    }
  return true;
}

#if 0
class INDEXREC {
public:
  INDEXREC() {
    GpIndex      = NULL;
    KeyIndex     = NULL;
    KeySortTable = NULL;
  }

  int Clear()
    {
      if (!useIndexMap)
        {
          if (KeyIndex)     delete[] KeyIndex;
          if (GpIndex)      delete[] GpIndex;
          if (KeySortTable) delete[] KeySortTable;
        }
      else
        IndexMap.Unmap();
    }

  MMAP        IndexMap;
  bool useIndexMap;
#if USE_MDTHASHTABLE
  bool fastAdd;
#endif
  MMAP        MdtMap;
  bool useMdtMap;

  KEYREC     *KeyIndex;
  GPREC      *GpIndex;
  KEYSORT    *KeySortTable;

  MDTRECORD  *MdtIndex;

  size_t      lastIndex;
};
#endif

static int InstanceCount = 0;

int  MDT::Version() const
{
  return  (2*1000)+sizeof(MDTRECORD);
}


MDT::MDT (INDEX *Index)
{
  MdtFp = NULL;
  MDTHashTable = NULL;

  if (Index)
    {
      static const STRING MdtInfo("_MdtControl");

      const char *def = (InstanceCount < MaxMDTInstances) ? "1" : "0";
      // Do we use an Index Map??
      useIndexMap    = Index-> ProfileGetString(MdtInfo, "UseIndexMap", def).GetBool();
#if USE_MDTHASHTABLE
      fastAdd        = Index->GetMergeStatus() == iNothing;
#endif
      useMdtMap      = Index-> ProfileGetString(MdtInfo, "UseMdtMap", def).GetBool();
      MdtWrongEndian = Index->IsWrongEndian();
      FileStem       = Index->GetDbFileStem();
      Fpt            = Index->GetMainFpt();
    }
  else
    {
      useIndexMap    = true;
      useMdtMap      = true;
#if USE_MDTHASHTABLE
      fastAdd        = false;
#endif
      MdtWrongEndian = !IsBigEndian ();
      FileStem       = __IB_DefaultDbName;
      Fpt            = NULL;
    }

  GpIndex   = NULL;
  KeyIndex  = NULL;
  KeySortTable = NULL; 
  KeyIndexSorted = false;

  ReadOnly = true;

  if (FileStem.GetLength())
    Init();
}

MDT::MDT (const STRING& DbFileStem, const bool WrongEndian)
{
  MdtFp          = NULL;
  MDTHashTable   = NULL;
  useIndexMap    = (InstanceCount < MaxMDTInstances);
  useMdtMap      = (InstanceCount < MaxMDTInstances);
#if USE_MDTHASHTABLE
  fastAdd        = false;
#endif
  FileStem       = DbFileStem;
  MdtWrongEndian = WrongEndian;

  GpIndex   = NULL;
  KeyIndex  = NULL;
  KeySortTable = NULL; 
  KeyIndexSorted = false;

  ReadOnly = true;

  if (DbFileStem.GetLength())
    Init();
}

void MDT::Init()
{
  size_t realTotal = 0;
  bool rebuiltIndex = false;

  if (MdtFp)
    {
      fclose(MdtFp);
      MdtFp = NULL;
    }
  InstanceCount++;
  message_log(LOG_DEBUG, "MDT::Init() instance %d", InstanceCount);

  if (IsBigEndian())
    Magic = MdtWrongEndian ? "!MDT" : "<MDT";
  else
    Magic = MdtWrongEndian ? "!mdt" : "!MDT";

  if (useIndexMap)
    IndexMap.Unmap();
  else
    {
      delete[] KeyIndex;
      delete[] GpIndex;
      delete[] KeySortTable;
    }
  if (useMdtMap)
    MdtMap.Unmap();

  GpIndex = NULL;
  KeyIndex = NULL;
  KeySortTable = NULL;
  MdtIndex = NULL;
  TotalEntries = 0;
  NextGlobalGp = 0;
  MdtName = FileStem + DbExtMdt;
  MdtIndexName = FileStem + DbExtMdtIndex;

  if (useMdtMap)
    {
      MdtMap.CreateMap(MdtName, MapRandom);
      if (MdtMap.Ok() && MdtMap.Size() >= SIZEOF_MAGIC &&
          ((MdtMap.Size() - SIZEOF_MAGIC) % sizeof(MDTRECORD)) == 0)
        {
          const size_t mappedTotal =
            (MdtMap.Size() - SIZEOF_MAGIC) / sizeof(MDTRECORD);
          if (mappedTotal <= MDT_CAPACITY)
            {
              realTotal = mappedTotal;
              MdtIndex = reinterpret_cast<MDTRECORD *>(
                MdtMap.Ptr() + SIZEOF_MAGIC);
            }
          else
            {
              MdtMap.Unmap();
              useMdtMap = false;
            }
        }
      else
        {
          MdtMap.Unmap();
          useMdtMap = false;
        }
    }

  bool cacheLoaded = false;
  if (useIndexMap)
    {
      IndexMap.CreateMap(MdtIndexName, MapRandom);
      if (IndexMap.Ok() && IndexMap.Size() >= 2 * sizeof(GPTYPE))
        {
          const BYTE *ptr = IndexMap.Ptr();
          GPTYPE cacheVersion = 0;
          GPTYPE storedTotal = 0;
          memcpy(&cacheVersion, ptr, sizeof(cacheVersion));
          memcpy(&storedTotal, ptr + sizeof(GPTYPE), sizeof(storedTotal));
          storedTotal = NTOHL(storedTotal);

          size_t cacheTotal = 0;
          size_t expectedBytes = 0;
          if (cacheVersion == CacheVersion &&
              CacheCountToSize(storedTotal, &cacheTotal) &&
              CacheFileSize(cacheTotal, &expectedBytes) &&
              expectedBytes == IndexMap.Size() && cacheTotal != 0)
            {
              GpIndex = reinterpret_cast<GPREC *>(
                const_cast<BYTE *>(ptr + 2 * sizeof(GPTYPE)));
              KeyIndex = reinterpret_cast<KEYREC *>(
                reinterpret_cast<BYTE *>(GpIndex) +
                cacheTotal * sizeof(GPREC));
              KeySortTable = reinterpret_cast<KEYSORT *>(
                reinterpret_cast<BYTE *>(KeyIndex) +
                cacheTotal * sizeof(KEYREC));
              TotalEntries = cacheTotal;
              cacheLoaded = true;
            }
        }

      if (!cacheLoaded)
        {
          IndexMap.Unmap();
          useIndexMap = false;
          GpIndex = NULL;
          KeyIndex = NULL;
          KeySortTable = NULL;
        }
    }

  if (!cacheLoaded)
    {
      const int fd = open(MdtIndexName, O_RDONLY);
      if (fd != -1)
        {
#ifdef _WIN32
          setmode(fd, O_BINARY);
#endif
          GPTYPE cacheVersion = 0;
          GPTYPE storedTotal = 0;
          UINT8 cacheBytes = 0;
          bool valid = ReadFully(fd, &cacheVersion, sizeof(cacheVersion)) &&
                       ReadFully(fd, &storedTotal, sizeof(storedTotal)) &&
                       GetFdLength(fd, &cacheBytes);
          storedTotal = NTOHL(storedTotal);

          size_t cacheTotal = 0;
          size_t expectedBytes = 0;
          valid = valid && cacheVersion == CacheVersion &&
                  CacheCountToSize(storedTotal, &cacheTotal) &&
                  CacheFileSize(cacheTotal, &expectedBytes) &&
                  cacheBytes == static_cast<UINT8>(expectedBytes) &&
                  cacheTotal != 0;

          if (valid)
            {
              try
                {
                  GpIndex = new GPREC[cacheTotal];
                  KeyIndex = new KEYREC[cacheTotal];
                  KeySortTable = new KEYSORT[cacheTotal];
                }
              catch (...)
                {
                  delete[] GpIndex;
                  delete[] KeyIndex;
                  delete[] KeySortTable;
                  GpIndex = NULL;
                  KeyIndex = NULL;
                  KeySortTable = NULL;
                  valid = false;
                }
            }

          if (valid)
            valid = ReadFully(fd, GpIndex, cacheTotal * sizeof(GPREC)) &&
                    ReadFully(fd, KeyIndex, cacheTotal * sizeof(KEYREC)) &&
                    ReadFully(fd, KeySortTable, cacheTotal * sizeof(KEYSORT));
          close(fd);

          if (valid)
            {
              TotalEntries = cacheTotal;
              cacheLoaded = true;
            }
          else
            {
              delete[] GpIndex;
              delete[] KeyIndex;
              delete[] KeySortTable;
              GpIndex = NULL;
              KeyIndex = NULL;
              KeySortTable = NULL;
            }
        }
    }

  message_log(LOG_DEBUG, "Index %sMapped, MDT %sMapped",
              useIndexMap ? "" : "not ", useMdtMap ? "" : "not ");

  bool tryOpenStream = true;
open_mdt_stream:
  ReadOnly = false;
  MdtFp = fopen(MdtName, "r+b");
  if (MdtFp == NULL)
    {
      MdtFp = fopen(MdtName, "w+b");
      if (MdtFp != NULL)
        {
          WriteHeader();
          fclose(MdtFp);
          MdtFp = fopen(MdtName, "r+b");
        }
      else
        {
          MdtFp = fopen(MdtName, "rb");
          ReadOnly = true;
        }
    }

  if (MdtFp == NULL)
    {
      if (errno == EMFILE && tryOpenStream && Fpt != NULL)
        {
          message_log(LOG_INFO, "Insufficient file/stream handles in O/S");
          Fpt->CloseAll();
          tryOpenStream = false;
          goto open_mdt_stream;
        }
      message_log(LOG_FATAL | LOG_ERRNO, "Could not create/open %s (MDT)",
                  MdtName.c_str());
    }
  else
    {
      message_log(LOG_DEBUG, "MDT %s -> %d", MdtName.c_str(), fileno(MdtFp));
      ReadTimestamp();

      UINT8 mdtBytes = 0;
      if (!GetStreamLength(MdtFp, &mdtBytes) || mdtBytes < SIZEOF_MAGIC ||
          ((mdtBytes - SIZEOF_MAGIC) % sizeof(MDTRECORD)) != 0)
        message_log(LOG_FATAL, "MDT '%s' has an invalid file length",
                    MdtName.c_str());
      else
        {
          const UINT8 count =
            (mdtBytes - SIZEOF_MAGIC) / sizeof(MDTRECORD);
          if (count > static_cast<UINT8>(MDT_CAPACITY))
            message_log(LOG_FATAL,
                        "MDT '%s' contains %llu records; this build supports %llu",
                        MdtName.c_str(),
                        static_cast<unsigned long long>(count),
                        static_cast<unsigned long long>(MDT_CAPACITY));
          else
            realTotal = static_cast<size_t>(count);
        }
    }

  if (useMdtMap && realTotal !=
      (MdtMap.Size() - SIZEOF_MAGIC) / sizeof(MDTRECORD))
    {
      MdtMap.Unmap();
      MdtIndex = NULL;
      useMdtMap = false;
    }

  if (!cacheLoaded || TotalEntries != realTotal)
    {
      if (useIndexMap)
        {
          IndexMap.Unmap();
          useIndexMap = false;
        }
      else
        {
          delete[] GpIndex;
          delete[] KeyIndex;
          delete[] KeySortTable;
        }
      GpIndex = NULL;
      KeyIndex = NULL;
      KeySortTable = NULL;
      TotalEntries = realTotal;

      if (TotalEntries != 0)
        {
          if (!RebuildIndex())
            message_log(LOG_FATAL, "Could not rebuild MDT lookup indexes");
          else
            rebuiltIndex = true;
        }
    }
  else
    TotalEntries = realTotal;

#if USE_MDTHASHTABLE
  if (MDTHashTable != NULL)
    {
      message_log(LOG_ERROR, "MDT Hash Table was already inited in '%s'",
                  MDTHashTable->Filename().c_str());
      delete MDTHashTable;
    }
  try
    {
      MDTHashTable = new MDTHASHTABLE(FileStem, fastAdd);
    }
  catch (...)
    {
      message_log(LOG_PANIC | LOG_ERRNO,
                  "Can't allocate multiple document strings table!");
      MDTHashTable = NULL;
    }
  if (_globalMDTHashTable == NULL)
    _globalMDTHashTable = MDTHashTable;
#endif

  Changed = rebuiltIndex;
  KeyIndexSorted = true;
  GpIndexSorted = true;
  MaxEntries = TotalEntries;
  lastKeyIndex = lastIndex = TotalEntries / 2;
}

bool MDT::RebuildIndex()
{
  if (!Ok())
    {
      if (useIndexMap)
        {
          IndexMap.Unmap();
          useIndexMap = false;
          KeySortTable = NULL;
        }
      return false;
    }

  if (TotalEntries == 0)
    return true;

  message_log(LOG_NOTICE, "Rebuilding Key/Gp indexes..");
  if (useIndexMap)
    {
      IndexMap.Unmap();
      useIndexMap = false;
    }
  else
    {
      delete[] KeyIndex;
      delete[] GpIndex;
      delete[] KeySortTable;
    }
  KeyIndex = NULL;
  GpIndex = NULL;
  KeySortTable = NULL;

  try
    {
      GpIndex = new GPREC[TotalEntries];
      KeyIndex = new KEYREC[TotalEntries];
    }
  catch (...)
    {
      delete[] GpIndex;
      delete[] KeyIndex;
      GpIndex = NULL;
      KeyIndex = NULL;
      message_log(LOG_PANIC | LOG_ERRNO,
                  "MDT index allocation for %llu records failed",
                  static_cast<unsigned long long>(TotalEntries));
      return false;
    }

  for (size_t i = 0; i < TotalEntries; ++i)
    {
      MDTREC Mdtrec;
      if (!GetEntry(i + 1, &Mdtrec))
        return false;

      GpIndex[i].GpStart = HTONL(Mdtrec.GetGlobalFileStart() +
                                  Mdtrec.GetLocalRecordStart());
      GpIndex[i].GpEnd = HTONL(Mdtrec.GetGlobalFileStart() +
                                Mdtrec.GetLocalRecordEnd());
      GpIndex[i].Date = Mdtrec.GetDate();

      _index_id_t index = static_cast<_index_id_t>(i + 1);
      if (Mdtrec.GetDeleted())
        SET_DELETE_BITS(index);
      GpIndex[i].Index = HTONL(index);

#define SET_KEYINDEX_KEY(_slot, _key) \
  KeyIndex[_slot].Hash = KeyHash32((const char *)memcpy(KeyIndex[_slot].Key, _key, DocumentKeySize));

      SET_KEYINDEX_KEY(i, Mdtrec.Key);
      KeyIndex[i].Index = HTONL(index);
    }

  KeyIndexSorted = false;
  GpIndexSorted = true;
  return BuildKeySortTable();
}


INT MDT::GetIndexNum() const
{
  INT2 num = 0;

  if (TotalEntries > 0 && MdtFp)
    {
      if (fseek(MdtFp, 4, SEEK_SET) != -1)
	{
	  ::Read(&num, MdtFp);
	}
    }
  return num >= 0 ? num : 0;
}

bool MDT::SetIndexNum(INT Num) const
{
  if (MdtFp== NULL)
    {
      message_log(LOG_ERROR, "MDT:: Can't set index number, stream NIL?");
    }
  else if (Num >= 0)
    {
      if (fseek(MdtFp, 4, SEEK_SET) != -1)
	{
	  const INT2 number = (INT2)(Num & 0xFFFF);
	  ::Write(number, MdtFp);
	  fflush(MdtFp);
	  return true;
	}
      else
	message_log (LOG_ERRNO, "Could not seek to index byte in MDT (fd=%d)", fileno(MdtFp));
    }
  return false;
}

void MDT::WriteTimestamp()
{
  if (Changed)
    {
      time_t Now = time((time_t *)NULL);
      Timestamp.Set(&Now);
      message_log (LOG_DEBUG, "Set timestamp to: %s", Timestamp.ISOdate().c_str());
      if (MdtFp && !ReadOnly)
	{
	  if (fseek(MdtFp, 12, SEEK_SET) != -1)
	    ::Write((UINT4)Now, MdtFp);
	  else
	    message_log (LOG_ERRNO, "Could not write timestamp (%s)", Timestamp.ISOdate().c_str());
	}
    }
}


void MDT::ReadTimestamp()
{
  if (MdtFp)
    {
      // Seek 12 bytes
      if (fseek(MdtFp, 12, SEEK_SET) != -1)
	{
	  SRCH_DATE newTimestamp;
	  UINT4 x = 0;
	  ::Read(&x, MdtFp);
	  time_t timeval = x;
	  if (timeval == 0) /* Old Indexes */
	    newTimestamp.SetTimeOfFile(MdtFp);
	  else
	    newTimestamp.Set(&timeval);
	  if (!Timestamp.Ok() || (newTimestamp > Timestamp))
	    Timestamp = newTimestamp;
	  return;
	}
    }
  Timestamp.SetNow(); // Set the timestamp to now..
}

/*
Byte 0-1  is the number of .idx and .sis
Byte 2-12 are Magic header information
Byte 13-16 are reserved
*/

void MDT::WriteHeader() const
{
  if (MdtFp)
    {
      int num;

      if ((num = (TotalEntries ? GetIndexNum() : 0)) < 0)
	num = 0;
      
      if (fseek(MdtFp, 0, SEEK_SET) != -1)
	{
	  fwrite(Magic, 4, sizeof(char),  MdtFp);
/* 4 */	  ::Write((INT2)num,              MdtFp);
/* 6 */	  ::Write((UINT2)(sizeof(MDTRECORD)),MdtFp);
/* 8 */	  ::Write((UCHR)DocumentKeySize,  MdtFp);
/* 9 */	  ::Write((UCHR)DocumentTypeSize, MdtFp);
#if USE_MDTHASHTABLE
/*10 */   ::Write((UINT2)0,               MdtFp); // Reserved
#else
/*10 */	  ::Write((UINT2)MaxDocPathNameSize, MdtFp);
#endif
/*12 */	  ::Write((UINT4)time((time_t *)NULL), MdtFp); // time 
	}
      else
	message_log (LOG_ERRNO, "Can't write MDT Header");
    }
  else message_log (LOG_ERROR, "Can't write MDT Header: MDT not opened!");
}

bool MDT::Ok() const
{
  if (MdtFp && TotalEntries)
    {
      if (fseek(MdtFp, 0, SEEK_SET) != -1)
	{
	  UINT2 val;
 	  UCHR ch;
	  char  tmp[5];
#pragma GCC diagnostic ignored "-Wunused-result"
	  fread(tmp, 4, sizeof(char), MdtFp);
	  if (memcmp(tmp, Magic, 4) != 0)
	    return false;
	  ::Read(&val, MdtFp); // Number of indexes
	  ::Read(&val, MdtFp);
	  if (val != sizeof(MDTRECORD))
	    {
	      message_log (LOG_NOTICE, "MDT's records are not compatible with this version.");
	      return false;
	    }
	  ::Read(&ch, MdtFp);
	  if (ch != (UCHR)DocumentKeySize)
	    return false;
	  ::Read(&ch, MdtFp);
	  if (ch != (UCHR)DocumentTypeSize)
	    return false;
#if! USE_MDTHASHTABLE
	  ::Read(&val, MdtFp);
	  if (val != (UINT2)MaxDocPathNameSize)
	    return false;
#endif
	}
    }
  return true;
}

bool MDT::IsSystemFile (const STRING& Filename)
{
  return ((MdtName == Filename) || (MdtIndexName == Filename));
}

size_t MDT::AddEntry(const MDTREC& MdtRecord)
{
  if (ReadOnly)
    return 0;

  if (TotalEntries >= MDT_CAPACITY)
    {
      message_log(LOG_PANIC,
                  "MDT capacity of %llu records has been exceeded!",
                  static_cast<unsigned long long>(MDT_CAPACITY));
      return 0;
    }

  if (TotalEntries == MaxEntries)
    {
      size_t requested = GROWTH_FACTOR(MaxEntries);
      if (requested <= MaxEntries || requested > MDT_CAPACITY)
        requested = MDT_CAPACITY;
      Resize(requested);
      if (MaxEntries <= TotalEntries || KeyIndex == NULL || GpIndex == NULL)
        return 0;
    }

  size_t slot = TotalEntries;
  const char *key = MdtRecord.Key;

  if (KeyIndexSorted && TotalEntries != 0)
    {
      const int tailCompare =
        strncmp(key, KeyIndex[TotalEntries - 1].Key, DocumentKeySize);
      if (tailCompare == 0)
        {
          const STRING oldKey(key, DocumentKeySize);
          STRING newKey(oldKey);
          MDTREC mdtrec(MdtRecord);
          GetUniqueKey(&newKey, false);
          mdtrec.SetKey(newKey);
          message_log(LOG_ERROR,
                      "Duplicate Key \"%s\". Setting key to \"%s\".",
                      oldKey.c_str(), newKey.c_str());
          return AddEntry(mdtrec);
        }
      if (tailCompare < 0)
        {
          size_t left = TotalEntries;
          if (TotalEntries == 1)
            left = 0;
          else
            for (size_t i = (left - 1) / 2, oldPosition,
                        low = 0, high = left - 1;;)
              {
                oldPosition = i;
                const int result =
                  strncmp(key, KeyIndex[i].Key, DocumentKeySize);
                if (result == 0)
                  {
                    const STRING oldKey(key, DocumentKeySize);
                    STRING newKey(oldKey);
                    MDTREC mdtrec(MdtRecord);
                    GetUniqueKey(&newKey, false);
                    mdtrec.SetKey(newKey);
                    message_log(LOG_ERROR,
                                "Duplicate Key \"%s\" (found at %llu). Setting key to \"%s\".",
                                oldKey.c_str(),
                                static_cast<unsigned long long>(i),
                                newKey.c_str());
                    return AddEntry(mdtrec);
                  }
                if (result < 0)
                  high = i;
                else
                  low = i;
                if (high - low <= 0 ||
                    (i = (high + low) / 2) == oldPosition)
                  {
                    left = high;
                    break;
                  }
              }
          slot = left;
        }
    }

  const size_t newTotal = TotalEntries + 1;
  bool written = false;
  if (MdtWrongEndian)
    {
      MDTREC temp(MdtRecord);
      temp.FlipBytes();
      written = temp.Write(MdtFp, newTotal);
    }
  else
    written = MdtRecord.Write(MdtFp, newTotal);

  if (!written)
    {
      message_log(LOG_ERROR | LOG_ERRNO, "Could not write MDT record %llu",
                  static_cast<unsigned long long>(newTotal));
      return 0;
    }

  if (slot < TotalEntries)
    memmove(&KeyIndex[slot + 1], &KeyIndex[slot],
            (TotalEntries - slot) * sizeof(KEYREC));
  SET_KEYINDEX_KEY(slot, key);

  _index_id_t index = static_cast<_index_id_t>(newTotal);
  if (MdtRecord.GetDeleted())
    SET_DELETE_BITS(index);
  KeyIndex[slot].Index = HTONL(index);

  const GPTYPE oldGlobalGp = NextGlobalGp;
  NextGlobalGp = MdtRecord.GetGlobalFileStart();
  GpIndex[TotalEntries].Date = MdtRecord.GetDate();
  GpIndex[TotalEntries].GpStart =
    HTONL(NextGlobalGp + MdtRecord.GetLocalRecordStart());
  NextGlobalGp += MdtRecord.GetLocalRecordEnd();
  GpIndex[TotalEntries].GpEnd = HTONL(NextGlobalGp);
  ++NextGlobalGp;

  if (NextGlobalGp < oldGlobalGp)
    message_log(LOG_FATAL | LOG_PANIC,
                "Physical database capacity exceeded (max %llu MB).",
                static_cast<unsigned long long>(MAX_GPTYPE / (1024ULL * 1024ULL)));
  else if (NextGlobalGp > (MAX_GPTYPE - 1048576))
    message_log(LOG_WARN,
                "Physical database capacity nearly reached (%lluK, max %llu MB).",
                static_cast<unsigned long long>(NextGlobalGp / 1024),
                static_cast<unsigned long long>(MAX_GPTYPE / (1024ULL * 1024ULL)));

  GpIndex[TotalEntries].Index = HTONL(index);
  if (GpIndexSorted && TotalEntries > 1 &&
      GpIndex[TotalEntries - 1].GpStart <
        GpIndex[TotalEntries - 2].GpStart)
    GpIndexSorted = false;

  TotalEntries = newTotal;
  Changed = true;
  return TotalEntries;
}


STRING MDT::GetKey(const size_t Index, int *Hash) const
{
  STRING Key;
  if (KeyIndex == NULL || Index == 0 || Index > TotalEntries)
    return Key;

  size_t position = TotalEntries;
  if (KeySortTable != NULL)
    position = KeySortPosition(static_cast<_index_id_t>(Index));
  else
    {
      for (size_t i = 0; i < TotalEntries; ++i)
        {
          if (INDEX_MASK(NTOHL(KeyIndex[i].Index)) ==
              static_cast<_index_id_t>(Index))
            {
              position = i;
              break;
            }
        }
    }

  if (position >= TotalEntries ||
      INDEX_MASK(NTOHL(KeyIndex[position].Index)) !=
        static_cast<_index_id_t>(Index))
    {
      message_log(LOG_PANIC, "MDT::GetKey() table glitch for record %llu",
                  static_cast<unsigned long long>(Index));
      return Key;
    }

  Key = STRING(KeyIndex[position].Key, DocumentKeySize);
  if (Hash)
    *Hash = KeyIndex[position].Hash;
  return Key;
}



static int MdtCompareKeysByIndex(const void *KeyRecPtr1,
                                     const void *KeyRecPtr2)
{
  const _index_id_t index1 = KeyRecPtr1 ?
    INDEX_MASK(NTOHL(static_cast<const KEYREC *>(KeyRecPtr1)->Index)) : 0;
  const _index_id_t index2 = KeyRecPtr2 ?
    INDEX_MASK(NTOHL(static_cast<const KEYREC *>(KeyRecPtr2)->Index)) : 0;
  return index1 < index2 ? -1 : (index1 > index2 ? 1 : 0);
}

static int MdtCompareGpByIndex(const void *GpRecPtr1,
                               const void *GpRecPtr2)
{
  const _index_id_t index1 = GpRecPtr1 ?
    INDEX_MASK(NTOHL(static_cast<const GPREC *>(GpRecPtr1)->Index)) : 0;
  const _index_id_t index2 = GpRecPtr2 ?
    INDEX_MASK(NTOHL(static_cast<const GPREC *>(GpRecPtr2)->Index)) : 0;
  return index1 < index2 ? -1 : (index1 > index2 ? 1 : 0);
}


void MDT::IndexSortByIndex()
{
  if (KeyIndex != NULL && TotalEntries > 1)
    QSORT(KeyIndex, TotalEntries, sizeof(KEYREC), MdtCompareKeysByIndex);
  if (GpIndex != NULL && TotalEntries > 1)
    QSORT(GpIndex, TotalEntries, sizeof(GPREC), MdtCompareGpByIndex);

  delete[] KeySortTable;
  KeySortTable = NULL;
  KeyIndexSorted = false;
  GpIndexSorted = false;
  lastKeyIndex = lastIndex = TotalEntries / 2;
  Changed = true;
}



size_t MDT::GetTotalDeleted() const
{
  size_t count = 0;
  for (size_t x=1; x<=TotalEntries; x++)
    {
      if (IsDeleted(x))
	count++;
    } 
  return count;
}

size_t MDT::RemoveDeleted ()
{
  size_t delcount = 0;

#if 1
  if (ReadOnly == false)
    {
      int    ready_to_modify = 0;
      size_t n = 1;
      MDTREC Mdtrec;

      for (size_t x = 1; x <= TotalEntries; x++)
	{
	  if (GetEntry (x, &Mdtrec) && Mdtrec.GetDeleted () == false)
	    {
	      if (x != n)
		{
		  if (!ready_to_modify)
		    {
                      if (useIndexMap || useMdtMap)
                        Resize(TotalEntries);
                      if (useIndexMap || useMdtMap)
                        {
                          message_log(LOG_ERROR,
                                      "RemoveDeleted: could not detach mapped MDT indexes");
                          return delcount;
                        }
                      IndexSortByIndex();
		      ready_to_modify = 1;
		    }
		  if (MdtWrongEndian) Mdtrec.FlipBytes ();
		  if (Mdtrec.Write(MdtFp, n) == false)
		    message_log (LOG_ERROR|LOG_ERRNO, "RemoveDeleted: Could not write to MDT");
		  KeyIndex[n - 1] = KeyIndex[x - 1];
		  KeyIndex[n - 1].Index = HTONL(static_cast<_index_id_t>(n));
		  GpIndex[n - 1] = GpIndex[x - 1];
		  GpIndex[n - 1].Index = HTONL(static_cast<_index_id_t>(n));
		}
	      n++;
	    }
	  else
	    delcount++;
	}
      if (delcount)
	{
	  // Clear the rest memory
	  for (size_t x=n-1; x < TotalEntries; x++)
	    {
	      KeyIndex[x].Clear();
	      GpIndex[x].Clear();
	    }
          TotalEntries = n - 1;
          const UINT8 newLength = SIZEOF_MAGIC +
            static_cast<UINT8>(TotalEntries) * sizeof(MDTRECORD);
          if (!TruncateMdtFile(fileno(MdtFp), newLength))
            message_log(LOG_ERROR | LOG_ERRNO,
                        "Could not truncate MDT to %llu entries",
                        static_cast<unsigned long long>(TotalEntries));
          delete[] KeySortTable;
          KeySortTable = NULL;
	  NextGlobalGp = 0;
	  Changed = true;
	}
    }
#endif
  return delcount;
}

bool MDT::GetEntry (const size_t Index, MDTREC* MdtrecPtr) const
{
  if (MdtFp == NULL || MdtrecPtr == NULL)
    {
      message_log(LOG_PANIC, "MDT::GetEntry called with a null stream or record");
      return false;
    }

  if ((Index > 0) && (Index <= TotalEntries))
    {
      message_log(LOG_DEBUG, "GetEntry(%llu,..) from fd=%d",
                  static_cast<unsigned long long>(Index), fileno(MdtFp));
      if (useMdtMap)
	{
	  *MdtrecPtr = MdtIndex[Index - 1];
	}
      else if (MdtrecPtr->Read(MdtFp, Index) == false)
	{
	  return false;
	}
      if (MdtWrongEndian)
	MdtrecPtr->FlipBytes ();
      MdtrecPtr->HashTable = MDTHashTable;
      return true;
    }
  message_log(LOG_PANIC,
              "MDT::GetEntry Index=%llu: out of bounds (1,%llu)",
              static_cast<unsigned long long>(Index),
              static_cast<unsigned long long>(TotalEntries));
  return false;
}

MDTREC *MDT::GetEntry(const size_t Index)
{
  if (GetEntry(Index, &tmpMdtrec) == true)
    return &tmpMdtrec;
  return NULL;
}


bool MDT::SetDeleted(const size_t Index, bool Delete)
{
  if (ReadOnly || Index == 0 || Index > TotalEntries)
    {
      if (!ReadOnly)
        message_log(LOG_ERROR,
                    "MDT::SetDeleted failed: MDT Index %llu OUT-OF-RANGE (>%llu)!",
                    static_cast<unsigned long long>(Index),
                    static_cast<unsigned long long>(TotalEntries));
      return false;
    }

  MDTREC mdtrec;
  if (!GetEntry(Index, &mdtrec))
    {
      message_log(LOG_ERROR,
                  "MDT::SetDeleted failed: Index %llu not available",
                  static_cast<unsigned long long>(Index));
      return false;
    }

  if (mdtrec.GetDeleted() == Delete)
    {
      message_log(LOG_INFO, "MDT::SetDeleted: Entry #%llu already %sdeleted",
                  static_cast<unsigned long long>(Index), Delete ? "" : "un");
      return true;
    }

  if (useIndexMap || useMdtMap)
    Resize(TotalEntries);
  if (useIndexMap || useMdtMap || KeyIndex == NULL || GpIndex == NULL)
    {
      message_log(LOG_ERROR,
                  "MDT::SetDeleted could not detach mapped indexes for record %llu",
                  static_cast<unsigned long long>(Index));
      return false;
    }

  mdtrec.SetDeleted(Delete);
  if (!mdtrec.Write(MdtFp, Index))
    return false;

  message_log(LOG_INFO, "MDT::SetDeleted: Entry #%llu %sdeleted",
              static_cast<unsigned long long>(Index), Delete ? "" : "un");

  for (size_t i = 0; i < TotalEntries; ++i)
    if (INDEX_MASK(NTOHL(GpIndex[i].Index)) ==
        static_cast<_index_id_t>(Index))
      {
        _index_id_t gpId = NTOHL(GpIndex[i].Index);
        SET_DELETE_STATE(gpId, Delete);
        GpIndex[i].Index = HTONL(gpId);
        break;
      }

  for (size_t i = 0; i < TotalEntries; ++i)
    if (INDEX_MASK(NTOHL(KeyIndex[i].Index)) ==
        static_cast<_index_id_t>(Index))
      {
        _index_id_t keyId = NTOHL(KeyIndex[i].Index);
        SET_DELETE_STATE(keyId, Delete);
        KeyIndex[i].Index = HTONL(keyId);
        break;
      }

  Changed = true;
  return true;
}

bool MDT::IsDeleted(const size_t Index) const
{
  if ((Index > 0) && (Index <= TotalEntries))
    {
      if (!useMdtMap)
	{
	  MDTREC Mdtrec;
	  return Mdtrec.IsDeleted(MdtFp, Index);
	}
      return MdtIndex[Index - 1].GetDeleted();
   }
  return true; // Non-existant records are considered deleted....
}


void MDT::SetEntry(const size_t Index, const MDTREC& MdtRecord)
{
  if (ReadOnly || Index == 0 || Index > TotalEntries)
    return;

  if (useIndexMap || useMdtMap)
    Resize(TotalEntries);
  if (useIndexMap || useMdtMap || KeyIndex == NULL || GpIndex == NULL)
    {
      message_log(LOG_ERROR,
                  "MDT::SetEntry could not detach mapped indexes for record %llu",
                  static_cast<unsigned long long>(Index));
      return;
    }

  bool written = false;
  if (MdtWrongEndian)
    {
      MDTREC temp(MdtRecord);
      temp.FlipBytes();
      written = temp.Write(MdtFp, Index);
    }
  else
    written = MdtRecord.Write(MdtFp, Index);

  if (!written)
    {
      message_log(LOG_ERROR | LOG_ERRNO,
                  "MDT::SetEntry could not write record %llu",
                  static_cast<unsigned long long>(Index));
      return;
    }

  for (size_t x = 0; x < TotalEntries; ++x)
    {
      if (INDEX_MASK(NTOHL(KeyIndex[x].Index)) !=
          static_cast<_index_id_t>(Index))
        continue;

      if (strncmp(KeyIndex[x].Key, MdtRecord.Key, DocumentKeySize) != 0)
        {
          SET_KEYINDEX_KEY(x, MdtRecord.Key)
          delete[] KeySortTable;
          KeySortTable = NULL;
          if (KeyIndexSorted)
            {
              if (x > 0 &&
                  strncmp(KeyIndex[x].Key, KeyIndex[x - 1].Key,
                          DocumentKeySize) < 0)
                KeyIndexSorted = false;
              else if (x + 1 < TotalEntries &&
                       strncmp(KeyIndex[x + 1].Key, KeyIndex[x].Key,
                               DocumentKeySize) < 0)
                KeyIndexSorted = false;
            }
        }

      _index_id_t keyId = NTOHL(KeyIndex[x].Index);
      SET_DELETE_STATE(keyId, MdtRecord.GetDeleted());
      KeyIndex[x].Index = HTONL(keyId);
      break;
    }

  const GPTYPE gpStart = MdtRecord.GetGlobalFileStart() +
                         MdtRecord.GetLocalRecordStart();
  const GPTYPE gpEnd = MdtRecord.GetGlobalFileStart() +
                       MdtRecord.GetLocalRecordEnd();

  for (size_t x = 0; x < TotalEntries; ++x)
    {
      if (INDEX_MASK(NTOHL(GpIndex[x].Index)) !=
          static_cast<_index_id_t>(Index))
        continue;

      if (GpIndexSorted)
        {
          if (x > 0 && gpStart < NTOHL(GpIndex[x - 1].GpStart))
            GpIndexSorted = false;
          else if (x + 1 < TotalEntries &&
                   NTOHL(GpIndex[x + 1].GpStart) < gpStart)
            GpIndexSorted = false;
        }

      GpIndex[x].GpStart = HTONL(gpStart);
      GpIndex[x].GpEnd = HTONL(gpEnd);
      GpIndex[x].Date = MdtRecord.GetDate();
      _index_id_t gpId = NTOHL(GpIndex[x].Index);
      SET_DELETE_STATE(gpId, MdtRecord.GetDeleted());
      GpIndex[x].Index = HTONL(gpId);
      break;
    }

  if (Index == TotalEntries)
    NextGlobalGp = _NextGlobal(MdtRecord);
  Changed = true;
}


static int MdtCompareKeys (const void *KeyRecPtr1, const void *KeyRecPtr2)
{
  return strncmp ((((KEYREC *) KeyRecPtr1)->Key), (((KEYREC *) KeyRecPtr2)->Key), DocumentKeySize);
}

void MDT::SortKeyIndex ()
{
  if (KeyIndexSorted == false && KeyIndex)
    {
      if (TotalEntries > 1)
	{
	  QSORT(KeyIndex, TotalEntries, sizeof (KEYREC), MdtCompareKeys);
	  Changed = true;
	}
      KeyIndexSorted = true;
    }
}

void MDT::Resize(const size_t Entries)
{
  size_t target = Entries;
  if (target > MDT_CAPACITY)
    {
      message_log(LOG_WARN, "MDT capacity is %llu records in this build.",
                  static_cast<unsigned long long>(MDT_CAPACITY));
      target = MDT_CAPACITY;
    }

  const bool detachMaps = useIndexMap || useMdtMap;
  if (target > TotalEntries ||
      (detachMaps && target >= TotalEntries && target != 0))
    {
      if (target < TotalEntries)
        target = TotalEntries;

      KEYREC *newKeyIndex = NULL;
      GPREC *newGpIndex = NULL;
      try
        {
          newKeyIndex = new KEYREC[target];
          newGpIndex = new GPREC[target];
        }
      catch (...)
        {
          delete[] newKeyIndex;
          delete[] newGpIndex;
          message_log(LOG_ERRNO,
                      "Memory allocation failed while growing MDT caches to %llu records",
                      static_cast<unsigned long long>(target));
          return;
        }

      if (TotalEntries != 0)
        {
          memcpy(newKeyIndex, KeyIndex, TotalEntries * sizeof(KEYREC));
          memcpy(newGpIndex, GpIndex, TotalEntries * sizeof(GPREC));
        }

      if (useIndexMap)
        IndexMap.Unmap();
      else
        {
          delete[] KeyIndex;
          delete[] GpIndex;
          delete[] KeySortTable;
        }
      if (useMdtMap)
        MdtMap.Unmap();

      KeyIndex = newKeyIndex;
      GpIndex = newGpIndex;
      KeySortTable = NULL;
      MdtIndex = NULL;
      useIndexMap = false;
      useMdtMap = false;
      MaxEntries = target;
    }
  else if (Entries == 0)
    {
      if (useIndexMap)
        IndexMap.Unmap();
      else
        {
          delete[] KeyIndex;
          delete[] GpIndex;
          delete[] KeySortTable;
        }
      if (useMdtMap)
        MdtMap.Unmap();

      KeyIndex = NULL;
      GpIndex = NULL;
      KeySortTable = NULL;
      MdtIndex = NULL;
      useIndexMap = false;
      useMdtMap = false;
      MaxEntries = 0;
      TotalEntries = 0;
    }
}


#if 0


static int MdtCompareKeys (const void *KeyRecPtr1, const void *KeyRecPtr2)
{
  STRING Ref (((KEYREC *) KeyRecPtr1)->Key);
  return strncmp (Ref, (((KEYREC *) KeyRecPtr2)->Key), Ref->GetLength());
}

size_t* MDT::LookupByKeys (const STRING& Key)
{
  const size_t kLen = Key.Length();
  if (!Key.IsWild() || (kLen >  DocumentKeySize) || kLen == 0)
    {
       size_t i = LookupByKey(Key);
       return i;
    }
  STRINGINDEX i = Key.Search("?");
  if (i > 0) Key.EraseAfter(i);
  if ((i = Key.Search("*")) > 0) Key.EraseAfter(i);

  if (TotalEntries)
    {
      if (KeyIndexSorted == false)
        {
          diff = 0;
          SortKeyIndex ();
        }
      const size_t offset = diff > 0 ? lastKeyIndex : 0;
      if (offset != TotalEntries)
        {
          KEYREC KeyRec;
          Key.GetCString (KeyRec.Key, DocumentKeySize);
          const KEYREC *KeyRecPtr= (const KEYREC *)bsearch (&KeyRec, KeyIndex+offset,
                TotalEntries-offset, sizeof (KEYREC), MdtCompareFirstKeys);
          if (KeyRecPtr)
            {
              lastKeyIndex = KeyRecPtr - KeyIndex; // 2007.10 @@@ not /sizeof(KEYREC)
              return INDEX_MASK ( NTOHL(KeyRecPtr->Index) );
            }
        }
    }
  return 0; // Not Found
}



#endif

size_t MDT::LookupByKey (const STRING& Key)
{
  if (TotalEntries)
    {
      INT diff = lastKeyIndex < TotalEntries ? Key.Compare(KeyIndex[lastKeyIndex].Key, DocumentKeySize) : 0;
      if (diff == 0)
	return INDEX_MASK ( NTOHL(KeyIndex[lastKeyIndex].Index) );

      const size_t kLen = Key.Length();
      if (kLen == 0)
	{
	  message_log (LOG_DEBUG, "MDT::LookupByKey: Zero-length Key lookup?");
	  return 0;
	}
      if (kLen > DocumentKeySize)
	{
#if 1
	  // Search using the Right side of Key
	  return LookupByKey(Key.Right(DocumentKeySize));
#else
          // Search using a truncated key
	  return LookupByKey(STRING(Key).EraseAfterNul(DocumentKeySize));
#endif
	}

      if (KeyIndexSorted == false)
	{
	  diff = 0;
          SortKeyIndex ();
	}
      const size_t offset = diff > 0 ? lastKeyIndex : 0;
      if (offset != TotalEntries)
	{
	  KEYREC KeyRec;
	  Key.GetCString (KeyRec.Key, DocumentKeySize);
	  const KEYREC *KeyRecPtr= (const KEYREC *)bsearch (&KeyRec, KeyIndex+offset,
		TotalEntries-offset, sizeof (KEYREC), MdtCompareKeys);
	  if (KeyRecPtr)
	    {
	      lastKeyIndex = (KeyRecPtr - KeyIndex); // @@@@ 2007.10 not /sizeof(KEYREC)
	      return INDEX_MASK ( NTOHL(KeyRecPtr->Index) );
	    }
	}
    }
  return 0; // Not Found
}

GPTYPE MDT::GetNameByGlobal(GPTYPE gp, STRING *Path, GPTYPE *Size, GPTYPE *LS, DOCTYPE_ID *Doctype)
{
   GPTYPE          Start = 0;
   MDTREC          Mdtrec;

  if (GetMdtRecord (gp, &Mdtrec))
    {
      if (Path)    *Path    = Mdtrec.GetFullFileName();
      if (Doctype) *Doctype = Mdtrec.GetDocumentType();
      if (Size)    *Size    = Mdtrec.GetLocalRecordEnd() - Mdtrec.GetLocalRecordStart() + 1;
      if (LS)      *LS      = Mdtrec.GetLocalRecordStart();
      Start = Mdtrec.GetGlobalFileStart() + Mdtrec.GetLocalRecordStart();
   } else
     message_log(LOG_ERROR, "Lookup failed for %ld", (long) gp);
   return (Start);
}

size_t MDT::GetMdtRecord (const STRING& Key, MDTREC *MdtrecPtr)
{
  const size_t x = LookupByKey (Key);
  if (x == 0)
    *MdtrecPtr = MDTREC(this);
  else if (GetEntry (x, MdtrecPtr))
    return x;
  return 0; // Nope
}

static int MdtCompareGpStarts(const void *GpRecPtr1, const void *GpRecPtr2)
{
  const GPTYPE left = NTOHL(static_cast<const GPREC *>(GpRecPtr1)->GpStart);
  const GPTYPE right = NTOHL(static_cast<const GPREC *>(GpRecPtr2)->GpStart);
  return left < right ? -1 : (left > right ? 1 : 0);
}

static int MdtCompareGps (const void *GpPtr, const void *GpRecPtr)
{
  const GPTYPE Gp = NTOHL(*((GPTYPE *) GpPtr));

  if (Gp < NTOHL(((GPREC *) GpRecPtr)->GpStart))
    return -1;
  if (Gp <= NTOHL(((GPREC *) GpRecPtr)->GpEnd))
    return 0;
  return 1;
}


void MDT::SortGpIndex ()
{
  if (GpIndexSorted == false)
    {
      if (GpIndex)
	{
	  QSORT (GpIndex, TotalEntries, sizeof (GPREC), MdtCompareGpStarts);
	}
      GpIndexSorted = true;
      Changed = true;
    }
}

#if 0
size_t MDT::LookupByGp (const GPTYPE Gp, SRCH_DATE *Date)
{

}
#endif


size_t MDT::LookupByGp (const GPTYPE Gp, FC *Fc)
{
  errno = 0;
  if (TotalEntries)
    {
      size_t s_offset = 0;
      size_t e_offset = 0;

      if (lastIndex < TotalEntries)
	{
	  GPTYPE start = NTOHL(GpIndex[lastIndex].GpStart);
	  if (Gp >= start)
	    {
	      GPTYPE end = NTOHL(GpIndex[lastIndex].GpEnd);
	      if (Gp <= end)
		{
		  if (Fc)
		    {
		      Fc->SetFieldStart( start );
		      Fc->SetFieldEnd ( end );
		    }
		  if (DELETED_MASK( GpIndex[lastIndex].Index ))
		    {
		      errno = ENOENT;
		      return 0; // DELETED
		    }
		  return INDEX_MASK( GpIndex[lastIndex].Index );
		}
	      else if (lastIndex == TotalEntries)
		return 0; // Not found
#if 0
	      if (Gp <= (end = NTOHL(GpIndex[lastIndex+1].GpEnd)))
		{
		  if (Gp >= (start = NTOHL(GpIndex[lastIndex+1].GpStart)))
		    {
		      if (Fc)
			{
			  Fc->SetFieldStart( start );
			  Fc->SetFieldEnd ( end );
			}
		      return INDEX_MASK ( GpIndex[++lastIndex].Index );
		    }
		}
#endif
	      s_offset = e_offset = lastIndex; 
	    }
	  else if (lastIndex == 0)
	    {
	      return 0; // Before the first?
	    }
	  else
	    {
	      e_offset = TotalEntries - lastIndex; // Experimental
//cerr << "e_offset=" << e_offset << endl;
	    }
	}
      if (GpIndexSorted == false)
	{
          SortGpIndex ();
	  s_offset = e_offset = 0;
	}
      GPREC *GpRecPtr= (GPREC *) bsearch (&Gp, GpIndex+s_offset, TotalEntries-e_offset, sizeof (GPREC), MdtCompareGps);
      if (GpRecPtr)
	{
	  lastIndex = (GpRecPtr - GpIndex); // @@@ 2007.10:  not /sizeof(GPREC)
//cerr << "lastIndex=" << lastIndex << endl;
	  if (Fc)
	    {
	      Fc->SetFieldStart(NTOHL(GpRecPtr->GpStart));
	      Fc->SetFieldEnd (NTOHL(GpRecPtr->GpEnd));
	    }
	  if (DELETED_MASK( NTOHL(GpRecPtr->Index) ))
	    {
	      errno = ENOENT;
	      return 0; // DELETED
	    }
	  return INDEX_MASK ( NTOHL(GpRecPtr->Index) );
	}
#if 0
     cerr << "Could NOT FIND " << Gp << " Debug Dump..." << endl;
     Dump (0, cerr);
#endif
    }
  errno = EIDRM;
  return 0; // Not Found
}

size_t MDT::GetMdtRecord (const GPTYPE gp, MDTREC *MdtrecPtr)
{
  const size_t x = LookupByGp (gp);
  if (x == 0)
    {
      if (errno == EIDRM) message_log (LOG_PANIC, "Could not find GP %ld!", gp);
      *MdtrecPtr = MDTREC(this);
    }
  else if (GetEntry (x, MdtrecPtr))
    {
      return x;
    }
  return 0; // Nope
}

GPTYPE MDT::GetNextGlobal ()
{
  if (TotalEntries && NextGlobalGp == 0)
    {
      // Note: Could also use GPREC index
      MDTREC Mdtrec;
      if (GetEntry (TotalEntries, &Mdtrec))
        NextGlobalGp = _NextGlobal(Mdtrec);
    }
  return NextGlobalGp;
}

STRING& MDT::GetUniqueKey (PSTRING StringPtr, bool Override)
{
  STRING OldKey;
  size_t Index = 0;
  int    cut_len = 4;

  // Is it already Unique?
  if (!StringPtr->IsEmpty())
    {
      OldKey = *StringPtr;
      if ((Index = LookupByKey(*StringPtr)) == 0)
	return *StringPtr;
      STRINGINDEX x = StringPtr->SearchReverse(',');
      if (x > (StringPtr->GetLength() - 6) )
	StringPtr->EraseAfterNul(x-1);
      if ((StringPtr->GetLength() + cut_len) > DocumentKeySize)
	StringPtr->EraseAfterNul(DocumentKeySize-4);
    }

  // Not Unique

  STRING NewKey;
  INT x = 0, y = 0;
  // bit encoding..
  static const char digits[] ="zyxwvutsrqponmlkjihgfedcba_ZYXWVUTSRQPONMLKJIHGFEDCBA@9876543210";
  const int cut = (1<<(4*(cut_len-1))) - 1;
  do {
      if (y)
	{
	  if (y < (INT)(sizeof(digits)/sizeof(char)))
	    NewKey.form("%s,,%c%c", StringPtr->c_str(), digits[y], digits[x]);
	  else
	    NewKey.form("%x%s%c", y, StringPtr->c_str(), digits[x]);
	}
      else
	NewKey.form("%s,%03x", StringPtr->c_str(), cut-x); // lower case
      if (NewKey.GetLength() > DocumentKeySize)
	{
	  if (StringPtr->IsEmpty())
	    {
	      message_log (LOG_PANIC, "Can't caculate a unique key. Contact software support.");
	      break;
	    }
	  y = StringPtr->GetInt();
	  StringPtr->Clear();
	  continue;
	}
      if (y == 0 && x < cut)
	{
	  x++;
	}
      else if (++x >= (INT)(sizeof(digits)/sizeof(char)))
	{
	  x = 0;
	  y++;
	}
    } while (LookupByKey(NewKey));
  if (Override && Index)
    {
      MDTREC mdtrec;
      if (GetEntry (Index, &mdtrec))
	{
	  message_log (LOG_INFO, "Previous duplicate key '%s'%s set to '%s' and marking record deleted.",
		OldKey.c_str(),
		(OldKey.GetLength() > DocumentKeySize) ? "(truncated)" : "",
		NewKey.c_str());
	  mdtrec.SetKey(NewKey);
	  mdtrec.SetDeleted(true);
	  SetEntry (Index, mdtrec);
	  return *StringPtr;
	}
    }
  return *StringPtr = NewKey;  
}

#if 1


#include <iomanip>
#include <stdio.h>

void MDT::Dump(INT Skip, ostream& os) const
{
  STRING key;
  MDTREC Mdtrec;

  const size_t skipped =
    Skip > 0 ? static_cast<size_t>(Skip) : 0;

  const int RangeWidth      = 24;
  const int KeyWidth        = 32;
  const int GlobalWidth     = 12;
  const int LocalStartWidth = 14;
  const int LocalEndWidth   = 14;

  const ios::fmtflags savedFlags = os.flags();
  const char savedFill = os.fill();

  os << '\n'
     << "Total Entries in MDT: "
     << TotalEntries
     << '\n';

  if (TotalEntries > skipped)
    {
      os << std::left
         << std::setw(RangeWidth)
         << "Range"

         << std::setw(KeyWidth)
         << "Record Key"

         << std::right
         << std::setw(GlobalWidth)
         << "Global"

         << std::setw(LocalStartWidth)
         << "Local Start"

         << std::setw(LocalEndWidth)
         << "Local End"

         << "  File"
         << '\n';
    }

  char tmp[64];

  for (size_t x = skipped + 1; x <= TotalEntries; ++x)
    {
      STRING s0;
      STRING s1;

      GetEntry(x, &Mdtrec);
      Mdtrec.GetKey(&key);

      const unsigned long long globalStart =
        static_cast<unsigned long long>(Mdtrec.GetGlobalFileStart());
      const unsigned long long localStart =
        static_cast<unsigned long long>(Mdtrec.GetLocalRecordStart());
      const unsigned long long localEnd =
        static_cast<unsigned long long>(Mdtrec.GetLocalRecordEnd());

      snprintf(tmp, sizeof(tmp), "%llu-%llu",
               globalStart + localStart, globalStart + localEnd);

      os << std::setfill(' ') << std::left << std::setw(RangeWidth) << tmp;

      os << key;

      const size_t keyLength = key.GetLength();

      if (keyLength < static_cast<size_t>(KeyWidth))
        {
          const size_t padding =
            static_cast<size_t>(KeyWidth) - keyLength;

          for (size_t n = 0; n < padding; ++n)
            os.put(' ');
        }
      else
        {
          // Ensure separation when the key exceeds KeyWidth.
          os.put(' ');
        }

      os << std::right
         << std::setw(GlobalWidth)
         << globalStart

         << std::setw(LocalStartWidth)
         << localStart

         << std::setw(LocalEndWidth)
         << localEnd

         << "  \""
         << (s0 = Mdtrec.GetFileName())
         << '"';

      if ((s1 = Mdtrec.GetOrigFileName()) != s0)
        os << " <orig: \"" << s1 << "\">";

      if (Mdtrec.GetDeleted())
        os << " <deleted>";

      os << '\n';
    }

  os.flags(savedFlags);
  os.fill(savedFill);
}


#else

void MDT::Dump (INT Skip, ostream& os) const
{
  STRING key;
  MDTREC Mdtrec;

  os << endl << "Total Entries in MDT: " << TotalEntries << endl;
  if (TotalEntries > (size_t)Skip)
    {
#ifdef SOLARIS
      os << "                  Record Key                     Global          Local      \tFile" << endl;
#else
      os << "          \tRecordKey\tGlobal\tLocal\tFile" << endl;
#endif
    }
  char tmp[64];
  for (size_t x = 1 + Skip; x <= TotalEntries; x++)
    {
      STRING  s0, s1;
      GetEntry (x, &Mdtrec);
      Mdtrec.GetKey (&key);

      sprintf(tmp, "%ld-%ld ",
	(long)(Mdtrec.GetGlobalFileStart () + Mdtrec.GetLocalRecordStart ()),
	(long)(Mdtrec.GetGlobalFileStart () + Mdtrec.GetLocalRecordEnd ()) );
#ifdef SOLARIS
      os << setw(16) << tmp <<  
	setw(DocumentKeySize - key.GetLength()) << std::setfill(' ') << key <<
	"0x" << setw(6) << std::setfill('0') << hex << Mdtrec.GetGlobalFileStart () <<
	"   0x" << setw(6) << std::setfill('0') << hex << Mdtrec.GetLocalRecordStart () <<
	"-0x" << setw(6) << std::setfill('0') << hex << Mdtrec.GetLocalRecordEnd () <<
	"\t\"" << '\"' << (s0 = Mdtrec.GetFileName ()) << '\"';
      if ((s1 = Mdtrec.GetOrigFileName()) != s0)
        os << " <orig: \"" << s1 << "\" >";

      if (Mdtrec.GetDeleted())
	os << " <deleted> ";
      os << std::setfill(' ') << endl;
#else
      os << tmp << '\t' << key << '\t' << Mdtrec.GetGlobalFileStart () << '\t' <<
	Mdtrec.GetLocalRecordStart () << "\t" << Mdtrec.GetLocalRecordEnd () << "\t\"" <<
	 (s0 = Mdtrec.GetFileName ()) << '\"';

      if ((s1 = Mdtrec.GetOrigFileName()) != s0)
	os << " <orig: \"" << s1 << "\" >";
      if (Mdtrec.GetDeleted())
	os << " <deleted> ";
      os << endl;
#endif
    }
}
#endif

bool MDT::KillAll()
{
  bool result = true;

  message_log (LOG_DEBUG, "MDT KillAll()");
  Resize(0);
  if (!ReadOnly && MdtFp) {
    if (EraseFileContents (MdtFp) == -1)
      {
        message_log (LOG_ERRNO, "KillAll: Could not truncate MDT to ZERO entries");
	result = false;
      }
  }
  Changed = false;
  if (UnlinkFile(MdtIndexName) != 0)
    {
      const STRING bak ( MdtIndexName+"~");
      if (RenameFile(MdtIndexName, bak) == 0)
	AddtoGarbageFileList(bak);
      else if ( EraseFileContents(MdtIndexName) != 0 && result)
	result = false;
    }
#if USE_MDTHASHTABLE
  if (MDTHashTable && MDTHashTable->KillAll() == false && result)
    result = false;
#endif

  // Added 26 Feb 2004 . Is this needed?????
  if (!useIndexMap)
    {
      if (KeyIndex)     delete[] KeyIndex;
      if (GpIndex)      delete[] GpIndex;
      if (KeySortTable) delete[] KeySortTable;
    }
  else
    {
      // Make sure!
      MdtMap.Unmap(); IndexMap.Unmap(); 
      useIndexMap = useMdtMap = false;
    }
  KeyIndex = NULL;
  GpIndex = NULL;
  KeySortTable = NULL;

  NextGlobalGp = 0; // Reset Count
  TotalEntries = 0;

  WriteTimestamp(); // New time
  return result;
}

void MDT::FlushMDTIndexes()
{
  message_log(LOG_DEBUG, "Flushing MDT...");

  if (Changed && !ReadOnly)
    {
      if (!GpIndexSorted)
        SortGpIndex();
      if (!KeyIndexSorted)
        SortKeyIndex();

      bool cacheWritten = BuildKeySortTable();
      if (cacheWritten &&
          static_cast<size_t>(static_cast<GPTYPE>(TotalEntries)) != TotalEntries)
        {
          message_log(LOG_ERROR,
                      "MDT cache cannot encode %llu records with this GPTYPE",
                      static_cast<unsigned long long>(TotalEntries));
          cacheWritten = false;
        }

      int fd = -1;
      if (cacheWritten)
        fd = open(MdtIndexName, O_WRONLY | O_CREAT | O_TRUNC, 0666);
      if (cacheWritten && fd == -1)
        {
          message_log(LOG_ERROR | LOG_ERRNO,
                      "Could not open MDT lookup cache '%s' for writing",
                      MdtIndexName.c_str());
          cacheWritten = false;
        }

      if (fd != -1)
        {
#ifdef _WIN32
          setmode(fd, O_BINARY);
#endif
          message_log(LOG_DEBUG,
                      "Writing MDT '%s' lookup cache v%llu, %llu elements",
                      MdtIndexName.c_str(),
                      static_cast<unsigned long long>(CacheVersion),
                      static_cast<unsigned long long>(TotalEntries));
          const GPTYPE total = HTONL(static_cast<GPTYPE>(TotalEntries));
          cacheWritten = WriteFully(fd, &CacheVersion, sizeof(CacheVersion)) &&
                         WriteFully(fd, &total, sizeof(total)) &&
                         WriteFully(fd, GpIndex,
                                    sizeof(GPREC) * TotalEntries) &&
                         WriteFully(fd, KeyIndex,
                                    sizeof(KEYREC) * TotalEntries) &&
                         WriteFully(fd, KeySortTable,
                                    sizeof(KEYSORT) * TotalEntries);
          if (close(fd) != 0)
            cacheWritten = false;
        }

      if (!cacheWritten)
        {
          message_log(LOG_ERROR,
                      "Failed to write complete MDT lookup cache '%s'",
                      MdtIndexName.c_str());
          UnlinkFile(MdtIndexName);
        }

      WriteTimestamp();
      if (cacheWritten)
        Changed = false;
    }
  else if (MaxEntries == 0 && !Changed && !ReadOnly)
    {
      message_log(LOG_DEBUG, "Removing MDT rests.");
      UnlinkFile(MdtName);
      UnlinkFile(MdtIndexName);
    }
}



MDT::~MDT ()
{
  WriteTimestamp();

  // Close MdtFp
  if (MdtFp)
    {
      fclose (MdtFp);
      MdtFp = NULL;
    }

  // Flush
  FlushMDTIndexes();

  // Free resource
  if (!useIndexMap)
    {
      if (KeyIndex)     delete[] KeyIndex;
      if (GpIndex)      delete[] GpIndex;
      if (KeySortTable) delete[] KeySortTable;
    }
  InstanceCount--;
#if USE_MDTHASHTABLE
  if (MDTHashTable)
    {
      if (InstanceCount == 0)
	{
	  // Last instance so we can
	  if (MDTHashTable != _globalMDTHashTable)
	    {
	      message_log (LOG_DEBUG, "Deleting (delayed) _globalMDTHashTable");
	      delete _globalMDTHashTable;
	    }
	  _globalMDTHashTable = NULL; // Remove global pressence
	  delete MDTHashTable;
	  message_log (LOG_DEBUG, "Deleting current MDTHashTable");
	}
      else if (MDTHashTable != _globalMDTHashTable) // Other instances still being used
	{
	  message_log (LOG_DEBUG, "Deleting current MDTHashTable (not _global)");
	  delete MDTHashTable;
	}
      MDTHashTable = NULL;
    }
#endif
  if (InstanceCount)
    message_log (LOG_DEBUG, "Deleted MDT Instance, %d instances still exist", InstanceCount);
  else
    message_log (LOG_DEBUG, "Disposed of all MDT Instances");
}


// Search MDT..

/*
     #define PEEKC      (*sp)
     #define UNGETC(c)    (--sp)
     #define RETURN(*c)    return;
     #define ERROR(c)     regerr
     #include <regexp.h>
      . . .
           (void) compile(*argv, expbuf, &expbuf[ESIZE],'\0');
      . . .
           if (step(linebuf, expbuf))
                             succeed;
*/
