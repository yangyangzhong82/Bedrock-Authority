#include "HttpServer.h"
#include "ll/api/io/Logger.h"
#include <drogon/drogon.h>
#include <filesystem>
#include <json/json.h> // For Json::Value

namespace BA {
namespace http {

HttpServer::HttpServer(ll::mod::NativeMod& self, const config::Config& config, permission::PermissionManager& pm)
: mSelf(self),
  mConfig(config),
  mPermissionManager(pm) {
    mSelf.getLogger().debug("HttpServer constructor called.");
}

HttpServer::~HttpServer() {
    mSelf.getLogger().debug("HttpServer destructor called.");
    if (mHttpServerThread.joinable()) {
        mSelf.getLogger().warn("HttpServer is being destroyed but the server thread is still running. Forcing stop.");
        stop();
    }
}

void HttpServer::start() {
    if (!mConfig.http_server_enabled) {
        mSelf.getLogger().info("HTTP server is disabled in config.");
        return;
    }

    // --- 关键改动 ---
    // 1. 在主线程中完成所有 Drogon 的配置
    mSelf.getLogger()
        .info("Configuring HTTP server on %s:%d...", mConfig.http_server_host.c_str(), mConfig.http_server_port);

    drogon::app().addListener(mConfig.http_server_host, mConfig.http_server_port);
    setupRoutes();
    setupStaticFileServer();

    // 2. 在一个单独的线程中只运行 Drogon 的事件循环，以避免阻塞主线程。
    mHttpServerThread = std::thread([this]() {
        mSelf.getLogger().info("Starting Drogon's event loop in a new thread.");

        // 这个调用会阻塞当前线程（即我们新创建的 mHttpServerThread），这是正确的行为。
        drogon::app().run();

        mSelf.getLogger().info("Drogon's event loop has stopped.");
    });
}

void HttpServer::stop() {
    mSelf.getLogger().info("Stopping HTTP server...");

    if (mHttpServerThread.joinable()) {
        // 通知 Drogon 的事件循环退出。
        drogon::app().quit();

        // 等待服务器线程执行完毕。
        mHttpServerThread.join();
        mSelf.getLogger().info("HTTP server thread joined successfully.");
    } else {
        mSelf.getLogger().info("HTTP server was not running or already stopped.");
    }
}

void HttpServer::setupRoutes() {
    mSelf.getLogger().debug("Setting up HTTP routes.");

    // 获取所有权限组
    drogon::app().registerHandler(
        "/api/groups",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            auto        groups = mPermissionManager.getAllGroups();
            Json::Value data;
            data["groups"].resize(0); // 确保即使没有组，也是一个空数组
            for (const auto& group : groups) {
                data["groups"].append(group);
            }
            auto resp = drogon::HttpResponse::newHttpResponse();
            sendJsonResponse(resp, data);
            callback(resp);
        },
        {drogon::HttpMethod::Get}
    );

    // 创建权限组
    drogon::app().registerHandler(
        "/api/groups",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            try {
                Json::Value json;
                if (req->body().empty() || !Json::Reader().parse(std::string(req->body()), json)) {
                    sendErrorResponse(resp, "Invalid JSON body", drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                if (!json.isMember("name") || !json["name"].isString()) {
                    sendErrorResponse(resp, "Missing or invalid 'name' in request body", drogon::k400BadRequest);
                    callback(resp);
                    return;
                }
                std::string groupName   = json["name"].asString();
                std::string description = json.isMember("description") && json["description"].isString()
                                            ? json["description"].asString()
                                            : "";

                if (mPermissionManager.createGroup(groupName, description)) {
                    Json::Value data;
                    data["message"]   = "Group created successfully";
                    data["groupName"] = groupName;
                    sendJsonResponse(resp, data, drogon::k201Created);
                } else {
                    sendErrorResponse(resp, "Failed to create group or group already exists", drogon::k409Conflict);
                }
            } catch (const std::exception& e) {
                mSelf.getLogger().error("Error creating group: %s", e.what());
                sendErrorResponse(resp, "Internal server error", drogon::k500InternalServerError);
            }
            callback(resp);
        },
        {drogon::HttpMethod::Post}
    );

    // 删除权限组
    drogon::app().registerHandler(
        "/api/groups/{groupName}",
        [this](
            const drogon::HttpRequestPtr&                         req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
            const std::string&                                    groupName
        ) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            if (mPermissionManager.deleteGroup(groupName)) {
                Json::Value data;
                data["message"] = "Group deleted successfully";
                sendJsonResponse(resp, data);
            } else {
                sendErrorResponse(resp, "Failed to delete group or group not found", drogon::k404NotFound);
            }
            callback(resp);
        },
        {drogon::HttpMethod::Delete}
    );

    // 获取组详情
    drogon::app().registerHandler(
        "/api/groups/{groupName}",
        [this](
            const drogon::HttpRequestPtr&                         req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
            const std::string&                                    groupName
        ) {
            auto                         resp    = drogon::HttpResponse::newHttpResponse();
            BA::permission::GroupDetails details = mPermissionManager.getGroupDetails(groupName);
            if (details.isValid) { // isValid 是成员变量
                Json::Value data;
                data["id"]          = details.id;
                data["name"]        = details.name;
                data["description"] = details.description;
                data["priority"]    = details.priority;
                sendJsonResponse(resp, data);
            } else {
                sendErrorResponse(resp, "Group not found", drogon::k404NotFound);
            }
            callback(resp);
        },
        {drogon::HttpMethod::Get}
    );

    // 更新组描述
    drogon::app().registerHandler(
        "/api/groups/{groupName}/description",
        [this](
            const drogon::HttpRequestPtr&                         req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
            const std::string&                                    groupName
        ) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            try {
                Json::Value json;
                if (req->body().empty() || !Json::Reader().parse(std::string(req->body()), json)) {
                    sendErrorResponse(resp, "Invalid JSON body", drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                if (!json.isMember("description") || !json["description"].isString()) {
                    sendErrorResponse(resp, "Missing or invalid 'description' in request body", drogon::k400BadRequest);
                    callback(resp);
                    return;
                }
                std::string newDescription = json["description"].asString();

                if (mPermissionManager.updateGroupDescription(groupName, newDescription)) {
                    Json::Value data;
                    data["message"] = "Group description updated successfully";
                    sendJsonResponse(resp, data);
                } else {
                    sendErrorResponse(
                        resp,
                        "Failed to update group description or group not found",
                        drogon::k404NotFound
                    );
                }
            } catch (const std::exception& e) {
                mSelf.getLogger().error("Error updating group description: %s", e.what());
                sendErrorResponse(resp, "Internal server error", drogon::k500InternalServerError);
            }
            callback(resp);
        },
        {drogon::HttpMethod::Put}
    );

    // 设置组优先级
    drogon::app().registerHandler(
        "/api/groups/{groupName}/priority",
        [this](
            const drogon::HttpRequestPtr&                         req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
            const std::string&                                    groupName
        ) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            try {
                Json::Value json;
                if (req->body().empty() || !Json::Reader().parse(std::string(req->body()), json)) {
                    sendErrorResponse(resp, "Invalid JSON body", drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                if (!json.isMember("priority") || !json["priority"].isInt()) {
                    sendErrorResponse(
                        resp,
                        "Missing or invalid 'priority' (must be integer) in request body",
                        drogon::k400BadRequest
                    );
                    callback(resp);
                    return;
                }
                int priority = json["priority"].asInt();

                if (mPermissionManager.setGroupPriority(groupName, priority)) {
                    Json::Value data;
                    data["message"] = "Group priority updated successfully";
                    sendJsonResponse(resp, data);
                } else {
                    sendErrorResponse(resp, "Failed to set group priority or group not found", drogon::k404NotFound);
                }
            } catch (const std::exception& e) {
                mSelf.getLogger().error("Error setting group priority: %s", e.what());
                sendErrorResponse(resp, "Internal server error", drogon::k500InternalServerError);
            }
            callback(resp);
        },
        {drogon::HttpMethod::Put}
    );

    // 获取组的直接权限
    drogon::app().registerHandler(
        "/api/groups/{groupName}/permissions/direct",
        [this](
            const drogon::HttpRequestPtr&                         req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
            const std::string&                                    groupName
        ) {
            auto        resp        = drogon::HttpResponse::newHttpResponse();
            auto        permissions = mPermissionManager.getDirectPermissionsOfGroup(groupName);
            Json::Value data;
            data["permissions"].resize(0);
            for (const auto& perm : permissions) {
                // getDirectPermissionsOfGroup 返回的是 std::string，不是 CompiledPermissionRule
                data["permissions"].append(perm);
            }
            sendJsonResponse(resp, data);
            callback(resp);
        },
        {drogon::HttpMethod::Get}
    );

    // 获取组的所有权限 (包括继承)
    drogon::app().registerHandler(
        "/api/groups/{groupName}/permissions/effective",
        [this](
            const drogon::HttpRequestPtr&                         req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
            const std::string&                                    groupName
        ) {
            auto        resp        = drogon::HttpResponse::newHttpResponse();
            auto        permissions = mPermissionManager.getPermissionsOfGroup(groupName); // 返回 CompiledPermissionRule
            Json::Value data;
            data["permissions"].resize(0);
            for (const auto& perm : permissions) {
                Json::Value permJson;
                permJson["pattern"] = perm.pattern;
                permJson["state"]   = perm.state;
                data["permissions"].append(permJson);
            }
            sendJsonResponse(resp, data);
            callback(resp);
        },
        {drogon::HttpMethod::Get}
    );

    // 向组添加权限
    drogon::app().registerHandler(
        "/api/groups/{groupName}/permissions",
        [this](
            const drogon::HttpRequestPtr&                         req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
            const std::string&                                    groupName
        ) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            try {
                Json::Value json;
                if (req->body().empty() || !Json::Reader().parse(std::string(req->body()), json)) {
                    sendErrorResponse(resp, "Invalid JSON body", drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                if (!json.isMember("permission") || !json["permission"].isString()) {
                    sendErrorResponse(resp, "Missing or invalid 'permission' in request body", drogon::k400BadRequest);
                    callback(resp);
                    return;
                }
                std::string permissionRule = json["permission"].asString();

                if (mPermissionManager.addPermissionToGroup(groupName, permissionRule)) {
                    Json::Value data;
                    data["message"] = "Permission added to group successfully";
                    sendJsonResponse(resp, data, drogon::k201Created);
                } else {
                    sendErrorResponse(
                        resp,
                        "Failed to add permission to group or group/permission already exists",
                        drogon::k409Conflict
                    );
                }
            } catch (const std::exception& e) {
                mSelf.getLogger().error("Error adding permission to group: %s", e.what());
                sendErrorResponse(resp, "Internal server error", drogon::k500InternalServerError);
            }
            callback(resp);
        },
        {drogon::HttpMethod::Post}
    );

    // 从组移除权限
    drogon::app().registerHandler(
        "/api/groups/{groupName}/permissions",
        [this](
            const drogon::HttpRequestPtr&                         req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
            const std::string&                                    groupName
        ) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            try {
                Json::Value json;
                if (req->body().empty() || !Json::Reader().parse(std::string(req->body()), json)) {
                    sendErrorResponse(resp, "Invalid JSON body", drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                if (!json.isMember("permission") || !json["permission"].isString()) {
                    sendErrorResponse(resp, "Missing or invalid 'permission' in request body", drogon::k400BadRequest);
                    callback(resp);
                    return;
                }
                std::string permissionRule = json["permission"].asString();

                if (mPermissionManager.removePermissionFromGroup(groupName, permissionRule)) {
                    Json::Value data;
                    data["message"] = "Permission removed from group successfully";
                    sendJsonResponse(resp, data);
                } else {
                    sendErrorResponse(
                        resp,
                        "Failed to remove permission from group or group/permission not found",
                        drogon::k404NotFound
                    );
                }
            } catch (const std::exception& e) {
                mSelf.getLogger().error("Error removing permission from group: %s", e.what());
                sendErrorResponse(resp, "Internal server error", drogon::k500InternalServerError);
            }
            callback(resp);
        },
        {drogon::HttpMethod::Delete}
    );

    // 获取组的所有祖先组
    drogon::app().registerHandler(
        "/api/groups/{groupName}/ancestors",
        [this](
            const drogon::HttpRequestPtr&                         req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
            const std::string&                                    groupName
        ) {
            auto        resp         = drogon::HttpResponse::newHttpResponse();
            auto        ancestorGroups = mPermissionManager.getAllAncestorGroups(groupName);
            Json::Value data;
            data["ancestors"].resize(0);
            for (const auto& ancestor : ancestorGroups) {
                data["ancestors"].append(ancestor);
            }
            sendJsonResponse(resp, data);
            callback(resp);
        },
        {drogon::HttpMethod::Get}
    );

    // 获取组的直接父组 (与前端 /api/groups/{groupName}/parents 匹配)
    drogon::app().registerHandler(
        "/api/groups/{groupName}/parents", // 注意这里是 /parents
        [this](
            const drogon::HttpRequestPtr&                         req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
            const std::string&                                    groupName
        ) {
            auto        resp         = drogon::HttpResponse::newHttpResponse();
            auto        directParentGroups = mPermissionManager.getDirectParentGroups(groupName);
            Json::Value data;
            data["parents"].resize(0); // 确保键名与前端期望的 'parents' 匹配
            for (const auto& parent : directParentGroups) {
                data["parents"].append(parent);
            }
            sendJsonResponse(resp, data);
            callback(resp);
        },
        {drogon::HttpMethod::Get}
    );

    // 添加组继承
    drogon::app().registerHandler(
        "/api/groups/{groupName}/parents",
        [this](
            const drogon::HttpRequestPtr&                         req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
            const std::string&                                    groupName
        ) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            try {
                Json::Value json;
                if (req->body().empty() || !Json::Reader().parse(std::string(req->body()), json)) {
                    sendErrorResponse(resp, "Invalid JSON body", drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                if (!json.isMember("parentGroup") || !json["parentGroup"].isString()) {
                    sendErrorResponse(resp, "Missing or invalid 'parentGroup' in request body", drogon::k400BadRequest);
                    callback(resp);
                    return;
                }
                std::string parentGroupName = json["parentGroup"].asString();

                if (mPermissionManager.addGroupInheritance(groupName, parentGroupName)) {
                    Json::Value data;
                    data["message"] = "Group inheritance added successfully";
                    sendJsonResponse(resp, data, drogon::k201Created);
                } else {
                    sendErrorResponse(
                        resp,
                        "Failed to add group inheritance or inheritance already exists/invalid groups",
                        drogon::k409Conflict
                    );
                }
            } catch (const std::exception& e) {
                mSelf.getLogger().error("Error adding group inheritance: %s", e.what());
                sendErrorResponse(resp, "Internal server error", drogon::k500InternalServerError);
            }
            callback(resp);
        },
        {drogon::HttpMethod::Post}
    );

    // 移除组继承
    drogon::app().registerHandler(
        "/api/groups/{groupName}/parents",
        [this](
            const drogon::HttpRequestPtr&                         req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
            const std::string&                                    groupName
        ) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            try {
                Json::Value json;
                if (req->body().empty() || !Json::Reader().parse(std::string(req->body()), json)) {
                    sendErrorResponse(resp, "Invalid JSON body", drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                if (!json.isMember("parentGroup") || !json["parentGroup"].isString()) {
                    sendErrorResponse(resp, "Missing or invalid 'parentGroup' in request body", drogon::k400BadRequest);
                    callback(resp);
                    return;
                }
                std::string parentGroupName = json["parentGroup"].asString();

                if (mPermissionManager.removeGroupInheritance(groupName, parentGroupName)) {
                    Json::Value data;
                    data["message"] = "Group inheritance removed successfully";
                    sendJsonResponse(resp, data);
                } else {
                    sendErrorResponse(
                        resp,
                        "Failed to remove group inheritance or inheritance not found",
                        drogon::k404NotFound
                    );
                }
            } catch (const std::exception& e) {
                mSelf.getLogger().error("Error removing group inheritance: %s", e.what());
                sendErrorResponse(resp, "Internal server error", drogon::k500InternalServerError);
            }
            callback(resp);
        },
        {drogon::HttpMethod::Delete}
    );

    // 获取组中的玩家
    drogon::app().registerHandler(
        "/api/groups/{groupName}/players",
        [this](
            const drogon::HttpRequestPtr&                         req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
            const std::string&                                    groupName
        ) {
            auto        resp    = drogon::HttpResponse::newHttpResponse();
            auto        players = mPermissionManager.getPlayersInGroup(groupName);
            Json::Value data;
            data["players"].resize(0);
            for (const auto& player : players) {
                data["players"].append(player);
            }
            sendJsonResponse(resp, data);
            callback(resp);
        },
        {drogon::HttpMethod::Get}
    );

    // 将玩家添加到组
    drogon::app().registerHandler(
        "/api/groups/{groupName}/players",
        [this](
            const drogon::HttpRequestPtr&                         req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
            const std::string&                                    groupName
        ) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            try {
                Json::Value json;
                if (req->body().empty() || !Json::Reader().parse(std::string(req->body()), json)) {
                    sendErrorResponse(resp, "Invalid JSON body", drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                if (!json.isMember("playerUuid") || !json["playerUuid"].isString()) {
                    sendErrorResponse(resp, "Missing or invalid 'playerUuid' in request body", drogon::k400BadRequest);
                    callback(resp);
                    return;
                }
                std::string playerUuid = json["playerUuid"].asString();

                if (mPermissionManager.addPlayerToGroup(playerUuid, groupName)) {
                    Json::Value data;
                    data["message"] = "Player added to group successfully";
                    sendJsonResponse(resp, data, drogon::k201Created);
                } else {
                    sendErrorResponse(
                        resp,
                        "Failed to add player to group or player already in group/group not found",
                        drogon::k409Conflict
                    );
                }
            } catch (const std::exception& e) {
                mSelf.getLogger().error("Error adding player to group: %s", e.what());
                sendErrorResponse(resp, "Internal server error", drogon::k500InternalServerError);
            }
            callback(resp);
        },
        {drogon::HttpMethod::Post}
    );

    // 从组移除玩家
    drogon::app().registerHandler(
        "/api/groups/{groupName}/players",
        [this](
            const drogon::HttpRequestPtr&                         req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
            const std::string&                                    groupName
        ) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            try {
                Json::Value json;
                if (req->body().empty() || !Json::Reader().parse(std::string(req->body()), json)) {
                    sendErrorResponse(resp, "Invalid JSON body", drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                if (!json.isMember("playerUuid") || !json["playerUuid"].isString()) {
                    sendErrorResponse(resp, "Missing or invalid 'playerUuid' in request body", drogon::k400BadRequest);
                    callback(resp);
                    return;
                }
                std::string playerUuid = json["playerUuid"].asString();

                if (mPermissionManager.removePlayerFromGroup(playerUuid, groupName)) {
                    Json::Value data;
                    data["message"] = "Player removed from group successfully";
                    sendJsonResponse(resp, data);
                } else {
                    sendErrorResponse(
                        resp,
                        "Failed to remove player from group or player/group not found",
                        drogon::k404NotFound
                    );
                }
            } catch (const std::exception& e) {
                mSelf.getLogger().error("Error removing player from group: %s", e.what());
                sendErrorResponse(resp, "Internal server error", drogon::k500InternalServerError);
            }
            callback(resp);
        },
        {drogon::HttpMethod::Delete}
    );

    // 获取玩家在组中的过期时间
    drogon::app().registerHandler(
        "/api/players/{playerUuid}/groups/{groupName}/expiration",
        [this](
            const drogon::HttpRequestPtr&                         req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
            const std::string&                                    playerUuid,
            const std::string&                                    groupName
        ) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            try {
                auto expirationTime = mPermissionManager.getPlayerGroupExpirationTime(playerUuid, groupName);
                Json::Value data;
                if (expirationTime.has_value()) {
                    data["expirationTime"] = expirationTime.value();
                } else {
                    data["expirationTime"] = -1; // -1 表示永不过期或玩家不在组中
                }
                sendJsonResponse(resp, data);
            } catch (const std::exception& e) {
                mSelf.getLogger().error("Error getting player group expiration time: %s", e.what());
                sendErrorResponse(resp, "Internal server error", drogon::k500InternalServerError);
            }
            callback(resp);
        },
        {drogon::HttpMethod::Get}
    );

    // 设置玩家在组中的过期时间
    drogon::app().registerHandler(
        "/api/players/{playerUuid}/groups/{groupName}/expiration",
        [this](
            const drogon::HttpRequestPtr&                         req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
            const std::string&                                    playerUuid,
            const std::string&                                    groupName
        ) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            try {
                Json::Value json;
                if (req->body().empty() || !Json::Reader().parse(std::string(req->body()), json)) {
                    sendErrorResponse(resp, "Invalid JSON body", drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                if (!json.isMember("durationSeconds") || !json["durationSeconds"].isInt64()) {
                    sendErrorResponse(
                        resp,
                        "Missing or invalid 'durationSeconds' (must be integer) in request body",
                        drogon::k400BadRequest
                    );
                    callback(resp);
                    return;
                }
                long long durationSeconds = json["durationSeconds"].asInt64();

                if (mPermissionManager.setPlayerGroupExpirationTime(playerUuid, groupName, durationSeconds)) {
                    Json::Value data;
                    data["message"] = "Player group expiration time updated successfully";
                    sendJsonResponse(resp, data);
                } else {
                    sendErrorResponse(
                        resp,
                        "Failed to set player group expiration time or player/group not found",
                        drogon::k404NotFound
                    );
                }
            } catch (const std::exception& e) {
                mSelf.getLogger().error("Error setting player group expiration time: %s", e.what());
                sendErrorResponse(resp, "Internal server error", drogon::k500InternalServerError);
            }
            callback(resp);
        },
        {drogon::HttpMethod::Put}
    );
} // setupRoutes 方法的结束括号

void HttpServer::setupStaticFileServer() {
    mSelf.getLogger().info("Setting up static file server for path: %s", mConfig.http_server_static_path.c_str());

    std::string finalStaticPath = mConfig.http_server_static_path;
    bool        pathFound       = false;

    if (std::filesystem::exists(finalStaticPath)) {
        pathFound = true;
        mSelf.getLogger().info("Configured static path exists: %s", finalStaticPath.c_str());
    } else {
        mSelf.getLogger().error("Configured static path does not exist: %s", finalStaticPath.c_str());

        std::string alternativePath1 = "src/http_static";
        if (std::filesystem::exists(alternativePath1)) {
            finalStaticPath = alternativePath1;
            pathFound       = true;
            mSelf.getLogger().info("Found static files at: %s, using this path instead", finalStaticPath.c_str());
        } else {
            std::string alternativePath2 = "../src/http_static";
            if (std::filesystem::exists(alternativePath2)) {
                finalStaticPath = alternativePath2;
                pathFound       = true;
                mSelf.getLogger().info("Found static files at: %s, using this path instead", finalStaticPath.c_str());
            } else {
                mSelf.getLogger().error(
                    "Could not find static files directory anywhere! Static file server might not work correctly."
                );
            }
        }
    }

    if (pathFound) {
        mSelf.getLogger().info("Files in static directory:");
        try {
            for (const auto& entry : std::filesystem::directory_iterator(finalStaticPath)) {
                mSelf.getLogger().info(" - %s", entry.path().string().c_str());
            }
        } catch (const std::exception& e) {
            mSelf.getLogger().error("Error listing files in static directory: %s", e.what());
        }
    }

    drogon::app().setDocumentRoot(finalStaticPath);
    mSelf.getLogger().info("Document root set to: %s", finalStaticPath.c_str());

    drogon::app().registerHandler(
        "/{path}",
        [](const drogon::HttpRequestPtr&                         req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
           const std::string&                                    path) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k204NoContent);
            resp->addHeader("Access-Control-Allow-Origin", "*");
            resp->addHeader("Access-Control-Allow-Methods", "GET,POST,PUT,DELETE,OPTIONS");
            resp->addHeader("Access-Control-Allow-Headers", "Content-Type,Authorization");
            resp->addHeader("Access-Control-Max-Age", "86400");
            callback(resp);
        },
        {drogon::HttpMethod::Options}
    );
}

void HttpServer::sendJsonResponse(
    const drogon::HttpResponsePtr& resp,
    const Json::Value&             data,
    drogon::HttpStatusCode         code
) {
    resp->setStatusCode(code);
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "GET,POST,PUT,DELETE,OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type,Authorization");
    resp->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    resp->addHeader("Pragma", "no-cache");
    resp->addHeader("Expires", "0");
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    resp->setBody(Json::writeString(builder, data));
}

void HttpServer::sendErrorResponse(
    const drogon::HttpResponsePtr& resp,
    const std::string&             message,
    drogon::HttpStatusCode         code
) {
    Json::Value error;
    error["error"] = message;
    sendJsonResponse(resp, error, code);
}

} // namespace http
} // namespace BA
