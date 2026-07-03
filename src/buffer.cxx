/*
Copyright (c) 2020-21 Project re-Isearch and its contributors: See CONTRIBUTORS.
It is made available and licensed under the Apache 2.0 license: see LICENSE
*/
#include <stdlib.h>
#include <memory.h>
#include <new>
#include "common.hxx"
#include "buffer.hxx"

#ifndef PAGESIZE
#ifdef _WIN32
#  define PAGESIZE winGetPageSize()
extern UINT winGetPageSize();
#else
#  define PAGESIZE 4096
#endif
#endif

// Alignment size target (64-byte for L3 cache line, fits AVX-512/Neon/SVE)
constexpr size_t HARDWARE_ALIGNMENT = 64; 

BUFFER::BUFFER() : Buffer(nullptr), Buffer_size(0), PageSize(PAGESIZE) {}

BUFFER::BUFFER(size_t want, size_t size) : Buffer(nullptr), Buffer_size(0), PageSize(PAGESIZE)
{
  Want(want, size);
}

// Move Constructor
BUFFER::BUFFER(BUFFER&& other) noexcept 
  : PageSize(other.PageSize), Buffer_size(other.Buffer_size), Buffer(other.Buffer)
{
  other.Buffer = nullptr;
  other.Buffer_size = 0;
}

// Move Assignment Operator
BUFFER& BUFFER::operator=(BUFFER&& other) noexcept
{
  if (this != &other) {
    Free();
    PageSize = other.PageSize;
    Buffer_size = other.Buffer_size;
    Buffer = other.Buffer;
    
    other.Buffer = nullptr;
    other.Buffer_size = 0;
  }
  return *this;
}

void BUFFER::SetPageSize(size_t size)
{
  PageSize = (1 + (size / 512)) * 512;
}

size_t BUFFER::CalculateAllocSize(size_t want, size_t size) const
{
  // Optimized cleanly to prevent macro side-effects
  return ((1 + ((want * size + size) / PageSize)) * PageSize - (size - ((size / PageSize) * PageSize)));
}

void BUFFER::SetMagicBytes()
{
  if (Buffer) {
    Buffer[Buffer_size] = static_cast<unsigned char>(((Buffer_size + 1) / 1024) & 0xFF);
  }
}

bool BUFFER::Ok() const
{
  if (Buffer) {
    return Buffer[Buffer_size] == static_cast<unsigned char>(((Buffer_size + 1) / 1024) & 0xFF);
  }
  return Buffer_size == 0;
}

bool BUFFER::Free(const char *Class, const char *What)
{
  bool res = true;
  if (Buffer) {
    if (!Ok()) {
      message_log(LOG_PANIC, "Buffer %s::%s (%u kbytes) got corrupted (%x != %x)!",
                  Class ? Class : "", What && *What ? What : "<Unknown>",
                  (unsigned)Buffer_size, Buffer[Buffer_size], 
                  static_cast<unsigned char>(((Buffer_size + 1) / 1024) & 0xFF));
      res = false;
    }
#ifdef _WIN32
    _aligned_free(Buffer);
#else
    free(Buffer); // posix_memalign allocations are freed with standard free()
#endif
  }
  Buffer = nullptr;
  Buffer_size = 0;
  return res;
}

bool BUFFER::Free(const char *What) { return Free(nullptr, What); }
bool BUFFER::Free() { return Free(nullptr, nullptr); }

void *BUFFER::Expand(size_t want, size_t size)
{
  if (Buffer == nullptr || want >= (Buffer_size / size)) {
    unsigned char *oldBuffer = Buffer;
    size_t oldsize = Buffer_size;
    Buffer_size = CalculateAllocSize(want, size) + size;

    // Fast Hardware Aligned Allocation
#ifdef _WIN32
    Buffer = static_cast<unsigned char*>(_aligned_malloc(Buffer_size + 1, HARDWARE_ALIGNMENT));
#else
    void* memptr = nullptr;
    if (posix_memalign(&memptr, HARDWARE_ALIGNMENT, Buffer_size + 1) != 0) {
      memptr = nullptr;
    }
    Buffer = static_cast<unsigned char*>(memptr);
#endif

    if (!Buffer) {
      message_log(LOG_ERRNO | LOG_PANIC, "Could not expand buffer from %u to %u bytes", (unsigned)oldsize, (unsigned)Buffer_size);
      Buffer_size = 0;
      return nullptr;
    }

    if (oldBuffer) {
      if (oldsize) {
        memcpy(Buffer, oldBuffer, oldsize);
      }
#ifdef _WIN32
      _aligned_free(oldBuffer);
#else
      free(oldBuffer);
#endif
    }
    SetMagicBytes();
  }
  return (void *)Buffer;
}

void *BUFFER::Want(size_t want, size_t size)
{
  if (Buffer == nullptr || want >= (Buffer_size / size)) {
    if (Buffer) {
#ifdef _WIN32
      _aligned_free(Buffer);
#else
      free(Buffer);
#endif
      Buffer = nullptr;
    }
    
    Buffer_size = CalculateAllocSize(want, size) + size;

#ifdef _WIN32
    Buffer = static_cast<unsigned char*>(_aligned_malloc(Buffer_size + 1, HARDWARE_ALIGNMENT));
#else
    void* memptr = nullptr;
    if (posix_memalign(& memptr, HARDWARE_ALIGNMENT, Buffer_size + 1) != 0) {
      memptr = nullptr;
    }
    Buffer = static_cast<unsigned char*>(memptr);
#endif

    if (!Buffer) {
      message_log(LOG_ERRNO | LOG_PANIC, "Could not create a %u byte buffer", (unsigned)Buffer_size);
      Buffer_size = 0;
      return nullptr;
    }
    
    memset(Buffer, 0, Buffer_size * sizeof(unsigned char));
    SetMagicBytes();
  } else {
    // Explicitly re-assert the magic bytes if reusing the buffer 
    // to prevent leakage from previous calculations clobbering the check.
    SetMagicBytes();
  }

  if (Buffer_size > size) {
    memset(Buffer, 0, size * sizeof(unsigned char));
  }
  return (void *)Buffer;
}

BUFFER::~BUFFER()
{
  if (Buffer) Free();
}
