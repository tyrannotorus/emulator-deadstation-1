# Deadstation's frontend: its menu and pages, drawn on the CPU, shared by Deadstation and every recomp module
# (one look, one menu: a module adds no menu of its own). Needs nothing of the emulator.
add_library(deadstation_frontend STATIC
        canvas.cpp
        canvas.h
        catalog.h
        download.cpp
        download.h
        options.cpp
        options.h
        recomp.cpp
        recomp.h
        sha256.cpp
        sha256.h
        ui.cpp
        ui.h)
target_include_directories(deadstation_frontend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(deadstation_frontend PUBLIC cxx_std_17)
# FreeType for the text (the sysroot's; the device has the same libfreetype.so.6, 6.12.1); the device's
# libcurl and zlib for the recomp modules' downloads
find_package(ZLIB REQUIRED)
find_library(DEADSTATION_FREETYPE NAMES libfreetype.so REQUIRED)
if(NOT DEADSTATION_CURL_INCLUDE OR NOT DEADSTATION_CURL_LIBRARY)
	message(FATAL_ERROR "DEADSTATION_CURL_INCLUDE / DEADSTATION_CURL_LIBRARY: libcurl's headers and the device's libcurl.so.4")
endif()
add_library(deadstation_curl SHARED IMPORTED)
set_target_properties(deadstation_curl PROPERTIES IMPORTED_LOCATION ${DEADSTATION_CURL_LIBRARY}
		INTERFACE_INCLUDE_DIRECTORIES ${DEADSTATION_CURL_INCLUDE})
target_include_directories(deadstation_frontend PRIVATE ${CMAKE_SYSROOT}/usr/include/freetype2)
target_link_libraries(deadstation_frontend PRIVATE ${DEADSTATION_FREETYPE} deadstation_curl ZLIB::ZLIB)

# the menu's font, Noto Sans Bold (SIL OFL 1.1, fonts/NotoSans-OFL.txt), into the library itself
set(DEADSTATION_FONT_ZIP ${CMAKE_CURRENT_SOURCE_DIR}/../fonts/NotoSans-Bold.ttf.zip)
set(DEADSTATION_FONT ${CMAKE_CURRENT_BINARY_DIR}/font/NotoSans-Bold.ttf)
if(NOT EXISTS ${DEADSTATION_FONT} OR ${DEADSTATION_FONT_ZIP} IS_NEWER_THAN ${DEADSTATION_FONT})
    file(ARCHIVE_EXTRACT INPUT ${DEADSTATION_FONT_ZIP} DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/font)
endif()
target_compile_definitions(deadstation_frontend PRIVATE DEADSTATION_FONT="${DEADSTATION_FONT}")
set_source_files_properties(ui.cpp PROPERTIES OBJECT_DEPENDS ${DEADSTATION_FONT})
