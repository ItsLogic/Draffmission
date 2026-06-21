#!/usr/bin/env bash
set -euo pipefail

backend=auto
install_nix=0
install_apt=1
while [[ $# -gt 0 ]]; do
  case "$1" in
    --backend)
      if [[ $# -lt 2 ]]; then
        printf 'missing argument to --backend\n' >&2
        exit 2
      fi
      backend="$2"
      case "$backend" in
        auto|native|nix) ;;
        *)
          printf 'invalid --backend: %s\n' "$backend" >&2
          exit 2
          ;;
      esac
      shift 2
      ;;
    --install-nix)
      install_nix=1
      shift
      ;;
    --no-apt)
      install_apt=0
      shift
      ;;
    -h|--help)
      cat <<'EOF'
Usage: scripts/vast-init-build.sh [--backend auto|native|nix] [--install-nix] [--no-apt]

Initializes a Vast.ai/Linux machine/container for this project and builds the
binary. In Vast CUDA containers, the default auto backend builds natively with
the container's nvcc. On Nix/NixOS machines, it can build through the flake dev
shell.

Options:
  --backend auto   Prefer native nvcc when available, otherwise use Nix.
  --backend native Build with system/container nvcc, gcc/g++, and make.
  --backend nix    Build through nix develop.
  --install-nix   Install Nix using the Determinate Systems installer if nix is missing.
  --no-apt         Do not try to install missing apt packages.
EOF
      exit 0
      ;;
    *)
      printf 'unknown option: %s\n' "$1" >&2
      exit 2
      ;;
  esac
done

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

printf '==> Repository: %s\n' "$repo_root"

if [[ -d /usr/local/cuda/bin ]]; then
  export PATH="/usr/local/cuda/bin:$PATH"
fi
if [[ -d /usr/local/cuda/lib64 ]]; then
  export LD_LIBRARY_PATH="/usr/local/cuda/lib64:${LD_LIBRARY_PATH:-}"
fi

if command -v nvidia-smi >/dev/null 2>&1; then
  printf '==> NVIDIA devices visible:\n'
  nvidia-smi --query-gpu=index,name,driver_version,compute_cap --format=csv,noheader
else
  printf 'warning: nvidia-smi is not available on PATH\n' >&2
fi

have_native=0
if command -v nvcc >/dev/null 2>&1 && command -v make >/dev/null 2>&1; then
  have_native=1
fi

if [[ "$backend" == "auto" && "$have_native" -eq 1 ]]; then
  backend=native
elif [[ "$backend" == "auto" ]]; then
  backend=nix
fi

build_native() {
  if [[ "$install_apt" -eq 1 ]] && command -v apt-get >/dev/null 2>&1; then
    missing=()
    command -v make >/dev/null 2>&1 || missing+=(make)
    command -v gcc >/dev/null 2>&1 || missing+=(build-essential)
    command -v g++ >/dev/null 2>&1 || missing+=(build-essential)
    command -v git >/dev/null 2>&1 || missing+=(git)
    if [[ "${#missing[@]}" -gt 0 ]]; then
      printf '==> Installing missing build packages: %s\n' "${missing[*]}"
      if [[ "$(id -u)" -eq 0 ]]; then
        apt-get update
        DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${missing[@]}"
      elif command -v sudo >/dev/null 2>&1; then
        sudo apt-get update
        sudo DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${missing[@]}"
      else
        printf 'error: missing packages and sudo is unavailable\n' >&2
        exit 1
      fi
    fi
  fi

  if ! command -v nvcc >/dev/null 2>&1; then
    cat >&2 <<'EOF'
error: nvcc is not available.

For vastai/base-image:cuda-13.3.0-auto, check that /usr/local/cuda/bin exists
or run with a CUDA development image that includes nvcc.
EOF
    exit 1
  fi
  if ! command -v make >/dev/null 2>&1 || ! command -v g++ >/dev/null 2>&1 || ! command -v gcc >/dev/null 2>&1; then
    cat >&2 <<'EOF'
error: make/gcc/g++ are required for native container builds.

Install them with apt, or re-run without --no-apt so this script can install
minimal build dependencies.
EOF
    exit 1
  fi

  printf '==> nvcc version:\n'
  nvcc --version
  printf '==> Host compiler: '
  g++ --version | head -n 1

  printf '==> Building project natively inside CUDA container\n'
  make clean
  make -j"$(nproc)"
}

build_nix() {
  if ! command -v nix >/dev/null 2>&1; then
  if [[ "$install_nix" -eq 1 ]]; then
    printf '==> Installing Nix via Determinate Systems installer\n'
    curl --proto '=https' --tlsv1.2 -sSf -L https://install.determinate.systems/nix | sh -s -- install --no-confirm
    if [[ -e /nix/var/nix/profiles/default/etc/profile.d/nix-daemon.sh ]]; then
      # shellcheck source=/dev/null
      . /nix/var/nix/profiles/default/etc/profile.d/nix-daemon.sh
    fi
  else
    cat >&2 <<'EOF'
error: nix is not installed.

Run this script with --install-nix, or install Nix manually, then re-run:
  scripts/vast-init-build.sh --install-nix
EOF
    exit 1
  fi
  fi

  printf '==> Nix version: '
  nix --version

  printf '==> Building project in nix dev shell\n'
  nix --extra-experimental-features 'nix-command flakes' develop -c make clean
  nix --extra-experimental-features 'nix-command flakes' develop -c make -j"$(nproc)"
}

case "$backend" in
  native) build_native ;;
  nix) build_nix ;;
esac

printf '\n==> Build complete\n'
ls -lh ./main

visible_devices=""
if command -v nvidia-smi >/dev/null 2>&1; then
  visible_devices="$(nvidia-smi --query-gpu=index --format=csv,noheader | paste -sd, -)"
fi

cat <<EOF

Suggested multi-GPU run:
  ./main --device all --threads 8 --output output.txt

Alternative explicit-device run:
  ./main --device ${visible_devices:-0,1,2,3,4,5,6,7} --threads 8 --output output.txt

Nix run command, if you built with --backend nix:
  nix --extra-experimental-features 'nix-command flakes' develop -c ./main --device all --threads 8 --output output.txt

Notes:
  - Use --device all to use every CUDA device visible through CUDA_VISIBLE_DEVICES.
  - Increase --threads if CPU verification queue grows; decrease it if CPU threads contend with the GPU feeder.
EOF
