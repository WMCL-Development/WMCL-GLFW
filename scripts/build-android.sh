#!/usr/bin/env bash
# Builds WMCL's single-window GLFW shim for Android arm64.
#
# Two link modes are supported:
#   * NDK mode:          --ndk /path/to/android-ndk-r27
#                        The normal, reproducible cross build (needs the x86_64 NDK
#                        host toolchain).
#   * Bionic sysroot:    --sysroot /path/to/aarch64-sysroot --bionic-libc /path/to/libc.so
#                        For hosts that already carry an Android arm64 sysroot. It links
#                        directly against a real Bionic libc, so the result is still a
#                        Bionic shared object; it just does not use the NDK driver.
set -euo pipefail

usage() {
    echo "Usage: $0 --out DIR (--ndk NDK_DIR | --sysroot DIR --bionic-libc LIBC_SO [--cc GCC]) --glfw-headers DIR" >&2
    exit 2
}
out= ndk= sysroot= bionic= headers= cc_override=
while (( $# )); do
    (( $# >= 2 )) || usage
    case "$1" in
        --out) out=$2 ;;
        --ndk) ndk=$2 ;;
        --sysroot) sysroot=$2 ;;
        --bionic-libc) bionic=$2 ;;
        --glfw-headers) headers=$2 ;;
        --cc) cc_override=$2 ;;
        *) usage ;;
    esac
    shift 2
done
[[ -n "$out" && -n "$headers" ]] || usage
repo=$(cd -- "$(dirname -- "$0")/.." repo=$(cd -- "$(dirname -- "$0")/.." && pwd -P)repo=$(cd -- "$(dirname -- "$0")/.." && pwd -P) pwd -P)
source_c="$repo/src/wmcl_glfw.c"
header="$headers/GLFW/glfw3.h"
[[ -f "$source_c" && -f "$header" ]] || { echo "missing source or GLFW headers" >&2; exit 2; }
printf '%s  %s\n' aa370985f6b493bbe0358a36ab49f5780a6397c0209c6d98a62143dea595b73c "$header" | sha256sum --check --status ||
    { echo "GLFW headers must be the pinned upstream 3.4 glfw3.h" >&2; exit 2; }

mkdir -p -- "$out"
tmp=$(mktemp -d "$out/.wmcl-glfw.XXXXXXXX")
trap 'rm -rf -- "$tmp"' EXIT
mkdir -p "$tmp/GLLFW"
cp -- "$headers"/GLFW/*.h "$tmp/GLLFW/"; mv "$tmp/GLLFW" "$tmp/GLFW"

common=(-std=c11 -O2 -Wall -Wextra -Werror -fPIC -I "$tmp"
        -Wl,--no-undefined -Wl,-z,max-page-size=16384 -Wl,-soname,libwmcl_glfw.so)
if [[ -n "$ndk" ]]; then
    host_tag=$([[ $(uname -s) == Linux ]] && echo linux-x86_64 || echo darwin-x86_64)
    cc="$ndk/toolchains/llvm/prebuilt/$host_tag/bin/aarch64-linux-android26-clang"
    [[ -x "$cc" ]] || { echo "NDK compiler not found: $cc" >&2; exit 2; }
    "$cc" "${common[@]}" -shared "$source_c" -o "$tmp/libwmcl_glfw.so"
elif [[ -n "$sysroot" && -n "$bionic" ]]; then
    cc=${cc_override:-$(command -v aarch64-linux-gnu-gcc-14 || command -v aarch64-linux-gnu-gcc || true)}
    [[ -n "$cc" && -f "$bionic" ]] || { echo "missing cross compiler or Bionic libc" >&2; exit 2; }
    read -r -a sysinc <<<"$sysroot"
    "$cc" --sysroot="$sysinc" -B /usr/bin/ "${common[@]}" -fPIE -c "$source_c" -o "$tmp/wmcl_glfw.o"
    aarch64-linux-gnu-ld -shared --hash-style=both -z max-page-size=16384 --no-undefined \
        -soname libwmcl_glfw.so -o "$tmp/libwmcl_glfw.so" "$tmp/wmcl_glfw.o" "$bionic"
else
    usage
fi
mv -f -- "$tmp/libwmcl_glfw.so" "$out/libwmcl_glfw.so"
sha256sum -- "$out/libwmcl_glfw.so"
