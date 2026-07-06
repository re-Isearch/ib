#include <new>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include "common.hxx"
#include "index.hxx"


// We let the intrinsics and compiler optimizer handle the SIMD lifting
//


// Fallback if the compiler/standard library doesn't fully export the macro yet
#ifdef __cpp_lib_hardware_interference_size
    using std::hardware_destructive_interference_size;
#else
    // Safe standard fallback for modern x86_64 and Apple Silicon/ARM64 cores
    constexpr size_t hardware_destructive_interference_size = 64;
#endif

// Calculate the exact maximum text space available in a single hardware cache line
// On standard 64-byte cache line systems with 8-byte GPTYPE, this equals exactly 56.
constexpr size_t HW_PERFECT_LEN = hardware_destructive_interference_size - sizeof(GPTYPE); 

template <size_t N>
struct alignas(hardware_destructive_interference_size) SIMDVariableTermKey {
    unsigned char text[N];     // Must be unsigned char for clean array indexing
    GPTYPE        gp_value;    // Original address offset
};

/**
 * @brief Recursive Most Significant Digit (MSD) Radix Sort Core
 * @param src The array of keys to sort
 * @param dst A temporary scratchpad array of matching size
 * @param count Number of elements in this block
 * @param digit Current byte column position we are sorting (0 to N-1)
 */
template <size_t N>
void RadixSortMSDCore(SIMDVariableTermKey<N>* src, SIMDVariableTermKey<N>* dst, 
                      size_t count, size_t digit) 
{
    // Base Case: If the slice is small or we finished scanning text bytes,
    // break any remaining ties using the GPTYPE value (implicitly encodes indexing order).

    // Dynamically scale the fallback threshold to fit exactly into a ~1KB L1 window:
    // 32-bit  String -> Struct is 64 bytes  -> Threshold 16 (1024 bytes)
    // 64-bit  String -> Struct is 128 bytes -> Threshold 8  (1024 bytes)
    // 128-bit String -> Struct is 192 bytes -> Threshold 5  (960 bytes)
    constexpr size_t FALLBACK_THRESHOLD = 
        (sizeof(SIMDVariableTermKey<N>) <= 64)  ? 16 : 
        (sizeof(SIMDVariableTermKey<N>) <= 128) ? 8  : 5;

    if (count < FALLBACK_THRESHOLD || digit >= N) {
        std::sort(src, src + count, [digit](const SIMDVariableTermKey<N>& x, const SIMDVariableTermKey<N>& y) {
            if (digit < N) {
                int res = memcmp(x.text + digit, y.text + digit, N - digit);
                if (res != 0) return res < 0;
            }
            return x.gp_value < y.gp_value;
        });
        return;
    }

    // 256 Buckets for a complete byte range
    size_t count_buckets[256] = {0};
    size_t offset_buckets[257] = {0};

    // Pass 1: Compute frequencies (Histogram)
    for (size_t i = 0; i < count; ++i) {
        count_buckets[src[i].text[digit]]++;
    }

    // Pass 2: Compute prefix sums (Determine exact boundaries for memory destinations)
    for (size_t i = 0; i < 256; ++i) {
        offset_buckets[i + 1] = offset_buckets[i] + count_buckets[i];
    }

    // Pass 3: Scatter elements into their destination buckets branchlessly
    for (size_t i = 0; i < count; ++i) {
        size_t bucket_idx = src[i].text[digit];
        dst[offset_buckets[bucket_idx]++] = src[i];
    }

    // Copy bucket data back to the primary source array
    std::memcpy(src, dst, count * sizeof(SIMDVariableTermKey<N>));

    // Recurse down into each populated bucket for the next byte column position
    size_t last_offset = 0;
    for (size_t i = 0; i < 256; ++i) {
        size_t current_offset = offset_buckets[i]; // Points to end of bucket now
        size_t bucket_size = current_offset - last_offset;

        if (bucket_size > 1) {
            // Drill down into the next character index column
            RadixSortMSDCore<N>(src + last_offset, dst + last_offset, bucket_size, digit + 1);
        }
        last_offset = current_offset;
    }
}


template <size_t N>
void INDEX::ExecuteTemplatedSort(const void* base, void* a, size_t n) const {
    PGPTYPE elements = (PGPTYPE)a;
    const char* text_buffer = (const char*)base;

    // Compile-time check: Alert the developer if an alignment choice causes 
    // a single element to spill across multiple cache lines.
    constexpr size_t element_size = sizeof(SIMDVariableTermKey<N>);
#if defined(DEBUG) || !defined(NDEBUG)
    if constexpr (element_size > hardware_destructive_interference_size)
      message_log(LOG_DEBUG, "Notice: Track <%lu> element size (%luB) spans multiple cache lines.", N, element_size);
#endif

    // Allocate the primary staging array
    SIMDVariableTermKey<N>* staging_array = new (std::nothrow) SIMDVariableTermKey<N>[n];
    // Allocate the secondary scratchpad buffer required for Radix scattering
    SIMDVariableTermKey<N>* scratchpad_array = new (std::nothrow) SIMDVariableTermKey<N>[n];

    if (!staging_array || !scratchpad_array) {
        delete[] staging_array;
        delete[] scratchpad_array;
        TermSort(base, a, n); // Fallback on memory allocation constraint
        return;
    }

    // Ingestion Pass: Extract exact bytes from the memory-mapped buffer
    for (size_t i = 0; i < n; ++i) {
        staging_array[i].gp_value = elements[i];
        const char* source_word = text_buffer + elements[i];
        
        size_t j = 0;
        while (j < N && source_word[j] != '\0') {
            staging_array[i].text[j] = static_cast<unsigned char>(source_word[j]);
            j++;
        }
        while (j < N) {
            staging_array[i].text[j] = '\0';
            j++;
        }
    }

    // Execute the ultra-fast linear Radix pipeline starting at byte column 0
    RadixSortMSDCore<N>(staging_array, scratchpad_array, n, 0);

    // Flush the sorted pointer offsets straight back to your legacy indexing layout
    for (size_t i = 0; i < n; ++i) {
        elements[i] = staging_array[i].gp_value;
    }

    delete[] staging_array;
    delete[] scratchpad_array;
}




void INDEX::TermSort_SIMD(const void *base, void *a, size_t n) const
{       
    // If the array is tiny, jump straight to the Bentley-McIlroy fallback path
    if (n < 32) {
        TermSort(base, a, n);
        return;
    }   
    // Dynamic Routing based on the application's current configuration
    // We match the active lookahead variable to the tightest safe template slot.
    if (StringCompLength <= 32)
      ExecuteTemplatedSort<32>(base, a, n);
    else if (StringCompLength <= 56)
      ExecuteTemplatedSort<56>(base, a, n); // Cache-line sweet spot (56 text + 8 ptr = 64B)
    else if (StringCompLength <= 64)
      ExecuteTemplatedSort<64>(base, a, n); // Historical ib default
    else if (StringCompLength <= 128)
      ExecuteTemplatedSort<128>(base, a, n); // Genomic Token window
    else if (StringCompLength <= 256)
      ExecuteTemplatedSort<256>(base, a, n); // Extended Structural Search window
    else {
      // SAFEST RUNTIME FALLBACK:
      // If they need an arbitrary long length not optimized by a template slot,
      // we fall back directly to the trusted original Bentley-McIlroy sort
      // which safely scales to infinity using the text buffer directly!
      TermSort(base, a, n); 
    }
}

