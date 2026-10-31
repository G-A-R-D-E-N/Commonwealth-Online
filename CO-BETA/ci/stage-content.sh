#!/usr/bin/env bash
# Stages the CO-BETA content tree for content-manifest fingerprinting, mirroring
# what CO-BETA/stage-mod.bat puts into Data\ - the ESP plugins, asset folders,
# and compiled/source Papyrus - minus:
#   - the four client binaries (CommonwealthOnline.dll,
#     CommonwealthOnline-NG.dll, CommonwealthOnline-OG.dll,
#     commonwealth_online_gns_bridge.dll), which are
#     excluded by name from the fingerprint contract, and
#   - the private CommonwealthOnlineUI submodule package, which CI cannot fetch
#     (submodules are not checked out on the runners).
# Prints the staged directory path on stdout; progress notes go to stderr.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"

ESP_DIR="$ROOT_DIR/creation-kit/esp"
ASSETS_DIR="$ROOT_DIR/creation-kit/assets"
COMPILED_DIR="$ROOT_DIR/creation-kit/scripts/compiled"
SOURCE_DIR="$ROOT_DIR/creation-kit/scripts/source"

STAGE_DIR="$(mktemp -d "${TMPDIR:-/tmp}/co-content-stage.XXXXXX")"

stage_papyrus() {
    local source_dir="$1"
    local dest_dir="$2"
    local pattern="$3"

    [ -d "$source_dir" ] || return 0
    while IFS= read -r -d '' file; do
        local base rel dest_file
        base="$(basename "$file")"
        # Reference CoSync* scripts are dead content; stage-mod.bat skips them.
        if [[ "${base,,}" == cosync* ]]; then
            echo "skipping reference script: $base" >&2
            continue
        fi
        rel="${file#"$source_dir"/}"
        dest_file="$STAGE_DIR/$dest_dir/$rel"
        mkdir -p "$(dirname "$dest_file")"
        cp "$file" "$dest_file"
    done < <(find "$source_dir" -type f -name "$pattern" -print0)
}

# Plugins: every top-level .esp is content. None at all is not a valid package.
shopt -s nullglob
esp_files=("$ESP_DIR"/*.esp)
shopt -u nullglob
if [ "${#esp_files[@]}" -eq 0 ]; then
    echo "error: no .esp plugins under $ESP_DIR to stage" >&2
    rm -rf "$STAGE_DIR"
    exit 1
fi
for esp in "${esp_files[@]}"; do
    cp "$esp" "$STAGE_DIR/"
done

# Asset folders, same lookup order as stage-mod.bat (top-level, then the
# DataFolder fallback). RawAssets is a source tree, not staged content.
for folder in Materials Meshes Textures; do
    source_dir="$ASSETS_DIR/$folder"
    [ -d "$source_dir" ] || source_dir="$ASSETS_DIR/DataFolder/$folder"
    [ -d "$source_dir" ] || continue
    cp -R "$source_dir" "$STAGE_DIR/"
done

stage_papyrus "$COMPILED_DIR" Scripts '*.pex'
stage_papyrus "$SOURCE_DIR" Scripts/Source/User '*.psc'

printf '%s\n' "$STAGE_DIR"
