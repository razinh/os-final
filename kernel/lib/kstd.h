// used the C++ standard library and Claude as a reference for this file; implemented everything from scratch to avoid any dependencies
// on external libraries. This file provides basic types, memory functions, string functions, formatted output, byte order conversion, 
// and utility functions that are commonly used.
#pragma once


// ============================================================================
// Kernel Standard Library (kstd)
// ============================================================================

// ============================================================================
// BASIC TYPES
// ============================================================================

using uint8_t = unsigned char;
using uint16_t = unsigned short;
using uint32_t = unsigned int;
using uint64_t = unsigned long long;

using int8_t = signed char;
using int16_t = signed short;
using int32_t = signed int;
using int64_t = signed long long;

using size_t = unsigned long;
using ssize_t = signed long;

using uintptr_t = unsigned long;
using intptr_t  = signed long;

#ifndef NULL
#define NULL nullptr
#endif

// ============================================================================
// MEMORY FUNCTIONS
// ============================================================================

inline void* memcpy(void* dest, const void* src, size_t n) {
    unsigned char* d = static_cast<unsigned char*>(dest);
    const unsigned char* s = static_cast<const unsigned char*>(src);
    
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    
    return dest;
}

inline int memcmp(const void* s1, const void* s2, size_t n) {
    const unsigned char* p1 = static_cast<const unsigned char*>(s1);
    const unsigned char* p2 = static_cast<const unsigned char*>(s2);
    
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return (p1[i] < p2[i]) ? -1 : 1;
        }
    }
    
    return 0;
}

inline void* memset(void* dest, int val, size_t n) {
    unsigned char* d = static_cast<unsigned char*>(dest);
    unsigned char v = static_cast<unsigned char>(val);
    
    for (size_t i = 0; i < n; i++) {
        d[i] = v;
    }
    
    return dest;
}

inline void* memmove(void* dest, const void* src, size_t n) {
    unsigned char* d = static_cast<unsigned char*>(dest);
    const unsigned char* s = static_cast<const unsigned char*>(src);
    
    if (d < s) {
        for (size_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else if (d > s) {
        for (size_t i = n; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }
    
    return dest;
}

inline void bzero(void* dest, size_t n) {
    memset(dest, 0, n);
}

// ============================================================================
// STRING FUNCTIONS
// ============================================================================

inline int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return static_cast<unsigned char>(*s1) - static_cast<unsigned char>(*s2);
}

inline int strncmp(const char* s1, const char* s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return static_cast<unsigned char>(*s1) - static_cast<unsigned char>(*s2);
}

inline char* strcpy(char* dest, const char* src) {
    char* original_dest = dest;
    while ((*dest++ = *src++) != '\0');
    return original_dest;
}

inline char* strncpy(char* dest, const char* src, size_t n) {
    size_t i;
    
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    
    return dest;
}

inline char* strchr(const char* s, int c) {
    while (*s != '\0') {
        if (*s == c) {
            return const_cast<char*>(s);
        }
        s++;
    }
    return (c == '\0') ? const_cast<char*>(s) : nullptr;
}

// ============================================================================
// STRING SEARCH AND MANIPULATION
// ============================================================================

// Find first occurrence of substring needle in haystack
inline char* strstr(const char* haystack, const char* needle) {
    if (*needle == '\0') return const_cast<char*>(haystack);
    
    for (; *haystack != '\0'; haystack++) {
        const char* h = haystack;
        const char* n = needle;
        
        while (*h && *n && (*h == *n)) {
            h++;
            n++;
        }
        
        if (*n == '\0') {
            return const_cast<char*>(haystack);
        }
    }
    
    return nullptr;
}

// ============================================================================
// FORMATTED OUTPUT (Basic Implementation)
// ============================================================================

// Simple integer to string conversion helper
inline int int_to_str(char* buf, int value, int base) {
    if (base < 2 || base > 16) return 0;
    
    char tmp[32];
    int i = 0;
    int neg = 0;
    
    if (value < 0 && base == 10) {
        neg = 1;
        value = -value;
    }
    
    if (value == 0) {
        tmp[i++] = '0';
    } else {
        while (value > 0) {
            int digit = value % base;
            tmp[i++] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
            value /= base;
        }
    }
    
    if (neg) tmp[i++] = '-';
    
    // Reverse
    int len = i;
    for (int j = 0; j < i; j++) {
        buf[j] = tmp[i - 1 - j];
    }
    buf[len] = '\0';
    
    return len;
}

// Simple snprintf implementation (limited format support)
// Supports: %s, %d, %u, %x, %zu, %c, %%
inline int snprintf(char* buffer, size_t size, const char* format, ...) {
    if (size == 0) return 0;
    
    __builtin_va_list args;
    __builtin_va_start(args, format);
    
    char* buf = buffer;
    size_t remaining = size - 1; // Leave room for null terminator
    
    while (*format && remaining > 0) {
        if (*format == '%' && *(format + 1)) {
            format++;
            
            // Width specifier (e.g., %15s)
            int width = 0;
            while (*format >= '0' && *format <= '9') {
                width = width * 10 + (*format - '0');
                format++;
            }
            
            switch (*format) {
                case 's': { // String
                    const char* s = __builtin_va_arg(args, const char*);
                    if (!s) s = "(null)";
                    while (*s && remaining > 0) {
                        *buf++ = *s++;
                        remaining--;
                    }
                    break;
                }
                
                case 'd': { // Signed integer
                    int val = __builtin_va_arg(args, int);
                    char tmp[32];
                    int len = int_to_str(tmp, val, 10);
                    for (int i = 0; i < len && remaining > 0; i++) {
                        *buf++ = tmp[i];
                        remaining--;
                    }
                    break;
                }
                
                case 'u': { // Unsigned integer
                    unsigned int val = __builtin_va_arg(args, unsigned int);
                    char tmp[32];
                    int_to_str(tmp, static_cast<int>(val), 10);
                    const char* p = tmp;
                    while (*p && remaining > 0) {
                        *buf++ = *p++;
                        remaining--;
                    }
                    break;
                }
                
                case 'x': { // Hexadecimal
                    unsigned int val = __builtin_va_arg(args, unsigned int);
                    char tmp[32];
                    int_to_str(tmp, static_cast<int>(val), 16);
                    const char* p = tmp;
                    while (*p && remaining > 0) {
                        *buf++ = *p++;
                        remaining--;
                    }
                    break;
                }
                
                case 'z': { // size_t (expect 'u' after 'z')
                    if (*(format + 1) == 'u') {
                        format++;
                        size_t val = __builtin_va_arg(args, size_t);
                        char tmp[32];
                        int_to_str(tmp, static_cast<int>(val), 10);
                        const char* p = tmp;
                        while (*p && remaining > 0) {
                            *buf++ = *p++;
                            remaining--;
                        }
                    }
                    break;
                }
                
                case 'c': { // Character
                    char c = static_cast<char>(__builtin_va_arg(args, int));
                    if (remaining > 0) {
                        *buf++ = c;
                        remaining--;
                    }
                    break;
                }
                
                case '%': { // Literal %
                    if (remaining > 0) {
                        *buf++ = '%';
                        remaining--;
                    }
                    break;
                }
                
                default:
                    // Unknown format, just copy it
                    if (remaining > 0) {
                        *buf++ = '%';
                        remaining--;
                    }
                    if (remaining > 0) {
                        *buf++ = *format;
                        remaining--;
                    }
                    break;
            }
            format++;
        } else {
            *buf++ = *format++;
            remaining--;
        }
    }
    
    *buf = '\0';
    __builtin_va_end(args);
    
    return static_cast<int>(buf - buffer);
}

// Simple printf (prints to... well, we need a way to output)
// For kernel, you should use KPRINT instead
// This is just a stub that does nothing
inline int printf(const char* format, ...) {
    // In a real kernel, you'd send this to your console/serial output
    // For now, just ignore it or call your kernel's print function
    (void)format;
    return 0;
}

// Simple sscanf implementation (very limited)
// Only supports: %s, %d, %[...]
inline int sscanf(const char* str, const char* format, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, format);
    
    int matches = 0;
    
    while (*format && *str) {
        // Skip whitespace in format
        while (*format == ' ' || *format == '\t') format++;
        
        if (*format == '%' && *(format + 1)) {
            format++;
            
            // Width specifier
            int width = 0;
            while (*format >= '0' && *format <= '9') {
                width = width * 10 + (*format - '0');
                format++;
            }
            if (width == 0) width = 999; // No limit
            
            switch (*format) {
                case 's': { // String
                    char* dest = __builtin_va_arg(args, char*);
                    // Skip leading whitespace
                    while (*str == ' ' || *str == '\t') str++;
                    int i = 0;
                    while (*str && *str != ' ' && *str != '\t' && i < width - 1) {
                        dest[i++] = *str++;
                    }
                    dest[i] = '\0';
                    if (i > 0) matches++;
                    break;
                }
                
                case 'd': { // Integer
                    int* dest = __builtin_va_arg(args, int*);
                    // Skip whitespace
                    while (*str == ' ' || *str == '\t') str++;
                    
                    int sign = 1;
                    if (*str == '-') {
                        sign = -1;
                        str++;
                    } else if (*str == '+') {
                        str++;
                    }
                    
                    int value = 0;
                    int digits = 0;
                    while (*str >= '0' && *str <= '9' && digits < width) {
                        value = value * 10 + (*str - '0');
                        str++;
                        digits++;
                    }
                    
                    if (digits > 0) {
                        *dest = sign * value;
                        matches++;
                    }
                    break;
                }
                
                case '[': { // Character class [^\r\n] etc
                    char* dest = __builtin_va_arg(args, char*);
                    format++; // Skip '['
                    
                    bool invert = false;
                    if (*format == '^') {
                        invert = true;
                        format++;
                    }
                    
                    // Build character set
                    char charset[256] = {0};
                    while (*format && *format != ']') {
                        charset[static_cast<unsigned char>(*format)] = 1;
                        format++;
                    }
                    
                    // Match characters
                    int i = 0;
                    while (*str && i < width - 1) {
                        bool in_set = charset[static_cast<unsigned char>(*str)];
                        if (invert ? in_set : !in_set) {
                            break;
                        }
                        dest[i++] = *str++;
                    }
                    dest[i] = '\0';
                    
                    if (i > 0) matches++;
                    break;
                }
            }
            format++;
        } else {
            // Literal character match
            if (*format == *str) {
                format++;
                str++;
            } else {
                break;
            }
        }
    }
    
    __builtin_va_end(args);
    return matches;
}

inline char* strrchr(const char* s, int c) {
    const char* last = nullptr;
    
    while (*s != '\0') {
        if (*s == c) {
            last = s;
        }
        s++;
    }
    
    if (c == '\0') {
        return const_cast<char*>(s);
    }
    
    return const_cast<char*>(last);
}

inline int strcasecmp(const char* s1, const char* s2) {
    while (*s1 && *s2) {
        char c1 = *s1;
        char c2 = *s2;
        
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        
        if (c1 != c2) {
            return c1 - c2;
        }
        
        s1++;
        s2++;
    }
    
    return *s1 - *s2;
}

// ============================================================================
// BYTE ORDER CONVERSION
// ============================================================================

inline uint16_t bswap16(uint16_t x) {
    return (x >> 8) | (x << 8);
}

inline uint32_t bswap32(uint32_t x) {
    return ((x >> 24) & 0x000000FF) |
           ((x >>  8) & 0x0000FF00) |
           ((x <<  8) & 0x00FF0000) |
           ((x << 24) & 0xFF000000);
}

inline uint64_t bswap64(uint64_t x) {
    return ((x >> 56) & 0x00000000000000FFULL) |
           ((x >> 40) & 0x000000000000FF00ULL) |
           ((x >> 24) & 0x0000000000FF0000ULL) |
           ((x >>  8) & 0x00000000FF000000ULL) |
           ((x <<  8) & 0x000000FF00000000ULL) |
           ((x << 24) & 0x0000FF0000000000ULL) |
           ((x << 40) & 0x00FF000000000000ULL) |
           ((x << 56) & 0xFF00000000000000ULL);
}

inline uint16_t ntohs(uint16_t n) { return bswap16(n); }
inline uint32_t ntohl(uint32_t n) { return bswap32(n); }
inline uint64_t ntohll(uint64_t n) { return bswap64(n); }

inline uint16_t htons(uint16_t h) { return bswap16(h); }
inline uint32_t htonl(uint32_t h) { return bswap32(h); }
inline uint64_t htonll(uint64_t h) { return bswap64(h); }

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

template<typename T>
inline constexpr T min(T a, T b) {
    return (a < b) ? a : b;
}

template<typename T>
inline constexpr T max(T a, T b) {
    return (a > b) ? a : b;
}

template<typename T>
inline constexpr T clamp(T value, T min_val, T max_val) {
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
}

template<typename T>
inline void swap(T& a, T& b) {
    T temp = a;
    a = b;
    b = temp;
}

template<typename T>
inline constexpr T abs(T x) {
    return (x < 0) ? -x : x;
}

// ============================================================================
// CHARACTER CLASSIFICATION
// ============================================================================

inline bool isdigit(int c) {
    return c >= '0' && c <= '9';
}

inline bool isalpha(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

inline bool isalnum(int c) {
    return isalpha(c) || isdigit(c);
}

inline bool isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

inline bool isupper(int c) {
    return c >= 'A' && c <= 'Z';
}

inline bool islower(int c) {
    return c >= 'a' && c <= 'z';
}

inline int toupper(int c) {
    if (islower(c)) return c - 32;
    return c;
}

inline int tolower(int c) {
    if (isupper(c)) return c + 32;
    return c;
}

// ============================================================================
// SIMPLE STRING CONVERSION
// ============================================================================

inline int atoi(const char* str) {
    int result = 0;
    int sign = 1;
    
    while (isspace(*str)) str++;
    
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    
    while (isdigit(*str)) {
        result = result * 10 + (*str - '0');
        str++;
    }
    
    return sign * result;
}

inline char* itoa(int value, char* buffer, int base = 10) {
    if (base < 2 || base > 36) {
        *buffer = '\0';
        return buffer;
    }
    
    char* ptr = buffer;
    char* start = buffer;
    int tmp_value;
    
    if (value < 0 && base == 10) {
        *ptr++ = '-';
        start = ptr;
        value = -value;
    }
    
    do {
        tmp_value = value;
        value /= base;
        *ptr++ = "0123456789abcdefghijklmnopqrstuvwxyz"[tmp_value - value * base];
    } while (value);
    
    *ptr-- = '\0';
    
    char tmp;
    while (start < ptr) {
        tmp = *start;
        *start++ = *ptr;
        *ptr-- = tmp;
    }
    
    return buffer;
}