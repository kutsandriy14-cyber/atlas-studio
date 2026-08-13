cmake_minimum_required(VERSION 3.16)

project(AtlasStudio VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

include(CTest)

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets Test)

add_library(atlas_studio_miniz STATIC
    third_party/miniz/miniz.c
)
target_include_directories(atlas_studio_miniz PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

add_library(atlas_studio_services STATIC
    src/core/atlas_code_compiler.cpp
    src/core/atlas_code_compiler.h
    src/packager/atlas_package_builder.cpp
    src/packager/atlas_package_builder.h
)
target_include_directories(atlas_studio_services PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(atlas_studio_services PUBLIC
    Qt5::Core
    atlas_studio_miniz
)

add_executable(AtlasStudio WIN32
    src/main.cpp
    src/studio/atlas_studio_window.cpp
    src/studio/atlas_studio_window.h
)
target_link_libraries(AtlasStudio PRIVATE
    atlas_studio_services
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
)

add_executable(AtlasCompiler
    src/compiler/main.cpp
)
target_link_libraries(AtlasCompiler PRIVATE
    atlas_studio_services
    Qt5::Core
)

install(TARGETS AtlasStudio AtlasCompiler
    RUNTIME DESTINATION .
)
install(DIRECTORY examples/ DESTINATION examples OPTIONAL)
install(FILES README.md LICENSE DESTINATION . OPTIONAL)

if(BUILD_TESTING)
    add_test(NAME atlas_compiler_help COMMAND $<TARGET_FILE:AtlasCompiler> --help)

    add_executable(atlas_studio_package_test
        tests/atlas_studio_package_test.cpp
    )
    target_link_libraries(atlas_studio_package_test PRIVATE
        atlas_studio_services
        Qt5::Core
        Qt5::Test
    )
    add_test(NAME atlas_studio_package_test COMMAND atlas_studio_package_test)
endif()
