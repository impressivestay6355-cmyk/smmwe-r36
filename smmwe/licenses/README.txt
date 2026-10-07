Third-party components shipped with this port
=============================================

bin/gmloadernext.aarch64  gmloader-next (JohnnyonFlame / PortsMaster), GPL-2.0
                      -> LICENSE.gmloader-next.md (modified for this port)
                      statically links: libcurl (curl license, LICENSE.curl.txt),
                      Mbed TLS (Apache-2.0, LICENSE.mbedtls.txt),
                      Dear ImGui (MIT, LICENSE.imgui.txt),
                      libzip (BSD-3, LICENSE.libzip.txt),
                      nlohmann/json (MIT, LICENSE.nlohmann-json.txt),
                      libbsd / libmd (BSD-style, LICENSE.libbsd.txt, LICENSE.libmd.txt)
bin/smmwe-install     libzip (BSD-3), SDL2 (zlib, system library)
lib/arm64-v8a/        Android runtime libraries needed by the game:
                      libc++_shared.so, libcompiler_rt.so (LLVM, Apache-2.0 with
                      LLVM exception, LICENSE.libc++.txt), libm.so (Android bionic,
                      Apache-2.0 / BSD, LICENSE.Apache-2.0.txt)
resources/fonts/NotoSans-Regular.ttf  SIL Open Font License 1.1 (OFL-NotoSans.txt)
resources/cacert.pem  Mozilla CA certificate bundle (MPL-2.0, LICENSE.MPL-2.0.txt)
resources/fonts/NewSuperMarioFontU.ttf  "New Super Mario Font U" (2013), from
                      dafont.com; free for non-commercial use only
                      ("You may not use this font for commercial purposes").
bin/smmwe-install     also uses stb_truetype (public domain / MIT, Sean Barrett)

The game itself (APK) is not included and belongs to its authors.
