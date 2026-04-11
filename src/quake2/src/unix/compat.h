/*
 * compat.h -- Windows to Unix compatibility layer
 * Provides Unix-compatible versions of Microsoft-specific C11 functions
 */

#ifndef _COMPAT_H_
#define _COMPAT_H_

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>

/* errno_t typedef for Windows compatibility */
typedef int errno_t;

/* Define _inline for Unix */
#define _inline static inline

/* min/max macros - only for C, not C++ (which has std::min/std::max) */
#ifndef __cplusplus
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif


/*
 * Microsoft's safe string functions (C11 Annex K)
 * These are not available on most Unix systems, so we provide compatible implementations
 */

/* strcpy_s */
static inline int strcpy_s(char *dest, size_t destsz, const char *src)
{
    if (!dest || !src || destsz == 0)
        return 22; /* EINVAL */
    
    size_t src_len = strlen(src);
    if (src_len >= destsz)
        return 34; /* ERANGE */
    
    memcpy(dest, src, src_len + 1);
    return 0;
}

/* strcat_s */
static inline int strcat_s(char *dest, size_t destsz, const char *src)
{
    if (!dest || !src || destsz == 0)
        return 22; /* EINVAL */
    
    size_t dest_len = strlen(dest);
    size_t src_len = strlen(src);
    
    if (dest_len + src_len >= destsz)
        return 34; /* ERANGE */
    
    memcpy(dest + dest_len, src, src_len + 1);
    return 0;
}

/* strncpy_s */
static inline int strncpy_s(char *dest, size_t destsz, const char *src, size_t count)
{
    if (!dest || !src || destsz == 0)
        return 22; /* EINVAL */
    
    size_t src_len = strlen(src);
    size_t copy_len = (count < src_len) ? count : src_len;
    
    if (copy_len >= destsz)
        return 34; /* ERANGE */
    
    memcpy(dest, src, copy_len);
    dest[copy_len] = '\0';
    return 0;
}

/* fopen_s */
static inline int fopen_s(FILE **pFile, const char *filename, const char *mode)
{
    if (!pFile || !filename || !mode)
        return 22; /* EINVAL */
    
    *pFile = fopen(filename, mode);
    if (*pFile == NULL)
        return 2; /* ENOENT */
    
    return 0;
}

/* fread_s */
static inline size_t fread_s(void *buffer, size_t bufferSize, size_t elementSize, size_t count, FILE *stream)
{
    if (!buffer || !stream || bufferSize == 0)
        return 0;
    
    size_t bytes_to_read = elementSize * count;
    if (bytes_to_read > bufferSize)
        bytes_to_read = bufferSize;
    
    return fread(buffer, 1, bytes_to_read, stream);
}

/* vsprintf_s */
static inline int vsprintf_s(char *buffer, size_t bufferCount, const char *format, va_list args)
{
    if (!buffer || !format || bufferCount == 0)
        return -1;
    
    return vsnprintf(buffer, bufferCount, format, args);
}

/* sprintf_s */
static inline int sprintf_s(char *buffer, size_t bufferCount, const char *format, ...)
{
    if (!buffer || !format || bufferCount == 0)
        return -1;
    
    va_list args;
    va_start(args, format);
    int result = vsnprintf(buffer, bufferCount, format, args);
    va_end(args);
    
    return result;
}

/* sscanf_s */
static inline int sscanf_s(const char *buffer, const char *format, ...)
{
    if (!buffer || !format)
        return -1;
    
    va_list args;
    va_start(args, format);
    int result = vsscanf(buffer, format, args);
    va_end(args);
    
    return result;
}

/* memmove_s */
static inline int memmove_s(void *dest, size_t destsz, const void *src, size_t count)
{
    if (!dest || !src || destsz == 0)
        return 22; /* EINVAL */
    
    if (count > destsz)
        return 34; /* ERANGE */
    
    memmove(dest, src, count);
    return 0;
}

/* strtok_s */
static inline char *strtok_s(char *str, const char *delimiters, char **context)
{
    if (!delimiters || !context)
        return NULL;
    
    if (str == NULL)
        str = *context;
    
    if (str == NULL || *str == '\0')
        return NULL;
    
    /* Skip leading delimiters */
    str += strspn(str, delimiters);
    if (*str == '\0')
        return NULL;
    
    /* Find end of token */
    char *end = str + strcspn(str, delimiters);
    if (*end == '\0')
    {
        *context = end;
    }
    else
    {
        *end = '\0';
        *context = end + 1;
    }
    
    return str;
}

/* localtime_s - Unix equivalent */
#include <time.h>
static inline errno_t localtime_s(struct tm* const tmDest, const time_t* const sourceTime)
{
    if (tmDest == NULL || sourceTime == NULL) return 22; /* EINVAL */
    const struct tm* result = localtime(sourceTime);
    if (result == NULL) return 22;
    *tmDest = *result;
    return 0;
}

/* strncat_s */
static inline errno_t strncat_s(char* dest, size_t destsz, const char* src, size_t count)
{
    if (dest == NULL || src == NULL || destsz == 0) return 22; /* EINVAL */
    size_t destlen = strlen(dest);
    if (destlen >= destsz) return 34; /* ERANGE */
    size_t tocopy = strlen(src);
    if (tocopy > count) tocopy = count;
    if (destlen + tocopy >= destsz) return 34; /* ERANGE */
    memcpy(dest + destlen, src, tocopy);
    dest[destlen + tocopy] = '\0';
    return 0;
}

#endif /* _COMPAT_H_ */
