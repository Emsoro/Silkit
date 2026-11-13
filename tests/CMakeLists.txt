cmake_minimum_required(VERSION 3.21)

# 单元测试：纯逻辑 + SQLite 内存库；不依赖 WebView2 / 窗口。
# 共享静态库避免两个测试目标重复编译业务代码。
set(SILKIT_TEST_LIB_SOURCES
    "${CMAKE_SOURCE_DIR}/src/core/json_merge.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/logger.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/paths.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/settings.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/profiles.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/secrets.cpp"
    "${CMAKE_SOURCE_DIR}/src/db/types.cpp"
    "${CMAKE_SOURCE_DIR}/src/db/driver_registry.cpp"
    "${CMAKE_SOURCE_DIR}/src/db/identifier.cpp"
    "${CMAKE_SOURCE_DIR}/src/db/type_map.cpp"
    "${CMAKE_SOURCE_DIR}/src/db/sql_splitter.cpp"
    "${CMAKE_SOURCE_DIR}/src/db/sql_util.cpp"
    "${CMAKE_SOURCE_DIR}/src/db/session.cpp"
    "${CMAKE_SOURCE_DIR}/src/db/session_manager.cpp"
    "${CMAKE_SOURCE_DIR}/src/db/drivers/sqlite_driver.cpp"
    "${CMAKE_SOURCE_DIR}/src/util/base64.cpp"
)

add_library(silkit_test_lib STATIC ${SILKIT_TEST_LIB_SOURCES})

target_include_directories(silkit_test_lib PUBLIC
    "${CMAKE_SOURCE_DIR}/src"
    "${NLOHMANN_INCLUDE_DIR}"
)

target_compile_definitions(silkit_test_lib PUBLIC
    SILKIT_VERSION="${PROJECT_VERSION}"
    UNICODE
    _UNICODE
)

target_link_libraries(silkit_test_lib PUBLIC
    unofficial::sqlite3::sqlite3
    crypt32
    shell32
    ole32
)

if(MSVC)
    target_compile_options(silkit_test_lib PRIVATE /W4 /permissive- /utf-8)
endif()

add_executable(silkit_tests test_main.cpp)
target_link_libraries(silkit_tests PRIVATE silkit_test_lib)
add_test(NAME silkit_smoke COMMAND silkit_tests)
set_tests_properties(silkit_smoke PROPERTIES TIMEOUT 30)

add_executable(test_sqlite_driver test_sqlite_driver.cpp)
target_link_libraries(test_sqlite_driver PRIVATE silkit_test_lib)
add_test(NAME sqlite_driver COMMAND test_sqlite_driver)
set_tests_properties(sqlite_driver PROPERTIES TIMEOUT 30)
