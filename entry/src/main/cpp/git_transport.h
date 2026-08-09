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

std::string BuildRemoteAdvertisementUrl(
    const std::string& remoteUrl,
    std::string* error);
std::string BuildRemoteUploadPackUrl(
    const std::string& remoteUrl,
    std::string* error);
std::string BuildUploadPackRequest(
    const std::vector<std::string>& wants,
    const std::vector<std::string>& haves,
    const std::vector<std::string>& availableCapabilities,
    std::string* error);
RemoteAdvertisement ParseRemoteAdvertisement(
    const std::string& payload);
RemotePackResponse ParseUploadPackResponse(
    const std::string& payload);
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
    const std::vector<std::string>& patterns);

}  // namespace harmony_git

#endif  // HARMONY_GIT_TRANSPORT_H
