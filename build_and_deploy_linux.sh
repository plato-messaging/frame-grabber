#!/bin/bash
clang -shared -Ofast -I${JAVA_HOME}/include -I${JAVA_HOME}/include/darwin -fpic src/frame_grabber.c src/com_plato_utils_fgrabber_FrameGrabber.c -o ./out/libfgrabber.dylib -lavcodec -lavformat -lavutil

cp ./libfgrabber.so /lib/x86_64-linux-gnu/.
