// 通用常量：constants 定义应用名称、配置路径和数据目录默认值。
// 这些常量用于跨模块保持路径与命名一致。
#pragma once

namespace constants
{
// 应用程序名称。
inline constexpr char kApplicationName[] = "VisionInspectionSystem";
// 组织名称。
inline constexpr char kOrganizationName[] = "Codex";
// 配置目录名。
inline constexpr char kConfigDirectoryName[] = "config";
// 配置文件名。
inline constexpr char kConfigFileName[] = "settings.ini";
// 数据目录名。
inline constexpr char kDataDirectoryName[] = "data";
// SQLite 数据库文件名。
inline constexpr char kDatabaseFileName[] = "inspection.db";
// 图片归档目录。
inline constexpr char kImageArchiveDirectoryName[] = "data/images";
// 日志目录名。
inline constexpr char kLogDirectoryName[] = "logs";
} // namespace constants
