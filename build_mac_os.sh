
#bash

#
# Clean up
#
rm libfgrabber.dylib
rm -R out
mkdir out

#
# Build
#
clang -shared -Ofast -I${JAVA_HOME}/include -I${JAVA_HOME}/include/darwin -fpic src/frame_grabber.c src/com_plato_utils_fgrabber_FrameGrabber.c -o ./out/libfgrabber.dylib -lavcodec -lavformat -lavutil
clang -g src/main.c ./out/libfgrabber.dylib -lavutil -o ./out/test_frame_grabber

#
# Deploy
#
cp out/libfgrabber.dylib .