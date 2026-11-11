# Use Prusa's writer directly: its Convert component assumes PrusaSlicer input.
FetchContent_Declare(libbgcode
    URL https://codeload.github.com/prusa3d/libbgcode/tar.gz/d4da9073616d70a43c151e8c1d7fbff879d2e08a
    URL_HASH SHA256=d26778992d44b7cfaab99b542d871355f7a66210a512a5503a5b8a941a7d409b
    SOURCE_SUBDIR forward-writer-only
    TIMEOUT 60
)
FetchContent_Declare(heatshrink
    URL https://codeload.github.com/atomicobject/heatshrink/tar.gz/refs/tags/v0.4.1
    URL_HASH SHA256=7529a1c8ac501191ad470b166773364e66d9926aad632690c72c63a1dea7e9a6
    TIMEOUT 60
)
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "Build zlib examples" FORCE)
FetchContent_Declare(zlib
    URL https://codeload.github.com/madler/zlib/tar.gz/refs/tags/v1.3.1
    URL_HASH SHA256=17e88863f3600672ab49182f217281b6fc4d3c762bde361935e436a95214d05c
    TIMEOUT 60
)
FetchContent_MakeAvailable(libbgcode heatshrink zlib)
set_target_properties(zlib PROPERTIES EXCLUDE_FROM_ALL TRUE)

set(bgcode_include "${CMAKE_CURRENT_BINARY_DIR}/bgcode-include")
foreach(header heatshrink_common.h heatshrink_config.h heatshrink_encoder.h heatshrink_decoder.h)
    configure_file("${heatshrink_SOURCE_DIR}/${header}" "${bgcode_include}/heatshrink/${header}" COPYONLY)
endforeach()
add_library(mirdain_bgcode STATIC
    "${libbgcode_SOURCE_DIR}/src/LibBGCode/core/core.cpp"
    "${libbgcode_SOURCE_DIR}/src/LibBGCode/binarize/binarize.cpp"
    "${libbgcode_SOURCE_DIR}/src/LibBGCode/binarize/meatpack.cpp"
    "${heatshrink_SOURCE_DIR}/heatshrink_encoder.c"
    "${heatshrink_SOURCE_DIR}/heatshrink_decoder.c"
)
include(GenerateExportHeader)
generate_export_header(mirdain_bgcode BASE_NAME BGCODE_CORE EXPORT_FILE_NAME "${bgcode_include}/core/export.h")
generate_export_header(mirdain_bgcode BASE_NAME BGCODE_BINARIZE EXPORT_FILE_NAME "${bgcode_include}/binarize/export.h")
include("${libbgcode_SOURCE_DIR}/cmake/ProjectVersion.cmake")
target_compile_definitions(mirdain_bgcode PRIVATE HEATSHRINK_DYNAMIC_ALLOC=1 LibBGCode_VERSION="${LibBGCode_VERSION}")
target_include_directories(mirdain_bgcode PUBLIC "${libbgcode_SOURCE_DIR}/src/LibBGCode" "${bgcode_include}")
target_compile_features(mirdain_bgcode PRIVATE cxx_std_17)
target_link_libraries(mirdain_bgcode PRIVATE zlibstatic)

set(bgcode_licenses)
foreach(dependency libbgcode heatshrink zlib)
    set(notice "${CMAKE_CURRENT_BINARY_DIR}/licenses/${dependency}-LICENSE")
    configure_file("${${dependency}_SOURCE_DIR}/LICENSE" "${notice}" COPYONLY)
    list(APPEND bgcode_licenses "${notice}")
endforeach()
