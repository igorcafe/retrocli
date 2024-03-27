# retrocli

retrocli is a CLI libretro frontend, based on [nanoarch from Higor Eurípedes](https://github.com/heuripedes/nanoarch).
It uses ncurses for capturing input and rendering the game and alsa lib for playing the audio.

https://github.com/igoracmelo/retrocli/assets/85039990/548a90fb-a8f0-42cf-96c3-4edcf5314ed6


## Building and running

Requires [nix](https://nixos.org/download/).

1. Build

```sh
nix-build
```

2. Run

```
./result <core> <rom>
```
