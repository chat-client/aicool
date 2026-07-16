#!/bin/sh

set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"

"${SCRIPT_DIR}/build-mac.sh" --version 2.0.0 \
  --sign-app-identity F75A4786D88240E4B651D717C930D5F1E5B0CED4 \
  --sign-installer-identity 839AE2544C625D4D915BB495D9901217E76374BB \
  --notarize --notary-profile webcool-notary \
  --universal
