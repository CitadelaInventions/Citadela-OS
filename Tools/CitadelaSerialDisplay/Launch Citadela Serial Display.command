#!/bin/zsh

# Finder opens .command files in Terminal. Keep all generated Python packages
# beside this launcher so the system Python installation is left untouched.
set -u

SCRIPT_DIR=${0:A:h}
VENV_DIR="$SCRIPT_DIR/.venv"

fail() {
    print -u2 -- ""
    print -u2 -- "$1"
    print -u2 -- ""
    read -r "?Press Return to close..."
    exit 1
}

PYTHON=""
python_candidates=(
    "${commands[python3]:-}"
    /Library/Frameworks/Python.framework/Versions/Current/bin/python3
    /opt/homebrew/bin/python3
    /usr/local/bin/python3
)

for candidate in $python_candidates; do
    if [[ -n "$candidate" && -x "$candidate" ]] &&
       "$candidate" -c 'import sys, tkinter; raise SystemExit(sys.version_info < (3, 9))' >/dev/null 2>&1; then
        PYTHON="$candidate"
        break
    fi
done

if [[ -z "$PYTHON" ]]; then
    fail "Citadela Serial Display needs Python 3.9 or newer with Tk. Install the macOS package from https://www.python.org/downloads/macos/ and run this launcher again."
fi

if [[ ! -x "$VENV_DIR/bin/python" ]]; then
    print -- "Preparing Citadela Serial Display for its first run..."
    "$PYTHON" -m venv "$VENV_DIR" || fail "Could not create the local Python environment."
fi

if ! "$VENV_DIR/bin/python" -c 'import PIL, serial' >/dev/null 2>&1; then
    print -- "Installing Citadela Serial Display dependencies..."
    "$VENV_DIR/bin/python" -m pip install --disable-pip-version-check -r "$SCRIPT_DIR/requirements.txt" ||
        fail "Could not install Pillow and pyserial. Check the internet connection and try again."
fi

cd "$SCRIPT_DIR" || fail "Could not open the Citadela Serial Display folder."
"$VENV_DIR/bin/python" "$SCRIPT_DIR/viewer.py" "$@"
status=$?
if (( status != 0 )); then
    fail "Citadela Serial Display exited with status $status."
fi
