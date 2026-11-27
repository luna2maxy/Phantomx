#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

#include <curl/curl.h>
#include <jansson.h>

#include "phantomx/common/cli.hpp"
#include "phantomx/common/logging.hpp"

namespace {
std::atomic<bool> g_stop{false};
void on_sig(int) { g_stop.store(true); }

size_t write_cb(void* contents, size_t size, size_t nmemb, void* userp) {
  const size_t total = size * nmemb;
  static_cast<std::string*>(userp)->append(static_cast<char*>(contents), total);
  return total;
}

std::string rpc_call(const std::string& url, const std::string& token, const std::string& method,
                     json_t* params_obj) {
  json_t* root = json_object();
  json_object_set_new(root, "id", json_string("1"));
  json_object_set_new(root, "method", json_string(method.c_str()));
  json_object_set_new(root, "token", json_string(token.c_str()));
  json_object_set_new(root, "params", params_obj ? params_obj : json_object());
  char* dumped = json_dumps(root, JSON_COMPACT);
  std::string body = dumped ? dumped : "{}";
  if (dumped) free(dumped);
  json_decref(root);

  CURL* curl = curl_easy_init();
  if (!curl) return "{\"ok\":false,\"error_message\":\"curl init failed\"}";
  std::string resp;
  struct curl_slist* hdrs = nullptr;
  hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
  const auto rc = curl_easy_perform(curl);
  if (rc != CURLE_OK) {
    resp = std::string("{\"ok\":false,\"error_message\":\"") + curl_easy_strerror(rc) + "\"}";
  }
  curl_slist_free_all(hdrs);
  curl_easy_cleanup(curl);
  return resp;
}

bool json_ok(const std::string& s) {
  json_error_t err{};
  json_t* root = json_loads(s.c_str(), 0, &err);
  if (!root) return false;
  json_t* ok = json_object_get(root, "ok");
  if (ok && json_is_boolean(ok)) { const bool v = json_is_true(ok); json_decref(root); return v; }
  json_t* error = json_object_get(root, "error");
  const bool v = (error == nullptr || json_is_null(error)) && json_object_get(root, "result") != nullptr;
  json_decref(root);
  return v;
}

std::string json_result_field(const std::string& s, const char* field) {
  json_error_t err{};
  json_t* root = json_loads(s.c_str(), 0, &err);
  if (!root) return {};
  json_t* result = json_object_get(root, "result");
  json_t* v = result ? json_object_get(result, field) : nullptr;
  std::string out;
  if (v && json_is_string(v)) out = json_string_value(v);
  else if (v && json_is_integer(v)) out = std::to_string(json_integer_value(v));
  json_decref(root);
  return out;
}
}  // namespace

int main(int argc, char* argv[]) {
  phantomx::CliParser parser("phantomx-poi");
  parser.add_option("--mode", "PoI mode (verifier|contributor)", "verifier");
  parser.add_option("--rpc", "Node RPC URL", "http://127.0.0.1:8741");
  parser.add_option("--token", "Node RPC token", "local-dev-token");
  parser.add_option("--domain", "Verifier domain/endpoint", "");
  parser.add_option("--bandwidth-mb", "Bandwidth limit in MB", "100");
  parser.add_option("--interval", "Poll interval seconds", "10");
  parser.add_option("--log-level", "Log level", "info");

  auto parsed = parser.parse(argc, argv);
  if (!parsed.ok()) { std::cerr << parsed.error().to_string() << "\n" << parser.help_text(); return 2; }
  const auto& args = parsed.value();
  auto get = [&](const char* k, const char* def) {
    auto it = args.options.find(k);
    return it != args.options.end() ? it->second : std::string(def);
  };
  auto has = [&](const char* k) {
    return std::find(args.flags.begin(), args.flags.end(), k) != args.flags.end();
  };
  if (has("--help")) { std::cout << parser.help_text(); return 0; }
  if (has("--version")) { std::cout << "phantomx-poi 0.1.0\n"; return 0; }
  auto logger = phantomx::Logger("phantomx-poi", std::clog);
  logger.set_level(phantomx::parse_log_level(get("--log-level", "info")));
  const std::string mode = get("--mode", "verifier");
  const std::string rpc = get("--rpc", "http://127.0.0.1:8741");
  const std::string token = get("--token", "local-dev-token");
  const std::string domain = get("--domain", "");
  const std::string bw = get("--bandwidth-mb", "100");
  int interval = 10;
  try { interval = std::max(2, std::stoi(get("--interval", "10"))); } catch (...) {}

  std::signal(SIGINT, on_sig);
  std::signal(SIGTERM, on_sig);
  curl_global_init(CURL_GLOBAL_DEFAULT);

  logger.info("startup", {{"mode", mode}, {"rpc", rpc}, {"domain", domain.empty() ? "not_set" : domain}, {"bandwidth_mb", bw}});

  // Best-effort register/start with node RPC.
  if (mode == "verifier") {
    // get an address for verifier registration
    auto addrResp = rpc_call(rpc, token, "getnewaddress", json_object());
    std::string addr = json_result_field(addrResp, "address");
    if (addr.empty()) {
      // fallback: try creating via getnewaddress with label
      json_t* ap = json_object(); json_object_set_new(ap, "label", json_string("verifier_demo"));
      addrResp = rpc_call(rpc, token, "getnewaddress", ap);
      addr = json_result_field(addrResp, "address");
    }
    json_t* p = json_object();
    json_object_set_new(p, "id", json_string("verifier-demo-1"));
    json_object_set_new(p, "address", json_string(addr.c_str()));
    json_object_set_new(p, "endpoint", json_string(domain.empty() ? "local.verifier:9000" : domain.c_str()));
    auto resp = rpc_call(rpc, token, "verifier_register", p);
    logger.info("verifier_register", {{"ok", json_ok(resp) ? "true" : "false"}, {"address", addr}, {"response", resp}});
  }
  {
    json_t* p = json_object();
    json_object_set_new(p, "bandwidth_value", json_string(bw.c_str()));
    json_object_set_new(p, "bandwidth_unit", json_string("MB"));
    json_object_set_new(p, "bandwidth_bytes", json_string(std::to_string(std::stoull(bw) * 1024ULL * 1024ULL).c_str()));
    if (!domain.empty()) json_object_set_new(p, "domain", json_string(domain.c_str()));
    auto resp = rpc_call(rpc, token, "startpoi", p);
    logger.info("startpoi", {{"ok", json_ok(resp) ? "true" : "false"}, {"status", json_result_field(resp, "status")}});
  }

  while (!g_stop.load()) {
    auto info = rpc_call(rpc, token, "getpoiinfo", json_object());
    auto ver = rpc_call(rpc, token, "getverifierinfo", json_object());
    logger.info("heartbeat", {
        {"mode", mode},
        {"poi_ok", json_ok(info) ? "true" : "false"},
        {"poi_status", json_result_field(info, "status")},
        {"verifier_ok", json_ok(ver) ? "true" : "false"}
    });
    for (int i = 0; i < interval && !g_stop.load(); ++i) std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  auto stop = rpc_call(rpc, token, "stoppoi", json_object());
  logger.info("stoppoi", {{"ok", json_ok(stop) ? "true" : "false"}});
  curl_global_cleanup();
  logger.info("shutdown", {{"mode", mode}});
  return 0;
}
