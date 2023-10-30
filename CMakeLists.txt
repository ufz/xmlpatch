cmake_minimum_required(VERSION 3.13)
project(xmlpatch VERSION 0.4.0)

find_package(LibXml2 REQUIRED)

add_library(xmlpatch src/xml_patch.c)
target_link_libraries(xmlpatch PUBLIC LibXml2::LibXml2)
target_include_directories(xmlpatch INTERFACE src)
set_target_properties(xmlpatch PROPERTIES PUBLIC_HEADER src/xml_patch.h)
install(TARGETS xmlpatch)
