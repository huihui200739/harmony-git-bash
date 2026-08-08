#ifndef HARMONY_GIT_BASH_GIT_REPOSITORY_H
#define HARMONY_GIT_BASH_GIT_REPOSITORY_H

#include <cstdint>
#include <string>
#include <vector>

namespace harmony_git {

struct FileStatus {
  std::string path;
  std::string indexState;
  std::string workTreeState;
  bool tracked = false;
  bool staged = false;
};

struct Remote {
  std::string name;
  std::string fetchUrl;
  std::string pushUrl;
};

struct RepositorySnapshot {
  bool valid = false;
  std::string repositoryPath;
  std::string gitDirectory;
  std::string branch;
  std::string head;
  bool detached = false;
  uint32_t indexVersion = 0;
  std::vector<FileStatus> files;
  std::vector<std::string> branches;
  std::vector<Remote> remotes;
  std::string error;
};

std::string NormalizeInputPath(const std::string& input);
RepositorySnapshot InspectRepository(const std::string& startPath);
RepositorySnapshot InitializeRepository(
    const std::string& repositoryPath,
    bool seedDemoFiles);
bool DirectoryExists(const std::string& path);
std::vector<std::string> ListDirectory(
    const std::string& path,
    std::string* error);

}  // namespace harmony_git

#endif
