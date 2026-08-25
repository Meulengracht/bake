#!/bin/bash

export GOROOT=/usr/local/go
export PATH=$PATH:$GOROOT/bin
# GOPATH is automatically set to $HOME/go, /chef/go for chef

GO_VERSION=go1.25.1
UBUNTU_VERSION=24.04
CHISEL_VERSION=v1.4.2

DIR="$1"
ARCH="$2"

# add libraries we may need here
PACKAGES=(
    bash_bins
    passwd_config
    libc6_config
    libc6_gconv
    libgcc-s1_libs
    libc-bin_locale
    libc-bin_nsswitch
    libpam-runtime_config
    netbase_default-hosts
    netbase_default-networks
)

# ensure go is available
wget "https://go.dev/dl/${GO_VERSION}.linux-amd64.tar.gz"
tar -C /usr/local -xzf "${GO_VERSION}.linux-amd64.tar.gz"

# ensure chisel is available
if ! command -v chisel >/dev/null 2>&1; then
    go install github.com/canonical/chisel/cmd/chisel@${CHISEL_VERSION}
fi

# build a whitespace separated list
delim=""
JOINED=""
for item in "${PACKAGES[@]}"; do
    JOINED="$JOINED$delim$item"
    delim=" "
done

# setup rootfs symlinks that are not created by chisel
install_merged_directory() {
  if [ ! -d "$DIR/$2" ]; then
    mkdir -p "$DIR/$2"
    chown "root:$4" "$DIR/$2"
    chmod "$3" "$DIR/$2"
  fi
  if [ ! -L "$DIR/$1" ]; then
    ln -sfn "$2" "$DIR/$1"
  fi
}

# create the directory
mkdir -p "$DIR"

/chef/go/bin/chisel cut --release "ubuntu-${UBUNTU_VERSION}" --root $DIR --arch $ARCH $JOINED

# ensure a user-merge system
install_merged_directory "bin" "usr/bin" 755 root
install_merged_directory "lib" "usr/lib" 755 root
install_merged_directory "lib64" "usr/lib64" 755 root
install_merged_directory "lib32" "usr/lib32" 755 root
install_merged_directory "sbin" "usr/sbin" 755 root
