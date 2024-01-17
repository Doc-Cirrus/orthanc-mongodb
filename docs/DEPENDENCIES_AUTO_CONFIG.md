# Orthanc MongoDB autoconfig

Install plugin dependencies automatically.

## Brief overview

The Orthanc MongoDB plugin now supports new cmake option - ```AUTO_INSTALL_DEPENDENCIES```

If this option is enabled, cmake will take care about downloading, configuring and installing of next dependencies:
2. Mongoc ( + bson) - https://github.com/mongodb/mongo-c-driver
3. Mongocxx ( + bsoncxx) - https://github.com/mongodb/mongo-cxx-driver

**Note**: You only need to take care about installing dependencies, which are required by above libraries. To find which dependencies are required, just look through the above links or links for instalation guides in main [Build Prerequisites](./PREREQUISITES.md)  file. Such dependencies could be easily installed with a package manager, they do not require additional configuration steps.

## Current versions

2. ```MONGO_C_VERSION``` - 1.23.2
3. ```MONGO_CXX_VERSION``` - 3.7.0

To use another versions of the above libraries - change version in respective cmake macros in [CMake/autoconfig.cmake](https://github.com/andrewDubyk/orthanc-mongodb/blob/Orthanc-1.5.7/CMake/autoconfig.cmake) file.

## Usage exmaple

Dependencies will be downloded and installed when you execute cmake command. After that you can find all resulted files under build folder.

Before building plugin itself - be sure that you have all needed libraries intsalled on your PC.

## UNIX

```bash
mkdir orthanc-mongodb/build
cd orthanc-mongodb/build
cmake -DCMAKE_CXX_FLAGS='-fPIC' -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release -DAUTO_INSTALL_DEPENDENCIES=ON ../MongoDB/
make
sudo make install
```
Use devtoolset-N commands (where N - toolset version) if it is need.
## Windows with MSVC

```bash
cmake -DCMAKE_CXX_FLAGS="/EHsc" -DCMAKE_BUILD_TYPE=Release -DAUTO_INSTALL_DEPENDENCIES=ON -DBOOST_ROOT=<path_to_boost_folder> ../MongoDB/
```

* On Windows mongocxx require boost library, so ```BOOST_ROOT``` will be passed to mongocxx cmake during execution (tested with ```1_60_0```, but newer versions also could be used).
* Windows Run-Time linkage:
    * If you want to link with MSVC static Run-Time Library - you need to build plugin with ```-DLINK_STATIC_LIBS=ON``` flag. In this case ```/MT(d)``` option will be added to build instructions.
    * In other case resulted dll files will be linked with dynamic Run-Time libraries and you must be sure that all of them are included in Windows system PATH varaibles or place them in the same folder as plugin dlls. Use [Dependency Walker](https://www.dependencywalker.com/) to figure out which dlls are missing.
* Also you could experiment with other ```CMAKE_C_FLAGS``` and ```CMAKE_CXX_FLAGS``` flags.

---

