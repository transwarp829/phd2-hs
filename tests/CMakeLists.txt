cmake_minimum_required(VERSION 3.16)

# This suite can be configured independently of PHD2 and the vendor SDKs.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(PHD2CaptureRegressionTests LANGUAGES C CXX)
    enable_testing()
endif()

find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME chinese_help_test
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/help_translation_test.py --cmake ${CMAKE_COMMAND})
endif()

foreach(camera qhy toup zwo)
    add_executable(${camera}_capture_test ${camera}_capture_test.cpp)
    target_include_directories(${camera}_capture_test PRIVATE ../src)
    target_compile_features(${camera}_capture_test PRIVATE cxx_std_14)
    add_test(NAME ${camera}_capture_test COMMAND ${camera}_capture_test)
endforeach()

option(PHD2_TEST_WXIMAGE "Test real wxImage pixels and native help loading" ON)
set(PHD2_TEST_WX_SOURCE_DIR "" CACHE PATH "Optional wxWidgets source tree for standalone tests")
if(PHD2_TEST_WXIMAGE)
    if(PHD2_TEST_WX_SOURCE_DIR)
        set(wxBUILD_SHARED OFF CACHE BOOL "" FORCE)
        set(wxBUILD_SAMPLES OFF CACHE BOOL "" FORCE)
        set(wxBUILD_TESTS OFF CACHE BOOL "" FORCE)
        add_subdirectory("${PHD2_TEST_WX_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets" EXCLUDE_FROM_ALL)
    elseif(NOT wxWidgets_LIBRARIES AND NOT TARGET wx::core)
        find_package(wxWidgets 3.0 REQUIRED COMPONENTS html core base)
        include(${wxWidgets_USE_FILE})
    endif()
    add_executable(star_profile_image_test star_profile_image_test.cpp)
    target_include_directories(star_profile_image_test PRIVATE ../src)
    target_compile_features(star_profile_image_test PRIVATE cxx_std_14)
    if(TARGET wx::core)
        target_link_libraries(star_profile_image_test PRIVATE wx::core wx::base)
    else()
        target_include_directories(star_profile_image_test PRIVATE ${wxWidgets_INCLUDE_DIRS})
        target_compile_definitions(star_profile_image_test PRIVATE ${wxWidgets_DEFINITIONS})
        target_compile_options(star_profile_image_test PRIVATE ${wxWidgets_CXX_FLAGS})
        target_link_libraries(star_profile_image_test PRIVATE ${wxWidgets_LIBRARIES})
    endif()
    add_test(NAME star_profile_image_test COMMAND star_profile_image_test)

    add_executable(help_load_test help_load_test.cpp)
    target_compile_features(help_load_test PRIVATE cxx_std_14)
    if(TARGET wx::html)
        target_link_libraries(help_load_test PRIVATE wx::html wx::core wx::base)
    else()
        target_include_directories(help_load_test PRIVATE ${wxWidgets_INCLUDE_DIRS})
        target_compile_definitions(help_load_test PRIVATE ${wxWidgets_DEFINITIONS})
        target_compile_options(help_load_test PRIVATE ${wxWidgets_CXX_FLAGS})
        target_link_libraries(help_load_test PRIVATE ${wxWidgets_LIBRARIES})
    endif()
    get_filename_component(test_repo_root "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
    set(test_help_source "${test_repo_root}/locale/zh_CN/help")
    file(GLOB test_help_pages CONFIGURE_DEPENDS "${test_help_source}/*.htm" "${test_help_source}/*.html")
    file(GLOB test_help_inputs CONFIGURE_DEPENDS "${test_help_source}/*")
    set(test_help_output "${CMAKE_CURRENT_BINARY_DIR}/chinese-help/PHD2GuideHelp.zip")
    add_custom_command(OUTPUT "${test_help_output}"
        COMMAND ${CMAKE_COMMAND}
            "-Dproject_root_dir=${test_repo_root}"
            "-Dinput_folder=${test_help_source}"
            "-Doutput_folder=${CMAKE_CURRENT_BINARY_DIR}/chinese-help"
            "-Dlist_of_files=${test_help_pages}"
            -P "${test_repo_root}/cmake_modules/PHD2GenerateDocScript.cmake"
        DEPENDS ${test_help_inputs}
            "${test_repo_root}/cmake_modules/PHD2GenerateDocScript.cmake"
            "${test_repo_root}/cmake_modules/PHD2BuildDoc.cmake"
        VERBATIM)
    add_custom_target(capture_test_help_archive DEPENDS "${test_help_output}")
    add_dependencies(help_load_test capture_test_help_archive)
    add_test(NAME help_load_test COMMAND help_load_test "${test_help_output}")
    set_tests_properties(help_load_test PROPERTIES TIMEOUT 30)
endif()
