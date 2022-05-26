# frame-grabber

A little program that grabs the first frame of a video and returns it in a JPEG format

Video is provided as a byte buffer and returns the frame as a byte buffer as well

### Usage
#### macOS
- Run `build.sh`
- Copy `out/libfgrabber.dylib` to `/usr/local/lib`

#### Linux
- Run `build_and_deploy_linux.sh`

*NB: `.vscode` intentionally added to repo to help whoever struggles setting up VSCode

### Ffmpeg dylib generation
- Run `./configure --prefix=/usr/local --enable-shared`(`enable-shared` to generate `*.dylib`)
- Run `make install`
