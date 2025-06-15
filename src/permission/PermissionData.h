#pragma once

#include <regex>
#include <set>
#include <string>
#include <vector>


// Define BA_API based on whether we are building the DLL or using it
#ifdef BA_EXPORTS // This should be defined by the build system when building the DLL
#ifdef _WIN32
#define BA_API __declspec(dllexport)
#else
#define BA_API __attribute__((visibility("default"))) // For GCC/Clang on Linux/macOS
#endif
#else // We are using the DLL
#ifdef _WIN32
#define BA_API __declspec(dllimport)
#else
#define BA_API // Import is default visibility on GCC/Clang
#endif
#endif

namespace BA {
namespace permission {

// 异步缓存失效任务类型
enum class CacheInvalidationTaskType {
    GROUP_MODIFIED,       // 组权限或继承关系改变
    PLAYER_GROUP_CHANGED, // 玩家组关系改变
    ALL_GROUPS_MODIFIED,  // 所有组的权限或默认权限改变
    ALL_PLAYERS_MODIFIED, // 所有玩家的默认权限改变
    SHUTDOWN              // 停止工作线程
};

// 异步缓存失效任务结构体
struct CacheInvalidationTask {
    CacheInvalidationTaskType type;
    std::string               data; // 存储组名或玩家UUID等数据
};

// 结构体用于返回组的详细信息
struct GroupDetails {
    std::string id;
    std::string name;
    std::string description;
    int         priority;
    bool        isValid = false;

    GroupDetails() = default;

    GroupDetails(std::string id, std::string name, std::string description, int priority)
    : id(std::move(id)),
      name(std::move(name)),
      description(std::move(description)),
      priority(priority),
      isValid(true) {}
};

// 结构体用于在 getAllPermissionsForPlayer 中统一处理组权限
struct GroupPermissionInfo {
    std::string              id;
    std::string              name;
    int                      priority;
    std::vector<std::string> directPermissionRules;

    GroupPermissionInfo(std::string id, std::string name, int priority, std::vector<std::string> rules)
    : id(std::move(id)),
      name(std::move(name)),
      priority(priority),
      directPermissionRules(std::move(rules)) {}
};

// 编译后的权限规则结构体
struct CompiledPermissionRule {
    std::string pattern;
    std::regex  regex;
    bool        state;

    CompiledPermissionRule(std::string p, std::regex r, bool s)
    : pattern(std::move(p)),
      regex(std::move(r)),
      state(s) {}
};

// 结构体用于存储权限的定义
struct PermissionDefinition {
    std::string name;
    std::string description;
    bool        defaultValue;
};


} // namespace permission
} // namespace BA