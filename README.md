LG webOS 5.x GStreamer - Bad Plugins
=====================================

## Description

This directory contains the gst-plugins-base source, as compiled by LG to be
included in webOS 5.x devices, such as **LG CX OLED TVs**.

Thus, unless LG applied some changes that have not yet been published, the
binaries produced by compiling the source from this repository should work
as a drop-in replacement for the GStreamer binaries that are officially used
by LG in their CX OLED Smart TVs.

This can be very useful if you have [rooted your TV](https://github.com/RootMyTV/RootMyTV.github.io/issues/85#issuecomment-1295058979)
and want to alter this source to enable or restore functionality that is
not provided by default on your CX model.

## Origin

This source, which is licensed under LPGL v2.0, was obtained through a legal
inquiry at https://opensource.lge.com/inquiry and was extracted from the
`webOS 5.0 JO 2.0` archive that can be downloaded [here](http://opensource.lge.com/product/list?page=&ctgr=005&subCtgr=006&keyword=OLED65CX5LB).

The changes that have been applied by LG on top of the official GStreamer
1.14.4 source can be found in [this commit](https://github.com/lgstreamer/gst-plugins-bad/commit/1b23e3a1db2782596d8f66f789d3f06836fa38d2).

## Compilation

### Toolchain installation

You will need a recent Linux system, with some GTK related system updates as
well as the webosbrew toolchain from https://www.webosbrew.org. On Debian,
the toolchain can be installed as follows:

```
apt install cmake doxygen libglib2.0-dev-bin gobject-introspection libgirepository1.0-dev
wget https://github.com/webosbrew/meta-lg-webos-ndk/releases/download/1.0.g-rev.5/webos-sdk-x86_64-armv7a-neon-toolchain-1.0.g.sh
chmod 755 webos-sdk-x86_64-armv7a-neon-toolchain-1.0.g.sh
./webos-sdk-x86_64-armv7a-neon-toolchain-1.0.g.sh
```

Note that, if using the toolchain above, you should also have compiled and
installed the GStreamer software from https://github.com/lgstreamer/gstreamer.

### Build process

Once the toolchain and base GStreamer have been installed, you can compile
and install the LG version of the base plugins (which you will need to do
in order to compile addtional GStreamer plugins) by issuing:

```
git clone https://github.com/lgstreamer/gst-plugins-bad.git
cd gst-plugins-bad
. /opt/webos-sdk-x86_64/1.0.g/environment-setup-armv7a-neon-webos-linux-gnueabi
./autogen.sh --noconfigure
patch -p1 < gst-plugins-bad-1.14.4-make43.patch
# NB, you *MUST* re-run autogen.sh here rather than invoke configure or else the build will fail
./autogen.sh --host=arm-webos-linux-gnueabi --with-sysroot=${SDKTARGETSYSROOT} \
  --prefix=${SDKTARGETSYSROOT}/usr/ \
  --disable-silent-rules --disable-dependency-tracking --disable-gtk-doc \
  --disable-introspection --disable-examples --enable-dvb --enable-netsim \
  --enable-shm --disable-acm --disable-android_media --disable-aom \
  --disable-apple_media --disable-avc --disable-bs2b --disable-chromaprint \
  --disable-daala --disable-decklink --disable-direct3d \
  --disable-directsound --disable-dts --disable-fbdev --disable-fdk_aac \
  --disable-gme --disable-gsm --disable-ipcpipeline --disable-iqa \
  --disable-kate --disable-ladspa --disable-lv2 --disable-mpeg2enc \
  --disable-mplex --disable-msdk --disable-musepack --disable-nvenc \
  --disable-ofa --disable-openexr --disable-openmpt --disable-openni2 \
  --disable-opensles --disable-soundtouch --disable-spandsp \
  --disable-spc --disable-srt --disable-teletextdec --disable-vcd \
  --disable-vdpau --disable-wasapi --disable-wildmidi \
  --disable-winks --disable-winscreencap --disable-x265 \
  --disable-zbar --disable-static --disable-assrender --disable-bluez \
  --enable-bz2 --enable-curl --enable-dash --disable-dc1394 \
  --disable-debug --disable-directfb --disable-dtls --disable-faac \
  --disable-faad --disable-flite --disable-fluidsynth --enable-gl \
  --enable-hls --with-hls-crypto=nettle --disable-kms --disable-lcms2 \
  --disable-libde265 --disable-libmms --disable-libssh2 --disable-modplug \
  --disable-msdk --disable-neon --disable-openal --disable-opencv \
  --disable-openh264 --disable-openjpeg --disable-openmpt \
  --disable-opus --disable-orc  --disable-resindvd --disable-rsvg \
  --disable-rtmp --disable-sbc --enable-smoothstreaming \
  --disable-sndfile --disable-srtp --disable-tinyalsa --disable-ttml \
  --disable-uvch264 --disable-valgrind --disable-voaacenc \
  --disable-voamrwbenc --disable-vulkan --enable-wayland --disable-webp \
  --disable-webrtc --disable-webrtcdsp --enable-nls --enable-adpcmdec
./fix_sysroot.sh
make -j6
```
