#!/usr/bin/env bash

set -euo pipefail

repo='Salvadego/bared'
branch=${VERSION:-dev}

base_url="https://raw.githubusercontent.com/$repo/$branch"
tar_url="https://github.com/$repo/archive/$branch.tar.gz"
dest_dir=${DEST:-.}

modules=(
        barebuf.h barebuilder.h bareio.h    baremath.h
        bareos.h  barepath.h    barepool.h  bareproc.h
        baresig.h barestd.h     barestrs.h  baresync.h
        barethreads.h baretime.h bareutf8.h barenet.h
)

core_modules=(
        barestd.h baretime.h bareio.h
        barestrs.h bareutf8.h bareos.h
)

thread_modules=(
        barestd.h baresync.h barethreads.h barepool.h
)

usage() {
        cat <<EOF
usage:
  $0 all
  $0 core
  $0 threads
  $0 <file> [file...]

env:
  DEST=dir       install directory (default: current)
  VERSION=tag    branch or tag to download (default: dev)
EOF
}

ensure_dest() {
        [[ -d $dest_dir ]] || mkdir -p "$dest_dir" || {
                echo "error: failed to create: $dest_dir" >&2
                        return 1
        }
}

fetch() {
        local url=$1 out=$2
        if command -v curl &>/dev/null; then
                curl -fsSL "$url" -o "$out"
        elif command -v wget &>/dev/null; then
                wget -qO "$out" "$url"
        else
                echo "error: neither curl nor wget found" >&2
                return 1
        fi
}

download_file() {
        local file=$1
        echo "fetching $file"
        fetch "$base_url/$file" "$dest_dir/$file" || {
                echo "error: failed to fetch $file" >&2
                        return 1
        }
}

install_list() {
        local file
        for file in "$@"; do
                download_file "$file" || return 1
        done
}

install_tarball() {
        local tmp
        tmp=$(mktemp -d)
        trap 'rm -rf "$tmp"' EXIT

        echo "downloading tarball..."
        fetch "$tar_url" "$tmp/archive.tar.gz" || {
                echo "error: tarball download failed" >&2
                        return 1
        }

tar xz -C "$tmp" -f "$tmp/archive.tar.gz" || {
        echo "error: tarball extraction failed" >&2
        return 1
}

local root
root="$tmp/bared-$branch"
[[ -d $root ]] || root=$(find "$tmp" -maxdepth 1 -mindepth 1 -type d | head -1)

local file
for file in "${modules[@]}"; do
        if [[ -f "$root/$file" ]]; then
                echo "installing $file"
                cp "$root/$file" "$dest_dir/"
        fi
done
}

install_all() {
        install_tarball || {
                echo "falling back to individual downloads..."
                        install_list "${modules[@]}"
        }
}

main() {
        if [[ $# -eq 0 ]]; then
                usage
                return 1
        fi

        ensure_dest || return 1

        case $1 in
                all)     install_all ;;
                core)    install_list "${core_modules[@]}" ;;
                threads) install_list "${thread_modules[@]}" ;;
                *)       install_list "$@" ;;
        esac
}

main "$@"
