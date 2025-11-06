#pragma once

#include "config/Config.h"
#include "ll/api/mod/NativeMod.h"
#include "permission/PermissionManager.h"
#include <memory>
#include <string>
#include <thread> 


namespace drogon {
using HttpResponsePtr = std::shared_ptr<class HttpResponse>;
enum HttpStatusCode;
} // namespace drogon

namespace Json {
class Value;
}

namespace BA {
namespace http {

class HttpServer {
public:
    HttpServer(ll::mod::NativeMod& self, const config::Config& config, permission::PermissionManager& pm);
    ~HttpServer();

    void start();
    void stop();

private:
    ll::mod::NativeMod&            mSelf;
    const config::Config&          mConfig;
    permission::PermissionManager& mPermissionManager;
    std::thread                    mHttpServerThread; // 持有服务器线程

    void setupRoutes();
    void setupStaticFileServer();
    void sendJsonResponse(
        const drogon::HttpResponsePtr& resp,
        const Json::Value&             data,
        drogon::HttpStatusCode         code
    );
    void sendErrorResponse(
        const drogon::HttpResponsePtr& resp,
        const std::string&             message,
        drogon::HttpStatusCode         code
    );
};

} // namespace http
} // namespace BA
