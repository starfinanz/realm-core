#!/bin/bash

set -e

#Set Script Name variable
SCRIPT=$(basename "${BASH_SOURCE[0]}")

# Number of cores
CORES=$(getconf _NPROCESSORS_ONLN)

function usage {
    echo "$Usage: ${SCRIPT} -t <build_type> -o <target_os> -v <version> [-a <android_abi>]"
    echo ""
    echo "Arguments:"
    echo "   build_type=<Release|Debug|MinSizeDebug>"
    echo "   target_os=<android|ios|watchos|tvos>"
    echo "   android_abi=<armeabi|armeabi-v7a|x86|mips|x86_64|arm64-v8a>"
    exit 1;
}

# Parse the options
while getopts ":o:a:t:v:" opt; do
    case "${opt}" in
        o)
            OS=${OPTARG}
            [ "${OS}" == "android" ] ||
            [ "${OS}" == "ios" ] ||
            [ "${OS}" == "watchos" ] ||
            [ "${OS}" == "tvos" ] || usage
            ;;
        a)
            ARCH=${OPTARG}
            [ "${ARCH}" == "armeabi" ] ||
            [ "${ARCH}" == "armeabi-v7a" ] ||
            [ "${ARCH}" == "x86" ] ||
            [ "${ARCH}" == "mips" ] ||
            [ "${ARCH}" == "x86_64" ] ||
            [ "${ARCH}" == "arm64-v8a" ] || usage
            ;;
        t)
            BUILD_TYPE=${OPTARG}
            [ "${BUILD_TYPE}" == "Debug" ] ||
            [ "${BUILD_TYPE}" == "MinSizeDebug" ] ||
            [ "${BUILD_TYPE}" == "Release" ] || usage
            ;;
        v) VERSION=${OPTARG};;
        *) usage;;
    esac
done

shift $((OPTIND-1))

# Check for obligatory fields
if [ -z "${OS}" ] || [ -z "${BUILD_TYPE}" ]; then
    echo "ERROR: options -o, -t and -v are always needed";
    usage
fi

# Check for android-related obligatory fields
if [ "${OS}" == "android" ] && [ -z "${ARCH}" ]; then
    echo "ERROR: option -a is needed for android builds";
    usage
fi

if [ "${OS}" == "android" ]; then
    mkdir -p "build-android-${ARCH}-${BUILD_TYPE}"
    cd "build-android-${ARCH}-${BUILD_TYPE}" || exit 1
    cmake -D CMAKE_TOOLCHAIN_FILE=../tools/cmake/android.toolchain.cmake \
          -D CMAKE_INSTALL_PREFIX=install \
          -D CMAKE_BUILD_TYPE="${BUILD_TYPE}" \
          -D ANDROID_ABI="${ARCH}" \
          -D REALM_ENABLE_ENCRYPTION=1 \
          -D REALM_VERSION="${VERSION}" \
          -D CPACK_SYSTEM_NAME="Android-${ARCH}" \
          ..

    make -j "${CORES}" -l "${CORES}" VERBOSE=1
    make package
else
    mkdir -p "build-${OS}-${BUILD_TYPE}"
    cd "build-${OS}-${BUILD_TYPE}" || exit 1
    case "${OS}" in
        ios) SDK="iphone";;
        watchos) SDK="watch";;
        tvos) SDK="appletv";;
    esac

    [[ "${BUILD_TYPE}" = "Release" ]] && suffix="" || suffix="-dbg"

    cmake -D CMAKE_TOOLCHAIN_FILE="../tools/cmake/${OS}.toolchain.cmake" \
          -D CMAKE_INSTALL_PREFIX="$(pwd)/install" \
          -D CMAKE_BUILD_TYPE="${BUILD_TYPE}" \
          -D REALM_NO_TESTS=1 \
          -D REALM_VERSION="${VERSION}" \
          -G Xcode ..

    xcodebuild -sdk "${SDK}os" \
               -configuration "${BUILD_TYPE}" \
               ONLY_ACTIVE_ARCH=NO
    xcodebuild -sdk "${SDK}simulator" \
               -configuration "${BUILD_TYPE}" \
               ONLY_ACTIVE_ARCH=NO
    mkdir -p "src/realm/${BUILD_TYPE}"
    lipo -create \
         -output "src/realm/${BUILD_TYPE}/librealm${suffix}.a" \
         "src/realm/${BUILD_TYPE}-${SDK}os/librealm${suffix}.a" \
         "src/realm/${BUILD_TYPE}-${SDK}simulator/librealm${suffix}.a"
    xcodebuild -sdk "${SDK}os" \
               -configuration "${BUILD_TYPE}" \
               -target install \
               ONLY_ACTIVE_ARCH=NO
    xcodebuild -sdk "${SDK}simulator" \
               -configuration "${BUILD_TYPE}" \
               -target install \
               ONLY_ACTIVE_ARCH=NO
    mkdir -p install/lib
    cp "src/realm/${BUILD_TYPE}/librealm${suffix}.a" install/lib
    cd install || exit 1
    tar -cvJf "realm-core-${BUILD_TYPE}-${VERSION}-${SDK}os.tar.xz" lib include
    mv ./*.tar.xz ..
fi
