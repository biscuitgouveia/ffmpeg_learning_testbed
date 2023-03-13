#!/bin/sh
set -e
if test "$CONFIGURATION" = "Debug"; then :
  cd /Users/ethangouveia/Documents/git/repos/ffmpeg_learning_testbed/build
  make -f /Users/ethangouveia/Documents/git/repos/ffmpeg_learning_testbed/build/CMakeScripts/ReRunCMake.make
fi
if test "$CONFIGURATION" = "Release"; then :
  cd /Users/ethangouveia/Documents/git/repos/ffmpeg_learning_testbed/build
  make -f /Users/ethangouveia/Documents/git/repos/ffmpeg_learning_testbed/build/CMakeScripts/ReRunCMake.make
fi
if test "$CONFIGURATION" = "MinSizeRel"; then :
  cd /Users/ethangouveia/Documents/git/repos/ffmpeg_learning_testbed/build
  make -f /Users/ethangouveia/Documents/git/repos/ffmpeg_learning_testbed/build/CMakeScripts/ReRunCMake.make
fi
if test "$CONFIGURATION" = "RelWithDebInfo"; then :
  cd /Users/ethangouveia/Documents/git/repos/ffmpeg_learning_testbed/build
  make -f /Users/ethangouveia/Documents/git/repos/ffmpeg_learning_testbed/build/CMakeScripts/ReRunCMake.make
fi

