if (MINGW AND DEFINED FREETYPE_MINGW_ROOT)
    # [n64recomp-cross] The bundled freetype.lib is an MSVC static lib (pulls LIBCMT/OLDNAMES);
    # use a FreeType built with the same llvm-mingw toolchain instead.
    set(FREETYPE_INCLUDE_DIRS ${FREETYPE_MINGW_ROOT}/include/freetype2)
    set(FREETYPE_LIBRARIES "${FREETYPE_MINGW_ROOT}/lib/libfreetype.a")
else()
set(FREETYPE_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/lib/freetype-windows-binaries/include)
set(FREETYPE_LIBRARIES "${CMAKE_SOURCE_DIR}/lib/freetype-windows-binaries/release static/vs2015-2022/win64/freetype.lib")
endif()
set(FREETYPE_FOUND TRUE)
add_library(Freetype::Freetype STATIC IMPORTED)
set_target_properties(Freetype::Freetype PROPERTIES
	IMPORTED_LOCATION ${FREETYPE_LIBRARIES}
)
target_include_directories(Freetype::Freetype INTERFACE
	${FREETYPE_INCLUDE_DIRS}
)
