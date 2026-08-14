cmake_minimum_required(VERSION 3.16)

project(AtlasStudio VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

include(CTest)

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets Network Test)

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
    src/services/update_service.cpp
    src/services/update_service.h
)
target_include_directories(atlas_studio_services PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(atlas_studio_services PUBLIC
    Qt5::Core
    Qt5::Network
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

add_executable(AtlasUpdater WIN32
    src/updater/main.cpp
)
target_include_directories(AtlasUpdater PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(AtlasUpdater PRIVATE
    atlas_studio_services
    Qt5::Core
    Qt5::Network
    Qt5::Widgets
)

install(TARGETS AtlasStudio AtlasCompiler AtlasUpdater
    RUNTIME DESTINATION .
)
install(DIRECTORY examples/ DESTINATION examples OPTIONAL)
install(FILES README.md LICENSE DESTINATION . OPTIONAL)

if(BUILD_TESTING)
    add_test(NAME atlas_compiler_help COMMAND $<TARGET_FILE:AtlasCompiler> --help)
    add_test(NAME atlas_updater_help COMMAND $<TARGET_FILE:AtlasUpdater> --help)
    if(UNIX)
        set_tests_properties(atlas_compiler_help atlas_updater_help PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        )
    endif()

    add_executable(atlas_studio_package_test
        tests/atlas_studio_package_test.cpp
    )
    target_link_libraries(atlas_studio_package_test PRIVATE
        atlas_studio_services
        Qt5::Core
    )
    add_test(NAME atlas_studio_package_test COMMAND atlas_studio_package_test)

    add_executable(atlas_studio_ui_test
        tests/atlas_studio_ui_test.cpp
        src/studio/atlas_studio_window.cpp
        src/studio/atlas_studio_window.h
    )
    target_link_libraries(atlas_studio_ui_test PRIVATE
        atlas_studio_services
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
    )
    add_test(NAME atlas_studio_ui_test COMMAND atlas_studio_ui_test)

    add_executable(atlas_studio_submit_test
        tests/atlas_studio_submit_test.cpp
        src/studio/atlas_studio_window.cpp
        src/studio/atlas_studio_window.h
    )
    target_link_libraries(atlas_studio_submit_test PRIVATE
        atlas_studio_services
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
        Qt5::Network
    )
    add_test(NAME atlas_studio_submit_test COMMAND atlas_studio_submit_test)

    if(UNIX)
        set_tests_properties(atlas_studio_submit_test PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        )
    endif()

    if(UNIX)
        set_tests_properties(atlas_studio_ui_test PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        )
    endif()
endif()
