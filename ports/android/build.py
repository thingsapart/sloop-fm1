#!/usr/bin/env python3
"""Build a debug APK with SDK/NDK tools only (no Gradle, Studio, or CMake)."""
from pathlib import Path
import os
import platform
import shutil
import subprocess
import sys
import zipfile

PORT = Path(__file__).resolve().parent
ROOT = Path(os.environ.get('SLOOP_SOURCE', PORT.parents[1])).resolve()
OUT = PORT / 'build'
SDK = Path(os.environ.get('ANDROID_SDK_ROOT', OUT / 'sdk')).expanduser().resolve()
NDK = Path(os.environ.get('ANDROID_NDK_ROOT', SDK / 'ndk' / '27.2.12479018')).expanduser().resolve()
TOOLS = SDK / 'build-tools' / '35.0.0'
JAR = SDK / 'platforms' / 'android-35' / 'android.jar'
JAVA = Path(os.environ['JAVA_HOME']) / 'bin' if 'JAVA_HOME' in os.environ else None

def run(args, **kwargs):
    subprocess.run([str(x) for x in args], check=True, **kwargs)

def java_tool(name):
    return str(JAVA / name) if JAVA else shutil.which(name) or name

if not JAR.exists() or not TOOLS.exists() or not NDK.exists():
    sys.exit('Missing SDK/NDK. See ports/android/README.md for command-line setup.')
run([sys.executable, PORT / 'prepare.py', ROOT])
HOST = 'darwin-x86_64' if platform.system() == 'Darwin' else 'linux-x86_64'
BIN = NDK / 'toolchains' / 'llvm' / 'prebuilt' / HOST / 'bin'
CLASSES = OUT / 'classes'
DEX = OUT / 'dex'
for folder in (CLASSES, DEX):
    if folder.exists(): shutil.rmtree(folder)
    folder.mkdir(parents=True)
ABI = os.environ.get('SLOOP_ABI', 'arm64-v8a')
TARGETS = {'arm64-v8a': 'aarch64-linux-android26', 'x86_64': 'x86_64-linux-android26'}
if ABI not in TARGETS: sys.exit('SLOOP_ABI must be arm64-v8a or x86_64')
LIB = OUT / 'lib' / ABI
LIB.mkdir(parents=True, exist_ok=True)
common = ['-O2', '-fPIC', '-fvisibility=hidden', '-fwrapv', '-Wno-int-to-pointer-cast',
          '-I'+str(OUT / 'gen'), '-I'+str(OUT / 'firmware' / 'src')]
run([BIN / (TARGETS[ABI]+'-clang'), '-std=c11', *common, '-c', PORT / 'native' / 'core.c', '-o', OUT / 'core.o'])
run([BIN / (TARGETS[ABI]+'-clang++'), '-std=c++17', *common, '-shared', '-static-libstdc++',
     '-Wl,-z,max-page-size=16384', '-Wl,-z,common-page-size=16384', '-Wl,--no-undefined',
     PORT / 'native' / 'audio.cpp', OUT / 'core.o', '-laaudio', '-llog', '-o', LIB / 'libsloop.so'])
run([java_tool('javac'), '--release', '8', '-Xlint:-options', '-classpath', JAR,
     '-d', CLASSES, *sorted((PORT / 'java').rglob('*.java'))])
run([TOOLS / 'd8', '--lib', JAR, '--min-api', '26', '--output', DEX, *sorted(CLASSES.rglob('*.class'))])
assets = OUT / 'apk-assets'
assets.mkdir(exist_ok=True)
for name in ['LICENSE', 'LICENSING.md']:
    shutil.copy2(ROOT / name, assets / name)
shutil.copy2(ROOT / 'assets/fonts/Terminus-LICENSE.txt', assets / 'Terminus-LICENSE.txt')
shutil.copy2(ROOT / 'assets/samples-cc0/ATTRIBUTION.txt', assets / 'SAMPLES-ATTRIBUTION.txt')
# Embedded source notices include Apache/MIT engine attribution.
for name in ['fm6_core.c', 'phys_dsp.c', 'eng_phys.c', 'eng_phase.c', 'eng_formant.c']:
    shutil.copy2(ROOT / 'firmware/src' / name, assets / name)
unsigned = OUT / 'unsigned.apk'
run([TOOLS / 'aapt2', 'link', '-o', unsigned, '--manifest', PORT / 'AndroidManifest.xml', '-I', JAR, '-A', assets])
with zipfile.ZipFile(unsigned, 'a', compression=zipfile.ZIP_DEFLATED) as z:
    z.write(DEX / 'classes.dex', 'classes.dex')
    z.write(LIB / 'libsloop.so', f'lib/{ABI}/libsloop.so')
aligned = OUT / 'aligned.apk'
run([TOOLS / 'zipalign', '-f', '-p', '4', unsigned, aligned])
key = OUT / 'debug.keystore'
if not key.exists():
    run([java_tool('keytool'), '-genkeypair', '-keystore', key, '-storepass', 'android',
         '-keypass', 'android', '-alias', 'androiddebugkey', '-keyalg', 'RSA', '-keysize', '2048',
         '-validity', '10000', '-dname', 'CN=Android Debug,O=SLOOP,C=US'])
apk = OUT / 'sloop-android.apk'
run([TOOLS / 'apksigner', 'sign', '--ks', key, '--ks-pass', 'pass:android', '--key-pass', 'pass:android', '--out', apk, aligned])
run([TOOLS / 'apksigner', 'verify', '--verbose', apk])
print(apk)
