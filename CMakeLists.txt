cmake_minimum_required(VERSION 3.21)
project(mirdain_forward VERSION 0.1.0 LANGUAGES CXX)

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Network HttpServer WebSockets Mqtt)
qt_standard_project_setup()

qt_add_executable(mirdain_forward MACOSX_BUNDLE
    main.cpp
    PrinterSender.h PrinterSender.cpp
    ElegooSender.h ElegooSender.cpp
    PrusaSender.h PrusaSender.cpp
    BambuSender.h BambuSender.cpp
)
if(APPLE)
    set(curl_executable "/usr/bin/curl")
else()
    find_program(curl_executable curl REQUIRED)
endif()
target_compile_definitions(mirdain_forward PRIVATE MIRDAIN_CURL_PATH="${curl_executable}")
qt_add_resources(mirdain_forward assets PREFIX "/" FILES assets/mirdain-mark.png)
target_compile_features(mirdain_forward PRIVATE cxx_std_17)
target_link_libraries(mirdain_forward PRIVATE Qt6::Widgets Qt6::Network Qt6::HttpServer Qt6::WebSockets Qt6::Mqtt)
set_target_properties(mirdain_forward PROPERTIES
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
