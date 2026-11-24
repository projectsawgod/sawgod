#!/usr/bin/env python3
"""Compile and run the Android-independent asset installer, optionally timing staged APK assets."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
if not shutil.which('javac') or not shutil.which('java'):
    print('Java compiler/runtime unavailable')
    sys.exit(77)
with tempfile.TemporaryDirectory(prefix='sawgod-assets-') as scratch:
    subprocess.run(['javac', '-J-XX:ActiveProcessorCount=8', '-d', scratch,
                    str(root / 'android/app/src/main/java/org/sawgod/app/BundledAssets.java'),
                    str(root / 'tests/android/BundledAssetsTest.java')], check=True)
    subprocess.run(['java', '-XX:ActiveProcessorCount=8', '-ea', '-cp', scratch,
                    'org.sawgod.app.BundledAssetsTest', scratch, *sys.argv[1:]], check=True)
