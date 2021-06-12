
#bash
clang -shared -fpic src/frame_grabber.c  -o ./libfgrabber.dylib -lavcodec -lavformat -lavutil
clang -g src/main.c libfgrabber.dylib -lavutil -o ./out/test_frame_grabber
mv libfgrabber.dylib ./out/.