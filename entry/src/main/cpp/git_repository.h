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

struct Commit {
  std::string id;
  std::string subject;
  std::string author;
  std::string timestamp;
};

struct ConfigEntry {
  std::string key;
  std::string value;
};

struct ReflogEntry {
  std::string oldId;
  std::string newId;
  std::string actor;
  std::string timestamp;
  std::string message;
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

struct RepositoryOperation {
  bool success = false;
  uint32_t changedCount = 0;
  RepositorySnapshot snapshot;
  std::string error;
};

std::string NormalizeInputPath(const std::string& input);
RepositorySnapshot InspectRepository(const std::string& startPath);
RepositorySnapshot InitializeRepository(
    const std::string& repositoryPath,
    bool seedDemoFiles);
RepositoryOperation StageRepository(
    const std::string& startPath,
    const std::vector<std::string>& paths);
RepositoryOperation RemoveRepositoryPaths(
    const std::string& startPath,
    const std::vector<std::string>& paths,
    bool cached,
    bool force,
    bool recursive);
RepositoryOperation MoveRepositoryPath(
    const std::string& startPath,
    const std::string& source,
    const std::string& destination,
    bool force);
RepositoryOperation RestoreStaged(
    const std::string& startPath,
    const std::vector<std::string>& paths);
RepositoryOperation RestoreWorkingTree(
    const std::string& startPath,
    const std::vector<std::string>& paths);
RepositoryOperation RestoreFromSource(
    const std::string& startPath,
    const std::string& source,
    const std::vector<std::string>& paths,
    bool staged,
    bool worktree);
RepositoryOperation ResetHard(const std::string& startPath);
RepositoryOperation CommitRepository(
    const std::string& startPath,
    const std::string& message);
RepositoryOperation CreateBranch(
    const std::string& startPath,
    const std::string& name,
    bool checkout);
RepositoryOperation MoveBranch(
    const std::string& startPath,
    const std::string& oldName,
    const std::string& newName,
    bool force);
RepositoryOperation CopyBranch(
    const std::string& startPath,
    const std::string& oldName,
    const std::string& newName,
    bool force);
RepositoryOperation SwitchBranch(
    const std::string& startPath,
    const std::string& name);
RepositoryOperation CheckoutBranch(
    const std::string& startPath,
    const std::string& name,
    const std::string& startPoint);
RepositoryOperation DeleteBranch(
    const std::string& startPath,
    const std::string& name,
    bool force);
std::string DiffRepository(
    const std::string& startPath,
    bool staged,
    std::string* error);
std::vector<Commit> ReadLog(
    const std::string& startPath,
    uint32_t maxCount,
    std::string* error);
std::string ShowRevision(
    const std::string& startPath,
    const std::string& revision,
    bool statOnly,
    bool oneLine,
    std::string* error);
std::vector<std::string> ReadTags(
    const std::string& startPath,
    std::string* error);
RepositoryOperation CreateTag(
    const std::string& startPath,
    const std::string& name,
    const std::string& target,
    bool force,
    bool annotated,
    const std::string& message);
RepositoryOperation DeleteTags(
    const std::string& startPath,
    const std::vector<std::string>& names);
std::vector<ConfigEntry> ReadConfig(
    const std::string& startPath,
    std::string* error);
RepositoryOperation SetConfigValue(
    const std::string& startPath,
    const std::string& key,
    const std::string& value);
RepositoryOperation UnsetConfigValue(
    const std::string& startPath,
    const std::string& key);
RepositoryOperation AddRemote(
    const std::string& startPath,
    const std::string& name,
    const std::string& url);
RepositoryOperation RemoveRemote(
    const std::string& startPath,
    const std::string& name);
RepositoryOperation RenameRemote(
    const std::string& startPath,
    const std::string& oldName,
    const std::string& newName);
std::string GetRemoteUrl(
    const std::string& startPath,
    const std::string& name,
    bool push,
    std::string* error);
RepositoryOperation SetRemoteUrl(
    const std::string& startPath,
    const std::string& name,
    const std::string& url,
    bool push);
std::vector<ReflogEntry> ReadReflog(
    const std::string& startPath,
    const std::string& ref,
    uint32_t maxCount,
    std::string* error);
bool DirectoryExists(const std::string& path);
std::vector<std::string> ListDirectory(
    const std::string& path,
    std::string* error);

}  // namespace harmony_git

#endif
