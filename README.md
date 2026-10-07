# SMM: World Engine — R36S port

Port by Impressive Stay. Super Mario Maker: World Engine created by Franyer Farías, developed by the SMM:WE team.

SMM:WE is a free fan game inspired by Super Mario Maker. This port brings
its Android APK to the R36S through PortMaster,
using [gmloader-next](https://github.com/PortsMaster/gmloader-next).
Online play, login, an on-screen keyboard and a pointer for the touch menus
all work.

This repository contains only the loader and port files: no SMM:WE code,
assets or APKs. You bring your own APK. This is a fan
project, not affiliated with Nintendo or the SMM:WE team
(see [LEGAL.md](LEGAL.md)).

It's made for any 64-bit handheld with PortMaster. So far I've tested it on
ArkOS and dArkOS.

## Install

1. Extract `smmwe.zip` into your `ports/` folder.
2. Drop the SMM:WE arm64 APK (Fullscreen or Keep Aspect Ratio) into `ports/smmwe/Setup/`.
3. Launch **SMM World Engine**. The first start takes about a minute, then it's ready.

## Account

You'll need an SMM:WE account and Wi-Fi to play online.
Here's how to create one: https://youtu.be/Cmlpp2RoutE

## Controls

| Button | Action |
| --- | --- |
| START | pause / confirm |
| Right stick | pointer |
| R3 | click |
| L2 | pointer on / off |
| SELECT + START | quit |

The on-screen keyboard pops up by itself whenever the game asks for text.

## Building

See [src/BUILD.md](src/BUILD.md).

## License

GPL-2.0, see [LICENSE](LICENSE). Credits in [CREDITS.md](CREDITS.md) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Have fun building!
