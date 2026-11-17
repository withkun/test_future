#pragma once
#ifndef INC_LIB_EXPORT_H_
#define INC_LIB_EXPORT_H_

// export and import for this dll
#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(test_future_EXPORTS) // add by CMake
#    ifdef __GNUC__
#      define  EXPORTED __attribute__(dllexport)
#    else
#      define  EXPORTED __declspec(dllexport)
#    endif
#  else
#    ifdef __GNUC__
#      define  EXPORTED __attribute__(dllimport)
#    else
#      define  EXPORTED __declspec(dllimport)
#    endif
#  endif // test_future_EXPORTS
#  define EXPORTED_CLASS
#  define NOT_EXPORTED

#elif defined __GNUC__
#  if __GNUC__ >= 4
#    define EXPORTED __attribute__ ((visibility ("default")))
#    define NOT_EXPORTED  __attribute__ ((visibility ("hidden")))
#  else
#    define EXPORTED
#    define NOT_EXPORTED
#  endif
#  define EXPORTED_CLASS

#elif defined __clang__
#  define EXPORTED __attribute__ ((visibility ("default")))
#  define NOT_EXPORTED  __attribute__ ((visibility ("hidden")))
#  define EXPORTED_CLASS __attribute__ ((visibility ("default")))

#else
#   error "Do not know how to export classes for this platform"
#endif


// export for all dlls
#ifndef DLL_EXPORTED
#if defined(_WIN32) || defined(__CYGWIN__)
#    ifdef __GNUC__
#      define  DLL_EXPORTED __attribute__(dllexport)
#    else
#      define  DLL_EXPORTED __declspec(dllexport)
#    endif

#elif defined __GNUC__
#  if __GNUC__ >= 4
#    define DLL_EXPORTED __attribute__ ((visibility ("default")))
#  else
#    define DLL_EXPORTED
#  endif

#elif defined __clang__
#  define DLL_EXPORTED __attribute__ ((visibility ("default")))

#else
#   error "Do not know how to export classes for this platform"
#endif
#endif

#endif