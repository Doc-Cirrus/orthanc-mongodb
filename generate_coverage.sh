PLUGIN_TEST_DIRS="--directory  ./build/CMakeFiles/IndexTest.dir/Core/ \
    --directory ./build/CMakeFiles/IndexTest.dir/IndexPlugin/ \
    --directory ./build/CMakeFiles/StorageTest.dir/Core/ \
    --directory ./build/CMakeFiles/StorageTest.dir/StoragePlugin/"
OUTPUT_FILE=./build/coverage.info
OUTPUT_DIR=./build/coverage_report

lcov --capture $PLUGIN_TEST_DIRS --output-file $OUTPUT_FILE
genhtml $OUTPUT_FILE --output-directory $OUTPUT_DIR

