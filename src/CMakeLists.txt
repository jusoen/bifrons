# Core library: QtCore only, no widgets, so it can be unit tested headless.
add_library(dcpp_core STATIC
    core/appinfo.h
    core/appinfo.cpp
    core/favourites.h
    core/favourites.cpp
    core/fileitem.h
    core/fileitem.cpp
    core/filemask.h
    core/filemask.cpp
    core/fileoperation.h
    core/fileoperation.cpp
    core/filesort.h
    core/filesort.cpp
    core/filesource.h
    core/localfileoperation.h
    core/localfileoperation.cpp
    core/localfilesource.h
    core/localfilesource.cpp
    core/renameengine.h
    core/renameengine.cpp
    core/searchengine.h
    core/searchengine.cpp
    core/searchresultfilesource.h
    core/searchresultfilesource.cpp
    core/transfer.h
    core/transfer.cpp
)
target_include_directories(dcpp_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dcpp_core PUBLIC Qt6::Core)
target_compile_definitions(dcpp_core PUBLIC DCPP_VERSION="${PROJECT_VERSION}")

qt_add_executable(dcpp WIN32 MACOSX_BUNDLE
    main.cpp
    ui/commandregistry.h
    ui/commandregistry.cpp
    ui/favouritesdialog.h
    ui/favouritesdialog.cpp
    ui/filelistmodel.h
    ui/filelistmodel.cpp
    ui/finddialog.h
    ui/finddialog.cpp
    ui/filepanel.h
    ui/filepanel.cpp
    ui/multirenamedialog.h
    ui/multirenamedialog.cpp
    ui/mainwindow.h
    ui/mainwindow.cpp
    ui/operationrunner.h
    ui/operationrunner.cpp
    ui/paneltabs.h
    ui/paneltabs.cpp
)
target_link_libraries(dcpp PRIVATE dcpp_core Qt6::Widgets Qt6::Concurrent)

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
