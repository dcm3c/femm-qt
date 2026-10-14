#!/usr/bin/env bash
# tag_release.sh -- tag a femm-qt release, refusing any tag that does not match
# the version the tagged commit itself declares.
#
#     packaging/tag_release.sh [-n] <commit>
#
# <commit> is a commit whose tree is the femm-qt source (CMakeLists.txt at its
# root) -- the release commit as published. The version is read from THAT
# commit's CMakeLists.txt, project(femm-qt VERSION x.y.z), never from the
# working tree, so the tag cannot disagree with what a download of it builds.
#
# The scheme is 5.0.YYYYMMDD with the release date (see the comment above
# project() in CMakeLists.txt); the tag is "v" plus that number. The script
# creates an annotated tag locally and prints the push command. It never
# pushes: publishing a tag is a separate, deliberate step.
#
#   -n   check only: report the tag that would be made, create nothing.
set -euo pipefail

dry=0
if [ "${1:-}" = "-n" ]; then dry=1; shift; fi
if [ $# -ne 1 ]; then
    echo "usage: $0 [-n] <commit>" >&2
    exit 2
fi
commit=$1

if ! sha=$(git rev-parse --verify -q "$commit^{commit}"); then
    echo "tag_release: '$commit' is not a commit" >&2
    exit 1
fi
if ! cm=$(git show "$sha:CMakeLists.txt" 2>/dev/null); then
    echo "tag_release: $commit has no CMakeLists.txt at its root -- pass the" >&2
    echo "  femm-qt-rooted release commit, not a commit of a larger tree" >&2
    exit 1
fi

ver=$(printf '%s\n' "$cm" |
      sed -n 's/^[[:space:]]*project([[:space:]]*femm-qt[[:space:]][[:space:]]*VERSION[[:space:]][[:space:]]*\([0-9.]*\).*/\1/p' |
      head -1)
if [ -z "$ver" ]; then
    echo "tag_release: no 'project(femm-qt VERSION ...)' line in $commit's CMakeLists.txt" >&2
    exit 1
fi

# major.minor.YYYYMMDD, and a date that could be one.
if ! printf '%s' "$ver" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]{8}$'; then
    echo "tag_release: version '$ver' is not major.minor.YYYYMMDD" >&2
    exit 1
fi
ymd=${ver##*.}
mm=${ymd:4:2}; dd=${ymd:6:2}
if [ "$((10#$mm))" -lt 1 ] || [ "$((10#$mm))" -gt 12 ] ||
   [ "$((10#$dd))" -lt 1 ] || [ "$((10#$dd))" -gt 31 ]; then
    echo "tag_release: version '$ver' does not end in a valid YYYYMMDD date" >&2
    exit 1
fi

tag="v$ver"
if git rev-parse --verify -q "refs/tags/$tag" >/dev/null; then
    at=$(git rev-parse "$tag^{commit}")
    echo "tag_release: $tag already exists (on ${at:0:7}); a release number is used once" >&2
    exit 1
fi

echo "tag_release: $commit (${sha:0:7}) declares version $ver -> tag $tag"
if [ "$dry" -eq 1 ]; then
    echo "tag_release: -n given, nothing created"
    exit 0
fi
git tag -a "$tag" "$sha" -m "FEMM $ver"
echo "tag_release: created $tag. Publish it with:  git push <remote> $tag"
