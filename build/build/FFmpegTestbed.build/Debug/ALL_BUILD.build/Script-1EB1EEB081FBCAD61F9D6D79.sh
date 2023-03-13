#!/bin/sh
set -e
if test "$CONFIGURATION" = "Debug"; then :
  cd /Users/ethangouveia/Documents/git/repos/ffmpeg_testbed/build
  echo Build\ all\ projects
fi
if test "$CONFIGURATION" = "Release"; then :
  cd /Users/ethangouveia/Documents/git/repos/ffmpeg_testbed/build
  echo Build\ all\ projects
fi
if test "$CONFIGURATION" = "MinSizeRel"; then :
  cd /Users/ethangouveia/Documents/git/repos/ffmpeg_testbed/build
  echo Build\ all\ projects
fi
if test "$CONFIGURATION" = "RelWithDebInfo"; then :
  cd /Users/ethangouveia/Documents/git/repos/ffmpeg_testbed/build
  echo Build\ all\ projects
fi

