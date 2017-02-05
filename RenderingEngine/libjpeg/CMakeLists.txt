cmake_minimum_required(VERSION 3.6)
project(libjpeg)

set(JPEG_LIB_NAME JPEG)

set(JPEG_SOURCE_FILES
        RenderingEngine/libjpeg/jaricom.c
        RenderingEngine/libjpeg/jcapimin.c
        RenderingEngine/libjpeg/jcapistd.c
        RenderingEngine/libjpeg/jcarith.c
        RenderingEngine/libjpeg/jccoefct.c
        RenderingEngine/libjpeg/jccolor.c
        RenderingEngine/libjpeg/jcdctmgr.c
        RenderingEngine/libjpeg/jchuff.c
        RenderingEngine/libjpeg/jcinit.c
        RenderingEngine/libjpeg/jcmainct.c
        RenderingEngine/libjpeg/jcmarker.c
        RenderingEngine/libjpeg/jcmaster.c
        RenderingEngine/libjpeg/jcomapi.c
        RenderingEngine/libjpeg/jcparam.c
        RenderingEngine/libjpeg/jcprepct.c
        RenderingEngine/libjpeg/jcsample.c
        RenderingEngine/libjpeg/jctrans.c
        RenderingEngine/libjpeg/jdapimin.c
        RenderingEngine/libjpeg/jdapistd.c
        RenderingEngine/libjpeg/jdarith.c
        RenderingEngine/libjpeg/jdatadst.c
        RenderingEngine/libjpeg/jdatasrc.c
        RenderingEngine/libjpeg/jdcoefct.c
        RenderingEngine/libjpeg/jdcolor.c
        RenderingEngine/libjpeg/jddctmgr.c
        RenderingEngine/libjpeg/jdhuff.c
        RenderingEngine/libjpeg/jdinput.c
        RenderingEngine/libjpeg/jdmainct.c
        RenderingEngine/libjpeg/jdmarker.c
        RenderingEngine/libjpeg/jdmaster.c
        RenderingEngine/libjpeg/jdmerge.c
        RenderingEngine/libjpeg/jdpostct.c
        RenderingEngine/libjpeg/jdsample.c
        RenderingEngine/libjpeg/jdtrans.c
        RenderingEngine/libjpeg/jerror.c
        RenderingEngine/libjpeg/jfdctflt.c
        RenderingEngine/libjpeg/jfdctfst.c
        RenderingEngine/libjpeg/jfdctint.c
        RenderingEngine/libjpeg/jidctflt.c
        RenderingEngine/libjpeg/jidctfst.c
        RenderingEngine/libjpeg/jidctint.c
        RenderingEngine/libjpeg/jmemmgr.c
        RenderingEngine/libjpeg/jmemnobs.c
        RenderingEngine/libjpeg/jquant1.c
        RenderingEngine/libjpeg/jquant2.c
        RenderingEngine/libjpeg/jutils.c)

add_library(${JPEG_LIB_NAME} STATIC ${JPEG_SOURCE_FILES})
