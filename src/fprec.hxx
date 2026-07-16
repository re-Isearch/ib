/* Copyright (c) 2020-21 Project re-Isearch and its contributors: See CONTRIBUTORS.
It is made available and licensed under the Apache 2.0 license: see LICENSE */
/*@@@
File:		fprec.hxx
Description:	Class FPREC - File Pointer Record
@@@*/

#ifndef FPREC_HXX
#define FPREC_HXX

class FPREC {
public:
  FPREC();
  FPREC(const FPREC& OtherFprec);
  FPREC& operator=(const FPREC& OtherFprec);
  void SetFileName(const STRING& NewFileName);
  const STRING& GetFileName() const           { return FileName;        }
  void SetFilePointer(const PFILE NewFp)      { FilePointer = NewFp;    }
  PFILE GetFilePointer() const                { return FilePointer;     }
  void SetPriority(const int NewPriority)     { Priority = NewPriority; }
  int GetPriority() const                     { return Priority;        }
  void SetClosed()                            { RefCount--;             }
  void SetOpened()                            { RefCount++;             }
  bool GetClosed() const                      { return RefCount <= 0;   }
  bool GetOpened() const                      { return RefCount > 0;    }
  void SetOpenMode(const STRING& NewMode)     { OpenMode = NewMode;     }

  // -1 means unknown or append/write mode
  off_t GetCachedSize() const                 { return CachedSize;      }
  void  SetCachedSize(off_t sz)               { CachedSize = sz;        }
  void  InvalidateCachedSize()                { CachedSize = -1;        }

  const STRING& GetOpenMode() const           { return OpenMode;        }
  void Dispose();
  operator       STRING () const;

  ~FPREC();
private:
  STRING FileName;
  FILE  *FilePointer;
  STRING OpenMode;
  int    Priority;
  int    RefCount;
  off_t  CachedSize = -1;  // -1 == unknown, must stat

};

typedef FPREC* PFPREC;

#endif
