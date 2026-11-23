#!/usr/bin/env python3
# Copyright 2013 The Flutter Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Applies the D3D11 scRGB swap chain patch to third_party/angle.

The patch lets ANGLE honor EGL_GL_COLORSPACE_SCRGB_LINEAR_EXT on D3D11 by
calling IDXGISwapChain3::SetColorSpace1, which the Windows embedder needs for
HDR presentation. Running it again on an already patched checkout is a no-op.
"""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PATCH = os.path.join(HERE, 'angle_scrgb_linear.patch')
ANGLE = os.path.normpath(os.path.join(HERE, '..', '..', 'third_party', 'angle'))


def git_apply(*args):
  return subprocess.run(['git', 'apply', *args, PATCH], cwd=ANGLE,
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0


def main():
  if not os.path.isdir(ANGLE):
    print('apply_angle_patch: %s not found, skipping' % ANGLE)
    return 0
  if git_apply('--reverse', '--check'):
    return 0
  if not git_apply('--check'):
    print('apply_angle_patch: patch does not apply to %s' % ANGLE, file=sys.stderr)
    return 1
  if not git_apply():
    print('apply_angle_patch: git apply failed', file=sys.stderr)
    return 1
  print('apply_angle_patch: applied %s' % os.path.basename(PATCH))
  return 0


if __name__ == '__main__':
  sys.exit(main())
