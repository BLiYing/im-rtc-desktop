#include <cstring>
#include <string>

#include "imrtc/DebugToken.h"
#include "imrtc/Errors.h"
#include "imrtc/imrtc_c.h"

#include "CApiConvert.h"

/** 调试签票的 C 入口。异常在这里吃掉转成错误码（CONVENTIONS §2 红线 5）。仅调试用。 */
std::int32_t imrtc_v1_debug_sign_token(const imrtc_v1_debug_token_options* options, char* out,
                                       std::uint32_t out_capacity, std::uint32_t* out_len) {
  if (options == nullptr || out == nullptr) return IMRTC_V1_ERR_BAD_PARAMS;
  if (options->struct_size < sizeof(imrtc_v1_debug_token_options)) return IMRTC_V1_ERR_BAD_PARAMS;
  using imrtc::capi_detail::cstr;
  imrtc::DebugTokenParams params;
  params.appId = cstr(options->app_id);
  params.keyId = cstr(options->key_id);
  params.secret = cstr(options->secret);
  params.uid = cstr(options->uid);
  params.deviceId = cstr(options->device_id);
  params.ttlSec = options->ttl_sec;
  params.nowUnix = options->now_unix;
  try {
    const std::string token = imrtc::signDebugToken(params);
    if (out_len != nullptr) *out_len = static_cast<std::uint32_t>(token.size());
    if (token.size() + 1 > out_capacity) return IMRTC_V1_ERR_BAD_PARAMS;
    std::memcpy(out, token.c_str(), token.size() + 1);
    return IMRTC_V1_OK;
  } catch (const imrtc::RtcError& error) {
    return error.code();
  } catch (...) {
    return IMRTC_V1_ERR_INTERNAL;
  }
}
