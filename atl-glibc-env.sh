# Source before ./mach: rustup's toolchain is not on the login PATH.
#   . ./atl-glibc-env.sh && ./mach build
# MOZCONFIG is resolved from this script rather than a fixed path, so a clone
# anywhere works. Override ATL_SHIM_DIR if the shim trees are not in the default
# place the mozconfig looks (see mozconfig-atl-glibc).
_here="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
export MOZCONFIG="${MOZCONFIG:-$_here/mozconfig-atl-glibc}"
export PATH="$HOME/.cargo/bin:$PATH"
unset _here
