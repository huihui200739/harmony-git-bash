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
  std::string error;
};

std::string BuildRemoteAdvertisementUrl(
    const std::string& remoteUrl,
    std::string* error);
RemoteAdvertisement ParseRemoteAdvertisement(
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
