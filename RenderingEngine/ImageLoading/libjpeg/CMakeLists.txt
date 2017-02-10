cmake_minimum_required(VERSION 3.6)
project(libjpeg)

set(CMAKE_VERBOSE_MAKEFILE false)
set(CMAKE_POSITION_INDEPENDENT_CODE ON) # -fPIC

set(JPEG_SOURCE_FILES
        jerror.h
        cderror.h
        jconfig.h
        jdct.h
        jinclude.h
        jmemsys.h
        jmorecfg.h
        jpegint.h
        jpeglib.h
        jversion.h
        jaricom.c
        jcapimin.c
        jcapistd.c
        jcarith.c
        jccoefct.c
        jccolor.c
        jcdctmgr.c
        jchuff.c
        jcinit.c
        jcmainct.c
        jcmarker.c
        jcmaster.c
        jcomapi.c
        jcparam.c
        jcprepct.c
        jcsample.c
        jctrans.c
        jdapimin.c
        jdapistd.c
        jdarith.c
        jdatadst.c
        jdatasrc.c
        jdcoefct.c
        jdcolor.c
        jddctmgr.c
        jdhuff.c
        jdinput.c
        jdmainct.c
        jdmarker.c
        jdmaster.c
        jdmerge.c
        jdpostct.c
        jdsample.c
        jdtrans.c
        jerror.c
        jfdctflt.c
        jfdctfst.c
        jfdctint.c
        jidctflt.c
        jidctfst.c
        jidctint.c
        jmemmgr.c
        jmemnobs.c
        jquant1.c
        jquant2.c
        jutils.c)

add_library(libjpeg STATIC ${JPEG_SOURCE_FILES})
set_target_properties(libjpeg PROPERTIES PREFIX "")
