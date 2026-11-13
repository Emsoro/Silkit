cmake_minimum_required(VERSION 3.21)

# M0 冒烟测试：不依赖 WebView2 / 窗口，只验证纯逻辑与默认配置。
add_executable(silkit_tests
    test_main.cpp
    "${CMAKE_SOURCE_DIR}/src/core/json_merge.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/logger.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/paths.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/settings.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/profiles.cpp"
)

target_include_directories(silkit_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${NLOHMANN_INCLUDE_DIR}"
)

target_compile_definitions(silkit_tests PRIVATE
    SILKIT_VERSION="${PROJECT_VERSION}"
    UNICODE
    _UNICODE
)

target_link_libraries(silkit_tests PRIVATE
    shell32
    ole32
)

if(MSVC)
    target_compile_options(silkit_tests PRIVATE /W4 /permissive- /utf-8)
endif()

add_test(NAME silkit_smoke COMMAND silkit_tests)
set_tests_properties(silkit_smoke PROPERTIES TIMEOUT 30)
