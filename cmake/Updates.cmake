# Feed URL and verification key are public release configuration, never secrets.
set(FORWARD_UPDATE_FEED_URL "" CACHE STRING "HTTPS appcast URL for this platform/architecture")
set(FORWARD_UPDATE_PUBLIC_KEY "" CACHE STRING "Base64 Ed25519 update verification public key")
set(forward_updates_configured false)
if(FORWARD_UPDATE_FEED_URL OR FORWARD_UPDATE_PUBLIC_KEY)
    if(NOT FORWARD_UPDATE_FEED_URL MATCHES "^https://[A-Za-z0-9./_?=&%:+~-]+$"
       OR NOT FORWARD_UPDATE_PUBLIC_KEY MATCHES "^[A-Za-z0-9+/]+=$")
        message(FATAL_ERROR "Set both an HTTPS FORWARD_UPDATE_FEED_URL and a base64 FORWARD_UPDATE_PUBLIC_KEY")
    endif()
    string(LENGTH "${FORWARD_UPDATE_PUBLIC_KEY}" public_key_length)
    if(NOT public_key_length EQUAL 44)
        message(FATAL_ERROR "FORWARD_UPDATE_PUBLIC_KEY must encode a 32-byte Ed25519 public key")
    endif()
    set(forward_updates_configured true)
else()
    message(STATUS "Update feed not configured; update controls will be unavailable")
endif()
string(REPLACE "&" "&amp;" FORWARD_UPDATE_FEED_PLIST_URL "${FORWARD_UPDATE_FEED_URL}")
configure_file(cmake/UpdaterConfig.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/UpdaterConfig.h" @ONLY)
target_include_directories(mirdain_forward PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(mirdain_forward PRIVATE FORWARD_APP_VERSION="${PROJECT_VERSION}")
target_sources(mirdain_forward PRIVATE src/platform/AppUpdater.h src/platform/AppUpdater.cpp src/platform/UpdaterBackend.h)

if(APPLE)
    FetchContent_Declare(sparkle
        URL https://github.com/sparkle-project/Sparkle/releases/download/2.10.1/Sparkle-2.10.1.tar.xz
        URL_HASH SHA256=1a856d2a9edb8e628de88121d7be04cf0e2167f8641650b78fca00b14244cf27
        SOURCE_SUBDIR no-cmake-project
    )
    FetchContent_MakeAvailable(sparkle)
    find_library(sparkle_framework Sparkle PATHS "${sparkle_SOURCE_DIR}" NO_DEFAULT_PATH REQUIRED)
    target_sources(mirdain_forward PRIVATE src/platform/macos/AppUpdater.mm)
    set_source_files_properties(src/platform/macos/AppUpdater.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(mirdain_forward PRIVATE "${sparkle_framework}")
elseif(WIN32)
    FetchContent_Declare(winsparkle
        URL https://github.com/vslavik/winsparkle/releases/download/v0.9.4/WinSparkle-0.9.4.zip
        URL_HASH SHA256=6037df37fc263bd1650a1c4949681a9d40ffe991d01f35892a406cb5d103c976
        SOURCE_SUBDIR no-cmake-project
    )
    FetchContent_MakeAvailable(winsparkle)
    # Some SDK ZIPs include a __MACOSX directory, so extraction may retain
    # the versioned top-level directory instead of stripping it.
    find_path(winsparkle_root NAMES include/winsparkle.h
        PATHS "${winsparkle_SOURCE_DIR}" "${winsparkle_SOURCE_DIR}/WinSparkle-0.9.4"
        NO_DEFAULT_PATH REQUIRED)
    configure_file(packaging/windows/installer.iss.in "${CMAKE_CURRENT_BINARY_DIR}/installer.iss" @ONLY)
    # The Windows distribution and CI currently target x64 only.
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR CMAKE_SYSTEM_PROCESSOR MATCHES "[Aa][Rr][Mm]")
        message(FATAL_ERROR "Forward's Windows updater package requires an x64 build")
    endif()
    add_library(WinSparkle SHARED IMPORTED)
    set_target_properties(WinSparkle PROPERTIES
        IMPORTED_IMPLIB "${winsparkle_root}/x64/Release/WinSparkle.lib"
        IMPORTED_LOCATION "${winsparkle_root}/x64/Release/WinSparkle.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${winsparkle_root}/include"
    )
    target_sources(mirdain_forward PRIVATE src/platform/windows/AppUpdater.cpp)
    target_link_libraries(mirdain_forward PRIVATE WinSparkle)
    add_custom_command(TARGET mirdain_forward POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:WinSparkle>" "$<TARGET_FILE_DIR:mirdain_forward>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${winsparkle_root}/COPYING" "$<TARGET_FILE_DIR:mirdain_forward>/WinSparkle-LICENSE"
            "${winsparkle_root}/COPYING.expat" "$<TARGET_FILE_DIR:mirdain_forward>/WinSparkle-expat-LICENSE"
        VERBATIM
    )
else()
    target_sources(mirdain_forward PRIVATE src/platform/generic/AppUpdater.cpp)
endif()
