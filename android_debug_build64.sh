cmake	-G "Unix Makefiles" \
	-DCMAKE_BUILD_TYPE=Debug \
	-DANDROID_ABI=arm64-v8a \
	-DANDROID_NATIVE_API_LEVEL=android-24 \
	-DANDROID_FORCE_ARM_BUILD=TRUE \
	-DCMAKE_INSTALL_PREFIX=install \
	-DANDROID_STL=gnustl_static \
	-DANDROID_BUILD_TESTS=OFF \
	-DANDROID_STL_FORCE_FEATURES=ON
cmake --build .

