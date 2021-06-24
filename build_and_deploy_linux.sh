#!/bin/bash
clang -shared -fpic -Ofast src/frame_grabber.c -o ./libfgrabber.so -lavcodec -lavformat -lavutil
cp ../libfgrabber.so /lib/x86_64-linux-gnu/.