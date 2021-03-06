#!/bin/bash
##
##  Copyright (c) 2012 The WebM project authors. All Rights Reserved.
##
##  Use of this source code is governed by a BSD-style license
##  that can be found in the LICENSE file in the root of the source
##  tree. An additional intellectual property rights grant can be found
##  in the file PATENTS.  All contributing project authors may
##  be found in the AUTHORS file in the root of the source tree.

# A script to generate an Omaha config WebM QuickTime.
#
# Example:
#   generate_omaha_config.sh webmquicktime.config
#
set -e

if [[ $(basename $(pwd)) != "installer" ]] || \
    [[ $(basename $(dirname $(pwd))) != "webmquicktime" ]]; then
  echo "$(basename $0) must be run from webmquicktime/installer"
  exit 1
fi

source scripts/build/read_bundle_plist.sh
source scripts/build/util.sh
file_exists "$(which openssl)" || die "openssl does not exist in PATH."

# generate_config <|COMPONENT|> <|DMG_BASE|> <|CONFIG_NAME|>
#
# Generates Omaha server configuration for |COMPONENT|. |COMPONENT| must be
# a bundle. |DMG_BASE| is the file name part of the DMG file in which
# |COMPONENT| will be distributed. |CONFIG_NAME| is the friendly name for the
# Omaha configuration.
#
# All parameters are required. |generate_config| will |die| when a param is
# missing, empty, or invalid.
generate_config() {
  local readonly COMPONENT="$1"
  file_exists "${COMPONENT}" || die "${COMPONENT} does not exist."
  local readonly DMG_BASE="$2"
  [[ -n "${DMG_BASE}" ]] || die "empty DMG_BASE in ${FUNCNAME}."
  local readonly CONFIG_NAME="$3"
  [[ -n "${CONFIG_NAME}" ]] || die "empty CONFIG_NAME in ${FUNCNAME}."

  # Read the application ID and version from |COMPONENT|.
  local readonly BUNDLE_ID=$(read_bundle_id "${COMPONENT}")
  [[ -n "${BUNDLE_ID}" ]] || die "empty BUNDLE_ID in ${FUNCNAME}."
  local readonly VERSION=$(read_bundle_version "${COMPONENT}")
  [[ -n "${VERSION}" ]] || die "empty VERSION in ${FUNCNAME}."

  # TODO(tomfinegan): Integrate the DMG build and config generation, and pass
  #                   the actual DMG file name around instead of this building
  #                   it nonsense.
  # Build |DMG_FILE| using |VERSION| and |DMG_BASE|.
  local readonly DMG_FILE="${DMG_BASE}_${VERSION}.dmg"
  file_exists "${DMG_FILE}" || die "${DMG_FILE} does not exist."

  # Get the size of |DMG_FILE|.
  local readonly DMG_SIZE="$(stat -f%z "${DMG_FILE}")"
  [[ -n "${DMG_SIZE}" ]] || die "empty DMG_SIZE in ${FUNCNAME}."

  # Generate the omaha hash (base64 encoded SHA1 hash of |DMG_FILE|).
  local readonly OMAHA_HASH=$(openssl sha1 -binary "${DMG_FILE}" \
      | openssl base64)
  [[ -n "${OMAHA_HASH}" ]] || die "empty OMAHA_HASH in ${FUNCNAME}."

  cat <<__END_CONFIG
# Autogenerated Omaha config for ${CONFIG_NAME}
AppId: "${BUNDLE_ID}"
ConfigName: "${CONFIG_NAME} $(date +%Y-%m-%d)"

Rule {
  Test {Version: "[-${VERSION})"}

  Update {
    Codebase: "https://dl.google.com/dl/webmquicktime/${DMG_FILE}"
    Hash: "${OMAHA_HASH}"
    Size: "${DMG_SIZE}"
  }
}
__END_CONFIG
}

readonly CONFIG_FILE="$1"
if [[ $# -lt 1 ]]; then
  errorlog "usage: $(basename $0) <config file>"
  die "config file is empty."
fi

generate_config "../build/Release/AWebM.component" \
    "webm_quicktime_installer" "WebM QuickTime Installer" \
    > "${CONFIG_FILE}"
debuglog "Done, wrote config to ${CONFIG_FILE}."
