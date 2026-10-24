CMAKE_MINIMUM_REQUIRED(VERSION 3.22)
PROJECT(DescriptorAccessTests LANGUAGES C CXX)

SET(CMAKE_CXX_STANDARD 11)
SET(CMAKE_CXX_STANDARD_REQUIRED ON)
SET(CMAKE_CXX_EXTENSIONS OFF)
ENABLE_TESTING()

ADD_EXECUTABLE(descriptor_access_tests
 descriptor_access_tests.cpp
 ../common/descriptor.cpp
 ../common/ustring.cpp
 ../common/bstrlib/bstrlib.c
 ../common/bstrlib/bstrwrap.cpp
)
ADD_TEST(NAME descriptor_access COMMAND descriptor_access_tests)

IF(NOT TARGET UEFIExtract)
 ADD_SUBDIRECTORY(../UEFIExtract "${CMAKE_CURRENT_BINARY_DIR}/UEFIExtract")
ENDIF()
GET_TARGET_PROPERTY(extract_sources UEFIExtract SOURCES)
GET_TARGET_PROPERTY(extract_directory UEFIExtract SOURCE_DIR)
SET(parser_sources)
FOREACH(source IN LISTS extract_sources)
 GET_FILENAME_COMPONENT(name "${source}" NAME)
 IF(NOT name MATCHES "^(uefiextract_main\\.cpp|ffsdumper\\.cpp|uefidump\\.cpp|uefiextract\\.manifest)$")
  GET_FILENAME_COMPONENT(absolute_source "${source}" ABSOLUTE BASE_DIR "${extract_directory}")
  LIST(APPEND parser_sources "${absolute_source}")
 ENDIF()
ENDFOREACH()
ADD_LIBRARY(parser_test_support STATIC ${parser_sources} ../UEFIExtract/ffsdumper.cpp)
TARGET_COMPILE_DEFINITIONS(parser_test_support PUBLIC
 U_ENABLE_NVRAM_PARSING_SUPPORT U_ENABLE_ME_PARSING_SUPPORT
 U_ENABLE_FIT_PARSING_SUPPORT U_ENABLE_GUID_DATABASE_SUPPORT)
ADD_EXECUTABLE(descriptor_parser_tests descriptor_parser_tests.cpp)
TARGET_LINK_LIBRARIES(descriptor_parser_tests PRIVATE parser_test_support)
ADD_TEST(NAME descriptor_parser COMMAND descriptor_parser_tests)

ADD_EXECUTABLE(parser_regression_tests parser_regression_tests.cpp brotli_decoder_tests.c)
TARGET_LINK_LIBRARIES(parser_regression_tests PRIVATE parser_test_support)
ADD_TEST(NAME parser_regressions COMMAND parser_regression_tests "${CMAKE_CURRENT_BINARY_DIR}/regression-output")
SET_TESTS_PROPERTIES(parser_regressions PROPERTIES TIMEOUT 30)
