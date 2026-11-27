#include "phantomx/wallet/node_client.hpp"

using phantomx::rpc::RpcRequest;
using phantomx::rpc::RpcResponse;

#include <curl/curl.h>
#include <jansson.h>

namespace phantomx::wallet {
namespace {

size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* out = static_cast<std::string*>(userdata);
  out->append(ptr, size * nmemb);
  return size * nmemb;
}

[[nodiscard]] Result<std::string> post_json(const std::string& url, const std::string& body) {
  CURL* curl = curl_easy_init();
  if (curl == nullptr) return MakeError(ErrorCode::kIoError, "curl_easy_init failed");
  std::string response;
  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 5000L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 2000L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "phantomx-wallet");
  curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0L);
  const CURLcode rc = curl_easy_perform(curl);
  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  curl_easy_cleanup(curl);
  curl_slist_free_all(headers);
  if (rc != CURLE_OK) return MakeError(ErrorCode::kIoError, std::string("HTTP request failed: ") + curl_easy_strerror(rc));
  if (http_code >= 400) return MakeError(ErrorCode::kIoError, "HTTP " + std::to_string(http_code) + " from " + url);
  return response;
}

[[nodiscard]] Result<std::string> build_request_json(const RpcRequest& req) {
  json_t* root = json_object();
  if (root == nullptr) return MakeError(ErrorCode::kInternalError, "json_object failed");

  json_object_set_new(root, "id", json_string(req.id.c_str()));
  json_object_set_new(root, "method", json_string(req.method.c_str()));
  json_object_set_new(root, "token", json_string(req.auth_token.c_str()));
  json_t* params = json_object();
  if (params == nullptr) {
    json_decref(root);
    return MakeError(ErrorCode::kInternalError, "json_object(params) failed");
  }
  for (const auto& [key, value] : req.params) {
    json_object_set_new(params, key.c_str(), json_string(value.c_str()));
  }
  json_object_set_new(root, "params", params);
  char* text = json_dumps(root, JSON_COMPACT | JSON_SORT_KEYS);
  std::string out = text != nullptr ? text : "";
  if (text != nullptr) free((void*)text);
  json_decref(root);
  if (out.empty()) return MakeError(ErrorCode::kInternalError, "json_dumps failed");
  return out;
}

}  // namespace

NodeClient::NodeClient(std::string base_url) : base_url_(std::move(base_url)) {
  static bool curl_initialized = [] {
    (void)curl_global_init(CURL_GLOBAL_DEFAULT);
    return true;
  }();
  (void)curl_initialized;
}

Result<RpcResponse> NodeClient::call(const RpcRequest& req) const {
  auto body = build_request_json(req);
  if (!body.ok()) return body.error();
  auto response = post_json(base_url_, body.value());
  if (!response.ok()) return response.error();

  json_error_t jerr;
  json_t* root = json_loads(response.value().c_str(), 0, &jerr);
  if (root == nullptr) {
    return MakeError(ErrorCode::kParseError,
                     "invalid JSON response: " + std::string(jerr.text) + " at line " +
                         std::to_string(jerr.line));
  }
  RpcResponse out;
  out.id = req.id;

  const json_t* err_field = json_object_get(root, "error");
  if (err_field != nullptr && err_field != json_null()) {
    const json_t* code = json_object_get(err_field, "code");
    const json_t* msg = json_object_get(err_field, "message");
    out.ok = false;
    if (code != nullptr) out.code = static_cast<phantomx::rpc::RpcErrorCode>(json_integer_value(code));
    if (msg != nullptr && json_is_string(msg)) out.error_message = json_string_value(msg);
    json_decref(root);
    return out;
  }

  out.ok = true;
  json_t* result = json_object_get(root, "result");
  if (result != nullptr && json_is_object(result)) {
    void* iter = json_object_iter(result);
    while (iter != nullptr) {
      const char* key = json_object_iter_key(iter);
      json_t* value = json_object_iter_value(iter);
      if (key != nullptr && value != nullptr) {
        if (json_is_string(value)) {
          out.result[key] = json_string_value(value);
        } else {
          char* text = json_dumps(value, JSON_COMPACT);
          if (text != nullptr) {
            out.result[key] = text;
            free(text);
          }
        }
      }
      iter = json_object_iter_next(result, iter);
    }
  }
  json_decref(root);
  return out;
}

}  // namespace phantomx::wallet
