{
  description = "Commission MC seed finder (CUDA build)";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  inputs.flake-utils.url = "github:numtide/flake-utils";

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs {
          inherit system;
          config.allowUnfree = true;
        };
        cuda = pkgs.cudaPackages_12;
        # CUDA 12.x supports up to gcc 13 as host compiler
        hostStdenv = pkgs.gcc13Stdenv;
      in
      {
        devShells.default = (pkgs.mkShell.override { stdenv = hostStdenv; }) {
          packages = [
            pkgs.gnumake
            cuda.cudatoolkit
          ];
          # nvcc's -ccbin will pick this up via the makefile
          CXX = "${pkgs.gcc13}/bin/g++";
          CC = "${pkgs.gcc13}/bin/gcc";

          # CUDA runtime needs the host driver's libcuda.so, which on NixOS
          # lives in /run/opengl-driver/lib (not in the cudatoolkit).
          LD_LIBRARY_PATH = "${cuda.cudatoolkit}/lib:/run/opengl-driver/lib";
        };
      });
}
