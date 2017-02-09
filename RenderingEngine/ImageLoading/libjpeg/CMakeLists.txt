cmake_minimum_required(VERSION 3.6)
project(libjpeg)

set(JPEG_LIB_NAME JPEG)

set(JPEG_SOURCE_FILES
        RenderingEngine/ImageLoading/libjpeg/jerror.h
        RenderingEngine/ImageLoading/libjpeg/cderror.h
        RenderingEngine/ImageLoading/libjpeg/jconfig.h
        RenderingEngine/ImageLoading/libjpeg/jdct.h
        RenderingEngine/ImageLoading/libjpeg/jinclude.h
        RenderingEngine/ImageLoading/libjpeg/jmemsys.h
        RenderingEngine/ImageLoading/libjpeg/jmorecfg.h
        RenderingEngine/ImageLoading/libjpeg/jpegint.h
        RenderingEngine/ImageLoading/libjpeg/jpeglib.h
        RenderingEngine/ImageLoading/libjpeg/jversion.h
        RenderingEngine/ImageLoading/libjpeg/jaricom.c
        RenderingEngine/ImageLoading/libjpeg/jcapimin.c
        RenderingEngine/ImageLoading/libjpeg/jcapistd.c
        RenderingEngine/ImageLoading/libjpeg/jcarith.c
        RenderingEngine/ImageLoading/libjpeg/jccoefct.c
        RenderingEngine/ImageLoading/libjpeg/jccolor.c
        RenderingEngine/ImageLoading/libjpeg/jcdctmgr.c
        RenderingEngine/ImageLoading/libjpeg/jchuff.c
        RenderingEngine/ImageLoading/libjpeg/jcinit.c
        RenderingEngine/ImageLoading/libjpeg/jcmainct.c
        RenderingEngine/ImageLoading/libjpeg/jcmarker.c
        RenderingEngine/ImageLoading/libjpeg/jcmaster.c
        RenderingEngine/ImageLoading/libjpeg/jcomapi.c
        RenderingEngine/ImageLoading/libjpeg/jcparam.c
        RenderingEngine/ImageLoading/libjpeg/jcprepct.c
        RenderingEngine/ImageLoading/libjpeg/jcsample.c
        RenderingEngine/ImageLoading/libjpeg/jctrans.c
        RenderingEngine/ImageLoading/libjpeg/jdapimin.c
        RenderingEngine/ImageLoading/libjpeg/jdapistd.c
        RenderingEngine/ImageLoading/libjpeg/jdarith.c
        RenderingEngine/ImageLoading/libjpeg/jdatadst.c
        RenderingEngine/ImageLoading/libjpeg/jdatasrc.c
        RenderingEngine/ImageLoading/libjpeg/jdcoefct.c
        RenderingEngine/ImageLoading/libjpeg/jdcolor.c
        RenderingEngine/ImageLoading/libjpeg/jddctmgr.c
        RenderingEngine/ImageLoading/libjpeg/jdhuff.c
        RenderingEngine/ImageLoading/libjpeg/jdinput.c
        RenderingEngine/ImageLoading/libjpeg/jdmainct.c
        RenderingEngine/ImageLoading/libjpeg/jdmarker.c
        RenderingEngine/ImageLoading/libjpeg/jdmaster.c
        RenderingEngine/ImageLoading/libjpeg/jdmerge.c
        RenderingEngine/ImageLoading/libjpeg/jdpostct.c
        RenderingEngine/ImageLoading/libjpeg/jdsample.c
        RenderingEngine/ImageLoading/libjpeg/jdtrans.c
        RenderingEngine/ImageLoading/libjpeg/jerror.c
        RenderingEngine/ImageLoading/libjpeg/jfdctflt.c
        RenderingEngine/ImageLoading/libjpeg/jfdctfst.c
        RenderingEngine/ImageLoading/libjpeg/jfdctint.c
        RenderingEngine/ImageLoading/libjpeg/jidctflt.c
        RenderingEngine/ImageLoading/libjpeg/jidctfst.c
        RenderingEngine/ImageLoading/libjpeg/jidctint.c
        RenderingEngine/ImageLoading/libjpeg/jmemmgr.c
        RenderingEngine/ImageLoading/libjpeg/jmemnobs.c
        RenderingEngine/ImageLoading/libjpeg/jquant1.c
        RenderingEngine/ImageLoading/libjpeg/jquant2.c
        RenderingEngine/ImageLoading/libjpeg/jutils.c)

add_library(${JPEG_LIB_NAME} STATIC ${JPEG_SOURCE_FILES})
