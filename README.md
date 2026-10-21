hi everyone :3

to compile ts, install msys2, then open a ucrt64 terminal and type:

`pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-glfw mingw-w64-ucrt-x86_64-ffmpeg mingw-w64-ucrt-x86_64-curl  make`

after that, type `make -B` to compile it

to compile ts on linux:

`sudo apt install build-essential pkg-config libglfw3-dev libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libavformat-dev libavcodec-dev libavutil-dev libswresample-dev libswscale-dev libssl-dev zlib1g-dev`

after that, type `make` to compile it, and `sudo apt install yt-dlp` for the url downloads

to make packages:

`make deb` / `make rpm` / `make aur` / `make tarball`

they end up in `dist/`
