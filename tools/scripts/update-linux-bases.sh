#!/bin/bash
set -e

# The purpose of this script is to construct the base build images used
# by chef for building packages. For linux images, we fetch the base images
# from https://cdimage.ubuntu.com/ubuntu-base/releases/, then build bakectl
# for each of those inside containers matching each release and inject bakectl
# into each of the base images, repack them as tar.gz files.

CDIMAGE_RELEASE_URL="https://cdimage.ubuntu.com/ubuntu-base/releases/"
CDIMAGE_RELEASES=22,24,26

WORK_DIRECTORY="/tmp/chef-base-update"

# Expect to run on the host are lxd containers named
# 22-04, 24-04 and 26-04 that we can build in

# Resolve the host architecture without using dpkg
HOST_ARCH=$(uname -m)

# Map to the corresponding architecture used in the Ubuntu base images
case "$HOST_ARCH" in
    x86_64) HOST_ARCH="amd64" ;;
    aarch64) HOST_ARCH="arm64" ;;
    *) echo "Unsupported architecture: $HOST_ARCH" ; exit 1 ;;
esac

# ensure a clean working space to make sure we start fresh for each run
rm -rf "${WORK_DIRECTORY}"
mkdir -p "${WORK_DIRECTORY}"

for release in ${CDIMAGE_RELEASES//,/ }
do
    SUBVERSION=".1"
    case "$release" in
        22) SUBVERSION=".5" ;;
        24) SUBVERSION=".4" ;;
        26) SUBVERSION=".1" ;;
        *) echo "Unsupported release: $release" ; exit 1 ;;
    esac

    BASE_IMAGE_URL="${CDIMAGE_RELEASE_URL}${release}.04/release/ubuntu-base-${release}.04${SUBVERSION}-base-${HOST_ARCH}.tar.gz"
    LXD_NAME="${release}-04"
    

    # download the image and unpack it into a subdirectory of the work directory
    wget -O "${WORK_DIRECTORY}/ubuntu-base-${release}.04${SUBVERSION}-base-${HOST_ARCH}.tar.gz" "$BASE_IMAGE_URL"
    mkdir -p "${WORK_DIRECTORY}/rootfs"
    tar -xzf "${WORK_DIRECTORY}/ubuntu-base-${release}.04${SUBVERSION}-base-${HOST_ARCH}.tar.gz" -C "${WORK_DIRECTORY}/rootfs"
    rm "${WORK_DIRECTORY}/ubuntu-base-${release}.04${SUBVERSION}-base-${HOST_ARCH}.tar.gz"
    
    # Checkout chef into the container for this release
    lxc exec "${LXD_NAME}" -- bash -c "rm -rf /tmp/chef /tmp/chef-build"
    lxc exec "${LXD_NAME}" -- bash -c "git clone --recurse-submodules https://github.com/meulengracht/bake.git /tmp/chef"
    lxc exec "${LXD_NAME}" -- bash -c "mkdir -p /tmp/chef-build"

    # Build bakectl inside the container for this release
    lxc exec "${LXD_NAME}" -- bash -c "cd /tmp/chef-build && cmake ../chef"
    lxc exec "${LXD_NAME}" -- bash -c "cd /tmp/chef-build && make"

    # Copy bakectl and pid1d
    lxc file pull "${LXD_NAME}/tmp/chef-build/bin/bakectl" "${WORK_DIRECTORY}/rootfs/usr/bin"
    lxc file pull "${LXD_NAME}/tmp/chef-build/bin/pid1d" "${WORK_DIRECTORY}/rootfs/usr/bin"

    # Repack the base image and remove the rootfs folder
    # We do not include the subversion as it's not interesting for the final image
    # and makes it easier for chef
    tar -czf "${WORK_DIRECTORY}/ubuntu-base-${release}.04-base-${HOST_ARCH}.tar.gz" -C "${WORK_DIRECTORY}/rootfs" .
    rm -rf "${WORK_DIRECTORY}/rootfs"
done
