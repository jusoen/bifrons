function(dcpp_add_test name)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE dcpp_core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    # Put Qt DLLs on PATH when tests run from the build tree.
    if(WIN32)
        get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(_qtbin "${_qmake}" DIRECTORY)
        set_tests_properties(${name} PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qtbin}")
    endif()
endfunction()

dcpp_add_test(tst_appinfo)
dcpp_add_test(tst_filemask)
dcpp_add_test(tst_filesort)
dcpp_add_test(tst_localfilesource)
dcpp_add_test(tst_localfileoperation)
dcpp_add_test(tst_transfer)
