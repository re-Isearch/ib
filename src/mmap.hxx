/*
Copyright (c) 2020-21 Project re-Isearch and its contributors: See CONTRIBUTORS.
It is made available and licensed under the Apache 2.0 license: see LICENSE
*/
#ifndef _MMAP_HXX
#define _MMAP_HXX 1

#include <cstddef>
#include <sys/types.h>
#include "string.hxx"

#include <list>
#include <unordered_map>

// Forward define internal types if missing
typedef unsigned char UCHR;
typedef unsigned char* PUCHR;

enum mapping_access { MapNormal, MapSequential, MapRandom };

class MMAP {
public:
  MMAP();
  MMAP(const STRING& name);
  MMAP(const STRING& name, enum mapping_access flag);
  MMAP(const STRING& name, off_t from, off_t to, enum mapping_access flag=MapNormal);
  MMAP(const char *name);
  
  // Explicitly Move-Only Semantics to eliminate double-munmap hazards
  MMAP(const MMAP&) = delete;
  MMAP& operator=(const MMAP&) = delete;
  MMAP(MMAP&& other) noexcept;
  MMAP& operator=(MMAP&& other) noexcept;
  
  ~MMAP();

  bool Unmap();
  bool CreateMap(const STRING& fileName);
  bool CreateMap(const STRING& fileName, enum mapping_access flag);
  bool CreateMap(int fd, enum mapping_access flag);
  bool CreateMap(int fd, off_t from = 0, enum mapping_access flag = MapNormal);
  bool CreateMap(int fd, off_t from = 0, off_t to = 0, enum mapping_access flag = MapNormal);
  
  bool Ok() const;
  UCHR       *Ptr() const { return ptr; }
  size_t      Size() const;
  bool Advise(int flag = MapNormal, size_t from=0, size_t to = 0);

  operator const char*() const { return (const char *)Ptr(); }
  operator const unsigned char*() const { return (const unsigned char *)Ptr(); }

protected:
  STRING      m_MappedFileName; //Cache the string path name (save a fstat)

private:
  UCHR       *map(const STRING& name, int permit = 0, off_t from = 0, off_t to = 0, enum mapping_access flag = MapNormal);
  UCHR       *map(int fd, int permit, off_t from, off_t to, enum mapping_access flag);
  
  UCHR       *ptr;
  void       *addr;
  size_t      size;
  ino_t       inode;
  off_t       window;
  size_t      chunk; 
  int         fhandle;
  off_t       len;
};

// ============================================================================
// ENCAPSULATION: Single-Element Cache Session for Sequential Multi-Record Parsing
// ============================================================================

#if 1


class MMAP_FILE_SESSION {
public:
  MMAP_FILE_SESSION() : m_MMapInstance(nullptr), m_BaseAddress(nullptr) {}
  ~MMAP_FILE_SESSION() { Close(); }

  const unsigned char* GetMemoryBase(const STRING& FileName, enum mapping_access flag = MapSequential) {
    if (m_CurrentFile == FileName && m_BaseAddress != nullptr) {
      return m_BaseAddress; 
    }

    Close(); 

    m_MMapInstance = new MMAP(FileName, flag);
    if (m_MMapInstance && m_MMapInstance->Ok()) {
      m_BaseAddress = reinterpret_cast<const unsigned char*>(m_MMapInstance->Ptr());
      size_t fileSize = m_MMapInstance->Size();

      // ======================================================================
      // HARDWARE FORCE: Explicitly Pre-Fault Every Memory Page 
      // ======================================================================
      if (flag == MapSequential && m_BaseAddress && fileSize > 0) {
        // Volatile prevents the compiler from optimizing out the read loop
        volatile unsigned char token = 0;
        const size_t pageSize = 4096; // Standard OS Page Step
        
        // Step page by page, reading a single byte to force immediate OS page allocation
        for (size_t offset = 0; offset < fileSize; offset += pageSize) {
          token = m_BaseAddress[offset];
        }
        // Touch the absolute last byte to guarantee the trailing partial page is hit
        token = m_BaseAddress[fileSize - 1];
        (void)token; // Silence unused variable warning securely
      }

      m_CurrentFile = FileName;
      return m_BaseAddress;
    }

    Close();
    return nullptr;
  }

  void Close() {
    if (m_MMapInstance) {
      m_MMapInstance->Unmap();
      delete m_MMapInstance;
      m_MMapInstance = nullptr;
    }
    m_CurrentFile = "";
    m_BaseAddress = nullptr;
  }

  size_t Size() const {  return  m_MMapInstance ? m_MMapInstance->Size() : 0 ;  }

private:
  STRING               m_CurrentFile;
  MMAP*                m_MMapInstance;
  const unsigned char* m_BaseAddress;
};



#else


class MMAP_FILE_SESSION {
public:
  MMAP_FILE_SESSION() : m_MMapInstance(nullptr), m_BaseAddress(nullptr) {}
  
  ~MMAP_FILE_SESSION() { Close(); }

  size_t Size() const {  return  m_MMapInstance ? m_MMapInstance->Size() : 0 ;  }

  // Returns the direct memory address of an opened file base. 
  // Automatically switches mappings transparently only when the filename changes.
  const unsigned char* GetMemoryBase(const STRING& FileName, enum mapping_access flag = MapSequential) {
    if (m_CurrentFile == FileName && m_BaseAddress != nullptr) {
      return m_BaseAddress; // Instant cache hit
    }

    Close(); // Unmap previous active file cleanly

    m_MMapInstance = new MMAP(FileName, flag);
    if (m_MMapInstance && m_MMapInstance->Ok()) {
      m_BaseAddress = reinterpret_cast<const unsigned char*>(m_MMapInstance->Ptr());
      m_CurrentFile = FileName;
      return m_BaseAddress;
    }

    Close();
    return nullptr;
  }

  void Close() {
    if (m_MMapInstance) {
      m_MMapInstance->Unmap();
      delete m_MMapInstance;
      m_MMapInstance = nullptr;
    }
    m_CurrentFile = "";
    m_BaseAddress = nullptr;
  }

private:
  STRING               m_CurrentFile;
  MMAP* m_MMapInstance;
  const unsigned char* m_BaseAddress;
};
#endif

// Modernized MMAP Table container using the safe move semantics
class MMAP_TABLE {
public:
  MMAP_TABLE(size_t Elements);
  size_t      TotalElements() const { return MaxElements; };
  MMAP       *Map(size_t Element) const;
  bool        CreateMap(size_t Element, const STRING& FileName, enum mapping_access flag = MapSequential);
  bool        Ok(size_t Element) const;
  PUCHR       Ptr(size_t Element) const;
  size_t      Size(size_t Element) const;
  bool Advise(size_t Element, int flag = MapNormal, size_t from=0, size_t to = 0);
  ~MMAP_TABLE();
private:
  size_t MaxElements;
  MMAP *Table;
};


#if 1

class MultiMMapSession
{
public:
  explicit MultiMMapSession(size_t maxBytes = 512ULL * 1024 * 1024) // e.g. 512MB budget
    : m_MaxBytes(maxBytes), m_CurrentBytes(0) {}

  ~MultiMMapSession() { Clear(); }

  const unsigned char* GetMemoryBase(const STRING& FileName,
                                      enum mapping_access flag = MapRandom)
  {
    auto it = m_Index.find(FileName);
    if (it != m_Index.end())
      {
        TouchMRU(it->second);
        return it->second->second.BaseAddress;
      }

    MMAP* Mapped = new MMAP(FileName, flag);
    if (!Mapped || !Mapped->Ok())
      {
        delete Mapped;
        return nullptr;
      }

    const size_t FileBytes = Mapped->Size();

    // Evict LRU entries until there's room for this one, or nothing left to evict.
    // A single huge file (e.g. 97MB "is") is allowed in even if it alone
    // exceeds a modest budget — see note below.
    while (m_CurrentBytes + FileBytes > m_MaxBytes && !m_List.empty())
      EvictLRU();

    Slot S;
    S.FileName     = FileName;
    S.MMapInstance = Mapped;
    S.BaseAddress  = reinterpret_cast<const unsigned char*>(Mapped->Ptr());
    S.Bytes        = FileBytes;

    m_List.push_front({FileName, S});
    m_Index[FileName] = m_List.begin();
    m_CurrentBytes += FileBytes;

    return S.BaseAddress;
  }

  void Invalidate(const STRING& FileName)
  {
    auto it = m_Index.find(FileName);
    if (it != m_Index.end())
      {
        m_CurrentBytes -= it->second->second.Bytes;
        CloseSlot(it->second->second);
        m_List.erase(it->second);
        m_Index.erase(it);
      }
  }

  void Clear()
  {
    for (auto& entry : m_List)
      CloseSlot(entry.second);
    m_List.clear();
    m_Index.clear();
    m_CurrentBytes = 0;
  }

  size_t CurrentBytes() const { return m_CurrentBytes; }

 size_t Size(const STRING& FileName) const {
     auto it = m_Index.find(FileName);
     return (it != m_Index.end() && it->second->second.MMapInstance)
           ? it->second->second.MMapInstance->Size()
           : 0;
    }

private:
  struct Slot
    {
      STRING FileName;
      MMAP*  MMapInstance = nullptr;
      const unsigned char* BaseAddress = nullptr;
      size_t Bytes = 0;
    };

  using ListEntry = std::pair<STRING, Slot>;
  using ListType  = std::list<ListEntry>;

  size_t   m_MaxBytes;
  size_t   m_CurrentBytes;
  ListType m_List;
  std::unordered_map<STRING, ListType::iterator> m_Index;

  void TouchMRU(ListType::iterator it)
  {
    m_List.splice(m_List.begin(), m_List, it);
  }

  void EvictLRU()
  {
    if (m_List.empty())
      return;
    auto& lru = m_List.back();
    m_CurrentBytes -= lru.second.Bytes;
    CloseSlot(lru.second);
    m_Index.erase(lru.first);
    m_List.pop_back();
  }

  void CloseSlot(Slot& S)
  {
    if (S.MMapInstance)
      {
        S.MMapInstance->Unmap();
        delete S.MMapInstance;
        S.MMapInstance = nullptr;
      }
  }
};




#endif













#endif
