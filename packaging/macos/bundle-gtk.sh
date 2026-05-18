#!/usr/bin/env bash
set -euo pipefail

APP_BUNDLE="${1:-build/Splashy.app}"
EXECUTABLE="$APP_BUNDLE/Contents/MacOS/splashy"
CONTENTS="$APP_BUNDLE/Contents"
FRAMEWORKS="$CONTENTS/Frameworks"
RESOURCES="$CONTENTS/Resources"

rm -rf "$FRAMEWORKS" "$RESOURCES/lib/gtk-3.0" "$RESOURCES/lib/gdk-pixbuf-2.0" "$RESOURCES/share/glib-2.0"
mkdir -p "$FRAMEWORKS" "$RESOURCES/lib" "$RESOURCES/share"

is_system_library() {
    case "$1" in
        /System/*|/usr/lib/*) return 0 ;;
        *) return 1 ;;
    esac
}

needs_rpath() {
    local file="$1"
    local rpath="$2"
    ! otool -l "$file" | awk '$1 == "path" { print $2 }' | grep -qx "$rpath"
}

add_rpath_if_needed() {
    local file="$1"
    local rpath="$2"
    if needs_rpath "$file" "$rpath"; then
        install_name_tool -add_rpath "$rpath" "$file"
    fi
}

copy_library() {
    local lib="$1"

    if [[ "$lib" != /* ]] || is_system_library "$lib"; then
        return
    fi

    local name
    name="$(basename "$lib")"
    local dest="$FRAMEWORKS/$name"

    if [[ ! -f "$dest" ]]; then
        cp -p "$lib" "$dest"
        chmod u+w "$dest"

        while read -r dep; do
            copy_library "$dep"
        done < <(otool -L "$lib" | awk 'NR > 1 { print $1 }')

        install_name_tool -id "@rpath/$name" "$dest" 2>/dev/null || true
        add_rpath_if_needed "$dest" "@loader_path"

        while read -r dep; do
            if [[ "$dep" == /* ]] && ! is_system_library "$dep"; then
                install_name_tool -change "$dep" "@rpath/$(basename "$dep")" "$dest"
            fi
        done < <(otool -L "$dest" | awk 'NR > 1 { print $1 }')
    fi
}

while read -r dep; do
    copy_library "$dep"
done < <(otool -L "$EXECUTABLE" | awk 'NR > 1 { print $1 }')

add_rpath_if_needed "$EXECUTABLE" "@executable_path/../Frameworks"

while read -r dep; do
    if [[ "$dep" == /* ]] && ! is_system_library "$dep"; then
        install_name_tool -change "$dep" "@rpath/$(basename "$dep")" "$EXECUTABLE"
    fi
done < <(otool -L "$EXECUTABLE" | awk 'NR > 1 { print $1 }')

gtk_prefix="$(pkg-config --variable=prefix gtk+-3.0)"
gtk_binary_version="$(pkg-config --variable=gtk_binary_version gtk+-3.0)"
pixbuf_prefix="$(pkg-config --variable=prefix gdk-pixbuf-2.0)"
pixbuf_binary_version="$(pkg-config --variable=gdk_pixbuf_binary_version gdk-pixbuf-2.0)"
schemas_dir="$(pkg-config --variable=schemasdir gio-2.0)"

rsync -aL "$gtk_prefix/share/themes" "$RESOURCES/share/" 2>/dev/null || true
rsync -aL "$gtk_prefix/share/icons" "$RESOURCES/share/" 2>/dev/null || true
rsync -aL "$(dirname "$(dirname "$schemas_dir")")" "$RESOURCES/share/"

if [[ -d "$gtk_prefix/lib/gtk-3.0/$gtk_binary_version/immodules" ]]; then
    mkdir -p "$RESOURCES/lib/gtk-3.0/$gtk_binary_version"
    rsync -aL "$gtk_prefix/lib/gtk-3.0/$gtk_binary_version/immodules" "$RESOURCES/lib/gtk-3.0/$gtk_binary_version/"
    "$gtk_prefix/bin/gtk-query-immodules-3.0" "$RESOURCES/lib/gtk-3.0/$gtk_binary_version/immodules/"*.so > "$RESOURCES/lib/gtk-3.0/$gtk_binary_version/immodules.cache"
    sed -i '' "s#$RESOURCES#@executable_path/../Resources#g" "$RESOURCES/lib/gtk-3.0/$gtk_binary_version/immodules.cache"
fi

if [[ -d "$pixbuf_prefix/lib/gdk-pixbuf-2.0/$pixbuf_binary_version/loaders" ]]; then
    mkdir -p "$RESOURCES/lib/gdk-pixbuf-2.0/$pixbuf_binary_version"
    rsync -aL "$pixbuf_prefix/lib/gdk-pixbuf-2.0/$pixbuf_binary_version/loaders" "$RESOURCES/lib/gdk-pixbuf-2.0/$pixbuf_binary_version/"
    "$pixbuf_prefix/bin/gdk-pixbuf-query-loaders" "$RESOURCES/lib/gdk-pixbuf-2.0/$pixbuf_binary_version/loaders/"*.so > "$RESOURCES/lib/gdk-pixbuf-2.0/$pixbuf_binary_version/loaders.cache"
    sed -i '' "s#$RESOURCES#@executable_path/../Resources#g" "$RESOURCES/lib/gdk-pixbuf-2.0/$pixbuf_binary_version/loaders.cache"
fi

find "$RESOURCES/lib" -type f \( -name '*.so' -o -name '*.dylib' \) -print0 | while IFS= read -r -d '' module; do
    chmod u+w "$module"
    install_name_tool -id "@loader_path/$(basename "$module")" "$module" 2>/dev/null || true
    while read -r dep; do
        copy_library "$dep"
    done < <(otool -L "$module" | awk 'NR > 1 { print $1 }')
    add_rpath_if_needed "$module" "@executable_path/../Frameworks"
    while read -r dep; do
        if [[ "$dep" == /* ]] && ! is_system_library "$dep"; then
            dep_name="$(basename "$dep")"
            if [[ -f "$(dirname "$module")/$dep_name" ]]; then
                install_name_tool -change "$dep" "@loader_path/$dep_name" "$module"
            else
                install_name_tool -change "$dep" "@rpath/$dep_name" "$module"
            fi
        fi
    done < <(otool -L "$module" | awk 'NR > 1 { print $1 }')
done

find "$APP_BUNDLE" -name .DS_Store -delete

echo "Bundled GTK libraries and resources into $APP_BUNDLE"
