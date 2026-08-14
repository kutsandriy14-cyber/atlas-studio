cmake_minimum_required(VERSION 3.16)

project(OrvexaStudio VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

include(CTest)

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Svg Widgets Network Test)

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
    src/studio/studio_test_runner.cpp
    src/studio/studio_test_runner.h
)
target_include_directories(atlas_studio_services PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(atlas_studio_services PUBLIC
    Qt5::Core
    Qt5::Network
    atlas_studio_miniz
)

add_library(atlas_studio_ui STATIC
    src/studio/studio_theme.cpp
    src/studio/studio_theme.h
    src/studio/atlas_code_highlighter.cpp
    src/studio/atlas_code_highlighter.h
)
target_include_directories(atlas_studio_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(atlas_studio_ui PUBLIC
    Qt5::Core
    Qt5::Gui
    Qt5::Svg
)

add_executable(OrvexaStudio WIN32
    src/main.cpp
    src/studio/atlas_studio_window.cpp
    src/studio/atlas_studio_window.h
    assets/assets.qrc
)
target_link_libraries(OrvexaStudio PRIVATE
    atlas_studio_services
    atlas_studio_ui
    Qt5::Core
    Qt5::Gui
    Qt5::Svg
    Qt5::Widgets
)

add_executable(OrvexaCompiler
    src/compiler/main.cpp
)
target_link_libraries(OrvexaCompiler PRIVATE
    atlas_studio_services
    Qt5::Core
)

add_executable(OrvexaUpdater WIN32
    src/updater/main.cpp
)
target_include_directories(OrvexaUpdater PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(OrvexaUpdater PRIVATE
    atlas_studio_services
    Qt5::Core
    Qt5::Network
    Qt5::Widgets
)

install(TARGETS OrvexaStudio OrvexaCompiler OrvexaUpdater
    RUNTIME DESTINATION .
)
install(DIRECTORY examples/ DESTINATION examples OPTIONAL)
install(FILES README.md LICENSE DESTINATION . OPTIONAL)

if(BUILD_TESTING)
    add_test(NAME atlas_compiler_help COMMAND $<TARGET_FILE:OrvexaCompiler> --help)
    add_test(NAME atlas_updater_help COMMAND $<TARGET_FILE:OrvexaUpdater> --help)
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
        src/studio/studio_theme.cpp
        src/studio/studio_theme.h
        src/studio/atlas_code_highlighter.cpp
        src/studio/atlas_code_highlighter.h
        assets/assets.qrc
    )
    target_link_libraries(atlas_studio_ui_test PRIVATE
        atlas_studio_services
        atlas_studio_ui
        Qt5::Core
        Qt5::Gui
        Qt5::Svg
        Qt5::Widgets
    )
    add_test(NAME atlas_studio_ui_test COMMAND atlas_studio_ui_test)

    add_executable(atlas_studio_submit_test
        tests/atlas_studio_submit_test.cpp
        src/studio/atlas_studio_window.cpp
        src/studio/atlas_studio_window.h
        src/studio/studio_theme.cpp
        src/studio/studio_theme.h
        src/studio/atlas_code_highlighter.cpp
        src/studio/atlas_code_highlighter.h
        assets/assets.qrc
    )
    target_link_libraries(atlas_studio_submit_test PRIVATE
        atlas_studio_services
        atlas_studio_ui
        Qt5::Core
        Qt5::Gui
        Qt5::Svg
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
