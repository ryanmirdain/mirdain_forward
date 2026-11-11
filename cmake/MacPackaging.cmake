# Copy the deployed bundle intact so installation does not rewrite signed binaries.
install(CODE [[
    execute_process(
        COMMAND /usr/bin/codesign --verify --deep --strict "$<TARGET_BUNDLE_DIR:mirdain_forward>"
        COMMAND_ERROR_IS_FATAL ANY
    )
]] COMPONENT Forward)
install(DIRECTORY "$<TARGET_BUNDLE_DIR:mirdain_forward>"
    DESTINATION . USE_SOURCE_PERMISSIONS COMPONENT Forward)
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/OpenSSL-LICENSE.txt"
    ${bgcode_licenses}
    DESTINATION Licenses COMPONENT Forward)
install(FILES "${miniz_SOURCE_DIR}/LICENSE"
    DESTINATION Licenses RENAME miniz-LICENSE COMPONENT Forward)
# Qt MQTT is distributed under the same GPL-3.0 text as Forward.
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    DESTINATION Licenses RENAME QtMqtt-LICENSE COMPONENT Forward)

set(package_arch "${CMAKE_SYSTEM_PROCESSOR}")
if(CMAKE_OSX_ARCHITECTURES)
    list(LENGTH CMAKE_OSX_ARCHITECTURES architecture_count)
    if(architecture_count GREATER 1)
        set(package_arch universal)
    else()
        set(package_arch "${CMAKE_OSX_ARCHITECTURES}")
    endif()
endif()

set(CPACK_GENERATOR DragNDrop)
set(CPACK_PACKAGE_NAME "Mirdain Forward")
set(CPACK_PACKAGE_VENDOR "Mirdain Systems")
set(CPACK_PACKAGE_FILE_NAME "mirdain_forward-macos-${package_arch}")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist")
set(CPACK_DMG_VOLUME_NAME "Mirdain Forward")
set(CPACK_DMG_FORMAT UDZO)
set(CPACK_PACKAGING_INSTALL_PREFIX /)
# Package only Forward, excluding third-party development files and libraries.
set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_CURRENT_BINARY_DIR};${PROJECT_NAME};Forward;/")
set(CPACK_VERBATIM_VARIABLES TRUE)
include(CPack)
