mkdir build
cd build
rm -rf *
cmake -G "Visual Studio 15 Win64" -DCMAKE_CXX_FLAGS="/std:c++17  /Zc:__cplusplus /D_ENABLE_EXTENDED_ALIGNED_STORAGE /EHsc -DWIN32 /IC:\google-test\include /MT"  -DBUILD_TESTS=ON -DGOOGLE_TEST_LIB="C:\google-test\lib\gtest.lib" ..
msbuild ALL_BUILD.vcxproj