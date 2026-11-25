cmake_minimum_required(VERSION 3.21)
# Qt 6.9's supported macOS baseline, also advertised in release appcasts.
if(CMAKE_HOST_APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version" FORCE)
endif()
project(mirdain_forward VERSION 1.3.0 LANGUAGES C CXX)

include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
set(BUILD_TESTS OFF CACHE BOOL "Build dependency tests" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "Build dependency examples" FORCE)
set(BUILD_FUZZERS OFF CACHE BOOL "Build dependency fuzzers" FORCE)
set(INSTALL_PROJECT OFF CACHE BOOL "Install miniz separately" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static third-party libraries" FORCE)
FetchContent_Declare(miniz
    URL https://codeload.github.com/richgel999/miniz/tar.gz/77d0dce8627735138c51770d1799a1ef48f2117d
    URL_HASH SHA256=baa14349e7c5efb4a493a03af7d2f29d0ef45d87d488567fe5360edb33f8b7ba
    TIMEOUT 60
)
FetchContent_MakeAvailable(miniz)
include(cmake/Bgcode.cmake)

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Network HttpServer WebSockets Mqtt Concurrent)
find_package(OpenSSL 3 REQUIRED)
qt_standard_project_setup()

qt_add_executable(mirdain_forward MACOSX_BUNDLE
    src/main.cpp
    src/app/AppConfig.h src/app/AppConfig.cpp
    src/app/AppPreferences.h src/app/AppPreferences.cpp
    src/app/Notifications.h src/app/Notifications.cpp
    src/app/Account.h src/app/Account.cpp
    src/app/LocalAddress.h
    src/app/SetupServer.h src/app/SetupServer.cpp
    src/archive/ZipArchive.h src/archive/ZipArchive.cpp
    src/platform/StartupRegistration.h
    src/platform/SupportedBrowser.h
    src/platform/RemovableDrives.h
    src/printers/PrinterSettings.h src/printers/PrinterSettings.cpp
    src/printers/PrinterSendJob.h
    src/printers/PrinterSender.h src/printers/PrinterSender.cpp
    src/printers/ElegooSender.h src/printers/ElegooSender.cpp
    src/printers/PrusaSender.h src/printers/PrusaSender.cpp
    src/printers/PrusaBgcode.h src/printers/PrusaBgcode.cpp
    src/printers/BambuSender.h src/printers/BambuSender.cpp
    src/printers/BambuFtpsUpload.h src/printers/BambuFtpsUpload.cpp
    src/printers/BambuFilaments.h src/printers/BambuFilaments.cpp
    src/platform/SlicerWindow.h src/platform/SlicerWindow.cpp
    src/platform/ApplicationReopen.h
    src/platform/DragWatcher.h
    src/platform/NotificationOverlay.h
    src/platform/ShellMenu.h
    src/gwaith/GwaithAutoSlice.h src/gwaith/GwaithAutoSlice.cpp
    src/gwaith/GwaithRuns.h src/gwaith/GwaithRuns.cpp
)
qt_add_resources(mirdain_forward assets PREFIX "/" FILES
    assets/mirdain-mark.png
    assets/fonts/Aileron-Regular.otf
    assets/fonts/Aileron-SemiBold.otf
)

# Forward's own pages (onboarding, Add Printer): a React app in web/, built into one HTML file.
find_program(npm NAMES npm.cmd npm REQUIRED)
set(web_dir "${CMAKE_CURRENT_SOURCE_DIR}/web")
set(web_output "${CMAKE_CURRENT_BINARY_DIR}/web/index.html")
file(GLOB_RECURSE web_sources CONFIGURE_DEPENDS "${web_dir}/src/*")
add_custom_command(OUTPUT "${web_dir}/node_modules/.package-lock.json"
    COMMAND "${npm}" ci --no-audit --no-fund
    WORKING_DIRECTORY "${web_dir}"
    DEPENDS "${web_dir}/package-lock.json"
    COMMENT "Installing web page dependencies"
    VERBATIM
)
add_custom_command(OUTPUT "${web_output}"
    COMMAND "${npm}" run build -- --outDir "${CMAKE_CURRENT_BINARY_DIR}/web" --emptyOutDir
    WORKING_DIRECTORY "${web_dir}"
    DEPENDS ${web_sources} "${web_dir}/index.html" "${web_dir}/vite.config.ts" "${web_dir}/tsconfig.json"
        "${web_dir}/node_modules/.package-lock.json"
    COMMENT "Building Forward's pages"
    VERBATIM
)
set_source_files_properties("${web_output}" PROPERTIES GENERATED TRUE)
qt_add_resources(mirdain_forward pages PREFIX "/web" BASE "${CMAKE_CURRENT_BINARY_DIR}/web" FILES "${web_output}")
target_include_directories(mirdain_forward PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(mirdain_forward PRIVATE cxx_std_17)
target_link_libraries(mirdain_forward PRIVATE Qt6::Widgets Qt6::Network Qt6::HttpServer Qt6::WebSockets Qt6::Mqtt Qt6::Concurrent miniz mirdain_bgcode OpenSSL::SSL)
set_target_properties(mirdain_forward PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.mirdainsystems.forward"
    MACOSX_BUNDLE_BUNDLE_NAME "Mirdain Forward"
    MACOSX_BUNDLE_ICON_FILE "Icon.icns"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
)

include(cmake/Updates.cmake)
# Gwaith slices models dropped on the drop panel; override only for local test builds.
set(FORWARD_GWAITH_URL "https://gwaith.mirdainsystems.com" CACHE STRING "Gwaith service that slices dropped models")
# A local Gwaith (plain HTTP) stores uploads on a local S3 server at host.docker.internal, whose
# plain-HTTP upload links App Transport Security would block in the slicer window. Only such
# development builds allow them; release builds use HTTPS Gwaith and get no exception.
set(FORWARD_DEV_TRANSPORT_SECURITY "")
if(FORWARD_GWAITH_URL MATCHES "^http://")
    set(FORWARD_DEV_TRANSPORT_SECURITY "
    <key>NSAppTransportSecurity</key>
    <dict><key>NSExceptionDomains</key><dict>
        <key>host.docker.internal</key><dict><key>NSExceptionAllowsInsecureHTTPLoads</key><true/></dict>
    </dict></dict>")
endif()
# Called with Forward's own session from its pages; override only for local test builds.
set(FORWARD_API_URL "https://api.mirdainsystems.com" CACHE STRING "Mirdain API that Forward's pages use")
# Loaded by Open Slicer; override only for local test builds, e.g. http://localhost:5173/.
set(FORWARD_SLICER_URL "https://slicer.mirdainsystems.com/" CACHE STRING "Web slicer opened in the slicer window")
target_compile_definitions(mirdain_forward PRIVATE FORWARD_API_URL="${FORWARD_API_URL}" FORWARD_SLICER_URL="${FORWARD_SLICER_URL}"
    FORWARD_GWAITH_URL="${FORWARD_GWAITH_URL}")

if(APPLE)
    set_target_properties(mirdain_forward PROPERTIES OUTPUT_NAME "Mirdain Forward")
    # TLS and platform plugins must resolve the bundled libraries first.
    set_property(TARGET mirdain_forward APPEND PROPERTY BUILD_RPATH "@executable_path/../Frameworks")
    enable_language(OBJCXX)
    target_sources(mirdain_forward PRIVATE
        src/platform/macos/StartupRegistration.mm
        src/platform/macos/SupportedBrowser.mm
        src/platform/macos/RemovableDrives.cpp
        src/platform/macos/SlicerWindow.mm
        src/platform/macos/ApplicationReopen.mm
        src/platform/macos/DragWatcher.mm
        src/platform/macos/NotificationOverlay.mm
        src/platform/macos/MirdainStyle.h src/platform/macos/MirdainStyle.mm
        src/platform/generic/ShellMenu.cpp
    )
    set_source_files_properties(
        src/platform/macos/StartupRegistration.mm
        src/platform/macos/SupportedBrowser.mm
        src/platform/macos/SlicerWindow.mm
        src/platform/macos/ApplicationReopen.mm
        src/platform/macos/DragWatcher.mm
        src/platform/macos/NotificationOverlay.mm
        src/platform/macos/MirdainStyle.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc"
    )
    find_library(service_management ServiceManagement REQUIRED)
    find_library(appkit AppKit REQUIRED)
    find_library(disk_arbitration DiskArbitration REQUIRED)
    find_library(webkit WebKit REQUIRED)
    find_library(core_text CoreText REQUIRED)
    find_library(quartz_core QuartzCore REQUIRED)
    target_link_libraries(mirdain_forward PRIVATE "${service_management}" "${appkit}" "${disk_arbitration}" "${webkit}" "${core_text}" "${quartz_core}")
    target_sources(mirdain_forward PRIVATE assets/Icon.icns)
    set_source_files_properties(assets/Icon.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(mirdain_forward PRIVATE packaging/OpenSSL-LICENSE.txt)
    set_source_files_properties(packaging/OpenSSL-LICENSE.txt PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(mirdain_forward PRIVATE ${bgcode_licenses})
    set_source_files_properties(${bgcode_licenses} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_property(TARGET mirdain_forward APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/Icon.icns"
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in"
    )
    get_filename_component(qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
    find_program(macdeployqt macdeployqt HINTS "${qt_prefix}/bin" REQUIRED)
    set(deploy_library_paths "-libpath=${qt_prefix}/lib")
    find_program(brew brew)
    if(brew)
        execute_process(COMMAND "${brew}" --prefix OUTPUT_VARIABLE brew_prefix OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
        list(APPEND deploy_library_paths "-libpath=${brew_prefix}/lib")
    endif()
    # Relinking restores Qt's development paths. Remove old deployed plugins so
    # the executable and Cocoa plugin cannot load different copies of Qt.
    add_custom_command(TARGET mirdain_forward PRE_LINK
        COMMAND "${CMAKE_COMMAND}" -E rm -rf
            "$<TARGET_BUNDLE_DIR:mirdain_forward>/Contents/Frameworks"
            "$<TARGET_BUNDLE_DIR:mirdain_forward>/Contents/PlugIns"
            "$<TARGET_BUNDLE_DIR:mirdain_forward>/Contents/Resources/qt.conf"
            "$<TARGET_BUNDLE_DIR:mirdain_forward>/Contents/_CodeSignature"
        VERBATIM
    )
    add_custom_command(TARGET mirdain_forward POST_BUILD
        COMMAND /usr/bin/ditto "${sparkle_SOURCE_DIR}/Sparkle.framework"
            "$<TARGET_BUNDLE_DIR:mirdain_forward>/Contents/Frameworks/Sparkle.framework"
        COMMAND "${macdeployqt}" "$<TARGET_BUNDLE_DIR:mirdain_forward>" ${deploy_library_paths} -codesign=-
        # Finder uses the bundle directory's date to invalidate cached icons.
        COMMAND "${CMAKE_COMMAND}" -E touch "$<TARGET_BUNDLE_DIR:mirdain_forward>"
        COMMENT "Bundling Qt frameworks and plugins"
        VERBATIM
    )
elseif(WIN32)
    target_sources(mirdain_forward PRIVATE
        src/platform/windows/StartupRegistration.cpp
        src/platform/windows/RemovableDrives.cpp
        src/platform/generic/SupportedBrowser.cpp
        src/platform/windows/SlicerWindow.cpp
        src/platform/windows/ShellMenu.cpp
        src/platform/generic/ApplicationReopen.cpp
        src/platform/generic/DragWatcher.cpp
        src/platform/generic/NotificationOverlay.cpp
    )
    # WebView2 SDK headers and static loader; the runtime ships with Windows 10 and 11.
    FetchContent_Declare(webview2
        URL https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.4258.31
        URL_HASH SHA256=56f7f4b8bf9aee4b8efefbbdd4f67d5f74ebd1b100ed0806da71bf76af481aa9
        DOWNLOAD_NAME webview2.zip
        TIMEOUT 60
    )
    FetchContent_MakeAvailable(webview2)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(webview2_arch x64)
    else()
        set(webview2_arch x86)
    endif()
    target_include_directories(mirdain_forward PRIVATE "${webview2_SOURCE_DIR}/build/native/include")
    target_link_libraries(mirdain_forward PRIVATE "${webview2_SOURCE_DIR}/build/native/${webview2_arch}/WebView2LoaderStatic.lib" version)
else()
    target_sources(mirdain_forward PRIVATE
        src/platform/generic/StartupRegistration.cpp
        src/platform/generic/RemovableDrives.cpp
        src/platform/generic/SupportedBrowser.cpp
        src/platform/generic/SlicerWindow.cpp
        src/platform/generic/ApplicationReopen.cpp
        src/platform/generic/DragWatcher.cpp
        src/platform/generic/NotificationOverlay.cpp
        src/platform/generic/ShellMenu.cpp
    )
endif()

if(WIN32)
    configure_file(packaging/windows/windows.rc.in "${CMAKE_CURRENT_BINARY_DIR}/windows.rc" @ONLY)
    target_sources(mirdain_forward PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/windows.rc")
    if(MSVC)
        target_compile_options(mirdain_forward PRIVATE /utf-8)
    endif()
    find_program(windeployqt windeployqt HINTS "${Qt6_DIR}/../../../bin" REQUIRED)
    get_filename_component(openssl_root "${OPENSSL_INCLUDE_DIR}" DIRECTORY)
    add_custom_command(TARGET mirdain_forward POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/packaging/OpenSSL-LICENSE.txt" "$<TARGET_FILE_DIR:mirdain_forward>/OpenSSL-LICENSE.txt"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${bgcode_licenses} "$<TARGET_FILE_DIR:mirdain_forward>"
        COMMAND "${windeployqt}" "$<IF:$<CONFIG:Debug>,--debug,--release>"
            --compiler-runtime --no-translations --openssl-root "${openssl_root}" "$<TARGET_FILE:mirdain_forward>"
        COMMENT "Bundling Qt DLLs and plugins"
        VERBATIM
    )
endif()

if(APPLE)
    include(cmake/MacPackaging.cmake)
endif()
