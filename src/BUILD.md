# Building

Both binaries are aarch64, built against glibc 2.31 headers with GCC 10 so
that they need **GLIBC 2.30** at most (ArkOS and older firmware).

## gmloadernext.aarch64

1. Clone [PortsMaster/gmloader-next](https://github.com/PortsMaster/gmloader-next)
   at commit `2a9aa04` (with submodules) and apply the patch:
   ```sh
   git clone --recursive https://github.com/PortsMaster/gmloader-next
   cd gmloader-next && git checkout 2a9aa04
   git apply ../src/gmloader-next.patch
   ```
2. Put [Dear ImGui v1.91.9](https://github.com/ocornut/imgui/releases/tag/v1.91.9)
   in `3rdparty/imgui`.
3. Build the static dependencies for aarch64 into `<deps>`:
   - Mbed TLS 3.6.2 with `MBEDTLS_HAVE_TIME_DATE` **disabled** (certificates
     are checked, their dates are not: handhelds often have no real-time
     clock);
   - curl 8.10.1, HTTP/HTTPS only, Mbed TLS backend, static.
4. Build:
   ```sh
   make -f Makefile.gmloader ARCH=aarch64-linux-gnu \
     CC=aarch64-linux-gnu-gcc-10 CXX=aarch64-linux-gnu-g++-10 \
     OPTM="-O2 -mcpu=cortex-a35 -mtune=cortex-a35 -I<deps>/include" \
     EXTRA_LDFLAGS="-static-libstdc++ -static-libgcc" \
     EXTRA_LIBS="<deps>/lib/libcurl.a <deps>/lib/libmbedtls.a <deps>/lib/libmbedx509.a <deps>/lib/libmbedcrypto.a <deps>/lib/libeverest.a <deps>/lib/libp256m.a"
   aarch64-linux-gnu-strip build/aarch64-linux-gnu/gmloader/gmloadernext.aarch64
   ```

What the patch adds:

- JNI: `java.lang.Object` / `Double`, object arrays by reference, `IsInstanceOf`.
- `RunnerJNILib`: `CallExtensionFunction` (the APK's Java extensions),
  HTTP requests (libcurl + Mbed TLS), text input, login and message dialogs,
  virtual keyboard, `OpenURL`, network status.
- Gamepad: built-in `gamepad_*` functions routed to the SDL gamepad code;
  `keyboard_key_press/release` from the game queued per frame as on Android.
- `android.media.AudioTrack`: ring buffer with an SDL callback.
- `gmloader/port_*.cpp`: on-screen keyboard, dialogs, file picker, pointer
  (Dear ImGui, GLES2), optional letterbox, FPS counter, quick exit.
- Case-insensitive file lookups in the game folders.

## smmwe-install

```sh
aarch64-linux-gnu-gcc-10 -O2 -mcpu=cortex-a35 -o smmwe-install \
  src/smmwe-install.c <deps>/lib/libzip.a -lSDL2 -lz -lm
```
libzip 1.11.4 (static); SDL2 and zlib from the system. `font8x8_basic.h`
and `stb_truetype.h` (public domain) must be next to the source.
