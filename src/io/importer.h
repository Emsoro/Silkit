#pragma once

#include "db/driver.h"
#include "io/csv.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace silkit::io {

/// M7-4：CSV → 表批量插入。批量 INSERT 包在事务里，失败时整批回滚。
struct CsvImportColumn {
    std::size_t index = 0;  // CSV 字段下标
    std::string name;       // 目标列名；为空表示忽略该字段
    std::string type;       // 建表类型；为空按引擎默认
};

struct CsvImportOptions {
    bool has_header = true;
    bool create_table = false;
    bool empty_as_null = true;
    std::size_t batch_size = 500;
    std::string scope;  // MySQL 库 / PG schema / SQLite 附加库；空表示不加前缀
    std::string table;
    std::vector<CsvImportColumn> columns;
};

struct CsvImportResult {
    std::uint64_t rows = 0;
    std::uint64_t batches = 0;
    std::uint64_t elapsed_ms = 0;
};

struct CsvImportError {
    std::string stage;  // parse / args / create / begin / insert / commit
    std::string message;
    std::size_t record = 0;  // 1-based 数据行（不含表头）
    std::size_t line = 0;    // 文件物理行号
    bool rolled_back = false;
};

enum class ImportStatus { Ok, Cancelled, Failed };

using ImportProgress = std::function<void(std::uint64_t rows, std::uint64_t total)>;

/// 在连接上执行导入。cancelled 返回 true 时回滚并返回 Cancelled。
ImportStatus ImportCsv(db::Connection& connection, const std::string& utf8_text,
                       const CsvOptions& csv_options, const CsvImportOptions& options,
                       CsvImportResult& result, CsvImportError& error,
                       const ImportProgress& progress = {}, const std::function<bool()>& cancelled = {});

}  // namespace silkit::io
