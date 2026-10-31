#!/bin/zsh
# Double-click this in Finder to play. It builds the engine on first run and
# then opens the demo launcher.
#
# The engine resolves scripts/ and assets/ against the working directory, so
# starting the binary from anywhere else opens an empty window. This pins the
# working directory to the repository before exec'ing it. Any arguments are
# passed through, so ./run.command --skeleton-demo works from a shell too.

cd "$(dirname "$0")" || exit 1

BIN=build/bin/fake2d_hello
if [[ ! -x $BIN ]]; then
    echo "No build found — compiling Fake2D (first run only)."
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release &&
        cmake --build build --target fake2d_hello -j 8 || {
        echo "Build failed; see the messages above."
        exit 1
    }
fi

exec ./$BIN "$@"
