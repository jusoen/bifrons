# Core library: QtCore only, no widgets, so it can be unit tested headless.
add_library(dcpp_core STATIC
    core/appinfo.h
    core/appinfo.cpp
)
target_include_directories(dcpp_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dcpp_core PUBLIC Qt6::Core)
target_compile_definitions(dcpp_core PUBLIC DCPP_VERSION="${PROJECT_VERSION}")

qt_add_executable(dcpp WIN32 MACOSX_BUNDLE
    main.cpp
    ui/mainwindow.h
    ui/mainwindow.cpp
)
target_link_libraries(dcpp PRIVATE dcpp_core Qt6::Widgets)

# Copy the Qt runtime next to the executable so it runs from the build tree.
if(WIN32)
    get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qtbin "${_qmake}" DIRECTORY)
    find_program(WINDEPLOYQT windeployqt HINTS "${_qtbin}" REQUIRED)
    add_custom_command(TARGET dcpp POST_BUILD
        COMMAND "${WINDEPLOYQT}" --no-translations --no-compiler-runtime
                --no-system-d3d-compiler --no-opengl-sw "$<TARGET_FILE:dcpp>"
        COMMENT "Deploying Qt runtime"
        VERBATIM
    )
endif()
