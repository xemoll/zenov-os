cmake_minimum_required(VERSION 3.24)
project(ZenApps LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ZEN_APPS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Sql Test)

function(zen_target_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc)
        if(ZEN_APPS_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
        if(ZEN_APPS_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

add_library(zenapps_core STATIC
    src/core/workspace.cpp
    src/core/expression.cpp
    src/core/recurrence.cpp
)
target_include_directories(zenapps_core PUBLIC src)
target_link_libraries(zenapps_core PUBLIC Qt6::Core Qt6::Sql)
zen_target_warnings(zenapps_core)

add_library(zenapps_ui STATIC
    src/ui/style.cpp
    src/ui/pages.cpp
)
target_include_directories(zenapps_ui PUBLIC src)
target_link_libraries(zenapps_ui PUBLIC zenapps_core Qt6::Gui Qt6::Widgets)
zen_target_warnings(zenapps_ui)

set(ZEN_APPS notes tasks calendar clock calculator reminders)
foreach(app IN LISTS ZEN_APPS)
    set(target "zen-${app}")
    add_executable(${target} src/main.cpp)
    target_compile_definitions(${target} PRIVATE ZEN_APP_KIND="${app}")
    target_link_libraries(${target} PRIVATE zenapps_ui)
    set_target_properties(${target} PROPERTIES
        WIN32_EXECUTABLE TRUE
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.zenov.${app}"
        MACOSX_BUNDLE_BUNDLE_NAME "${target}"
    )
    zen_target_warnings(${target})
    install(TARGETS ${target}
        RUNTIME DESTINATION bin
        BUNDLE DESTINATION .
    )
    if(UNIX AND NOT APPLE)
        install(FILES "packaging/linux/${target}.desktop"
            DESTINATION share/applications)
        install(FILES "packaging/linux/icons/${target}.svg"
            DESTINATION share/icons/hicolor/scalable/apps)
    endif()
endforeach()

include(CTest)
if(BUILD_TESTING)
    add_executable(zenapps-core-tests tests/core_tests.cpp)
    target_link_libraries(zenapps-core-tests PRIVATE zenapps_core Qt6::Test)
    zen_target_warnings(zenapps-core-tests)
    add_test(NAME zenapps-core COMMAND zenapps-core-tests)

    foreach(app IN LISTS ZEN_APPS)
        add_test(NAME "smoke-${app}"
            COMMAND "zen-${app}" --smoke-test --data-dir "${CMAKE_BINARY_DIR}/smoke/${app}")
        set_tests_properties("smoke-${app}" PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            TIMEOUT 20)
    endforeach()
endif()

set(CPACK_PACKAGE_NAME "zen-apps")
set(CPACK_PACKAGE_VENDOR "Zenov")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Standalone local-first Notes, Tasks, Calendar, Clock, Calculator and Reminders")
set(CPACK_PACKAGE_VERSION "rolling")
set(CPACK_PACKAGE_FILE_NAME "zen-apps-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_GENERATOR "TGZ;ZIP")
include(CPack)
