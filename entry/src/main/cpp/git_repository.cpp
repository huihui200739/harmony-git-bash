#include "git_repository.h"

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

namespace harmony_git {
namespace {

namespace fs = std::filesystem;

struct IndexEntry {
  std::string path;
  uint32_t mtimeSeconds = 0;
  uint32_t mtimeNanoseconds = 0;
  uint32_t mode = 0;
  uint32_t size = 0;
  uint16_t stage = 0;
};

std::string Trim(const std::string& value) {
  size_t start = 0;
  while (start < value.size() &&
         std::isspace(static_cast<unsigned char>(value[start])) != 0) {
    ++start;
  }
  size_t end = value.size();
  while (end > start &&
         std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
    --end;
  }
  return value.substr(start, end - start);
}

std::string ReadTextFile(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return "";
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

bool WriteTextFile(
    const fs::path& path,
    const std::string& content,
    std::string* error) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    if (error != nullptr) {
      *error = "Cannot write " + path.string();
    }
    return false;
  }
  output << content;
  if (!output.good()) {
    if (error != nullptr) {
      *error = "Failed while writing " + path.string();
    }
    return false;
  }
  return true;
}

bool IsHexCharacter(char value) {
  return (value >= '0' && value <= '9') ||
         (value >= 'a' && value <= 'f') ||
         (value >= 'A' && value <= 'F');
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

std::string UrlDecode(const std::string& value) {
  std::string decoded;
  decoded.reserve(value.size());
  for (size_t index = 0; index < value.size(); ++index) {
    if (value[index] == '%' && index + 2 < value.size() &&
        IsHexCharacter(value[index + 1]) &&
        IsHexCharacter(value[index + 2])) {
      decoded.push_back(static_cast<char>(
          HexValue(value[index + 1]) * 16 + HexValue(value[index + 2])));
      index += 2;
    } else {
      decoded.push_back(value[index]);
    }
  }
  return decoded;
}

fs::path AbsolutePath(const std::string& input) {
  std::error_code error;
  fs::path path(NormalizeInputPath(input));
  fs::path absolute = fs::absolute(path, error);
  return error ? path.lexically_normal() : absolute.lexically_normal();
}

bool ResolveGitDirectory(
    const fs::path& repositoryPath,
    fs::path* gitDirectory) {
  const fs::path marker = repositoryPath / ".git";
  std::error_code error;
  if (fs::is_directory(marker, error)) {
    *gitDirectory = marker;
    return true;
  }
  error.clear();
  if (!fs::is_regular_file(marker, error)) {
    return false;
  }
  const std::string markerText = Trim(ReadTextFile(marker));
  const std::string prefix = "gitdir:";
  if (markerText.rfind(prefix, 0) != 0) {
    return false;
  }
  fs::path resolved = Trim(markerText.substr(prefix.size()));
  if (resolved.is_relative()) {
    resolved = repositoryPath / resolved;
  }
  *gitDirectory = resolved.lexically_normal();
  return true;
}

fs::path ResolveCommonGitDirectory(const fs::path& gitDirectory) {
  const std::string commonDirectoryValue =
      Trim(ReadTextFile(gitDirectory / "commondir"));
  if (commonDirectoryValue.empty()) {
    return gitDirectory;
  }
  fs::path commonDirectory(commonDirectoryValue);
  if (commonDirectory.is_relative()) {
    commonDirectory = gitDirectory / commonDirectory;
  }
  return commonDirectory.lexically_normal();
}

bool DiscoverRepository(
    const std::string& startPath,
    fs::path* repositoryPath,
    fs::path* gitDirectory) {
  fs::path current = AbsolutePath(startPath);
  std::error_code error;
  if (!fs::exists(current, error)) {
    return false;
  }
  if (!fs::is_directory(current, error)) {
    current = current.parent_path();
  }
  while (!current.empty()) {
    fs::path resolvedGitDirectory;
    if (ResolveGitDirectory(current, &resolvedGitDirectory)) {
      *repositoryPath = current;
      *gitDirectory = resolvedGitDirectory;
      return true;
    }
    const fs::path parent = current.parent_path();
    if (parent == current) {
      break;
    }
    current = parent;
  }
  return false;
}

uint16_t ReadBigEndian16(const std::vector<uint8_t>& data, size_t offset) {
  return static_cast<uint16_t>(
      (static_cast<uint16_t>(data[offset]) << 8) |
      static_cast<uint16_t>(data[offset + 1]));
}

uint32_t ReadBigEndian32(const std::vector<uint8_t>& data, size_t offset) {
  return (static_cast<uint32_t>(data[offset]) << 24) |
         (static_cast<uint32_t>(data[offset + 1]) << 16) |
         (static_cast<uint32_t>(data[offset + 2]) << 8) |
         static_cast<uint32_t>(data[offset + 3]);
}

bool ReadIndex(
    const fs::path& indexPath,
    std::vector<IndexEntry>* entries,
    uint32_t* version,
    std::string* error) {
  std::ifstream input(indexPath, std::ios::binary);
  if (!input) {
    return true;
  }
  std::vector<uint8_t> data(
      (std::istreambuf_iterator<char>(input)),
      std::istreambuf_iterator<char>());
  if (data.size() < 12 ||
      data[0] != 'D' || data[1] != 'I' ||
      data[2] != 'R' || data[3] != 'C') {
    *error = "Unsupported or corrupt Git index header.";
    return false;
  }
  *version = ReadBigEndian32(data, 4);
  if (*version < 2 || *version > 4) {
    *error = "Unsupported Git index version " + std::to_string(*version) + ".";
    return false;
  }
  if (*version == 4) {
    *error =
        "Git index version 4 path compression is not supported by this native reader yet.";
    return false;
  }

  const uint32_t count = ReadBigEndian32(data, 8);
  size_t offset = 12;
  for (uint32_t entryIndex = 0; entryIndex < count; ++entryIndex) {
    const size_t entryStart = offset;
    if (entryStart + 62 > data.size()) {
      *error = "Git index entry is truncated.";
      return false;
    }
    IndexEntry entry;
    entry.mtimeSeconds = ReadBigEndian32(data, entryStart + 8);
    entry.mtimeNanoseconds = ReadBigEndian32(data, entryStart + 12);
    entry.mode = ReadBigEndian32(data, entryStart + 24);
    entry.size = ReadBigEndian32(data, entryStart + 36);
    const uint16_t flags = ReadBigEndian16(data, entryStart + 60);
    entry.stage = static_cast<uint16_t>((flags >> 12) & 0x3);

    size_t pathStart = entryStart + 62;
    if ((flags & 0x4000) != 0 && *version >= 3) {
      pathStart += 2;
    }
    if (pathStart >= data.size()) {
      *error = "Git index path is truncated.";
      return false;
    }
    size_t pathEnd = pathStart;
    while (pathEnd < data.size() && data[pathEnd] != 0) {
      ++pathEnd;
    }
    if (pathEnd >= data.size()) {
      *error = "Git index path is missing its terminator.";
      return false;
    }
    entry.path.assign(
        reinterpret_cast<const char*>(data.data() + pathStart),
        pathEnd - pathStart);
    entries->push_back(entry);

    const size_t entryLength = pathEnd - entryStart + 1;
    offset = entryStart + ((entryLength + 7) / 8) * 8;
  }
  return true;
}

uint32_t StatMtimeNanoseconds(const struct stat& fileStat) {
#if defined(__APPLE__)
  return static_cast<uint32_t>(fileStat.st_mtimespec.tv_nsec);
#else
  return static_cast<uint32_t>(fileStat.st_mtim.tv_nsec);
#endif
}

bool IsWorkingTreeModified(
    const fs::path& repositoryPath,
    const IndexEntry& entry,
    std::string* state) {
  const fs::path filePath = repositoryPath / fs::path(entry.path);
  struct stat fileStat {};
  if (lstat(filePath.c_str(), &fileStat) != 0) {
    *state = "D";
    return true;
  }

  const uint32_t indexType = entry.mode & 0170000U;
  if (indexType == 0120000U && !S_ISLNK(fileStat.st_mode)) {
    *state = "T";
    return true;
  }
  if (indexType == 0100000U && !S_ISREG(fileStat.st_mode)) {
    *state = "T";
    return true;
  }
  if (indexType == 0160000U && !S_ISDIR(fileStat.st_mode)) {
    *state = "T";
    return true;
  }
  if (indexType == 0160000U) {
    return false;
  }

  if (entry.size != static_cast<uint32_t>(fileStat.st_size) ||
      entry.mtimeSeconds != static_cast<uint32_t>(fileStat.st_mtime) ||
      entry.mtimeNanoseconds != StatMtimeNanoseconds(fileStat)) {
    *state = "M";
    return true;
  }
  return false;
}

std::string RelativeGitPath(
    const fs::path& repositoryPath,
    const fs::path& filePath) {
  std::error_code error;
  fs::path relative = fs::relative(filePath, repositoryPath, error);
  return (error ? filePath.lexically_relative(repositoryPath) : relative)
      .generic_string();
}

void AppendUntrackedFiles(
    const fs::path& repositoryPath,
    const std::set<std::string>& trackedPaths,
    std::vector<FileStatus>* files) {
  std::error_code error;
  fs::recursive_directory_iterator iterator(
      repositoryPath,
      fs::directory_options::skip_permission_denied,
      error);
  const fs::recursive_directory_iterator end;
  while (!error && iterator != end) {
    const fs::directory_entry entry = *iterator;
    const std::string relative =
        RelativeGitPath(repositoryPath, entry.path());
    if (relative == ".git" || relative.rfind(".git/", 0) == 0) {
      if (entry.is_directory(error)) {
        iterator.disable_recursion_pending();
      }
      iterator.increment(error);
      continue;
    }
    error.clear();
    if (!entry.is_directory(error) &&
        trackedPaths.find(relative) == trackedPaths.end()) {
      files->push_back({relative, "?", "?", false, false});
    }
    error.clear();
    iterator.increment(error);
  }
}

std::string ResolveHeadObject(
    const fs::path& gitDirectory,
    const fs::path& commonGitDirectory,
    const std::string& headText) {
  const std::string prefix = "ref:";
  if (headText.rfind(prefix, 0) != 0) {
    return Trim(headText);
  }
  const std::string refName = Trim(headText.substr(prefix.size()));
  std::string looseRef = Trim(ReadTextFile(gitDirectory / refName));
  if (looseRef.empty() && commonGitDirectory != gitDirectory) {
    looseRef = Trim(ReadTextFile(commonGitDirectory / refName));
  }
  if (!looseRef.empty()) {
    return looseRef;
  }
  std::istringstream packedRefs(
      ReadTextFile(commonGitDirectory / "packed-refs"));
  std::string line;
  while (std::getline(packedRefs, line)) {
    if (line.empty() || line[0] == '#' || line[0] == '^') {
      continue;
    }
    const size_t separator = line.find(' ');
    if (separator != std::string::npos &&
        Trim(line.substr(separator + 1)) == refName) {
      return line.substr(0, separator);
    }
  }
  return "";
}

std::string BranchFromHead(const std::string& headText, bool* detached) {
  const std::string prefix = "ref: refs/heads/";
  if (headText.rfind(prefix, 0) == 0) {
    *detached = false;
    return Trim(headText.substr(prefix.size()));
  }
  *detached = true;
  const std::string objectId = Trim(headText);
  return objectId.size() > 12 ? objectId.substr(0, 12) : objectId;
}

std::vector<std::string> ReadBranches(const fs::path& gitDirectory) {
  std::set<std::string> branches;
  const fs::path headsDirectory = gitDirectory / "refs" / "heads";
  std::error_code error;
  if (fs::is_directory(headsDirectory, error)) {
    fs::recursive_directory_iterator iterator(
        headsDirectory,
        fs::directory_options::skip_permission_denied,
        error);
    const fs::recursive_directory_iterator end;
    while (!error && iterator != end) {
      if (iterator->is_regular_file(error)) {
        branches.insert(RelativeGitPath(headsDirectory, iterator->path()));
      }
      error.clear();
      iterator.increment(error);
    }
  }

  std::istringstream packedRefs(ReadTextFile(gitDirectory / "packed-refs"));
  std::string line;
  const std::string prefix = "refs/heads/";
  while (std::getline(packedRefs, line)) {
    if (line.empty() || line[0] == '#' || line[0] == '^') {
      continue;
    }
    const size_t separator = line.find(' ');
    if (separator == std::string::npos) {
      continue;
    }
    const std::string refName = Trim(line.substr(separator + 1));
    if (refName.rfind(prefix, 0) == 0) {
      branches.insert(refName.substr(prefix.size()));
    }
  }
  return std::vector<std::string>(branches.begin(), branches.end());
}

std::vector<Remote> ReadRemotes(const fs::path& gitDirectory) {
  std::map<std::string, Remote> remotes;
  std::string currentRemote;
  std::istringstream config(ReadTextFile(gitDirectory / "config"));
  std::string line;
  while (std::getline(config, line)) {
    const std::string trimmed = Trim(line);
    if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
      continue;
    }
    if (trimmed.front() == '[' && trimmed.back() == ']') {
      currentRemote.clear();
      const std::string prefix = "[remote \"";
      if (trimmed.rfind(prefix, 0) == 0 && trimmed.size() > prefix.size() + 2) {
        const size_t closingQuote = trimmed.find('"', prefix.size());
        if (closingQuote != std::string::npos) {
          currentRemote =
              trimmed.substr(prefix.size(), closingQuote - prefix.size());
          remotes[currentRemote].name = currentRemote;
        }
      }
      continue;
    }
    if (currentRemote.empty()) {
      continue;
    }
    const size_t separator = trimmed.find('=');
    if (separator == std::string::npos) {
      continue;
    }
    const std::string key = Trim(trimmed.substr(0, separator));
    const std::string value = Trim(trimmed.substr(separator + 1));
    if (key == "url") {
      remotes[currentRemote].fetchUrl = value;
      if (remotes[currentRemote].pushUrl.empty()) {
        remotes[currentRemote].pushUrl = value;
      }
    } else if (key == "pushurl") {
      remotes[currentRemote].pushUrl = value;
    }
  }

  std::vector<Remote> result;
  for (const auto& item : remotes) {
    result.push_back(item.second);
  }
  return result;
}

bool EnsureDirectory(const fs::path& path, std::string* error) {
  std::error_code createError;
  fs::create_directories(path, createError);
  if (createError) {
    *error = "Cannot create " + path.string() + ": " + createError.message();
    return false;
  }
  return true;
}

}  // namespace

std::string NormalizeInputPath(const std::string& input) {
  if (input.rfind("file://", 0) != 0) {
    return input;
  }
  std::string decoded = UrlDecode(input.substr(7));
  if (decoded.empty() || decoded[0] != '/') {
    decoded.insert(decoded.begin(), '/');
  }
  return decoded;
}

RepositorySnapshot InspectRepository(const std::string& startPath) {
  RepositorySnapshot snapshot;
  fs::path repositoryPath;
  fs::path gitDirectory;
  if (!DiscoverRepository(startPath, &repositoryPath, &gitDirectory)) {
    snapshot.error = "Not a Git repository: " + NormalizeInputPath(startPath);
    return snapshot;
  }

  snapshot.valid = true;
  snapshot.repositoryPath = repositoryPath.generic_string();
  snapshot.gitDirectory = gitDirectory.generic_string();
  const fs::path commonGitDirectory =
      ResolveCommonGitDirectory(gitDirectory);
  const std::string headText = Trim(ReadTextFile(gitDirectory / "HEAD"));
  if (headText.empty()) {
    snapshot.valid = false;
    snapshot.error = "Git repository has no readable HEAD.";
    return snapshot;
  }
  snapshot.branch = BranchFromHead(headText, &snapshot.detached);
  snapshot.head =
      ResolveHeadObject(gitDirectory, commonGitDirectory, headText);
  snapshot.branches = ReadBranches(commonGitDirectory);
  if (!snapshot.detached &&
      std::find(
          snapshot.branches.begin(),
          snapshot.branches.end(),
          snapshot.branch) == snapshot.branches.end()) {
    snapshot.branches.push_back(snapshot.branch);
    std::sort(snapshot.branches.begin(), snapshot.branches.end());
  }
  snapshot.remotes = ReadRemotes(commonGitDirectory);

  std::vector<IndexEntry> indexEntries;
  std::string indexError;
  if (!ReadIndex(
          gitDirectory / "index",
          &indexEntries,
          &snapshot.indexVersion,
          &indexError)) {
    snapshot.error = indexError;
    return snapshot;
  }

  std::set<std::string> trackedPaths;
  for (const IndexEntry& entry : indexEntries) {
    trackedPaths.insert(entry.path);
    FileStatus status;
    status.path = entry.path;
    status.tracked = true;
    status.indexState =
        entry.stage != 0 ? "U" : (snapshot.head.empty() ? "A" : " ");
    status.staged = status.indexState != " ";
    status.workTreeState = " ";
    IsWorkingTreeModified(repositoryPath, entry, &status.workTreeState);
    if (status.indexState != " " || status.workTreeState != " ") {
      snapshot.files.push_back(status);
    }
  }
  AppendUntrackedFiles(repositoryPath, trackedPaths, &snapshot.files);
  std::sort(
      snapshot.files.begin(),
      snapshot.files.end(),
      [](const FileStatus& left, const FileStatus& right) {
        return left.path < right.path;
      });
  return snapshot;
}

RepositorySnapshot InitializeRepository(
    const std::string& repositoryPathValue,
    bool seedDemoFiles) {
  fs::path discoveredRepository;
  fs::path discoveredGitDirectory;
  if (DiscoverRepository(
          repositoryPathValue,
          &discoveredRepository,
          &discoveredGitDirectory)) {
    return InspectRepository(discoveredRepository.generic_string());
  }

  const fs::path repositoryPath = AbsolutePath(repositoryPathValue);
  const fs::path gitDirectory = repositoryPath / ".git";
  std::string error;
  if (!EnsureDirectory(gitDirectory / "objects" / "info", &error) ||
      !EnsureDirectory(gitDirectory / "objects" / "pack", &error) ||
      !EnsureDirectory(gitDirectory / "refs" / "heads", &error) ||
      !EnsureDirectory(gitDirectory / "refs" / "tags", &error) ||
      !EnsureDirectory(gitDirectory / "hooks", &error) ||
      !EnsureDirectory(gitDirectory / "info", &error)) {
    RepositorySnapshot failed;
    failed.error = error;
    return failed;
  }
  if (!fs::exists(gitDirectory / "HEAD") &&
      !WriteTextFile(gitDirectory / "HEAD", "ref: refs/heads/main\n", &error)) {
    RepositorySnapshot failed;
    failed.error = error;
    return failed;
  }
  if (!fs::exists(gitDirectory / "config")) {
    const std::string config =
        "[core]\n"
        "\trepositoryformatversion = 0\n"
        "\tfilemode = true\n"
        "\tbare = false\n"
        "\tlogallrefupdates = true\n";
    if (!WriteTextFile(gitDirectory / "config", config, &error)) {
      RepositorySnapshot failed;
      failed.error = error;
      return failed;
    }
  }
  if (!fs::exists(gitDirectory / "description")) {
    if (!WriteTextFile(
            gitDirectory / "description",
            "Harmony Git Bash repository\n",
            &error)) {
      RepositorySnapshot failed;
      failed.error = error;
      return failed;
    }
  }
  if (!fs::exists(gitDirectory / "info" / "exclude")) {
    if (!WriteTextFile(
            gitDirectory / "info" / "exclude",
            "# git ls-files --others --exclude-from=.git/info/exclude\n",
            &error)) {
      RepositorySnapshot failed;
      failed.error = error;
      return failed;
    }
  }
  if (seedDemoFiles) {
    const fs::path readme = repositoryPath / "README.md";
    if (!fs::exists(readme)) {
      if (!WriteTextFile(
              readme,
              "# Harmony Git Bash workspace\n\n"
              "This repository is backed by the HarmonyOS native Git service.\n",
              &error)) {
        RepositorySnapshot failed;
        failed.error = error;
        return failed;
      }
    }
    const fs::path notesDirectory = repositoryPath / "docs";
    if (!EnsureDirectory(notesDirectory, &error)) {
      RepositorySnapshot failed;
      failed.error = error;
      return failed;
    }
    const fs::path notes = notesDirectory / "porting-notes.md";
    if (!fs::exists(notes)) {
      if (!WriteTextFile(
              notes,
              "# Porting notes\n\n"
              "- Native repository discovery is enabled.\n"
              "- HEAD, refs, remotes and working tree status are read from disk.\n",
              &error)) {
        RepositorySnapshot failed;
        failed.error = error;
        return failed;
      }
    }
  }
  return InspectRepository(repositoryPath.generic_string());
}

bool DirectoryExists(const std::string& path) {
  std::error_code error;
  return fs::is_directory(AbsolutePath(path), error);
}

std::vector<std::string> ListDirectory(
    const std::string& path,
    std::string* error) {
  std::vector<std::string> entries;
  std::error_code iteratorError;
  fs::directory_iterator iterator(
      AbsolutePath(path),
      fs::directory_options::skip_permission_denied,
      iteratorError);
  const fs::directory_iterator end;
  if (iteratorError) {
    if (error != nullptr) {
      *error = iteratorError.message();
    }
    return entries;
  }
  while (iterator != end) {
    std::string name = iterator->path().filename().string();
    std::error_code typeError;
    if (iterator->is_directory(typeError)) {
      name += "/";
    }
    entries.push_back(name);
    iterator.increment(iteratorError);
    if (iteratorError) {
      if (error != nullptr) {
        *error = iteratorError.message();
      }
      break;
    }
  }
  std::sort(entries.begin(), entries.end());
  return entries;
}

}  // namespace harmony_git
