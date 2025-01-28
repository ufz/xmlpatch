add_executable(xml-patch xml_patch_main.c)
target_link_libraries(xml-patch PRIVATE xmlpatch)
install(TARGETS xml-patch)
add_test(NAME xml-patch
         COMMAND xml-patch -f ${CMAKE_CURRENT_SOURCE_DIR}/test.xml -p
                 ${CMAKE_CURRENT_SOURCE_DIR}/patch.xml)

if(TARGET xmldiff)
    add_executable(xml-diff xml_diff_main.c)
    target_link_libraries(xml-diff PRIVATE xmlpatch xmldiff)
    install(TARGETS xml-diff)
    add_test(NAME xml-diff
             COMMAND xml-diff -f ${CMAKE_CURRENT_SOURCE_DIR}/test.xml -t
                     ${CMAKE_CURRENT_SOURCE_DIR}/diff.xml)
    set_property(TEST xml-diff PROPERTY WILL_FAIL true)
    add_test(NAME xml-diff_equal
             COMMAND xml-diff -f ${CMAKE_CURRENT_SOURCE_DIR}/test.xml -t
                     ${CMAKE_CURRENT_SOURCE_DIR}/test.xml)
endif()
