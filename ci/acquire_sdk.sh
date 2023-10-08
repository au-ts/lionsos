#!/bin/bash

set -x

if ! command -v jq &> /dev/null
then
    echo "jq could not be found, it must be available to run the script."
    exit 1
fi

if ! command -v curl &> /dev/null
then
    echo "curl could not be found, it must be available to run the script."
    exit 1
fi

SDK_PATH=$1
GITHUB_TOKEN=$2
SDK_TARGET=$3
MICROKIT_REPO="Ivan-Velickovic/microkit"
# zip is the only available option
ARCHIVE_FORMAT="zip"

if [[ $SDK_TARGET == "macos-x86-64" ]]; then
    ARTIFACT_INDEX=0
elif [[ $SDK_TARGET == "macos-aarch64" ]]; then
    ARTIFACT_INDEX=1
elif [[ $SDK_TARGET == "linux-x86-64" ]]; then
    ARTIFACT_INDEX=2
else
    echo "Unknown SDK_TARGET: $SDK_TARGET"
    exit 1
fi

# @ivanv: should assert that the SDK target matches what we expect after
# we actually get the artifact. Or, even better, we find a way to extract
# the artifact ID that matches our target

ARTIFACT_ID=`curl \
  -H "Accept: application/vnd.github+json" \
  -H "Authorization: Bearer ${GITHUB_TOKEN}"\
  -H "X-GitHub-Api-Version: 2022-11-28" \
  https://api.github.com/repos/$MICROKIT_REPO/actions/artifacts | jq ".artifacts[$ARTIFACT_INDEX].id"`

echo "Downloading SDK with artifact ID: ${ARTIFACT_ID}"
curl \
  -L \
  -u "Ivan-Velickovic:${GITHUB_TOKEN}" \
  -o $SDK_PATH \
  -H "Accept: application/vnd.github+json" \
  -H "X-GitHub-Api-Version: 2022-11-28" \
  https://api.github.com/repos/$MICROKIT_REPO/actions/artifacts/$ARTIFACT_ID/$ARCHIVE_FORMAT
