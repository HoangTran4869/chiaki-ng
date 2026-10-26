#!/bin/bash
# Dung ChiakiIOS.xcframework (chiaki-lib + cau noi iOS) cho ung dung iPhone "Dich Phu De Game".
# Chay tren may macOS (GitHub Actions macos-14). AGPL-3.0 - ma nguon: thu muc ios-caunoi.
# Ket qua: build-ios/ChiakiIOS.xcframework (iPhone that arm64 + may ao iPhone arm64)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build-ios"
MIN=16.0
OPENSSL_VER=3.0.15
LIBEVENT_VER=2.1.12-stable
JOBS=$(sysctl -n hw.ncpu)
POLICY="-DCMAKE_POLICY_VERSION_MINIMUM=3.5"

mkdir -p "$OUT/src"
cd "$OUT/src"
[ -f openssl.tgz ] || curl -sSfL -o openssl.tgz "https://github.com/openssl/openssl/releases/download/openssl-$OPENSSL_VER/openssl-$OPENSSL_VER.tar.gz"
[ -f libevent.tgz ] || curl -sSfL -o libevent.tgz "https://github.com/libevent/libevent/releases/download/release-$LIBEVENT_VER/libevent-$LIBEVENT_VER.tar.gz"

build_one() {
	local SDK="$1"                      # iphoneos | iphonesimulator
	local P="$OUT/$SDK"
	local VMIN
	if [ "$SDK" = iphoneos ]; then VMIN="-mios-version-min=$MIN"; else VMIN="-mios-simulator-version-min=$MIN"; fi
	mkdir -p "$P"
	echo "::group::OpenSSL ($SDK)"
	rm -rf "$OUT/src/ossl-$SDK" && mkdir "$OUT/src/ossl-$SDK"
	tar xzf "$OUT/src/openssl.tgz" -C "$OUT/src/ossl-$SDK" --strip-components=1
	(
		cd "$OUT/src/ossl-$SDK"
		if [ "$SDK" = iphoneos ]; then T=ios64-xcrun; else T=iossimulator-xcrun; fi
		CFLAGS="-arch arm64 $VMIN" ./Configure "$T" no-shared no-tests no-engine no-async no-ui-console no-dso \
			--prefix="$P/openssl" --libdir=lib
		make -j"$JOBS" build_libs > /dev/null
		make install_dev > /dev/null
	)
	echo "::endgroup::"

	local CM=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT="$SDK"
		-DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=$MIN $POLICY
		-DCMAKE_FIND_ROOT_PATH="$P"
		-DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=BOTH
		-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=BOTH -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH)

	echo "::group::libevent ($SDK)"
	rm -rf "$OUT/src/lev-$SDK" && mkdir "$OUT/src/lev-$SDK"
	tar xzf "$OUT/src/libevent.tgz" -C "$OUT/src/lev-$SDK" --strip-components=1
	cmake -S "$OUT/src/lev-$SDK" -B "$OUT/src/lev-$SDK/b" "${CM[@]}" \
		-DCMAKE_INSTALL_PREFIX="$P/libevent" -DEVENT__LIBRARY_TYPE=STATIC \
		-DEVENT__DISABLE_OPENSSL=ON -DEVENT__DISABLE_MBEDTLS=ON -DEVENT__DISABLE_TESTS=ON \
		-DEVENT__DISABLE_BENCHMARK=ON -DEVENT__DISABLE_SAMPLES=ON -DEVENT__DISABLE_REGRESS=ON \
		-DEVENT__DISABLE_DEBUG_MODE=ON
	cmake --build "$OUT/src/lev-$SDK/b" --target install -j"$JOBS" > /dev/null
	echo "::endgroup::"

	echo "::group::chiaki-lib ($SDK)"
	local B="$OUT/chiaki-$SDK"
	export PKG_CONFIG_PATH="$P/libevent/lib/pkgconfig"
	export PKG_CONFIG_LIBDIR="$P/libevent/lib/pkgconfig"
	cmake -S "$ROOT" -B "$B" "${CM[@]}" \
		-DCHIAKI_ENABLE_TESTS=OFF -DCHIAKI_ENABLE_CLI=OFF -DCHIAKI_ENABLE_GUI=OFF -DCHIAKI_ENABLE_CAUNOI=OFF \
		-DCHIAKI_ENABLE_SETSU=OFF -DCHIAKI_ENABLE_STEAMDECK_NATIVE=OFF -DCHIAKI_ENABLE_STEAM_SHORTCUT=OFF \
		-DCHIAKI_ENABLE_FFMPEG_DECODER=OFF -DCHIAKI_ENABLE_PI_DECODER=OFF -DCHIAKI_LIB_ENABLE_OPUS=OFF \
		-DCHIAKI_ENABLE_SPEEX=OFF -DCHIAKI_USE_SYSTEM_JERASURE=OFF -DCHIAKI_USE_SYSTEM_NANOPB=OFF \
		-DCHIAKI_USE_SYSTEM_CURL=OFF -DCHIAKI_LIB_JSONC_EXTERNAL_PROJECT=ON -DCHIAKI_LIB_MINIUPNPC_EXTERNAL_PROJECT=ON \
		-DOPENSSL_ROOT_DIR="$P/openssl" -DOPENSSL_USE_STATIC_LIBS=ON \
		-DOPENSSL_INCLUDE_DIR="$P/openssl/include" \
		-DOPENSSL_CRYPTO_LIBRARY="$P/openssl/lib/libcrypto.a" -DOPENSSL_SSL_LIBRARY="$P/openssl/lib/libssl.a" \
		-DLIBEVENT_INCLUDE_DIRS="$P/libevent/include" -DLIBEVENT_LIBRARIES="$P/libevent/lib/libevent.a" \
		-DPKG_CONFIG_VERSION=1 -DCURL_USE_OPENSSL=ON -DCURL_USE_LIBPSL=OFF -DCURL_ZLIB=OFF -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF \
		-DUSE_NGHTTP2=OFF -DUSE_LIBIDN2=OFF -DCURL_USE_LIBSSH2=OFF -DCURL_USE_GSSAPI=OFF -DENABLE_UNIX_SOCKETS=OFF \
		-DPython3_EXECUTABLE="$(which python3)" -DPYTHON_EXECUTABLE="$(which python3)"
	cmake --build "$B" --target chiaki-lib -j"$JOBS"
	echo "::endgroup::"

	echo "::group::cau noi ($SDK)"
	xcrun -sdk "$SDK" clang -arch arm64 "$VMIN" -O2 -c "$ROOT/ios-caunoi/cau_noi_ios.c" \
		-I "$ROOT/lib/include" -I "$B/lib/include" -o "$B/cau_noi_ios.o"
	ar rcs "$B/libcau_noi_ios.a" "$B/cau_noi_ios.o"
	local LIBS=("$B/libcau_noi_ios.a")
	while IFS= read -r f; do LIBS+=("$f"); done < <(find "$B" -name "*.a" ! -name "libcau_noi_ios.a" ! -path "*/CMakeFiles/*" | sort)
	LIBS+=("$P/openssl/lib/libssl.a" "$P/openssl/lib/libcrypto.a" "$P/libevent/lib/libevent.a")
	[ -f "$P/libevent/lib/libevent_pthreads.a" ] && LIBS+=("$P/libevent/lib/libevent_pthreads.a")
	printf '  gop: %s\n' "${LIBS[@]}"
	libtool -static -o "$P/libChiakiIOS.a" "${LIBS[@]}" 2>&1 | grep -v "same member name" || true
	ls -la "$P/libChiakiIOS.a"
	echo "::endgroup::"
}

build_one iphoneos
build_one iphonesimulator

HDR="$OUT/headers"
rm -rf "$HDR" && mkdir -p "$HDR"
cp "$ROOT/ios-caunoi/cau_noi_ios.h" "$HDR/"
cat > "$HDR/module.modulemap" << 'EOF'
module ChiakiIOS {
	header "cau_noi_ios.h"
	export *
}
EOF
rm -rf "$OUT/ChiakiIOS.xcframework"
xcodebuild -create-xcframework \
	-library "$OUT/iphoneos/libChiakiIOS.a" -headers "$HDR" \
	-library "$OUT/iphonesimulator/libChiakiIOS.a" -headers "$HDR" \
	-output "$OUT/ChiakiIOS.xcframework"
echo "XONG: $OUT/ChiakiIOS.xcframework"
