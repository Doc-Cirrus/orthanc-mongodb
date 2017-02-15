#Clean untested files form report:

find . -name Plugin.cpp.gcda | xargs rm

PLUGIN_TEST_DIRS="$(find . -name *.gcda | xargs -I {} dirname {} | sort -u | sed 's/^/--directory /g')"
OUTPUT_FILE=coverage.info
OUTPUT_DIR=coverage_report

lcov --capture $PLUGIN_TEST_DIRS --output-file $OUTPUT_FILE
genhtml $OUTPUT_FILE --output-directory $OUTPUT_DIR

