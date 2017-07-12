#!/usr/bin/env bash

set -e

#Set Script Name variable
SCRIPT=$(basename "${BASH_SOURCE[0]}")

function usage {
    echo "Usage: ${SCRIPT} [-b] [-m] [-c <realm-cocoa-folder>]"
    echo ""
    echo "Arguments:"
    echo "   -b : build from source. If absent it will expect prebuilt packages"
    echo "   -m : build for macOS only"
    echo "   -c : copy core to the specified folder instead of packaging it"
    exit 1;
}

# Parse the options
while getopts ":bmc:" opt; do
    case "${opt}" in
        b) BUILD=1;;
        m) MACOS_ONLY=1;;
        c) COPY=1
           DESTINATION=${OPTARG};;
        *) usage;;
    esac
done

shift $((OPTIND-1))

BUILD_TYPES=( Release Debug )
[[ -z $MACOS_ONLY ]] && PLATFORMS=( macos ios watchos tvos ) || PLATFORMS=( macos )

if [[ ! -z $BUILD ]]; then
    for bt in "${BUILD_TYPES[@]}"; do
        for p in "${PLATFORMS[@]}"; do
            [[ $p != "macos" && $bt = "Debug" ]] && prefix="MinSize" || prefix=""
            folder_name="build-${p}-${prefix}${bt}"
            mkdir -p "${folder_name}"
            (
                cd "${folder_name}" || exit 1
                rm -f realm-core-*-devel.tar.gz
                cmake -D CMAKE_TOOLCHAIN_FILE="../tools/cmake/${p}.toolchain.cmake" \
                      -D CMAKE_BUILD_TYPE="${prefix}${bt}" \
                      -D REALM_VERSION="$(git describe)" \
                      -D REALM_SKIP_SHARED_LIB=ON \
                      -D REALM_BUILD_LIB_ONLY=ON \
                      -G Xcode ..
                cmake --build . --config "${prefix}${bt}" --target package
            )
        done
    done
fi

rm -rf core
mkdir core

filename=$(find "build-macos-Release" -maxdepth 1 -type f -name "realm-core-Release-*-Darwin-devel.tar.gz")
tar -C core -zxvf "${filename}" include LICENSE CHANGELOG.md

for bt in "${BUILD_TYPES[@]}"; do
    [[ "$bt" = "Release" ]] && suffix="" || suffix="-dbg"
    for p in "${PLATFORMS[@]}"; do
        [[ $p = "macos" ]] && infix="macosx" || infix="${p}"
        [[ $p != "macos" && $bt = "Debug" ]] && prefix="MinSize" || prefix=""
        filename=$(find "build-${p}-${prefix}${bt}" -maxdepth 1 -type f -name "realm-core-*-devel.tar.gz")
        if [[ -z $filename ]]; then
            filename=$(find "build-${p}-${prefix}${bt}" -maxdepth 1 -type f -name "realm-core-*.tar.gz")
        fi
        tar -C core -zxvf "${filename}" "lib/librealm${suffix}.a"
        mv "core/lib/librealm${suffix}.a" "core/librealm-${infix}${suffix}.a"
        rm -rf core/lib
    done
done

ln -s librealm-macosx.a core/librealm.a
ln -s librealm-macosx-dbg.a core/librealm-dbg.a

if [[ ! -z $COPY ]]; then
    rm -rf "${DESTINATION}/core"
    mkdir -p "${DESTINATION}"
    cp -R core "${DESTINATION}"
else
    v=$(git describe --tags)
    rm -f "realm-core-cocoa-${v}.tar.xz"
    tar -cJvf "realm-core-cocoa-${v}.tar.xz" core
fi
