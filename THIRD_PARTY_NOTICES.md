# Third-party notices

## gmloader-next

`smmwe/bin/gmloadernext.aarch64` is built from
[PortsMaster/gmloader-next](https://github.com/PortsMaster/gmloader-next)
(GPL-2.0) at upstream commit `2a9aa04`, **with the patch in
[src/gmloader-next.patch](src/gmloader-next.patch)** applied. Build steps:
[src/BUILD.md](src/BUILD.md).

## Statically linked libraries

- curl 8.10.1 (curl license)
- Mbed TLS 3.6.2 (Apache-2.0)
- Dear ImGui 1.91.9 (MIT)
- libzip 1.11.4 (BSD-3)
- nlohmann/json (MIT), libbsd / libmd (BSD-style), from gmloader-next
- LLVM libc++ / GCC runtime (static libstdc++, GCC Runtime Library Exception)

## Bundled files

- `smmwe/lib/arm64-v8a/`: Android runtime libraries the game needs
  (`libc++_shared.so`, `libcompiler_rt.so`: Apache-2.0 with LLVM exception;
  `libm.so`: Android bionic, Apache-2.0 / BSD).
- `smmwe/resources/fonts/NotoSans-Regular.ttf`: SIL Open Font License 1.1.
- `smmwe/resources/fonts/NewSuperMarioFontU.ttf`: "New Super Mario Font U",
  free for non-commercial use only.
- `smmwe/resources/cacert.pem`: Mozilla CA certificate bundle (MPL-2.0).

`smmwe/bin/smmwe-install` is built from [src/smmwe-install.c](src/smmwe-install.c)
with libzip (static) and the system SDL2 / zlib.

Full license texts: [smmwe/licenses](smmwe/licenses).
