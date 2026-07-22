#!/bin/sh

set -eu

if [ "$#" -ne 4 ]; then
	echo "usage: $0 BINARY APP_BUNDLE ICON_PNG INFO_PLIST" >&2
	exit 2
fi

binary="$1"
app_bundle="$2"
icon_png="$3"
info_plist="$4"

case "$app_bundle" in
	*.app) ;;
	*)
		echo "refusing to replace non-.app path: $app_bundle" >&2
		exit 2
		;;
esac

for required in "$binary" "$icon_png" "$info_plist"; do
	if [ ! -f "$required" ]; then
		echo "required app bundle input not found: $required" >&2
		exit 1
	fi
done

rm -rf "$app_bundle"
mkdir -p "$app_bundle/Contents/MacOS" "$app_bundle/Contents/Resources"
cp "$binary" "$app_bundle/Contents/MacOS/webcool"
chmod 0755 "$app_bundle/Contents/MacOS/webcool"
cp "$info_plist" "$app_bundle/Contents/Info.plist"
cp "$icon_png" "$app_bundle/Contents/Resources/webcool-icon-1024.png"
touch "$app_bundle"
