ANDROID_SDK=~/Android/Sdk
NDKROOT=$ANDROID_SDK/ndk-bundle
$ANDROID_SDK/cmake/3.6.4111459/bin/cmake --trace -G "Unix Makefiles" \
	-DCMAKE_MAKE_PROGRAM=$NDKROOT/prebuilt/linux-x86_64/bin/make \
	-DANDROID_LINKER_FLAGS=-Wl,--exclude-libs,libunwind.a  \
	-DCMAKE_BUILD_TYPE=Release \
	-DANDROID_ABI=x86_64 \
	-DANDROID_NATIVE_API_LEVEL=android-22 \
	-DANDROID_TOOLCHAIN_NAME=x86_64-4.9 \
	-DCMAKE_INSTALL_PREFIX=install \
	-DANDROID_STL=c++_shared \
	-DASSIMP_BUILD_TESTS=OFF \
	-DANDROID_STL_FORCE_FEATURES=ON \
	-DCMAKE_TOOLCHAIN_FILE=$NDKROOT/build/cmake/android.toolchain.cmake \
	-DANDROID_TOOLCHAIN=clang
$NDKROOT/prebuilt/linux-x86_64/bin/make -j12
$NDKROOT/toolchains/x86_64-4.9/prebuilt/linux-x86_64/x86_64-linux-android/bin/strip lib/libassimp.so

