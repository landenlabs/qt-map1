cmake_minimum_required(VERSION 3.16)
project(qt-map1 VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Point CMake at Homebrew Qt6 on macOS
if(APPLE)
    list(PREPEND CMAKE_PREFIX_PATH "/opt/homebrew")
endif()

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Quick
    QuickControls2
    Location
    Positioning
    OpenGL
    Network
    ShaderTools   # required for qt_add_shaders
)

qt_standard_project_setup(REQUIRES 6.5)

# ── API keys ──────────────────────────────────────────────────────────────────
# secrets.cmake is gitignored. Copy secrets.cmake.example → secrets.cmake and
# fill in real values before building.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/secrets.cmake")
    include("${CMAKE_CURRENT_SOURCE_DIR}/secrets.cmake")
else()
    message(WARNING
        "secrets.cmake not found. "
        "Copy secrets.cmake.example to secrets.cmake and fill in your API keys.")
    set(SUN_API_KEY "")
endif()

qt_add_executable(qt-map1
    main.cpp
    Logger.h
    Logger.cpp
    OverlayItem.h
    OverlayItem.cpp
    FloatGridMaterial.h
    FloatGridMaterial.cpp
    GridLoader.h
    GridLoader.cpp
    LayerManager.h
    LayerManager.cpp
    GridManager.h
    GridManager.cpp
    GridTileCache.h
    GridTileCache.cpp
)

qt_add_qml_module(qt-map1
    URI MapApp
    VERSION 1.0
    QML_FILES
        main.qml
)

# Compile GLSL shaders → .qsb (OpenGL/Vulkan/Metal cross-compiled by qsb)
# Output resources: :/shaders/floatgrid.vert.qsb  :/shaders/floatgrid.frag.qsb
qt_add_shaders(qt-map1 "floatgrid_shaders"
    BATCHABLE          # rewrites vertex shader with _qt_order attribute for SG batching
    GLSL "440"
    PREFIX "/shaders"
    FILES
        shaders/floatgrid.vert
        shaders/floatgrid.frag
)

target_compile_definitions(qt-map1 PRIVATE
    PROJECT_VERSION="${PROJECT_VERSION}"
    SUN_API_KEY="${SUN_API_KEY}"
    LAYERS_FILE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/layers.json"
    GRIDS_FILE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/grids.json"
)

target_link_libraries(qt-map1 PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Location
    Qt6::Positioning
    Qt6::OpenGL
    Qt6::Network
)
