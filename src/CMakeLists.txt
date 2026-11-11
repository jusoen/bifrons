# Core library: QtCore only, no widgets, so it can be unit tested headless.
add_library(dcpp_core STATIC
    core/appinfo.h
    core/appinfo.cpp
    core/codehighlight.h
    core/codehighlight.cpp
    core/markdownprep.h
    core/markdownprep.cpp
    core/textsniff.h
    core/textsniff.cpp
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
if(WIN32)
    # NetShareEnum, for listing the shares of a network server.
    target_link_libraries(dcpp_core PRIVATE netapi32)
endif()
target_compile_definitions(dcpp_core PUBLIC DCPP_VERSION="${PROJECT_VERSION}")

qt_add_executable(dcpp WIN32 MACOSX_BUNDLE
    main.cpp
    ui/codetheme.h
    ui/codetheme.cpp
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
    ui/markdownview.h
    ui/markdownview.cpp
    ui/paneltabs.h
    ui/paneltabs.cpp
    ui/paneltoolbar.h
    ui/paneltoolbar.cpp
    ui/pathbar.h
    ui/pathbar.cpp
    ui/terminal.h
    ui/terminal.cpp
    ui/viewerwidget.h
    ui/viewerwidget.cpp
    ui/viewerwindow.h
    ui/viewerwindow.cpp
)
target_link_libraries(dcpp PRIVATE dcpp_core Qt6::Widgets Qt6::Concurrent)

# App icon (Split Folder). dcpp.svg is the source, dcpp.png and dcpp.ico are rendered from it.
set(_icon_dir "${PROJECT_SOURCE_DIR}/resources/icons")
set_source_files_properties("${_icon_dir}/dcpp.png" PROPERTIES QT_RESOURCE_ALIAS "dcpp.png")
qt_add_resources(dcpp "app_icon" PREFIX "/icons" FILES "${_icon_dir}/dcpp.png")
if(WIN32)
    set(_icon_rc "${CMAKE_CURRENT_BINARY_DIR}/dcpp_icon.rc")
    file(TO_NATIVE_PATH "${_icon_dir}/dcpp.ico" _icon_ico)
    string(REPLACE "\\" "\\\\" _icon_ico "${_icon_ico}")
    file(CONFIGURE OUTPUT "${_icon_rc}" CONTENT "IDI_ICON1 ICON \"${_icon_ico}\"\n")
    target_sources(dcpp PRIVATE "${_icon_rc}")
endif()

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
