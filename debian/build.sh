#!/bin/sh
set -eu

distro="${1:-$(lsb_release -sc 2>/dev/null)}"

package="vchef"
version="$(dpkg-parsechangelog -S Version | sed 's/-[0-9][0-9]*$//')"

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT

srcdir="$tmpdir/${package}-${version}"
mkdir -p "$srcdir"

# Export the superproject.
git archive HEAD | tar -x -C "$srcdir"

# Export each submodule at the exact commit recorded by HEAD.
for submodule in libs/vafs libs/gracht; do
    commit="$(git rev-parse "HEAD:${submodule}")"

    mkdir -p "$srcdir/$submodule"

    (
        cd "$submodule"
        git archive "$commit"
    ) | tar -x -C "$srcdir/$submodule"
done

tar -C "$tmpdir" \
    -czf "../${package}_${version}.orig.tar.gz" \
    "${package}-${version}"

echo "Created ../${package}_${version}.orig.tar.gz"

# back up the changelog
cp debian/changelog debian/changelog.orig
sed -i "s/UNRELEASED/${distro}/g" debian/changelog

# do the package build
dpkg-buildpackage -S -sa -d -k613EB7C143E6388E

# restore the changelog
mv debian/changelog.orig debian/changelog

