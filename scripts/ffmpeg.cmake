if(WIN32)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FFMPEG_PKG QUIET libavcodec libavformat libavutil libswscale)
    endif()

    if(FFMPEG_PKG_FOUND)
        target_include_directories(MoleHole PRIVATE ${FFMPEG_PKG_INCLUDE_DIRS})
        link_directories(${FFMPEG_PKG_LIBRARY_DIRS})
        target_link_libraries(MoleHole ${FFMPEG_PKG_LIBRARIES})
        message(STATUS "FFmpeg: found via pkg-config")
    else()
        if(NOT DEFINED FFMPEG_DIR AND DEFINED ENV{FFMPEG_DIR})
            set(FFMPEG_DIR $ENV{FFMPEG_DIR})
        endif()

        set(_ffmpeg_hints
            ${FFMPEG_DIR}
            "C:/ffmpeg"
            "C:/Program Files/ffmpeg"
        )

        find_path(FFMPEG_INCLUDE_DIR
            NAMES libavcodec/avcodec.h avcodec.h
            HINTS ${_ffmpeg_hints}
            PATH_SUFFIXES include include/ffmpeg
        )
        find_library(AVCODEC_LIB  NAMES avcodec avcodec.lib libavcodec-58 libavcodec-59   HINTS ${_ffmpeg_hints} PATH_SUFFIXES lib lib64 lib/x64)
        find_library(AVFORMAT_LIB NAMES avformat avformat.lib libavformat-58 libavformat-59 HINTS ${_ffmpeg_hints} PATH_SUFFIXES lib lib64 lib/x64)
        find_library(AVUTIL_LIB   NAMES avutil avutil.lib libavutil-56 libavutil-57         HINTS ${_ffmpeg_hints} PATH_SUFFIXES lib lib64 lib/x64)
        find_library(SWSCALE_LIB  NAMES swscale swscale.lib libswscale-5 libswscale-6       HINTS ${_ffmpeg_hints} PATH_SUFFIXES lib lib64 lib/x64)

        if(FFMPEG_INCLUDE_DIR AND AVCODEC_LIB AND AVFORMAT_LIB AND AVUTIL_LIB)
            target_include_directories(MoleHole PRIVATE ${FFMPEG_INCLUDE_DIR})
            target_link_libraries(MoleHole ${AVCODEC_LIB} ${AVFORMAT_LIB} ${AVUTIL_LIB} ${SWSCALE_LIB})
            message(STATUS "FFmpeg: found and linked (include=${FFMPEG_INCLUDE_DIR})")

            # Copy DLLs to output dir
            get_filename_component(_ffmpeg_lib_dir "${AVCODEC_LIB}" DIRECTORY)
            get_filename_component(_ffmpeg_root    "${_ffmpeg_lib_dir}" DIRECTORY)
            set(_ffmpeg_dll_dir "${_ffmpeg_root}/bin")
            if(EXISTS "${_ffmpeg_dll_dir}")
                file(GLOB _ffmpeg_dlls "${_ffmpeg_dll_dir}/*.dll")
                foreach(_dll ${_ffmpeg_dlls})
                    add_custom_command(TARGET MoleHole POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "$<TARGET_FILE_DIR:MoleHole>"
                        COMMENT "Copying FFmpeg DLL: ${_dll}"
                    )
                endforeach()
                message(STATUS "FFmpeg DLLs will be copied from: ${_ffmpeg_dll_dir}")
            else()
                message(WARNING "FFmpeg DLL directory not found: ${_ffmpeg_dll_dir}")
            endif()
        else()
            message(WARNING "FFmpeg not found! Video export will be unavailable.")
        endif()
    endif()
endif()

if(UNIX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED libavcodec libavformat libavutil libswscale)
    target_include_directories(MoleHole PRIVATE ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(MoleHole ${FFMPEG_LIBRARIES})
    message(STATUS "FFmpeg: ${FFMPEG_libavcodec_VERSION}")
endif()

