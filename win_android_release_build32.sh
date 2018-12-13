cmake --trace  -G "MinGW Makefiles" \
	-DCMAKE_MAKE_PROGRAM=$NDKROOT/prebuilt/windows-x86_64/bin/make.exe \
	-DCMAKE_BUILD_TYPE=Release \
	-DANDROID_ABI=armeabi-v7a \
	-DANDROID_NATIVE_API_LEVEL=android-22 \
	-DANDROID_FORCE_ARM_BUILD=TRUE \
	-DCMAKE_INSTALL_PREFIX=install \
	-DANDROID_STL=c++_shared \
	-DANDROID_STL_FORCE_FEATURES=ON \
	-DANDROID_TOOLCHAIN_NAME=arm-linux-androideabi-4.9 \
	-DCMAKE_TOOLCHAIN_FILE=$NDKROOT/build/cmake/android.toolchain.cmake \
	-DANDROID_TOOLCHAIN=clang
$NDKROOT/prebuilt/windows-x86_64/bin/make.exe -j12
$NDKROOT/toolchains/arm-linux-androideabi-4.9/prebuilt/windows-x86_64/arm-linux-androideabi/bin/strip.exe lib/libassimp.so


