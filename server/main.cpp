cler#include <drogon/drogon.h>
#include <fstream>
#include <mutex>
#include <atomic>
#include <filesystem>
using namespace drogon;

static std::mutex gMu;
static Json::Value gLists(Json::arrayValue);
static Json::Value gTasks(Json::arrayValue);

static const std::string LISTS_FILE = "data/lists.json";
static const std::string TASKS_FILE = "data/tasks.json";

static std::string uid() {
    static std::atomic<uint64_t> c{0};
    auto v = ++c;
    return std::to_string(v) + "_" + std::to_string((uint64_t)trantor::Date::now().microSecondsSinceEpoch());
}

static void ensureDataFolder() {
    std::error_code ec;
    std::filesystem::create_directories("data", ec);
}

static void loadJson(const std::string &path, Json::Value &dst, const Json::Value &fallback) {
    std::ifstream f(path);
    if (!f.good()) { dst = fallback; return; }
    Json::CharReaderBuilder b; std::string errs; Json::Value v;
    if (Json::parseFromStream(b, f, &v, &errs)) dst = v;
    else dst = fallback;
}

static void saveJson(const std::string &path, const Json::Value &v) {
    std::ofstream f(path, std::ios::trunc);
    Json::StreamWriterBuilder b; b["indentation"] = "  ";
    f << Json::writeString(b, v);
}

static Json::Value nowIso() {
    auto d = trantor::Date::now();
    char buf[64];
    auto tm = d.toLocalTime();
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             tm.year(), tm.month(), tm.day(),
             tm.hour(), tm.minute(), tm.second());
    Json::Value s(buf);
    return s;
}

int main() {
    ensureDataFolder();
    Json::Value defaultLists(Json::arrayValue);
    {
        Json::Value inbox(Json::objectValue);
        inbox["id"]="inbox"; inbox["name"]="Inbox"; inbox["icon"]="📥"; inbox["color"]="#3b82f6";
        inbox["createdAt"]=nowIso(); inbox["updatedAt"]=nowIso();
        defaultLists.append(inbox);
    }
    {
        std::lock_guard<std::mutex> lk(gMu);
        loadJson(LISTS_FILE, gLists, defaultLists);
        loadJson(TASKS_FILE, gTasks, Json::arrayValue);
    }

    app().registerHandler("/api/lists",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)> cb){
            if (req->method()==Get) {
                std::lock_guard<std::mutex> lk(gMu);
                cb(HttpResponse::newHttpJsonResponse(gLists));
            } else if (req->method()==Post) {
                auto json = req->getJsonObject();
                if (!json || !(*json)["name"].isString()) return cb(HttpResponse::newHttpResponse(k400BadRequest));
                Json::Value list(Json::objectValue);
                list["id"]=uid();
                list["name"]=(*json)["name"].asString();
                list["icon"]=(*json)["icon"].isString()? (*json)["icon"] : "🗂️";
                list["color"]=(*json)["color"].isString()? (*json)["color"] : "#3b82f6";
                list["createdAt"]=nowIso(); list["updatedAt"]=nowIso();
                {
                    std::lock_guard<std::mutex> lk(gMu);
                    gLists.append(list);
                    saveJson(LISTS_FILE, gLists);
                }
                cb(HttpResponse::newHttpJsonResponse(list));
            } else cb(HttpResponse::newNotFoundResponse());
        }, {Get, Post});

    app().registerHandler("/api/lists/{1}",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)> cb, std::string id){
            if (req->method()==Put) {
                auto json = req->getJsonObject();
                if (!json) return cb(HttpResponse::newHttpResponse(k400BadRequest));
                std::lock_guard<std::mutex> lk(gMu);
                for (auto &l : gLists) {
                    if (l["id"].asString()==id) {
                        if ((*json)["name"].isString()) l["name"]=(*json)["name"];
                        if ((*json)["icon"].isString()) l["icon"]=(*json)["icon"];
                        if ((*json)["color"].isString()) l["color"]=(*json)["color"];
                        l["updatedAt"]=nowIso();
                        saveJson(LISTS_FILE, gLists);
                        return cb(HttpResponse::newHttpJsonResponse(l));
                    }
                }
                cb(HttpResponse::newNotFoundResponse());
            } else if (req->method()==Delete) {
                if (id=="inbox") return cb(HttpResponse::newHttpResponse(k400BadRequest));
                std::lock_guard<std::mutex> lk(gMu);
                Json::Value nl(Json::arrayValue);
                for (auto &l : gLists) if (l["id"].asString()!=id) nl.append(l);
                gLists = nl;
                Json::Value nt(Json::arrayValue);
                for (auto &t : gTasks) if (t["listId"].asString()!=id) nt.append(t);
                gTasks = nt;
                saveJson(LISTS_FILE, gLists); saveJson(TASKS_FILE, gTasks);
                cb(HttpResponse::newHttpResponse());
            } else cb(HttpResponse::newNotFoundResponse());
        }, {Put, Delete});

    app().registerHandler("/api/tasks",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)> cb){
            if (req->method()==Get) {
                auto listId = req->getParameter("listId");
                Json::Value out(Json::arrayValue);
                std::lock_guard<std::mutex> lk(gMu);
                for (auto &t : gTasks) if (listId.empty() || t["listId"].asString()==listId) out.append(t);
                cb(HttpResponse::newHttpJsonResponse(out));
            } else if (req->method()==Post) {
                auto json = req->getJsonObject();
                if (!json || !(*json)["title"].isString()) return cb(HttpResponse::newHttpResponse(k400BadRequest));
                Json::Value t(Json::objectValue);
                t["id"]=uid();
                t["listId"]=(*json)["listId"].isString()? (*json)["listId"].asString() : "inbox";
                t["title"]=(*json)["title"].asString();
                t["notes"]=(*json)["notes"].isString()? (*json)["notes"] : "";
                t["status"]=(*json)["status"].isString()? (*json)["status"] : "todo";
                t["priority"]=(*json)["priority"].isString()? (*json)["priority"] : "none";
                if ((*json)["dueDate"].isString()) t["dueDate"]=(*json)["dueDate"];
                if ((*json)["reminder"].isString()) t["reminder"]=(*json)["reminder"];
                t["recurrence"]=(*json)["recurrence"].isString()? (*json)["recurrence"] : "none";
                t["tags"]=(*json)["tags"].isArray()? (*json)["tags"] : Json::arrayValue;
                t["subtasks"]=(*json)["subtasks"].isArray()? (*json)["subtasks"] : Json::arrayValue;
                t["createdAt"]=nowIso(); t["updatedAt"]=nowIso();
                {
                    std::lock_guard<std::mutex> lk(gMu);
                    gTasks.append(t);
                    saveJson(TASKS_FILE, gTasks);
                }
                cb(HttpResponse::newHttpJsonResponse(t));
            } else cb(HttpResponse::newNotFoundResponse());
        }, {Get, Post});

    app().registerHandler("/api/tasks/{1}",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)> cb, std::string id){
            if (req->method()==Put) {
                auto json = req->getJsonObject();
                if (!json) return cb(HttpResponse::newHttpResponse(k400BadRequest));
                std::lock_guard<std::mutex> lk(gMu);
                for (auto &t : gTasks) {
                    if (t["id"].asString()==id) {
                        for (auto it = json->begin(); it != json->end(); ++it) {
                            auto key = it.key().asString();
                            if (key=="id" || key=="createdAt") continue;
                            t[key] = *it;
                        }
                        t["updatedAt"]=nowIso();
                        saveJson(TASKS_FILE, gTasks);
                        return cb(HttpResponse::newHttpJsonResponse(t));
                    }
                }
                cb(HttpResponse::newNotFoundResponse());
            } else if (req->method()==Delete) {
                std::lock_guard<std::mutex> lk(gMu);
                Json::Value nt(Json::arrayValue);
                bool found=false;
                for (auto &t : gTasks) {
                    if (t["id"].asString()==id) { found=true; continue; }
                    nt.append(t);
                }
                gTasks = nt;
                if (found) saveJson(TASKS_FILE, gTasks);
                cb(found? HttpResponse::newHttpResponse() : HttpResponse::newNotFoundResponse());
            } else cb(HttpResponse::newNotFoundResponse());
        }, {Put, Delete});

    app().addListener("0.0.0.0", 8080).setDocumentRoot("./static").setStaticFilesCacheTime(0);
    LOG_INFO << "ToDoListManager C++ server running on http://localhost:8080";
    app().run();
    return 0;
}
