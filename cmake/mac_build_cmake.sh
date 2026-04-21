REPO_ROOT_DIR="$(
    cd "$(dirname "$0")/.."
    pwd
)"

mkdir -p $REPO_ROOT_DIR/build
cd $REPO_ROOT_DIR/build
cmake .. -DQt6_DIR=~/Qt/6.9.1/macos/lib/cmake/Qt6/ -GNinja \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -Wno-dev
ninja
