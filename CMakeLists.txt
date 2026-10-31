cmake_minimum_required(VERSION 3.21)
project(mirdain_forward VERSION 0.1.0 LANGUAGES C CXX)

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

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Network HttpServer WebSockets Mqtt)
qt_standard_project_setup()

qt_add_executable(mirdain_forward MACOSX_BUNDLE
    main.cpp
    PrinterSettings.h PrinterSettings.cpp
    PrinterSendJob.h
    PrinterSender.h PrinterSender.cpp
    ElegooSender.h ElegooSender.cpp
    PrusaSender.h PrusaSender.cpp
    BambuSender.h BambuSender.cpp
    ZipArchive.h ZipArchive.cpp
)
qt_add_resources(mirdain_forward assets PREFIX "/" FILES assets/mirdain-mark.png)
target_compile_features(mirdain_forward PRIVATE cxx_std_17)
target_link_libraries(mirdain_forward PRIVATE Qt6::Widgets Qt6::Network Qt6::HttpServer Qt6::WebSockets Qt6::Mqtt miniz)
set_target_properties(mirdain_forward PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.mirdainsystems.forward"
    MACOSX_BUNDLE_BUNDLE_NAME "Mirdain Forward"
    MACOSX_BUNDLE_ICON_FILE "Icon.icns"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
)

if(APPLE)
    target_sources(mirdain_forward PRIVATE assets/Icon.icns)
    set_source_files_properties(assets/Icon.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_property(TARGET mirdain_forward APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/Icon.icns"
        "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
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
        COMMAND "${macdeployqt}" "$<TARGET_BUNDLE_DIR:mirdain_forward>" ${deploy_library_paths} -codesign=-
        # Finder uses the bundle directory's date to invalidate cached icons.
        COMMAND "${CMAKE_COMMAND}" -E touch "$<TARGET_BUNDLE_DIR:mirdain_forward>"
        COMMENT "Bundling Qt frameworks and plugins"
        VERBATIM
    )
endif()

if(WIN32)
    configure_file(assets/windows.rc.in "${CMAKE_CURRENT_BINARY_DIR}/windows.rc" @ONLY)
    target_sources(mirdain_forward PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/windows.rc")
    if(MSVC)
        target_compile_options(mirdain_forward PRIVATE /utf-8)
    endif()
    find_program(windeployqt windeployqt HINTS "${Qt6_DIR}/../../../bin" REQUIRED)
    add_custom_command(TARGET mirdain_forward POST_BUILD
        COMMAND "${windeployqt}" "$<IF:$<CONFIG:Debug>,--debug,--release>"
            --compiler-runtime --no-translations "$<TARGET_FILE:mirdain_forward>"
        COMMENT "Bundling Qt DLLs and plugins"
        VERBATIM
    )
endif()
