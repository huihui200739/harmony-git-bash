#ifndef HARMONY_GIT_TRANSPORT_H
#define HARMONY_GIT_TRANSPORT_H

#include <cstdint>
#include <string>
#include <vector>

namespace harmony_git {

struct RemoteReference {
  std::string objectId;
  std::string name;
};

struct RemoteAdvertisement {
  bool success = false;
  int32_t responseCode = 0;
  std::string headTarget;
  std::vector<RemoteReference> references;
  std::vector<std::string> capabilities;
  std::string error;
};

struct RemotePackResponse {
  bool success = false;
  bool acknowledged = false;
  uint32_t objectCount = 0;
  std::string packData;
  std::string progress;
  std::string error;
};

struct RemotePushUpdate {
  std::string oldObjectId;
  std::string newObjectId;
  std::string name;
};

struct RemotePushResult {
  bool success = false;
  bool unpacked = false;
  std::vector<std::string> output;
  std::string error;
};

struct RemoteTransportOptions {
  uint32_t connectTimeout = 30000;
  uint32_t readTimeout = 120000;
  std::string proxyMode = "system";
  std::string proxyHost;
  uint32_t proxyPort = 0;
  std::vector<std::string> proxyExclusions;
  std::string caPath;
  bool verifyCertificates = true;
};

bool ValidateRemoteTransportOptions(
    const RemoteTransportOptions& options,
    std::string* error);

std::string BuildRemoteAdvertisementUrl(
    const std::string& remoteUrl,
    std::string* error);
std::string BuildRemoteUploadPackUrl(
    const std::string& remoteUrl,
    std::string* error);
std::string BuildRemoteReceivePackAdvertisementUrl(
    const std::string& remoteUrl,
    std::string* error);
std::string BuildRemoteReceivePackUrl(
    const std::string& remoteUrl,
    std::string* error);
std::string BuildUploadPackRequest(
    const std::vector<std::string>& wants,
    const std::vector<std::string>& haves,
    const std::vector<std::string>& availableCapabilities,
    std::string* error);
std::string BuildReceivePackRequest(
    const std::vector<RemotePushUpdate>& updates,
    const std::string& packData,
    const std::vector<std::string>& availableCapabilities,
    std::string* error);
RemoteAdvertisement ParseRemoteAdvertisement(
    const std::string& payload);
RemotePackResponse ParseUploadPackResponse(
    const std::string& payload);
RemotePushResult ParseReceivePackResponse(
    const std::string& payload,
    const std::vector<std::string>& expectedReferences);
RemoteAdvertisement SelectRemoteReferences(
    const RemoteAdvertisement& advertisement,
    bool heads,
    bool tags,
    bool refsOnly,
    const std::vector<std::string>& patterns);
RemoteAdvertisement ListRemoteReferences(
    const std::string& remoteUrl,
    bool heads,
    bool tags,
    bool refsOnly,
    const std::vector<std::string>& patterns,
    const std::string& authorization = "",
    const RemoteTransportOptions& options = {});
RemoteAdvertisement ListRemoteReceivePackReferences(
    const std::string& remoteUrl,
    bool heads,
    bool tags,
    bool refsOnly,
    const std::vector<std::string>& patterns,
    const std::string& authorization = "",
    const RemoteTransportOptions& options = {});

}  // namespace harmony_git

#endif  // HARMONY_GIT_TRANSPORT_H
