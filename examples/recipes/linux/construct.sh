#!/bin/bash

export GOROOT=/usr/local/go
export PATH=$PATH:$GOROOT/bin
# GOPATH is automatically set to $HOME/go, /chef/go for chef

GO_VERSION=go1.25.1
UBUNTU_VERSION=24.04

DIR="$1"
ARCH="$2"

# add libraries we may need here
PACKAGES=(
    bash_bins
)

# ensure go is available
wget "https://go.dev/dl/${GO_VERSION}.linux-amd64.tar.gz"
tar -C /usr/local -xzf "${GO_VERSION}.linux-amd64.tar.gz"

# ensure chisel is available
if ! command -v chisel >/dev/null 2>&1; then
    go install github.com/canonical/chisel/cmd/chisel@latest
fi

# build a whitespace separated list
delim=""
JOINED=""
for item in "${PACKAGES[@]}"; do
    JOINED="$JOINED$delim$item"
    delim=" "
done

/chef/go/bin/chisel cut --release "ubuntu-${UBUNTU_VERSION}" --root $DIR --arch $ARCH $JOINED
