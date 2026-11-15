cmake_minimum_required(VERSION 3.21)
project(Silkit VERSION 1.0.1 LANGUAGES CXX)

# C++20（MSVC /std:c++20，VS 2022 17.x+）；C++ 标准由顶层统一控制，
# TauriCPP 内置副本的源码通过 file(GLOB) 直接编译，同样继承该标准。
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 静态 CRT：与 vcpkg x64-windows-static triplet 对齐，产出不依赖 VC 运行时的单文件 exe
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(MSVC)
    add_compile_options(/utf-8)
    add_compile_definitions(NOMINMAX)
endif()

option(EMBED_FRONTEND "把 frontend/ 资源嵌入 exe（关闭时从 exe 同级 frontend 目录热加载）" ON)

# ============================================================================
# TauriCPP（内置副本，按 StepDraw 的方式直接编译为静态库）
# ============================================================================
set(TAURICPP_DIR "${CMAKE_SOURCE_DIR}/TauriCPP" CACHE PATH "TauriCPP 框架根目录")
if(NOT EXISTS "${TAURICPP_DIR}/include/tauricpp/app.hpp")
    message(FATAL_ERROR "未找到 TauriCPP 框架: ${TAURICPP_DIR}")
endif()

set(NLOHMANN_INCLUDE_DIR "${TAURICPP_DIR}/third_party")
if(NOT EXISTS "${NLOHMANN_INCLUDE_DIR}/nlohmann/json.hpp")
    message(FATAL_ERROR "缺少 nlohmann/json.hpp: ${NLOHMANN_INCLUDE_DIR}")
endif()

# WebView2 / SQLite 都由 vcpkg manifest 安装，使用官方导出的 imported target，
# 避免手工 find_library 在 Debug/Release 配置间选错库。
find_package(unofficial-webview2 CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(unofficial-libmariadb CONFIG REQUIRED)
find_package(PostgreSQL REQUIRED)

message(STATUS "WebView2 target: unofficial::webview2::webview2")
message(STATUS "SQLite target: unofficial::sqlite3::sqlite3")
message(STATUS "MariaDB target: unofficial::libmariadb")
message(STATUS "PostgreSQL target: PostgreSQL::PostgreSQL")

file(GLOB TAURICPP_SOURCES CONFIGURE_DEPENDS "${TAURICPP_DIR}/src/*.cpp")
add_library(tauricpp STATIC ${TAURICPP_SOURCES})
target_include_directories(tauricpp PUBLIC
    "${TAURICPP_DIR}/include"
    "${NLOHMANN_INCLUDE_DIR}"
)
target_link_libraries(tauricpp PUBLIC
    shlwapi
    shell32
    comdlg32
    ole32
    unofficial::webview2::webview2
)

# ============================================================================
# 前端资源打包（Python 生成 .rc + resource_map.cpp）
# ============================================================================
set(FRONTEND_DIR "${CMAKE_SOURCE_DIR}/frontend")
set(GEN_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GEN_DIR}")
set(RESOURCES_RC "${GEN_DIR}/resources.rc")
set(RESOURCE_IDS_H "${GEN_DIR}/resource_ids.h")
set(RESOURCE_MAP_CPP "${GEN_DIR}/resource_map.cpp")
set(FRONTEND_PACK_SCRIPT "${CMAKE_SOURCE_DIR}/tools/pack_resources_cmake.cmake")

find_package(Python3 COMPONENTS Interpreter)

if(EMBED_FRONTEND)
    file(GLOB_RECURSE FRONTEND_FILES CONFIGURE_DEPENDS "${FRONTEND_DIR}/*")

    if(Python3_Interpreter_FOUND)
        message(STATUS "前端打包：TauriCPP pack_resources.py")
        add_custom_command(
            OUTPUT "${RESOURCES_RC}" "${RESOURCE_MAP_CPP}" "${RESOURCE_IDS_H}"
            COMMAND ${Python3_EXECUTABLE} "${TAURICPP_DIR}/tools/pack_resources.py"
                    "${FRONTEND_DIR}" -o "${RESOURCES_RC}" -t "TAURI_RES"
            DEPENDS "${TAURICPP_DIR}/tools/pack_resources.py" ${FRONTEND_FILES}
            COMMENT "Packing frontend resources into .rc"
            VERBATIM
        )
    else()
        # 没有可用的 Python 时使用内置 CMake 打包器，保证单文件构建开箱可用。
        message(STATUS "前端打包：未找到 Python3，使用 CMake 内置打包器")
        add_custom_command(
            OUTPUT "${RESOURCES_RC}" "${RESOURCE_MAP_CPP}" "${RESOURCE_IDS_H}"
            COMMAND ${CMAKE_COMMAND}
                    "-DFRONTEND_DIR=${FRONTEND_DIR}"
                    "-DGEN_DIR=${GEN_DIR}"
                    "-DRESOURCE_TYPE=TAURI_RES"
                    -P "${FRONTEND_PACK_SCRIPT}"
            DEPENDS "${FRONTEND_PACK_SCRIPT}" ${FRONTEND_FILES}
            COMMENT "Packing frontend resources into .rc (CMake)"
            VERBATIM
        )
    endif()
else()
    file(WRITE "${RESOURCES_RC}" "// Auto-generated: frontend not embedded\n")
    file(WRITE "${RESOURCE_IDS_H}" "// Auto-generated: frontend not embedded (dev mode)\n")
    file(WRITE "${RESOURCE_MAP_CPP}"
        "// Auto-generated: frontend not embedded (dev mode)\n"
        "#include \"tauricpp/virtual_fs.hpp\"\n"
        "#include <Windows.h>\n\n"
        "namespace tauricpp {\n"
        "void VirtualFS::LoadFromResources(HMODULE) {}\n"
        "}  // namespace tauricpp\n"
    )
endif()

# ============================================================================
# 版本资源 / DPI manifest
# ============================================================================
set(APP_ADDITIONAL_SOURCES "")

if(MSVC)
    file(WRITE "${GEN_DIR}/app_version.rc"
        "#pragma code_page(65001)\n"
        "#include <windows.h>\n"
        "VS_VERSION_INFO VERSIONINFO\n"
        " FILEVERSION ${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0\n"
        " PRODUCTVERSION ${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0\n"
        " FILEFLAGSMASK 0x3fL\n"
        " FILEFLAGS 0x0L\n"
        " FILEOS VOS_NT_WINDOWS32\n"
        " FILETYPE VFT_APP\n"
        " FILESUBTYPE VFT2_UNKNOWN\n"
        "BEGIN\n"
        "    BLOCK \"StringFileInfo\"\n"
        "    BEGIN\n"
        "        BLOCK \"040904b0\"\n"
        "        BEGIN\n"
        "            VALUE \"CompanyName\", \"Silkit\"\n"
        "            VALUE \"FileDescription\", \"Silkit - 跨库数据库工作台\"\n"
        "            VALUE \"FileVersion\", \"${PROJECT_VERSION}\"\n"
        "            VALUE \"InternalName\", \"Silkit\"\n"
        "            VALUE \"LegalCopyright\", \"Copyright (c) 2026 Silkit\"\n"
        "            VALUE \"OriginalFilename\", \"Silkit.exe\"\n"
        "            VALUE \"ProductName\", \"Silkit\"\n"
        "            VALUE \"ProductVersion\", \"${PROJECT_VERSION}\"\n"
        "        END\n"
        "    END\n"
        "    BLOCK \"VarFileInfo\"\n"
        "    BEGIN\n"
        "        VALUE \"Translation\", 0x409, 1200\n"
        "    END\n"
        "END\n"
    )
    list(APPEND APP_ADDITIONAL_SOURCES "${GEN_DIR}/app_version.rc")

    # 应用图标：由 icon.png 经 make_icon.py 生成的多分辨率 .ico
    list(APPEND APP_ADDITIONAL_SOURCES "${CMAKE_SOURCE_DIR}/app_icon.rc")

    file(WRITE "${GEN_DIR}/dpi_awareness.manifest"
        "<?xml version='1.0' encoding='UTF-8' standalone='yes'?>\n"
        "<assembly xmlns='urn:schemas-microsoft-com:asm.v1' manifestVersion='1.0'>\n"
        "  <trustInfo xmlns=\"urn:schemas-microsoft-com:asm.v3\">\n"
        "    <security>\n"
        "      <requestedPrivileges>\n"
        "        <requestedExecutionLevel level='asInvoker' uiAccess='false' />\n"
        "      </requestedPrivileges>\n"
        "    </security>\n"
        "  </trustInfo>\n"
        "  <application>\n"
        "    <windowsSettings>\n"
        "      <dpiAware xmlns='http://schemas.microsoft.com/SMI/2005/WindowsSettings'>true/pm</dpiAware>\n"
        "      <dpiAwareness xmlns='http://schemas.microsoft.com/SMI/2016/WindowsSettings'>PerMonitorV2</dpiAwareness>\n"
        "    </windowsSettings>\n"
        "  </application>\n"
        "  <compatibility xmlns='urn:schemas-microsoft-com:compatibility.v1'>\n"
        "    <application>\n"
        "      <supportedOS Id='{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}'/>\n"
        "    </application>\n"
        "  </compatibility>\n"
        "</assembly>\n")
    list(APPEND APP_ADDITIONAL_SOURCES "${GEN_DIR}/dpi_awareness.manifest")
endif()

# ============================================================================
# 主程序
# ============================================================================
set(SILKIT_SOURCES
    src/main.cpp
    src/core/paths.cpp
    src/core/logger.cpp
    src/core/json_merge.cpp
    src/core/settings.cpp
    src/core/profiles.cpp
    src/core/secrets.cpp
    src/db/types.cpp
    src/db/driver_registry.cpp
    src/db/sql_splitter.cpp
    src/db/sql_util.cpp
    src/db/identifier.cpp
    src/db/type_map.cpp
    src/db/engine_dialect.cpp
    src/db/session.cpp
    src/db/session_manager.cpp
    src/db/table_ops.cpp
    src/db/ddl_builder.cpp
    src/db/drivers/sqlite_driver.cpp
    src/db/drivers/mysql_driver.cpp
    src/db/drivers/postgres_driver.cpp
    src/io/encoding.cpp
    src/io/csv.cpp
    src/io/exporter.cpp
    src/io/importer.cpp
    src/io/dumper.cpp
    src/ipc/async_router.cpp
    src/ipc/commands.cpp
    src/ipc/commands_db_common.cpp
    src/ipc/commands_conn.cpp
    src/ipc/commands_meta.cpp
    src/ipc/commands_meta_engine.cpp
    src/ipc/commands_query.cpp
    src/ipc/commands_table.cpp
    src/ipc/commands_ddl.cpp
    src/ipc/commands_io.cpp
    src/util/base64.cpp
)

add_executable(Silkit WIN32
    ${SILKIT_SOURCES}
    "${RESOURCES_RC}"
    "${RESOURCE_MAP_CPP}"
    ${APP_ADDITIONAL_SOURCES}
)

target_include_directories(Silkit PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${GEN_DIR}"
)

if(MSVC)
    target_compile_options(Silkit PRIVATE /W4 /permissive- /Zc:__cplusplus /MP)
endif()

target_link_libraries(Silkit PRIVATE
    tauricpp
    unofficial::sqlite3::sqlite3
    unofficial::libmariadb
    PostgreSQL::PostgreSQL
    crypt32
    ws2_32
    secur32
    shell32
    shlwapi
    ole32
    comdlg32
)

target_compile_definitions(Silkit PRIVATE
    SILKIT_VERSION="${PROJECT_VERSION}"
    UNICODE
    _UNICODE
)

set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT Silkit)

# ============================================================================
# 测试
# ============================================================================
enable_testing()
add_subdirectory(tests)
