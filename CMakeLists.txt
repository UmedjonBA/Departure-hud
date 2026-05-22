cmake_minimum_required(VERSION 3.21)
project(departure-hud LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick)

option(ENABLE_LAYER_SHELL "Use wlr-layer-shell via LayerShellQt when available" ON)

set(HAS_LAYER_SHELL OFF)
if(ENABLE_LAYER_SHELL)
    find_package(LayerShellQt QUIET)
    if(LayerShellQt_FOUND)
        set(HAS_LAYER_SHELL ON)
        message(STATUS "LayerShellQt found — wlr-layer-shell support enabled")
    else()
        message(STATUS "LayerShellQt not found — falling back to a frameless window")
    endif()
endif()

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(departure-hud
    src/main.cpp
    src/sysdata.cpp
    src/sysdata.h
    src/configloader.cpp
    src/configloader.h
    src/maskcontroller.cpp
    src/maskcontroller.h
    src/visibilitywatcher.cpp
    src/visibilitywatcher.h
)

qt_add_qml_module(departure-hud
    URI DepartureHud
    VERSION 1.0
    QML_FILES
        Main.qml
        Hud.qml
    RESOURCES
        fonts/DepartureMono-Regular.otf
)

target_link_libraries(departure-hud PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
)

if(HAS_LAYER_SHELL)
    target_link_libraries(departure-hud PRIVATE LayerShellQtInterface)
    target_compile_definitions(departure-hud PRIVATE HAVE_LAYER_SHELL)
endif()

install(TARGETS departure-hud RUNTIME DESTINATION bin)
install(FILES config.json DESTINATION share/departure-hud)
