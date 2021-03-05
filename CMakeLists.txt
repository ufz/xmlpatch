cmake_minimum_required(VERSION 3.13)
project(xmlpatch VERSION 0.4.0)

find_package(LibXml2 REQUIRED)

set(VERSION ${PROJECT_VERSION})
configure_file(cmake_config.h.in ${PROJECT_BINARY_DIR}/config.h)

add_library(xmlpatch src/xml_patch.c)
target_link_libraries(xmlpatch PUBLIC LibXml2::LibXml2)
target_include_directories(xmlpatch INTERFACE src PRIVATE ${PROJECT_BINARY_DIR})

add_executable(xml_patch tests/xml_patch_main.c)
target_link_libraries(xml_patch xmlpatch)
target_include_directories(xml_patch PRIVATE ${PROJECT_BINARY_DIR})
