add_executable(xml_patch_main xml_patch_main.c)
target_link_libraries(xml_patch_main PRIVATE xmlpatch)
target_compile_definitions(xml_patch_main PRIVATE VERSION="0.4.x")
add_test(NAME xml_patch
         COMMAND xml_patch_main -f ${CMAKE_CURRENT_SOURCE_DIR}/test.xml -p
                 ${CMAKE_CURRENT_SOURCE_DIR}/patch.xml)
