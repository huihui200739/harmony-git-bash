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
  uint32_t index = 0;
  std::string selector;
  std::string subject;
  std::string author;
  std::string commitTimestamp;
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
  std::vector<std::string> output;
  std::string error;
};

struct ListFilesOptions {
  bool cached = false;
  bool modified = false;
  bool deleted = false;
  bool others = false;
  bool ignored = false;
  bool excludeStandard = false;
  bool stage = false;
  bool fullName = false;
  std::vector<std::string> paths;
};

struct ListTreeOptions {
  bool recursive = false;
  bool directoriesOnly = false;
  bool includeTrees = false;
  bool nameOnly = false;
  bool objectOnly = false;
  bool longFormat = false;
  bool fullName = false;
  bool fullTree = false;
  std::vector<std::string> paths;
};

struct ShowRefOptions {
  bool heads = false;
  bool tags = false;
  bool includeHead = false;
  bool dereference = false;
  bool verify = false;
  bool quiet = false;
  bool hashOnly = false;
  uint32_t abbreviation = 40;
  std::vector<std::string> patterns;
};

struct RevListOptions {
  bool all = false;
  bool branches = false;
  bool tags = false;
  bool remotes = false;
  bool parents = false;
  bool count = false;
  bool reverse = false;
  bool firstParent = false;
  bool noMerges = false;
  bool merges = false;
  bool abbreviate = false;
  uint32_t abbreviation = 7;
  uint32_t maxCount = UINT32_MAX;
  std::vector<std::string> revisions;
  std::vector<std::string> paths;
};

struct MergeBaseOptions {
  bool all = false;
  bool octopus = false;
  bool independent = false;
  std::vector<std::string> revisions;
};

struct ForEachRefOptions {
  uint32_t count = UINT32_MAX;
  std::string format;
  std::vector<std::string> sortKeys;
  std::vector<std::string> patterns;
  std::vector<std::string> excludes;
  std::string pointsAt;
  std::string merged;
  std::string noMerged;
  std::string contains;
  std::string noContains;
  bool ignoreCase = false;
  bool includeRootRefs = false;
};

struct CleanOptions {
  bool dryRun = false;
  bool directories = false;
  bool quiet = false;
  bool removeIgnored = false;
  bool ignoredOnly = false;
  uint32_t force = 0;
  std::vector<std::string> excludes;
  std::vector<std::string> paths;
};

struct CleanResult {
  bool success = false;
  uint32_t changedCount = 0;
  std::vector<std::string> cleanedPaths;
  std::vector<std::string> skippedRepositories;
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
    const std::vector<std::string>& paths,
    std::string* error);
std::vector<std::string> ReadTags(
    const std::string& startPath,
    const std::vector<std::string>& patterns,
    std::string* error);
std::vector<std::string> ReadFiles(
    const std::string& startPath,
    const ListFilesOptions& options,
    std::string* error);
std::vector<std::string> HashFiles(
    const std::string& startPath,
    const std::vector<std::string>& paths,
    const std::string& type,
    bool write,
    std::string* error);
std::string HashInput(
    const std::string& startPath,
    const std::string& payload,
    const std::string& type,
    bool write,
    std::string* error);
std::vector<std::string> CheckIgnored(
    const std::string& startPath,
    const std::vector<std::string>& paths,
    bool noIndex,
    bool verbose,
    std::string* error);
std::string ReadObjectContent(
    const std::string& startPath,
    const std::string& objectName,
    const std::string& mode,
    std::string* error);
std::vector<std::string> ReadTree(
    const std::string& startPath,
    const std::string& treeish,
    const ListTreeOptions& options,
    std::string* error);
std::vector<std::string> ReadReferences(
    const std::string& startPath,
    const ShowRefOptions& options,
    std::string* error);
std::vector<std::string> ExcludeExistingReferences(
    const std::string& startPath,
    const std::string& input,
    const std::string& pattern,
    std::string* error);
std::vector<std::string> ReadRevisionList(
    const std::string& startPath,
    const RevListOptions& options,
    std::string* error);
std::vector<std::string> ReadMergeBases(
    const std::string& startPath,
    const MergeBaseOptions& options,
    std::string* error);
bool IsAncestorRevision(
    const std::string& startPath,
    const std::string& ancestor,
    const std::string& descendant,
    std::string* error);
std::string FindForkPointRevision(
    const std::string& startPath,
    const std::string& reference,
    const std::string& derived,
    std::string* error);
std::vector<std::string> FormatReferences(
    const std::string& startPath,
    const ForEachRefOptions& options,
    std::string* error);
CleanResult CleanRepository(
    const std::string& startPath,
    const CleanOptions& options);
std::string ReadSymbolicReference(
    const std::string& startPath,
    const std::string& name,
    bool shortName,
    bool recurse,
    std::string* error);
RepositoryOperation UpdateSymbolicReference(
    const std::string& startPath,
    const std::string& name,
    const std::string& target,
    bool deleteReference,
    const std::string& message);
RepositoryOperation UpdateReference(
    const std::string& startPath,
    const std::string& name,
    const std::string& newValue,
    const std::string& oldValue,
    bool deleteReference,
    bool noDeref,
    const std::string& message,
    bool createReflog = false);
RepositoryOperation UpdateReferences(
    const std::string& startPath,
    const std::string& input,
    bool noDeref,
    bool createReflog,
    const std::string& message,
    bool nullTerminated = false,
    bool batchUpdates = false);
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
    const std::string& scope,
    bool includes,
    const std::string& explicitFile,
    std::string* error);
RepositoryOperation SetConfigValue(
    const std::string& startPath,
    const std::string& key,
    const std::string& value,
    const std::string& scope,
    bool append,
    const std::string& explicitFile);
RepositoryOperation UnsetConfigValue(
    const std::string& startPath,
    const std::string& key,
    const std::string& scope,
    bool all,
    const std::string& explicitFile);
bool SetCommandConfig(
    const std::vector<std::string>& assignments,
    std::string* error);
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
RepositoryOperation InstallRemotePack(
    const std::string& startPath,
    const std::string& remoteName,
    const std::string& packData,
    const std::vector<std::string>& referenceNames,
    const std::vector<std::string>& objectIds,
    const std::string& headTarget);
std::string BuildReceivePackPack(
    const std::string& startPath,
    const std::vector<std::string>& newObjectIds,
    const std::vector<std::string>& haves,
    std::string* error);
std::vector<ReflogEntry> ReadReflog(
    const std::string& startPath,
    const std::string& ref,
    uint32_t maxCount,
    std::string* error);
std::vector<ReflogEntry> ReadReflog(
    const std::string& startPath,
    const std::string& ref,
    uint32_t maxCount,
    uint32_t skip,
    const std::string& since,
    const std::string& until,
    std::string* error);
std::vector<std::string> ListReflogs(
    const std::string& startPath,
    std::string* error);
bool ReflogExists(
    const std::string& startPath,
    const std::string& ref,
    std::string* error);
RepositoryOperation WriteReflog(
    const std::string& startPath,
    const std::string& ref,
    const std::string& oldObjectId,
    const std::string& newObjectId,
    const std::string& message);
RepositoryOperation DeleteReflogEntries(
    const std::string& startPath,
    const std::vector<std::string>& selectors,
    bool rewrite,
    bool updateRef,
    bool dryRun,
    bool verbose);
RepositoryOperation ExpireReflogs(
    const std::string& startPath,
    const std::vector<std::string>& refs,
    const std::string& expire,
    const std::string& expireUnreachable,
    bool rewrite,
    bool updateRef,
    bool staleFix,
    bool dryRun,
    bool verbose,
    bool all,
    bool singleWorktree);
RepositoryOperation DropReflogs(
    const std::string& startPath,
    const std::vector<std::string>& refs,
    bool all,
    bool singleWorktree);
bool DirectoryExists(const std::string& path);
std::vector<std::string> ListDirectory(
    const std::string& path,
    std::string* error);
std::string ReadWorkspaceFile(
    const std::string& startPath,
    const std::string& filePath,
    std::string* error);
RepositoryOperation WriteWorkspaceFile(
    const std::string& startPath,
    const std::string& filePath,
    const std::string& content,
    bool append);

}  // namespace harmony_git

#endif
