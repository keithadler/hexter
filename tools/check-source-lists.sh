#!/bin/sh
# The engine's sources are listed in three places: the desktop CMake build, the
# Android CMake build, and the iOS XcodeGen spec. Adding a file to one and not
# the others builds fine on the machine you are sitting at and fails on a CI
# runner an hour later, which has now happened twice. This compares them.
#
# The Android and iOS builds also compile hexter_engine.c, which the desktop
# build keeps in a separate library, so that one name is expected to differ.
set -e
root=$(dirname "$0")/..
cd "$root"

list_desktop() {
    sed -n '/^set(HEXTER_CORE_SOURCES/,/)/p' CMakeLists.txt |
        grep -oE 'src/[a-z0-9_]+\.c' | sed 's|src/||' | sort
}

list_android() {
    sed -n '/add_library(hexter_engine STATIC/,/)/p' android/app/src/main/cpp/CMakeLists.txt |
        grep -oE 'src/[a-z0-9_]+\.c' | sed 's|src/||' | grep -v '^hexter_engine\.c$' | sort
}

list_ios() {
    sed -n '/name: Engine/,/compilerFlags/p' ios/project.yml |
        grep -oE '^ *- [a-z0-9_]+\.c' | sed 's/^ *- //' | grep -v '^hexter_engine\.c$' | sort
}

status=0
for other in android ios; do
    if ! diff -u /dev/stdin /dev/fd/3 3<<EOF2 <<EOF1 > /tmp/srclist.diff 2>&1
$(list_$other)
EOF2
$(list_desktop)
EOF1
    then
        echo "The $other source list does not match the desktop one:"
        sed -n '3,$p' /tmp/srclist.diff
        status=1
    fi
done

if [ $status -eq 0 ]; then
    echo "source lists agree: $(list_desktop | wc -l | tr -d ' ') engine sources in all three"
fi
exit $status
