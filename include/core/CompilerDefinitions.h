#pragma once

/// \brief platform acronyms
#define PLATFORM_WIN32_ACRONYM     "win32"    ///< Platform definition as a string "win32".
#define PLATFORM_WIN64_ACRONYM     "win64"    ///< Platform definition as a string "win64".
#define PLATFORM_XENON_ACRONYM     "xbox360"  ///< Platform definition as a string "xbox360".
#define PLATFORM_DURANGO_ACRONYM   "durango"  ///< Platform definition as a string "durango".
#define PLATFORM_PS3_PPU_ACRONYM   "ps3_ppu"  ///< Platform definition as a string "ps3_ppu".
#define PLATFORM_PS3_SPU_ACRONYM   "ps3_spu"  ///< Platform definition as a string "ps3_spu".
#define PLATFORM_VITA_ACRONYM      "vita"     ///< Platform definition as a string "vita".
#define PLATFORM_ORBIS_ACRONYM     "orbis"    ///< Platform definition as a string "orbis".
#define PLATFORM_MAC_ACRONYM       "mac"      ///< Platform definition as a string "mac".
#define PLATFORM_IOS_ACRONYM       "ios"      ///< Platform definition as a string "ios".
#define PLATFORM_CAFE_ACRONYM      "cafe"     ///< Platform definition as a string "cafe".
#define PLATFORM_LINUX_ACRONYM     "linux"    ///< Platform definition as a string "linux".
#define PLATFORM_ANDROID_ACRONYM   "android"  ///< Platform definition as a string "android".
#define PLATFORM_NX32_ACRONYM      "nx32"     ///< Platform definition as a string "nx32".
#define PLATFORM_NX64_ACRONYM      "nx64"     ///< Platform definition as a string "nx64".
#define PLATFORM_PROSPERO_ACRONYM  "prospero" ///< Platform definition as a string "prospero".
#define PLATFORM_SCARLETT_ACRONYM  "scarlett" ///< Platform definition as a string "scarlett".
#define PLATFORM_OUNCE_ACRONYM     "ounce"    ///< Platform definition as a string "ounce".

/// \brief Detect MSVC compiler.
#if defined(_MSC_VER)
    #define COMPILER_MSDEV 1
#else
    #define COMPILER_MSDEV 0
#endif

/// \brief CPP definitions based on compiler.
#if ( COMPILER_MSDEV )
#define CPLUSPLUS    _MSVC_LANG
#else
#define CPLUSPLUS    __cplusplus
#endif

/// \brief Definition to indicate that all C++17 core language features are available.
/// \note  This definition does not necessarily indicate standard library compliance with C++17.
#if ( ( CPLUSPLUS >= 201703L ) && ( !COMPILER_MSDEV || ( _MSC_VER >= 1914 ) ) )
    #define LANGUAGE_CPP17
#endif

/// \brief Definition to indicate that all C++20 core language features are available.
/// \note  This definition does not necessarily indicate standard library compliance with C++20.
#if ( CPLUSPLUS >= 202002L )
    #define LANGUAGE_CPP20
#endif

/// \brief   Macros to push/pop compiler warning and error state
#if defined(__clang__)
    #define COMPILER_WARNINGS_PUSH    _Pragma("clang diagnostic push")
    #define COMPILER_WARNINGS_POP     _Pragma("clang diagnostic pop")
#elif defined(__GNUC__)
    #define COMPILER_WARNINGS_PUSH    _Pragma("GCC diagnostic push")
    #define COMPILER_WARNINGS_POP     _Pragma("GCC diagnostic pop")
#else
    #define COMPILER_WARNINGS_PUSH
    #define COMPILER_WARNINGS_POP
#endif

/// \brief CPP Language specific attributes for unused variables.
/// C++17 guarantees [[maybe_unused]], and this project requires C++20.
#if ( defined( LANGUAGE_CPP17 ) || ( CPLUSPLUS >= 201703L ) )
    #define MAYBE_UNUSED     [[maybe_unused]]
#elif defined(__GNUC__) || defined(__clang__)
    #define MAYBE_UNUSED     __attribute__((unused))
#else
    #define MAYBE_UNUSED
#endif
