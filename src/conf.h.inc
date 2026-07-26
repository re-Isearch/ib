#ifndef CONF_H_INC
#define CONF_H_INC

#ifdef __cplusplus
extern "C" {
#endif

#define SIZEOF_CHAR 1
#define SIZEOF_UCHAR 1
#define SIZEOF_SHORT_INT 2
#define SIZEOF_INT 4
#define SIZEOF_LONG_INT 8
#define SIZEOF_LONG_LONG_INT 8
#define SIZEOF_FLOAT 4
#define SIZEOF_DOUBLE 8
#define SIZEOF_LONG_DOUBLE 8

#define IS_LITTLE_ENDIAN 1

#define TARGET_PLATFORM "arm64 Darwin 24.6.0"
#define TARGET_COMPILER "17.0.0 (clang-1700.6.4.2)"
#define HOST_PLATFORM TARGET_PLATFORM
#define HOST_COMPILER TARGET_COMPILER

#ifdef __cplusplus
}
#endif

#endif /* CONF_H_INC */
