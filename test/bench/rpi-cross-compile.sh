#!/usr/bin/env bash

set -euo pipefail

rpidir=./rpi
toolsdir="${rpidir}/tools"

mkdir -p "${toolsdir}"

if [ ! -d "${toolsdir}" ]; then
  git clone https://github.com/raspberrypi/tools "${toolsdir}"
  cd "${toolsdir}"
  git checkout master
else
  cd "${toolsdir}"
  git fetch
  git checkout master
fi
