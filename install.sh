#!/usr/bin/env bash

set -u

repo='Salvadego/bared'
branch='dev'

base_url="https://raw.githubusercontent.com/$repo/$branch"
tar_url="https://github.com/$repo/archive/$branch.tar.gz"

dest_dir=${DEST:-.}

modules=(
        barebuf.h
        barebuilder.h
        bareio.h
        baremath.h
        bareos.h
        barepath.h
        barepool.h
        bareproc.h
        baresig.h
        barestd.h
        barestrs.h
        baresync.h
        barethreads.h
        baretime.h
        bareutf8.h
)

core_modules=(
        barestd.h
        bareio.h
        baremath.h
        barestrs.h
        bareos.h
)

thread_modules=(
        barethreads.h
        baresync.h
        barepool.h
)

usage() {
        echo "usage:"
        echo "  bare all"
        echo "  bare core"
        echo "  bare threads"
        echo "  bare <file> [file...]"
        echo
        echo "env:"
        echo "  DEST=dir     install directory (default: current)"
}

ensure_dest() {
        [[ -d $dest_dir ]] || {
                mkdir -p "$dest_dir" || {
                        echo "failed to create directory: $dest_dir"
                                return 1
                }
        }
}

download_file() {
        local file=$1
        local url="$base_url/$file"
        local out="$dest_dir/$file"

        echo "fetching $file"

        curl -fsSL "$url" -o "$out" || {
                echo "failed: $file"
                        return 1
        }
}

install_list() {
        local file

        for file in "$@"; do
                download_file "$file" || return 1
        done
}

install_all_curl() {
        install_list "${modules[@]}"
}

install_tarball() {
        local tmp
        tmp=$(mktemp -d) || {
                echo "failed to create temp dir"
                        return 1
        }

        echo "downloading tarball..."

        curl -fsSL "$tar_url" | tar xz -C "$tmp" || {
                echo "failed to fetch tarball"
                return 1
        }

        local root
        root="$tmp/$(ls "$tmp")"

        local file
        for file in "${modules[@]}"; do
                if [[ -f "$root/$file" ]]; then
                        echo "installing $file"
                        cp "$root/$file" "$dest_dir/" || return 1
                fi
        done
}

install_all() {
        install_tarball || {
                echo "falling back to individual downloads"
                        install_all_curl
        }
}

main() {
        ensure_dest || return 1

        if (($# == 0)); then
                usage
                return 1
        fi

        case $1 in
                all)
                        install_all
                        ;;
                core)
                        install_list "${core_modules[@]}"
                        ;;
                threads)
                        install_list "${thread_modules[@]}"
                        ;;
                *)
                        install_list "$@"
                        ;;
        esac
}

main "$@"
