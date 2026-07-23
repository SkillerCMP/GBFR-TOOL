cmake_minimum_required(VERSION 3.20)
project(GBFRTool VERSION 1.10.0 LANGUAGES CXX)

set(GBFR_TOOL_DISPLAY_VERSION "1.10")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(GBFRToolCore STATIC
    src/CharacterSections.cpp
    src/DataFilePaths.cpp
    src/DataFilePaths.hpp
    src/CharacterSections.hpp
    src/GameDataCore.cpp
    src/GameDataCore.hpp
    src/HashDatabase.cpp
    src/LogicalFamilies.cpp
    src/RelationshipMap.cpp
    src/RelationshipMap.hpp
    src/SectionNames.cpp
    src/SectionNames.hpp
    src/LogicalFamilies.hpp
    src/HashDatabase.hpp
    src/ValueDecoder.cpp
    src/ValueDecoder.hpp
    src/WeaponRules.cpp
    src/WeaponRules.hpp
    src/version.hpp
)
target_include_directories(GBFRToolCore PUBLIC src)

function(gdtv_enable_strict_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /EHsc /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    endif()
endfunction()

gdtv_enable_strict_warnings(GBFRToolCore)

add_executable(GBFRToolCLI src/cli_main.cpp)
target_link_libraries(GBFRToolCLI PRIVATE GBFRToolCore)
gdtv_enable_strict_warnings(GBFRToolCLI)
set_target_properties(GBFRToolCLI PROPERTIES OUTPUT_NAME "GBFR-TOOL-CLI")

if(WIN32)
    enable_language(RC)

    # The static RC file contains VERSIONINFO and the application icon only.
    # Keeping it in the source tree avoids a configure_file race when MSBuild
    # checks several project targets at the same time. Do not compile
    # RT_MANIFEST here: MSVC already creates and embeds one linker manifest for
    # WIN32 targets. Common Controls v6 and asInvoker are added to that same
    # manifest through linker directives in win32_main.cpp.
    add_executable(GBFRTool WIN32
        src/win32_main.cpp
        resources/GBFRTool.rc
    )
    set_target_properties(GBFRTool PROPERTIES OUTPUT_NAME "GBFR-TOOL")
    target_compile_definitions(GBFRTool PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0601
    )
    target_link_libraries(GBFRTool PRIVATE
        GBFRToolCore comctl32 comdlg32 shell32 ole32
    )
    gdtv_enable_strict_warnings(GBFRTool)

    set(GBFR_TOOL_BUNDLED_DATA_FILES
        GameData-Section-Cross-Reference.csv
        GBFR-Character-Sections.txt
        GBFR-Hash-Database.txt
        GBFR-Section-Names.txt
    )

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Hashfolder")
        add_custom_command(TARGET GBFRTool POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:GBFRTool>/Hashfolder"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/Hashfolder"
                "$<TARGET_FILE_DIR:GBFRTool>/Hashfolder"
        )
    else()
        foreach(GBFR_TOOL_DATA_FILE IN LISTS GBFR_TOOL_BUNDLED_DATA_FILES)
            if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${GBFR_TOOL_DATA_FILE}")
                add_custom_command(TARGET GBFRTool POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${CMAKE_CURRENT_SOURCE_DIR}/${GBFR_TOOL_DATA_FILE}"
                        "$<TARGET_FILE_DIR:GBFRTool>/${GBFR_TOOL_DATA_FILE}"
                )
            endif()
        endforeach()
    endif()

    add_custom_command(TARGET GBFRTool POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
            "$<TARGET_FILE_DIR:GBFRTool>/README.md"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt"
            "$<TARGET_FILE_DIR:GBFRTool>/LICENSE.txt"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:GBFRTool>/Licenses"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/Licenses"
            "$<TARGET_FILE_DIR:GBFRTool>/Licenses"
    )
endif()

option(GBFR_TOOL_BUILD_TESTS "Build GBFR - TOOL core tests" OFF)
if(GBFR_TOOL_BUILD_TESTS)
    enable_testing()
    add_executable(GBFRToolTests tests/core_tests.cpp)
    target_link_libraries(GBFRToolTests PRIVATE GBFRToolCore)
    gdtv_enable_strict_warnings(GBFRToolTests)
    add_test(NAME GBFRToolCoreTests COMMAND GBFRToolTests)
endif()

install(TARGETS GBFRToolCLI RUNTIME DESTINATION .)
install(FILES README.md LICENSE.txt DESTINATION .)
install(DIRECTORY Licenses DESTINATION .)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Hashfolder")
    install(DIRECTORY Hashfolder DESTINATION .)
else()
    install(FILES ${GBFR_TOOL_BUNDLED_DATA_FILES} DESTINATION . OPTIONAL)
endif()
if(WIN32)
    install(TARGETS GBFRTool RUNTIME DESTINATION .)
endif()
