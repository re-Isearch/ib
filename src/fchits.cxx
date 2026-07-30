
#include <stdlib.h>
#include "fchits.hxx"
#include "string.hxx"
#include "magic.hxx"

void FCHITS::Write(PFILE fp) const
{
  const size_t total = GetTotalEntries();


// Hit count is per record. UINT2 would normally suffice, but UINT4
// avoids an artificial 65535-hit limit. UINT8 would provide no
// practical value for this persistent representation.
  if (total > UINT4_MAX)
    {
      message_log(
          LOG_PANIC,
          "FCHITS has %llu entries; persistent format supports only UINT4",
          static_cast<unsigned long long>(total));
      return;
    }

  ::Write(static_cast<UCHR>(objHITS), fp);
  ::Write(static_cast<UINT4>(total), fp);

  for (const FC& hit : *this)
    hit.Write(fp);
}
