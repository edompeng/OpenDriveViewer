CURRENT_DIRECTORY="$(
    cd "$(dirname "$0")"
    pwd
)"

mkdir -p $CURRENT_DIRECTORY/build
cd $CURRENT_DIRECTORY/build
cmake .. -DQt6_DIR=~/Qt/6.9.1/macos/lib/cmake/Qt6/ -GNinja \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -Wno-dev
ninja
