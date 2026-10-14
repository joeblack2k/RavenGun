#!/bin/sh
# Draait IN de container van scripts/build-lindbergh-loader.sh (packaging/containers/lindbergh-loader/Containerfile), zonder
# netwerk. Bouwt eerst de i386-afhankelijkheden van lindbergh.so die Ubuntu 22.04 niet heeft (SDL3, SDL3_ttf, SDL3_image en
# FAudio, statisch voor libsegaapi.so), dan lindbergh-loader zelf met de Makefile van de bron, en zet de runtime in /out:
#   lindbergh.so libsegaapi.so libkswapapi.so libposixtime.so(.1, .2.4)  -- de Lindbergh-laag (bron, CC BY-NC-SA 4.0)
#   libSDL3.so.0 libSDL3_ttf.so.0 libSDL3_image.so.0                        -- gebouwd uit de vastgepinde bron (zlib)
#   libstdc++.so.5 libncurses.so.5 libtinfo.so.5 libXmu.so.6 libXt.so.6     -- i386 uit Ubuntu 22.04; SteamOS heeft ze niet
#   libSM.so.6 libICE.so.6 libbsd.so.0 libmd.so.0                           -- idem (wat libXt verder vraagt)
#   libsndio.so.7                                                           -- onze stub (sndio-stub.c), voor libopenal.so.0
#   librg-stackalign.so                                                     -- onze GL-shim (tools/rg-stackalign), per profiel
#   libCg.so libopenal.so.0                                                 -- uit libs/ van de lindbergh-loader-bron
# Invoer: /src/<naam> (alleen lezen), /work (schrijfbaar, de bouwmappen), /out.
set -eu
J="$(nproc)"
L=/usr/lib/i386-linux-gnu
export PKG_CONFIG_PATH="$L/pkgconfig"
export PKG_CONFIG_LIBDIR="$L/pkgconfig:/usr/share/pkgconfig"

cmake32() {   # bron, bouwmap, extra opties
  src="$1"; bld="$2"; shift 2
  cmake -S "$src" -B "$bld" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS=-m32 -DCMAKE_CXX_FLAGS=-m32 \
    -DCMAKE_SYSTEM_PROCESSOR=i686 -DCMAKE_LIBRARY_ARCHITECTURE=i386-linux-gnu -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_INSTALL_LIBDIR=lib/i386-linux-gnu "$@" >/dev/null
  cmake --build "$bld" -j "$J" >/dev/null
  cmake --install "$bld" >/dev/null
}

echo "== SDL3"
cmake32 /src/SDL /work/b-sdl -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_WAYLAND=OFF \
  -DSDL_X11=ON -DSDL_PULSEAUDIO=ON -DSDL_ALSA=ON -DSDL_PIPEWIRE=OFF -DSDL_JACK=OFF -DSDL_SNDIO=OFF -DSDL_KMSDRM=OFF \
  -DSDL_OPENGL=ON -DSDL_VULKAN=OFF -DSDL_IBUS=OFF -DSDL_LIBURING=OFF -DSDL_HIDAPI_LIBUSB=OFF
echo "== SDL3_ttf"
cmake32 /src/SDL_ttf /work/b-ttf -DBUILD_SHARED_LIBS=ON -DSDLTTF_VENDORED=OFF -DSDLTTF_HARFBUZZ=OFF -DSDLTTF_PLUTOSVG=OFF \
  -DSDLTTF_SAMPLES=OFF -DSDLTTF_INSTALL_MAN=OFF
echo "== SDL3_image"
cmake32 /src/SDL_image /work/b-img -DBUILD_SHARED_LIBS=ON -DSDLIMAGE_VENDORED=OFF -DSDLIMAGE_BACKEND_STB=ON \
  -DSDLIMAGE_AVIF=OFF -DSDLIMAGE_JXL=OFF -DSDLIMAGE_TIF=OFF -DSDLIMAGE_WEBP=OFF -DSDLIMAGE_SAMPLES=OFF -DSDLIMAGE_TESTS=OFF
echo "== FAudio (statisch)"
cmake32 /src/FAudio /work/b-faudio -DBUILD_SHARED_LIBS=OFF -DBUILD_SDL3=ON -DCMAKE_POSITION_INDEPENDENT_CODE=ON

echo "== lindbergh-loader"
rm -rf /work/ll && cp -a /src/lindbergh-loader /work/ll
cd /work/ll
for p in /patches/*.patch; do
  [ -e "$p" ] || continue
  echo "patch: $(basename "$p")"
  patch -p1 --no-backup-if-mismatch -s < "$p"
done
make -j "$J" >/work/make.log 2>&1 || { tail -40 /work/make.log; exit 1; }

echo "== uitvoer"
cp -a build/lindbergh.so build/libsegaapi.so build/libkswapapi.so build/libposixtime.so /out/
ln -sf libposixtime.so /out/libposixtime.so.1
ln -sf libposixtime.so /out/libposixtime.so.2.4
for so in libSDL3.so.0 libSDL3_ttf.so.0 libSDL3_image.so.0; do cp -L "$L/$so" "/out/$so"; done
cp -L "$L/libstdc++.so.5" "$L/libXmu.so.6" /out/
cp -L /lib/i386-linux-gnu/libncurses.so.5 /lib/i386-linux-gnu/libtinfo.so.5 /out/
cp -L libs/libCg.so libs/libopenal.so.0 /out/
# libXmu.so.6 vraagt libXt.so.6 (GSEVO, Primevil: "libXt.so.6: cannot open shared object file", bench 2026-10-15); SteamOS heeft
# geen i386-libXt. libopenal.so.0 vraagt libsndio.so.7: onze stub (sndio-stub.c), want ook sndio is er niet voor i386.
cp -L "$L/libXt.so.6" "$L/libSM.so.6" "$L/libICE.so.6" "$L/libbsd.so.0" "$L/libmd.so.0" /out/   # libXt -> libSM, libICE -> libbsd -> libmd (libuuid heeft SteamOS wel)
gcc -m32 -O2 -shared -fPIC -Wl,-soname,libsndio.so.7 -o /out/libsndio.so.7 /sndio-stub.c
# librg-stackalign.so (tools/rg-stackalign, eigen code): GL-aanroepen van oude games met een uitgelijnde stapel; per profiel in "preload".
gcc -m32 -O2 -shared -fPIC -Wall -Wextra -Werror -o /out/librg-stackalign.so /rg_stackalign.c -ldl
echo "afhankelijkheden van lindbergh.so:"; objdump -p /out/lindbergh.so | sed -n 's/^ *NEEDED */  /p'
