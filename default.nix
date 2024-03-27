{ pkgs ? import <nixpkgs> {} }: pkgs.stdenv.mkDerivation {
  name = "retrocli";
  src = ./.;
  strictDeps = true;

  buildInputs = with pkgs; [
    alsaLib.dev
    ncurses.dev
  ];

  buildPhase = ''
  gcc retrocli.c -Wall -lncurses -lasound -o retrocli
  '';

  installPhase = ''
  cp retrocli $out
  '';
}