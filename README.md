# retrocli

retrocli is a CLI libretro frontend, based on [nanoarch from Higor Eurípedes](https://github.com/heuripedes/nanoarch).
It uses ncurses for capturing input and rendering the game and alsa lib for playing the audio.

## Building and running

Requires `nix`.

1. Build

```sh
nix-build
```

2. Run

```
./result <core> <rom>
```
