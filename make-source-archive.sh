#!/bin/bash
set -e

ver="$1"

if test -z "$ver"; then
    echo "Version number argument missing."
    exit 1
fi

git-archive-all --prefix "cmus-plugin-libvgm-$ver/" "cmus-plugin-libvgm-$ver.tar"
