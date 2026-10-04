#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Build only from AOSP tools and source, all output outside tracked tools/.
set -euo pipefail
src=$(cd "$(dirname "$0")" && pwd)
aosp=${1:?AOSP source required}; host=${2:?matching output required}/host/linux-x86
out=${3:?NEW output required}
[ ! -e "$out" ]; mkdir -p "$out/classes" "$out/dex"
export JAVA_HOME="$aosp/prebuilts/jdk/jdk21/linux-x86"
"$JAVA_HOME/bin/javac" -source 11 -target 11 -classpath "$aosp/prebuilts/sdk/current/public/android.jar" -d "$out/classes" "$src/AudioStressActivity.java"
"$JAVA_HOME/bin/java" -cp "$host/framework/d8.jar" com.android.tools.r8.D8 --min-api 29 --lib "$aosp/prebuilts/sdk/current/public/android.jar" --output "$out/dex" "$out"/classes/com/kiki/audiostress/*.class
"$host/bin/aapt2" link -o "$out/unsigned.apk" --manifest "$src/AndroidManifest.xml" -I "$aosp/prebuilts/sdk/current/public/android.jar"
(cd "$out/dex"; zip -q "$out/unsigned.apk" classes.dex)
"$host/bin/zipalign" -f 4 "$out/unsigned.apk" "$out/aligned.apk"
"$JAVA_HOME/bin/java" -Djava.library.path="$host/lib64" -jar "$host/framework/signapk.jar" "$aosp/build/make/target/product/security/testkey.x509.pem" "$aosp/build/make/target/product/security/testkey.pk8" "$out/aligned.apk" "$out/kiki-audio-stress.apk"
sha256sum "$out/kiki-audio-stress.apk"
