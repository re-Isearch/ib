
/*
Copyright (c) 2020-21 Project re-Isearch and its contributors: See CONTRIBUTORS.
It is made available and licensed under the Apache 2.0 license: see LICENSE
*/
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include "defs.hxx"
#include "string.hxx"
#include "mmap.hxx"
#include <errno.h>

#ifdef KERNEL32
# define MMAP_MAX MAX_INT
#else
# define MMAP_MAX SIZE_MAX/4L
#endif

#ifndef PAGESIZE
# ifdef _SC_PAGESIZE 
#  define PAGESIZE sysconf(_SC_PAGESIZE)
# else
#  define PAGESIZE 4096
# endif
#endif

#define ALLOC_GRAINULARITY PAGESIZE
#define PAGEOFFSET  (ALLOC_GRAINULARITY - 1)

MMAP::MMAP() : ptr(nullptr), addr(nullptr), size(0), inode(0), window(0), chunk(0), fhandle(0), len(0) {}

MMAP::MMAP(const STRING& name) : MMAP() {
  map(name, O_RDONLY, 0, 0, MapNormal);
}

MMAP::MMAP(const STRING& name, enum mapping_access flag) : MMAP() {
  map(name, O_RDONLY, 0, 0, flag);
}

MMAP::MMAP(const STRING& name, off_t from, off_t to, enum mapping_access flag) : MMAP() {
  map(name, O_RDONLY, from, to, flag);
}

MMAP::MMAP(const char *name) : MMAP() {
  map(name, O_RDONLY, 0, 0, MapNormal);
}

// Move Constructor
MMAP::MMAP(MMAP&& other) noexcept 
  : ptr(other.ptr), addr(other.addr), size(other.size), inode(other.inode),
    window(other.window), chunk(other.chunk), fhandle(other.fhandle), len(other.len) {
  other.ptr = nullptr;
  other.addr = nullptr;
  other.len = 0;
  other.size = 0;
  other.inode = 0;
}

// Move Assignment
MMAP& MMAP::operator=(MMAP&& other) noexcept {
  if (this != &other) {
    Unmap();
    ptr = other.ptr;
    addr = other.addr;
    size = other.size;
    inode = other.inode;
    window = other.window;
    chunk = other.chunk;
    fhandle = other.fhandle;
    len = other.len;

    other.ptr = nullptr;
    other.addr = nullptr;
    other.len = 0;
    other.size = 0;
    other.inode = 0;
  }
  return *this;
}


UCHR *MMAP::map(const STRING& name, int permit, off_t from, off_t to, enum mapping_access flag)
{
   if (ptr != nullptr && m_MappedFileName == name) {
    return ptr;
  }
  if (permit == 0) permit = O_RDONLY;
  if ((fhandle = open(name.c_str(), permit)) >= 0) {
    ptr = map(fhandle, permit, from, to, flag);
    close(fhandle);
    fhandle = -1;
    if (ptr != nullptr) {
      m_MappedFileName = name; // Secure the user-space string cache track
    }
  } else {
    Unmap();
  }
  return ptr;
}

UCHR *MMAP::map(int fd, int permit, off_t from, off_t to, enum mapping_access flag) {
  struct stat s;
  caddr_t p = nullptr;
  int prot;
  size_t length = 0;
  off_t offset = 0;

  if (permit == 0) permit = O_RDONLY;

  if (fstat(fd, &s) == 0) {
    const off_t start = from - (from & (PAGEOFFSET));
    const off_t end = (to == 0 ? s.st_size : (((s.st_size - to) <= 1) ? s.st_size : to));

    if (end > start) {
      if (inode == s.st_ino && inode > 0) {
        return ptr;
      }
      Unmap();

      if (permit == O_RDWR) prot = PROT_READ | PROT_WRITE;
      else if (permit == O_WRONLY) prot = PROT_WRITE;
      else prot = PROT_READ;

      if (((end - start) > (MMAP_MAX - PAGESIZE)) || (end < start) ||
          (p = (caddr_t)::mmap(nullptr, length = (size_t)(end - start), prot, MAP_SHARED, fd, start)) == MAP_FAILED) {
        p = nullptr;
        inode = 0;
        length = 0;
        size = 0;
      } else {
        offset = from - start;
        size = end - from;
        inode = s.st_ino;
      }
    }
  } else {
    Unmap();
  }

  addr = (void *)p;
  len = length;
  if (p) {
    p += offset;

   // Hardware-sympathetic pre-faulting for sequential single-file tracks
    if (flag == MapSequential) {
      ::madvise(addr, len, MADV_SEQUENTIAL);
      ::madvise(addr, len, MADV_WILLNEED);
    } else {
      int advice = (flag == MapRandom) ? MADV_RANDOM : MADV_NORMAL;
      ::madvise(addr, len, advice);
    }
  }
  return ptr = (UCHR *)p;
}

bool MMAP::Advise(int flag, size_t from, size_t to) {
  if ((off_t)(from + to) > len) return false;
  int advise = MADV_NORMAL;
  if (flag == MapSequential) advise = MADV_SEQUENTIAL;
  else if (flag == MapRandom) advise = MADV_RANDOM;
  size_t length = (to == 0 ? len : to) - from;
  return madvise((char *)(ptr + from), length, advise) != -1;
}

bool MMAP::Ok() const { return (len > 0 && inode); }
size_t MMAP::Size() const { return size; }

bool MMAP::Unmap() {
  int result = 0;
  if (len && addr) {
    result = munmap((caddr_t)addr, len);
  }
  addr = nullptr;
  ptr = nullptr;
  inode = 0;
  size = 0;
  len = 0;
  return result == 0;
}

bool MMAP::CreateMap(const STRING& fileName) { return map(fileName.c_str(), O_RDONLY, 0, 0, MapNormal) != nullptr; }
bool MMAP::CreateMap(const STRING& fileName, enum mapping_access flag) { return map(fileName.c_str(), O_RDONLY, 0, 0, flag) != nullptr; }
bool MMAP::CreateMap(int fd, enum mapping_access flag) { return map(fd, O_RDONLY, 0, 0, flag) != nullptr; }
bool MMAP::CreateMap(int fd, off_t from, enum mapping_access flag) { return map(fd, O_RDONLY, from, 0, flag) != nullptr; }
bool MMAP::CreateMap(int fd, off_t from, off_t to, enum mapping_access flag) { return map(fd, O_RDONLY, from, to, flag) != nullptr; }

MMAP::~MMAP() { Unmap(); }

// ============================================================================
// MMAP TABLE CLEANUP (Removed unsigned >= 0 warnings)
// ============================================================================
MMAP_TABLE::MMAP_TABLE(size_t Elements) {
  MaxElements = Elements > 0 ? Elements : 1;
  Table = new MMAP[MaxElements];
}

MMAP *MMAP_TABLE::Map(size_t Element) const {
  return (Element < MaxElements) ? &Table[Element] : nullptr;
}

bool MMAP_TABLE::CreateMap(size_t Element, const STRING& FileName, enum mapping_access flag) {
  return (Element < MaxElements) ? Table[Element].CreateMap(FileName, flag) : false;
}

bool MMAP_TABLE::Ok(size_t Element) const {
  return (Element < MaxElements) ? Table[Element].Ok() : false;
}

PUCHR MMAP_TABLE::Ptr(size_t Element) const {
  return (Element < MaxElements) ? Table[Element].Ptr() : nullptr;
}

size_t MMAP_TABLE::Size(size_t Element) const {
  return (Element < MaxElements) ? Table[Element].Size() : 0;
}

bool MMAP_TABLE::Advise(size_t Element, int flag, size_t from, size_t to) {
  return (Element < MaxElements) ? Table[Element].Advise(flag, from, to) : false;
}

MMAP_TABLE::~MMAP_TABLE() { delete[] Table; }

