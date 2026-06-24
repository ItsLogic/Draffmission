{
  description = "Commission MC seed finder (CUDA build)";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  inputs.flake-utils.url = "github:numtide/flake-utils";

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs {
          inherit system;
          config = {
            allowUnfree = true;
            allowUnsupportedSystem = true;
          };
        };
        cuda = pkgs.cudaPackages_13_2.overrideScope (final: prev: {
          cuda_compat = pkgs.runCommand "cuda_compat-fake" { } ''
            mkdir -p $out
          '';
          autoAddCudaCompatRunpathHook = pkgs.makeSetupHook
            { name = "auto-add-cuda-compat-runpath-hook-fake"; }
            (pkgs.writeText "auto-add-cuda-compat-runpath-hook.sh" "");
        });
        hostStdenv = cuda.backendStdenv;
        hostCC = hostStdenv.cc;
      in
      {
        devShells.default = (pkgs.mkShell.override { stdenv = hostStdenv; }) {
          packages = [
            pkgs.gnumake
            cuda.cudatoolkit
          ];
          CXX = "${hostCC}/bin/g++";
          CC = "${hostCC}/bin/gcc";
          LD_LIBRARY_PATH = "${cuda.cudatoolkit}/lib:/run/opengl-driver/lib";
        };
      });
}
