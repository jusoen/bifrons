# Core library: QtCore only, no widgets, so it can be unit tested headless.
add_library(bifrons_core STATIC
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
target_include_directories(bifrons_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bifrons_core PUBLIC Qt6::Core)
if(WIN32)
    # NetShareEnum, for listing the shares of a network server.
    target_link_libraries(bifrons_core PRIVATE netapi32)
endif()
target_compile_definitions(bifrons_core PUBLIC BIFRONS_VERSION="${PROJECT_VERSION}")

qt_add_executable(bifrons WIN32 MACOSX_BUNDLE
    main.cpp
    ui/codetheme.h
    ui/codetheme.cpp
    ui/commandregistry.h
    ui/commandregistry.cpp
    ui/favouritesdialog.h
    ui/favouritesdialog.cpp
    ui/fileicons.h
    ui/fileicons.cpp
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
    ui/operationqueue.h
    ui/operationqueue.cpp
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
target_link_libraries(bifrons PRIVATE bifrons_core Qt6::Widgets Qt6::Concurrent)

# Commands > Test Background Operation: writes and slowly copies scratch files to show background progress.
option(BIFRONS_DEMO_OPERATION "Include the background operation test command" OFF)
if(BIFRONS_DEMO_OPERATION)
    target_sources(bifrons PRIVATE ui/demooperation.h ui/demooperation.cpp)
    target_compile_definitions(bifrons PRIVATE BIFRONS_DEMO_OPERATION)
endif()

# App icon (Split Folder). bifrons.svg is the source, bifrons.png and bifrons.ico are rendered from it.
set(_icon_dir "${PROJECT_SOURCE_DIR}/resources/icons")
set_source_files_properties("${_icon_dir}/bifrons.png" PROPERTIES QT_RESOURCE_ALIAS "bifrons.png")
qt_add_resources(bifrons "app_icon" PREFIX "/icons" FILES "${_icon_dir}/bifrons.png")
if(WIN32)
    set(_icon_rc "${CMAKE_CURRENT_BINARY_DIR}/bifrons_icon.rc")
    file(TO_NATIVE_PATH "${_icon_dir}/bifrons.ico" _icon_ico)
    string(REPLACE "\\" "\\\\" _icon_ico "${_icon_ico}")
    file(CONFIGURE OUTPUT "${_icon_rc}" CONTENT "IDI_ICON1 ICON \"${_icon_ico}\"\n")
    target_sources(bifrons PRIVATE "${_icon_rc}")
endif()

# Copy the Qt runtime next to the executable so it runs from the build tree.
if(WIN32)
    get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qtbin "${_qmake}" DIRECTORY)
    find_program(WINDEPLOYQT windeployqt HINTS "${_qtbin}" REQUIRED)
    add_custom_command(TARGET bifrons POST_BUILD
        COMMAND "${WINDEPLOYQT}" --no-translations --no-compiler-runtime
                --no-system-d3d-compiler --no-opengl-sw "$<TARGET_FILE:bifrons>"
        COMMENT "Deploying Qt runtime"
        VERBATIM
    )
endif()
