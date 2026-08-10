#include "git_transport.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <set>
#include <mutex>
#include <sstream>

#if defined(__OHOS__)
#include <network/netstack/net_http.h>
#endif

namespace harmony_git {
namespace {

bool IsHexCharacter(char value) {
  const unsigned char character =
      static_cast<unsigned char>(value);
  return std::isdigit(character) != 0 ||
      (character >= 'a' && character <= 'f') ||
      (character >= 'A' && character <= 'F');
}

bool IsObjectId(const std::string& value) {
  if (value.size() != 40 && value.size() != 64) {
    return false;
  }
  return std::all_of(
      value.begin(),
      value.end(),
      [](char character) {
        return IsHexCharacter(character);
      });
}

int HexValue(char value) {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }
  return value - 'A' + 10;
}

std::string TrimLineEnding(std::string value) {
  while (!value.empty() &&
         (value.back() == '\n' || value.back() == '\r')) {
    value.pop_back();
  }
  return value;
}

[[maybe_unused]] std::string Join(
    const std::vector<std::string>& values,
    const std::string& separator) {
  std::string result;
  for (size_t index = 0; index < values.size(); ++index) {
    if (index > 0U) {
      result += separator;
    }
    result += values[index];
  }
  return result;
}

void ReadCapabilities(
    const std::string& text,
    RemoteAdvertisement* advertisement) {
  std::istringstream input(text);
  std::string capability;
  while (input >> capability) {
    advertisement->capabilities.push_back(capability);
    const std::string prefix = "symref=HEAD:";
    if (capability.rfind(prefix, 0) == 0) {
      advertisement->headTarget =
          capability.substr(prefix.size());
    }
  }
}

bool ParseReferenceLine(
    std::string line,
    RemoteAdvertisement* advertisement) {
  line = TrimLineEnding(line);
  if (line.empty() || line.rfind("# service=", 0) == 0) {
    return true;
  }
  if (line.rfind("version ", 0) == 0) {
    advertisement->error =
        "Git protocol v2 advertisement is not supported yet.";
    return false;
  }

  const size_t capabilitySeparator = line.find('\0');
  if (capabilitySeparator != std::string::npos) {
    ReadCapabilities(
        line.substr(capabilitySeparator + 1),
        advertisement);
    line.resize(capabilitySeparator);
  }
  const size_t separator = line.find_first_of(" \t");
  if (separator == std::string::npos) {
    advertisement->error =
        "Remote advertisement contains a malformed reference.";
    return false;
  }
  const std::string objectId = line.substr(0, separator);
  const std::string name = line.substr(separator + 1);
  if (!IsObjectId(objectId) || name.empty()) {
    advertisement->error =
        "Remote advertisement contains an invalid reference.";
    return false;
  }
  if (name == "capabilities^{}") {
    return true;
  }
  advertisement->references.push_back({objectId, name});
  return true;
}

bool WildcardMatch(
    const std::string& value,
    const std::string& pattern) {
  size_t valueIndex = 0;
  size_t patternIndex = 0;
  size_t wildcardIndex = std::string::npos;
  size_t wildcardValueIndex = 0;
  while (valueIndex < value.size()) {
    if (patternIndex < pattern.size() &&
        (pattern[patternIndex] == '?' ||
         pattern[patternIndex] == value[valueIndex])) {
      valueIndex++;
      patternIndex++;
    } else if (patternIndex < pattern.size() &&
               pattern[patternIndex] == '*') {
      wildcardIndex = patternIndex++;
      wildcardValueIndex = valueIndex;
    } else if (wildcardIndex != std::string::npos) {
      patternIndex = wildcardIndex + 1;
      valueIndex = ++wildcardValueIndex;
    } else {
      return false;
    }
  }
  while (patternIndex < pattern.size() &&
         pattern[patternIndex] == '*') {
    patternIndex++;
  }
  return patternIndex == pattern.size();
}

bool MatchesPattern(
    const std::string& name,
    const std::string& pattern) {
  if (WildcardMatch(name, pattern)) {
    return true;
  }
  for (size_t index = 0; index < name.size(); ++index) {
    if (name[index] == '/' &&
        WildcardMatch(name.substr(index + 1), pattern)) {
      return true;
    }
  }
  return false;
}

bool MatchesPatterns(
    const std::string& name,
    const std::vector<std::string>& patterns) {
  if (patterns.empty()) {
    return true;
  }
  return std::any_of(
      patterns.begin(),
      patterns.end(),
      [&name](const std::string& pattern) {
        return MatchesPattern(name, pattern);
      });
}

std::string NormalizeRemoteUrl(
    const std::string& remoteUrl,
    std::string* error) {
  if (error != nullptr) {
    error->clear();
  }
  if (remoteUrl.rfind("https://", 0) != 0 &&
      remoteUrl.rfind("http://", 0) != 0) {
    if (error != nullptr) {
      *error =
          "Only HTTP and HTTPS remote URLs are supported by NetworkKit.";
    }
    return "";
  }
  const size_t authorityStart = remoteUrl.find("://") + 3;
  const size_t pathStart = remoteUrl.find('/', authorityStart);
  const std::string authority = remoteUrl.substr(
      authorityStart,
      pathStart == std::string::npos
          ? std::string::npos
          : pathStart - authorityStart);
  if (authority.empty()) {
    if (error != nullptr) {
      *error = "The remote URL does not contain a host.";
    }
    return "";
  }
  if (authority.find('@') != std::string::npos) {
    if (error != nullptr) {
      *error =
          "Credentials embedded in remote URLs are not supported.";
    }
    return "";
  }
  if (remoteUrl.find('?', authorityStart) != std::string::npos ||
      remoteUrl.find('#', authorityStart) != std::string::npos) {
    if (error != nullptr) {
      *error =
          "Remote URLs with query strings or fragments are not supported.";
    }
    return "";
  }
  std::string result = remoteUrl;
  while (!result.empty() && result.back() == '/') {
    result.pop_back();
  }
  return result;
}

std::string EncodePacketLine(const std::string& payload) {
  static const char digits[] = "0123456789abcdef";
  const size_t length = payload.size() + 4U;
  if (length > 0xffffU) {
    return "";
  }
  std::string result(4, '0');
  result[0] = digits[(length >> 12U) & 0x0fU];
  result[1] = digits[(length >> 8U) & 0x0fU];
  result[2] = digits[(length >> 4U) & 0x0fU];
  result[3] = digits[length & 0x0fU];
  result += payload;
  return result;
}

bool ReadBigEndian32(
    const std::string& data,
    size_t offset,
    uint32_t* value) {
  if (offset + 4U > data.size()) {
    return false;
  }
  *value =
      (static_cast<uint32_t>(
           static_cast<unsigned char>(data[offset])) << 24U) |
      (static_cast<uint32_t>(
           static_cast<unsigned char>(data[offset + 1U])) << 16U) |
      (static_cast<uint32_t>(
           static_cast<unsigned char>(data[offset + 2U])) << 8U) |
      static_cast<uint32_t>(
          static_cast<unsigned char>(data[offset + 3U]));
  return true;
}

#if defined(__OHOS__)

struct HttpSyncResponse {
  bool complete = false;
  uint32_t errorCode = OH_HTTP_RESULT_OK;
  int32_t responseCode = 0;
  std::string body;
};

std::mutex gRequestMutex;
std::mutex gResponseMutex;
std::condition_variable gResponseCondition;
HttpSyncResponse* gActiveResponse = nullptr;

void OnHttpResponse(Http_Response* response, uint32_t errorCode) {
  {
    std::lock_guard<std::mutex> lock(gResponseMutex);
    if (gActiveResponse != nullptr) {
      gActiveResponse->errorCode = errorCode;
      if (response != nullptr) {
        gActiveResponse->responseCode =
            static_cast<int32_t>(response->responseCode);
        if (response->body.buffer != nullptr &&
            response->body.length > 0) {
          gActiveResponse->body.assign(
              response->body.buffer,
              response->body.length);
        }
      }
      gActiveResponse->complete = true;
    }
  }
  gResponseCondition.notify_all();
  if (response != nullptr &&
      response->destroyResponse != nullptr) {
    response->destroyResponse(&response);
  }
}

std::string HttpErrorMessage(uint32_t errorCode) {
  switch (errorCode) {
    case OH_HTTP_PERMISSION_DENIED:
      return "Network permission was denied.";
    case OH_HTTP_UNSUPPORTED_PROTOCOL:
      return "The remote protocol is not supported.";
    case OH_HTTP_INVALID_URL:
      return "The remote URL is invalid.";
    case OH_HTTP_RESOLVE_PROXY_FAILED:
      return "The configured proxy could not be resolved.";
    case OH_HTTP_RESOLVE_HOST_FAILED:
      return "The remote host could not be resolved.";
    case OH_HTTP_CONNECT_SERVER_FAILED:
      return "The remote server could not be reached.";
    case OH_HTTP_OPERATION_TIMEOUT:
      return "The remote request timed out.";
    case OH_HTTP_SSL_CERTIFICATE_ERROR:
    case OH_HTTP_INVALID_SSL_PEER_CERT:
    case OH_HTTP_SSL_CA_NOT_EXIST:
      return "TLS certificate validation failed.";
    case OH_HTTP_AUTHENTICATION_ERROR:
      return "Remote authentication failed.";
    default:
      return "HarmonyOS NetworkKit request failed with code " +
          std::to_string(errorCode) + ".";
  }
}

RemoteAdvertisement FetchRemoteAdvertisement(
    const std::string& requestUrl,
    const std::string& authorization,
    bool receivePack,
    const RemoteTransportOptions& transportOptions) {
  std::lock_guard<std::mutex> requestLock(gRequestMutex);
  RemoteAdvertisement result;
  std::string optionError;
  if (!ValidateRemoteTransportOptions(transportOptions, &optionError)) {
    result.error = optionError;
    return result;
  }
  std::string proxyMode = transportOptions.proxyMode;
  std::transform(
      proxyMode.begin(),
      proxyMode.end(),
      proxyMode.begin(),
      [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
      });
  Http_Request* request =
      OH_Http_CreateRequest(requestUrl.c_str());
  if (request == nullptr) {
    result.error = "Cannot create a HarmonyOS HTTP request.";
    return result;
  }
  Http_Headers* headers = OH_Http_CreateHeaders();
  if (headers == nullptr) {
    OH_Http_Destroy(&request);
    result.error = "Cannot allocate HTTP request headers.";
    return result;
  }
  OH_Http_SetHeaderValue(
      headers,
      "Accept",
      receivePack
          ? "application/x-git-receive-pack-advertisement"
          : "application/x-git-upload-pack-advertisement");
  OH_Http_SetHeaderValue(
      headers,
      "User-Agent",
      "Harmony-Git-Bash/0.1");
  if (!authorization.empty()) {
    OH_Http_SetHeaderValue(
        headers,
        "Authorization",
        authorization.c_str());
  }

  Http_RequestOptions options = {};
  options.method = NET_HTTP_METHOD_GET;
  options.headers = headers;
  options.readTimeout = transportOptions.readTimeout;
  options.connectTimeout = transportOptions.connectTimeout;
  Http_Proxy proxy = {};
  std::string exclusionLists;
  if (proxyMode == "none") {
    proxy.proxyType = HTTP_PROXY_NOT_USE;
    options.httpProxy = &proxy;
  } else if (proxyMode == "custom") {
    proxy.proxyType = HTTP_PROXY_CUSTOM;
    exclusionLists = Join(
        transportOptions.proxyExclusions,
        ",");
    proxy.customProxy.host = transportOptions.proxyHost.c_str();
    proxy.customProxy.port =
        static_cast<int32_t>(transportOptions.proxyPort);
    proxy.customProxy.exclusionLists = exclusionLists.c_str();
    options.httpProxy = &proxy;
  } else {
    proxy.proxyType = HTTP_PROXY_SYSTEM;
    options.httpProxy = &proxy;
  }
  request->options = &options;

  HttpSyncResponse response;
  {
    std::lock_guard<std::mutex> responseLock(gResponseMutex);
    gActiveResponse = &response;
  }
  Http_EventsHandler handler = {};
  const int requestError =
      OH_Http_Request(request, OnHttpResponse, handler);
  if (requestError == OH_HTTP_RESULT_OK) {
    std::unique_lock<std::mutex> responseLock(gResponseMutex);
    const uint64_t waitMilliseconds =
        static_cast<uint64_t>(transportOptions.connectTimeout) +
        static_cast<uint64_t>(transportOptions.readTimeout) +
        5000U;
    const bool completed = gResponseCondition.wait_for(
        responseLock,
        std::chrono::milliseconds(waitMilliseconds),
        [&response] {
          return response.complete;
        });
    gActiveResponse = nullptr;
    if (!completed) {
      result.error = "The remote request timed out.";
    }
  } else {
    std::lock_guard<std::mutex> responseLock(gResponseMutex);
    gActiveResponse = nullptr;
    result.error =
        HttpErrorMessage(static_cast<uint32_t>(requestError));
  }

  OH_Http_Destroy(&request);
  OH_Http_DestroyHeaders(&headers);
  if (!result.error.empty()) {
    return result;
  }
  result.responseCode = response.responseCode;
  if (response.errorCode != OH_HTTP_RESULT_OK) {
    result.error = HttpErrorMessage(response.errorCode);
    return result;
  }
  if (response.responseCode != OH_HTTP_OK) {
    result.error = "Remote server returned HTTP " +
        std::to_string(response.responseCode) + ".";
    return result;
  }
  result = ParseRemoteAdvertisement(response.body);
  result.responseCode = response.responseCode;
  return result;
}

#endif

}  // namespace

bool ValidateRemoteTransportOptions(
    const RemoteTransportOptions& options,
    std::string* error) {
  if (error != nullptr) {
    error->clear();
  }
  if (options.connectTimeout == 0U || options.readTimeout == 0U) {
    if (error != nullptr) {
      *error = "Remote transport timeouts must be greater than zero.";
    }
    return false;
  }
  std::string proxyMode = options.proxyMode;
  std::transform(
      proxyMode.begin(),
      proxyMode.end(),
      proxyMode.begin(),
      [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
      });
  if (proxyMode != "system" &&
      proxyMode != "none" &&
      proxyMode != "custom") {
    if (error != nullptr) {
      *error = "Remote transport proxy mode is invalid.";
    }
    return false;
  }
  if (proxyMode == "custom" &&
      (options.proxyHost.empty() ||
       options.proxyPort == 0U ||
       options.proxyPort > 65535U)) {
    if (error != nullptr) {
      *error =
          "Custom remote transport proxy requires a host and port.";
    }
    return false;
  }
  return true;
}

std::string BuildRemoteAdvertisementUrl(
    const std::string& remoteUrl,
    std::string* error) {
  const std::string result = NormalizeRemoteUrl(remoteUrl, error);
  return result.empty()
      ? ""
      : result + "/info/refs?service=git-upload-pack";
}

std::string BuildRemoteUploadPackUrl(
    const std::string& remoteUrl,
    std::string* error) {
  const std::string result = NormalizeRemoteUrl(remoteUrl, error);
  return result.empty() ? "" : result + "/git-upload-pack";
}

std::string BuildRemoteReceivePackAdvertisementUrl(
    const std::string& remoteUrl,
    std::string* error) {
  const std::string result = NormalizeRemoteUrl(remoteUrl, error);
  return result.empty()
      ? ""
      : result + "/info/refs?service=git-receive-pack";
}

std::string BuildRemoteReceivePackUrl(
    const std::string& remoteUrl,
    std::string* error) {
  const std::string result = NormalizeRemoteUrl(remoteUrl, error);
  return result.empty() ? "" : result + "/git-receive-pack";
}

std::string BuildUploadPackRequest(
    const std::vector<std::string>& wants,
    const std::vector<std::string>& haves,
    const std::vector<std::string>& availableCapabilities,
    std::string* error) {
  if (error != nullptr) {
    error->clear();
  }
  if (wants.empty()) {
    if (error != nullptr) {
      *error = "Upload-pack negotiation requires at least one wanted object.";
    }
    return "";
  }

  std::vector<std::string> uniqueWants;
  for (const std::string& objectId : wants) {
    if (!IsObjectId(objectId)) {
      if (error != nullptr) {
        *error = "Upload-pack negotiation contains an invalid wanted object.";
      }
      return "";
    }
    if (std::find(
            uniqueWants.begin(),
            uniqueWants.end(),
            objectId) == uniqueWants.end()) {
      uniqueWants.push_back(objectId);
    }
  }

  const auto hasCapability =
      [&availableCapabilities](
          const std::string& expected) -> bool {
        return std::any_of(
            availableCapabilities.begin(),
            availableCapabilities.end(),
            [&expected](const std::string& available) {
              return available == expected ||
                  available.rfind(expected + "=", 0) == 0;
            });
      };
  std::vector<std::string> requestedCapabilities;
  const std::vector<std::string> preferredCapabilities = {
      "multi_ack_detailed",
      "side-band-64k",
      "ofs-delta"};
  for (const std::string& capability : preferredCapabilities) {
    if (hasCapability(capability)) {
      requestedCapabilities.push_back(capability);
    }
  }
  if (hasCapability("agent")) {
    requestedCapabilities.push_back(
        "agent=Harmony-Git-Bash/0.1");
  }

  std::string request;
  for (size_t index = 0; index < uniqueWants.size(); ++index) {
    std::string line = "want " + uniqueWants[index];
    if (index == 0 && !requestedCapabilities.empty()) {
      for (const std::string& capability : requestedCapabilities) {
        line += " " + capability;
      }
    }
    line.push_back('\n');
    const std::string packet = EncodePacketLine(line);
    if (packet.empty()) {
      if (error != nullptr) {
        *error = "Upload-pack want packet is too large.";
      }
      return "";
    }
    request += packet;
  }
  request += "0000";

  std::vector<std::string> uniqueHaves;
  for (const std::string& objectId : haves) {
    if (!IsObjectId(objectId)) {
      if (error != nullptr) {
        *error = "Upload-pack negotiation contains an invalid local object.";
      }
      return "";
    }
    if (std::find(
            uniqueHaves.begin(),
            uniqueHaves.end(),
            objectId) == uniqueHaves.end()) {
      uniqueHaves.push_back(objectId);
      request += EncodePacketLine("have " + objectId + "\n");
    }
  }
  request += EncodePacketLine("done\n");
  return request;
}

std::string BuildReceivePackRequest(
    const std::vector<RemotePushUpdate>& updates,
    const std::string& packData,
    const std::vector<std::string>& availableCapabilities,
    std::string* error) {
  if (error != nullptr) {
    error->clear();
  }
  if (updates.empty()) {
    if (error != nullptr) {
      *error = "Receive-pack negotiation requires at least one ref update.";
    }
    return "";
  }

  const auto hasCapability =
      [&availableCapabilities](
          const std::string& expected) -> bool {
    return std::any_of(
        availableCapabilities.begin(),
        availableCapabilities.end(),
        [&expected](const std::string& available) {
          return available == expected ||
              available.rfind(expected + "=", 0) == 0;
        });
  };
  std::vector<std::string> requestedCapabilities;
  if (hasCapability("report-status")) {
    requestedCapabilities.push_back("report-status");
  } else if (hasCapability("report-status-v2")) {
    requestedCapabilities.push_back("report-status-v2");
  }
  for (const std::string& capability :
       {"side-band-64k", "ofs-delta"}) {
    if (hasCapability(capability)) {
      requestedCapabilities.push_back(capability);
    }
  }
  if (hasCapability("agent")) {
    requestedCapabilities.push_back(
        "agent=Harmony-Git-Bash/0.1");
  }

  std::string request;
  for (size_t index = 0; index < updates.size(); ++index) {
    const RemotePushUpdate& update = updates[index];
    const bool oldZero =
        update.oldObjectId == std::string(40, '0');
    const bool newZero =
        update.newObjectId == std::string(40, '0');
    if ((!oldZero && !IsObjectId(update.oldObjectId)) ||
        (!newZero && !IsObjectId(update.newObjectId)) ||
        update.name.empty() ||
        update.name.find_first_of(" \t\r\n\0") != std::string::npos) {
      if (error != nullptr) {
        *error = "Receive-pack contains an invalid ref update.";
      }
      return "";
    }
    std::string line =
        update.oldObjectId + " " +
        update.newObjectId + " " +
        update.name;
    if (index == 0 && !requestedCapabilities.empty()) {
      line.push_back('\0');
      for (size_t capabilityIndex = 0;
           capabilityIndex < requestedCapabilities.size();
           ++capabilityIndex) {
        if (capabilityIndex > 0) {
          line.push_back(' ');
        }
        line += requestedCapabilities[capabilityIndex];
      }
    }
    line.push_back('\n');
    const std::string packet = EncodePacketLine(line);
    if (packet.empty()) {
      if (error != nullptr) {
        *error = "Receive-pack ref update packet is too large.";
      }
      return "";
    }
    request += packet;
  }
  request += "0000";
  request += packData;
  return request;
}

RemoteAdvertisement ParseRemoteAdvertisement(
    const std::string& payload) {
  RemoteAdvertisement result;
  if (payload.empty()) {
    result.error = "Remote server returned an empty advertisement.";
    return result;
  }

  int initialPacketLength = 0;
  bool packetLines =
      payload.size() >= 4 &&
      IsHexCharacter(payload[0]) &&
      IsHexCharacter(payload[1]) &&
      IsHexCharacter(payload[2]) &&
      IsHexCharacter(payload[3]);
  if (packetLines) {
    for (size_t index = 0; index < 4; ++index) {
      initialPacketLength =
          initialPacketLength * 16 + HexValue(payload[index]);
    }
    packetLines =
        initialPacketLength >= 4 &&
        static_cast<size_t>(initialPacketLength) <= payload.size() &&
        payload.compare(4, 10, "# service=") == 0;
  }
  if (!packetLines) {
    std::istringstream input(payload);
    std::string line;
    while (std::getline(input, line)) {
      if (!ParseReferenceLine(line, &result)) {
        return result;
      }
    }
    result.success = true;
    return result;
  }

  size_t offset = 0;
  while (offset < payload.size()) {
    if (offset + 4 > payload.size()) {
      result.error =
          "Remote advertisement ended inside a packet header.";
      return result;
    }
    int packetLength = 0;
    for (size_t index = 0; index < 4; ++index) {
      if (!IsHexCharacter(payload[offset + index])) {
        result.error =
            "Remote advertisement contains an invalid packet length.";
        return result;
      }
      packetLength =
          packetLength * 16 + HexValue(payload[offset + index]);
    }
    offset += 4;
    if (packetLength == 0 || packetLength == 1 ||
        packetLength == 2) {
      continue;
    }
    if (packetLength < 4 ||
        offset + static_cast<size_t>(packetLength - 4) >
            payload.size()) {
      result.error =
          "Remote advertisement contains a truncated packet.";
      return result;
    }
    const size_t dataLength =
        static_cast<size_t>(packetLength - 4);
    if (!ParseReferenceLine(
            payload.substr(offset, dataLength),
            &result)) {
      return result;
    }
    offset += dataLength;
  }
  result.success = true;
  return result;
}

RemotePackResponse ParseUploadPackResponse(
    const std::string& payload) {
  RemotePackResponse result;
  if (payload.empty()) {
    result.error = "Remote upload-pack returned an empty response.";
    return result;
  }

  size_t offset = 0;
  while (offset < payload.size()) {
    if (payload.compare(offset, 4, "PACK") == 0) {
      result.packData.append(payload, offset, std::string::npos);
      offset = payload.size();
      break;
    }
    if (offset + 4U > payload.size()) {
      result.error =
          "Remote upload-pack response ended inside a packet header.";
      return result;
    }
    int packetLength = 0;
    for (size_t index = 0; index < 4U; ++index) {
      if (!IsHexCharacter(payload[offset + index])) {
        result.error =
            "Remote upload-pack response contains an invalid packet length.";
        return result;
      }
      packetLength =
          packetLength * 16 + HexValue(payload[offset + index]);
    }
    offset += 4U;
    if (packetLength == 0 || packetLength == 1 ||
        packetLength == 2) {
      continue;
    }
    if (packetLength < 4 ||
        offset + static_cast<size_t>(packetLength - 4) >
            payload.size()) {
      result.error =
          "Remote upload-pack response contains a truncated packet.";
      return result;
    }

    const size_t dataLength =
        static_cast<size_t>(packetLength - 4);
    const std::string packet =
        payload.substr(offset, dataLength);
    offset += dataLength;
    if (packet == "NAK\n" || packet == "NAK") {
      continue;
    }
    if (packet.rfind("ACK ", 0) == 0) {
      result.acknowledged = true;
      continue;
    }
    if (packet.rfind("shallow ", 0) == 0 ||
        packet.rfind("unshallow ", 0) == 0) {
      continue;
    }
    if (packet.rfind("ERR ", 0) == 0) {
      result.error =
          TrimLineEnding(packet.substr(4));
      return result;
    }
    if (packet.rfind("PACK", 0) == 0) {
      result.packData += packet;
      continue;
    }
    if (packet.empty()) {
      continue;
    }

    const uint8_t channel =
        static_cast<uint8_t>(packet[0]);
    if (channel == 1U) {
      result.packData.append(packet, 1U, std::string::npos);
    } else if (channel == 2U) {
      result.progress.append(packet, 1U, std::string::npos);
    } else if (channel == 3U) {
      result.error =
          TrimLineEnding(packet.substr(1));
      if (result.error.empty()) {
        result.error = "Remote upload-pack reported a fatal error.";
      }
      return result;
    } else {
      result.error =
          "Remote upload-pack response contains an unexpected packet.";
      return result;
    }
  }

  if (result.packData.empty() && result.acknowledged) {
    result.success = true;
    return result;
  }
  if (result.packData.size() < 32U ||
      result.packData.compare(0, 4, "PACK") != 0) {
    result.error =
        "Remote upload-pack response does not contain a complete Git pack.";
    return result;
  }
  uint32_t version = 0;
  if (!ReadBigEndian32(result.packData, 4, &version) ||
      (version != 2U && version != 3U)) {
    result.error =
        "Remote upload-pack response contains an unsupported pack version.";
    return result;
  }
  if (!ReadBigEndian32(
          result.packData,
          8,
          &result.objectCount)) {
    result.error =
        "Remote upload-pack response contains a truncated pack header.";
    return result;
  }
  result.success = true;
  return result;
}

RemotePushResult ParseReceivePackResponse(
    const std::string& payload,
    const std::vector<std::string>& expectedReferences) {
  RemotePushResult result;
  if (payload.empty()) {
    result.error = "Remote receive-pack returned an empty response.";
    return result;
  }

  std::set<std::string> acceptedReferences;
  size_t offset = 0;
  while (offset < payload.size()) {
    if (offset + 4U > payload.size()) {
      result.error =
          "Remote receive-pack response ended inside a packet header.";
      return result;
    }
    int packetLength = 0;
    for (size_t index = 0; index < 4U; ++index) {
      if (!IsHexCharacter(payload[offset + index])) {
        result.error =
            "Remote receive-pack response contains an invalid packet length.";
        return result;
      }
      packetLength =
          packetLength * 16 + HexValue(payload[offset + index]);
    }
    offset += 4U;
    if (packetLength == 0 || packetLength == 1 ||
        packetLength == 2) {
      continue;
    }
    if (packetLength < 4 ||
        offset + static_cast<size_t>(packetLength - 4) >
            payload.size()) {
      result.error =
          "Remote receive-pack response contains a truncated packet.";
      return result;
    }

    const size_t dataLength =
        static_cast<size_t>(packetLength - 4);
    std::string packet =
        payload.substr(offset, dataLength);
    offset += dataLength;
    if (!packet.empty() &&
        (static_cast<uint8_t>(packet[0]) == 1U ||
         static_cast<uint8_t>(packet[0]) == 2U ||
         static_cast<uint8_t>(packet[0]) == 3U)) {
      const uint8_t channel = static_cast<uint8_t>(packet[0]);
      packet.erase(0, 1);
      if (channel == 2U) {
        const std::string progress = TrimLineEnding(packet);
        if (!progress.empty()) {
          result.output.push_back("remote: " + progress);
        }
        continue;
      }
      if (channel == 3U) {
        result.error = TrimLineEnding(packet);
        if (result.error.empty()) {
          result.error = "Remote receive-pack reported a fatal error.";
        }
        return result;
      }
    }

    std::istringstream lines(packet);
    std::string line;
    while (std::getline(lines, line)) {
      line = TrimLineEnding(line);
      if (line.empty()) {
        continue;
      }
      if (line == "unpack ok") {
        result.unpacked = true;
        continue;
      }
      if (line.rfind("unpack ", 0) == 0) {
        result.error = line.substr(7);
        if (result.error.empty()) {
          result.error = "Remote receive-pack rejected the pack.";
        }
        continue;
      }
      if (line.rfind("ok ", 0) == 0) {
        const std::string reference = line.substr(3);
        acceptedReferences.insert(reference);
        result.output.push_back("ok " + reference);
        continue;
      }
      if (line.rfind("ng ", 0) == 0) {
        result.output.push_back(line);
        if (result.error.empty()) {
          result.error = line.substr(3);
        }
        continue;
      }
      if (line.rfind("option ", 0) == 0) {
        continue;
      }
      result.output.push_back(line);
    }
  }

  for (const std::string& reference : expectedReferences) {
    if (acceptedReferences.find(reference) ==
        acceptedReferences.end()) {
      if (result.error.empty()) {
        result.error =
            "Remote receive-pack did not accept " + reference + ".";
      }
      return result;
    }
  }
  result.success = result.unpacked && result.error.empty();
  if (!result.success && result.error.empty()) {
    result.error = "Remote receive-pack did not report a successful update.";
  }
  return result;
}

RemoteAdvertisement SelectRemoteReferences(
    const RemoteAdvertisement& advertisement,
    bool heads,
    bool tags,
    bool refsOnly,
    const std::vector<std::string>& patterns) {
  if (!advertisement.success) {
    return advertisement;
  }
  RemoteAdvertisement result = advertisement;
  result.references.clear();
  for (const RemoteReference& reference :
       advertisement.references) {
    const bool isHead =
        reference.name.rfind("refs/heads/", 0) == 0;
    const bool isTag =
        reference.name.rfind("refs/tags/", 0) == 0;
    if ((heads || tags) &&
        !((heads && isHead) || (tags && isTag))) {
      continue;
    }
    if (refsOnly &&
        (reference.name == "HEAD" ||
         (reference.name.size() >= 3 &&
          reference.name.compare(
              reference.name.size() - 3,
              3,
              "^{}") == 0))) {
      continue;
    }
    if (!MatchesPatterns(reference.name, patterns)) {
      continue;
    }
    result.references.push_back(reference);
  }
  return result;
}

RemoteAdvertisement ListRemoteReferences(
    const std::string& remoteUrl,
    bool heads,
    bool tags,
    bool refsOnly,
    const std::vector<std::string>& patterns,
    const std::string& authorization,
    const RemoteTransportOptions& options) {
  std::string error;
  const std::string requestUrl =
      BuildRemoteAdvertisementUrl(remoteUrl, &error);
  if (!error.empty()) {
    RemoteAdvertisement result;
    result.error = error;
    return result;
  }
#if defined(__OHOS__)
  return SelectRemoteReferences(
      FetchRemoteAdvertisement(
          requestUrl,
          authorization,
          false,
          options),
      heads,
      tags,
      refsOnly,
      patterns);
#else
  static_cast<void>(requestUrl);
  static_cast<void>(authorization);
  static_cast<void>(heads);
  static_cast<void>(tags);
  static_cast<void>(refsOnly);
  static_cast<void>(patterns);
  static_cast<void>(options);
  RemoteAdvertisement result;
  result.error =
      "HarmonyOS NetworkKit transport is available only on device.";
  return result;
#endif
}

RemoteAdvertisement ListRemoteReceivePackReferences(
    const std::string& remoteUrl,
    bool heads,
    bool tags,
    bool refsOnly,
    const std::vector<std::string>& patterns,
    const std::string& authorization,
    const RemoteTransportOptions& options) {
  std::string error;
  const std::string requestUrl =
      BuildRemoteReceivePackAdvertisementUrl(remoteUrl, &error);
  if (!error.empty()) {
    RemoteAdvertisement result;
    result.error = error;
    return result;
  }
#if defined(__OHOS__)
  return SelectRemoteReferences(
      FetchRemoteAdvertisement(
          requestUrl,
          authorization,
          true,
          options),
      heads,
      tags,
      refsOnly,
      patterns);
#else
  static_cast<void>(requestUrl);
  static_cast<void>(authorization);
  static_cast<void>(heads);
  static_cast<void>(tags);
  static_cast<void>(refsOnly);
  static_cast<void>(patterns);
  static_cast<void>(options);
  RemoteAdvertisement result;
  result.error =
      "HarmonyOS NetworkKit transport is available only on device.";
  return result;
#endif
}

}  // namespace harmony_git
