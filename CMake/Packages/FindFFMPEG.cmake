# - Try to find FFmpeg libraries (libavcodec, libavformat, libavutil, libswscale, libswresample)
# Once done this will define
#
#  FFMPEG_FOUND - system has FFmpeg
#  FFMPEG_INCLUDE_DIRS - the FFmpeg include directories
#  FFMPEG_LIBRARIES - the libraries needed to use FFmpeg

find_package(PkgConfig QUIET)

if(PKG_CONFIG_FOUND)
    pkg_check_modules(PC_AVCODEC QUIET libavcodec)
    pkg_check_modules(PC_AVFORMAT QUIET libavformat)
    pkg_check_modules(PC_AVUTIL QUIET libavutil)
    pkg_check_modules(PC_SWSCALE QUIET libswscale)
    pkg_check_modules(PC_SWRESAMPLE QUIET libswresample)
endif()

find_path(AVCODEC_INCLUDE_DIR
    NAMES libavcodec/avcodec.h
    HINTS ${PC_AVCODEC_INCLUDEDIR} ${PC_AVCODEC_INCLUDE_DIRS}
    PATH_SUFFIXES ffmpeg
)

find_path(AVFORMAT_INCLUDE_DIR
    NAMES libavformat/avformat.h
    HINTS ${PC_AVFORMAT_INCLUDEDIR} ${PC_AVFORMAT_INCLUDE_DIRS}
    PATH_SUFFIXES ffmpeg
)

find_path(AVUTIL_INCLUDE_DIR
    NAMES libavutil/avutil.h
    HINTS ${PC_AVUTIL_INCLUDEDIR} ${PC_AVUTIL_INCLUDE_DIRS}
    PATH_SUFFIXES ffmpeg
)

find_path(SWSCALE_INCLUDE_DIR
    NAMES libswscale/swscale.h
    HINTS ${PC_SWSCALE_INCLUDEDIR} ${PC_SWSCALE_INCLUDE_DIRS}
    PATH_SUFFIXES ffmpeg
)

find_path(SWRESAMPLE_INCLUDE_DIR
    NAMES libswresample/swresample.h
    HINTS ${PC_SWRESAMPLE_INCLUDEDIR} ${PC_SWRESAMPLE_INCLUDE_DIRS}
    PATH_SUFFIXES ffmpeg
)

find_library(AVCODEC_LIBRARY
    NAMES avcodec
    HINTS ${PC_AVCODEC_LIBDIR} ${PC_AVCODEC_LIBRARY_DIRS}
)

find_library(AVFORMAT_LIBRARY
    NAMES avformat
    HINTS ${PC_AVFORMAT_LIBDIR} ${PC_AVFORMAT_LIBRARY_DIRS}
)

find_library(AVUTIL_LIBRARY
    NAMES avutil
    HINTS ${PC_AVUTIL_LIBDIR} ${PC_AVUTIL_LIBRARY_DIRS}
)

find_library(SWSCALE_LIBRARY
    NAMES swscale
    HINTS ${PC_SWSCALE_LIBDIR} ${PC_SWSCALE_LIBRARY_DIRS}
)

find_library(SWRESAMPLE_LIBRARY
    NAMES swresample
    HINTS ${PC_SWRESAMPLE_LIBDIR} ${PC_SWRESAMPLE_LIBRARY_DIRS}
)

set(FFMPEG_INCLUDE_DIRS
    ${AVCODEC_INCLUDE_DIR}
    ${AVFORMAT_INCLUDE_DIR}
    ${AVUTIL_INCLUDE_DIR}
    ${SWSCALE_INCLUDE_DIR}
    ${SWRESAMPLE_INCLUDE_DIR}
)
list(REMOVE_DUPLICATES FFMPEG_INCLUDE_DIRS)

set(FFMPEG_LIBRARIES
    ${AVFORMAT_LIBRARY}
    ${AVCODEC_LIBRARY}
    ${AVUTIL_LIBRARY}
    ${SWSCALE_LIBRARY}
    ${SWRESAMPLE_LIBRARY}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFMPEG
    REQUIRED_VARS
        FFMPEG_LIBRARIES
        FFMPEG_INCLUDE_DIRS
        AVCODEC_LIBRARY
        AVFORMAT_LIBRARY
        AVUTIL_LIBRARY
        SWSCALE_LIBRARY
        SWRESAMPLE_LIBRARY
        AVCODEC_INCLUDE_DIR
        AVFORMAT_INCLUDE_DIR
        AVUTIL_INCLUDE_DIR
        SWSCALE_INCLUDE_DIR
        SWRESAMPLE_INCLUDE_DIR
)
