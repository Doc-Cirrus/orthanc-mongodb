mkdir build
cd build
rm -rf *
cmake -G "Visual Studio 14 Win64" ..
msbuild ALL_BUILD.vcxproj