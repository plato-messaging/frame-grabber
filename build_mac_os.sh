
#bash
clang -shared -Ofast -fpic src/frame_grabber.c  -o .out/libfgrabber.dylib -lavcodec -lavformat -lavutil
clang -g src/main.c ./out/libfgrabber.dylib -lavutil -o ./out/test_frame_grabber
