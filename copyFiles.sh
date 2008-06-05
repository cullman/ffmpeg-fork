#!/bin/sh
OSXBMC_HOME=~/Code/C++/Eclipse/XBMC-Fork/xbmc/lib/libffmpeg-OSX

#cp `find . -name avcodec.h -or -name avformat.h -or -name avio.h -or -name avutil.h -or -name common.h -or -name integer.h -or -name intfloat_readwrite.h -or -name log.h -or -name mathematics.h -or -name mem.h -or -name postprocess.h -or -name rational.h -or -name rgb2rgb.h -or -name rtp.h -or -name rtsp.h -or -name rtspcodes.h -or -name swscale.h` ~/Code/C++/Eclipse/XBMC-Fork/xbmc/lib/libffmpeg-OSX
#cp ${OSXBMC_HOME}/avcodec.h ${OSXBMC_HOME}/libavcodec
#cp ${OSXBMC_HOME}/avutil.h ${OSXBMC_HOME}/libavutil

cp ./libavcodec/libavcodec.a ${OSXBMC_HOME}/libavcodec-osx.a
cp ./libavformat/libavformat.a ${OSXBMC_HOME}/libavformat-osx.a
cp ./libavutil/libavutil.a ${OSXBMC_HOME}/libavutil-osx.a
cp ./libpostproc/libpostproc.a ${OSXBMC_HOME}/libpostproc-osx.a
cp ./libswscale/libswscale.a ${OSXBMC_HOME}/libswscale-osx.a
