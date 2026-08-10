#include "git_repository.h"
#include "git_transport.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

class TemporaryDirectory {
 public:
  TemporaryDirectory() {
    const fs::path base = fs::temp_directory_path();
    const auto timestamp =
        std::chrono::high_resolution_clock::now()
            .time_since_epoch()
            .count();
    path_ =
        base / ("harmony-git-repository-test-" +
                std::to_string(timestamp));
    std::error_code error;
    if (!fs::create_directories(path_, error) || error) {
      throw std::runtime_error("Cannot create native test directory.");
    }
  }

  ~TemporaryDirectory() {
    std::error_code error;
    fs::remove_all(path_, error);
  }

  const fs::path& path() const {
    return path_;
  }

 private:
  fs::path path_;
};

class ScopedEnvironmentVariable {
 public:
  ScopedEnvironmentVariable(
      const std::string& name,
      const std::string& value)
      : name_(name) {
    const char* existing = std::getenv(name.c_str());
    if (existing != nullptr) {
      hadValue_ = true;
      previousValue_ = existing;
    }
    if (setenv(name.c_str(), value.c_str(), 1) != 0) {
      throw std::runtime_error(
          "Cannot set test environment variable: " + name);
    }
  }

  ~ScopedEnvironmentVariable() {
    if (hadValue_) {
      setenv(name_.c_str(), previousValue_.c_str(), 1);
    } else {
      unsetenv(name_.c_str());
    }
  }

 private:
  std::string name_;
  std::string previousValue_;
  bool hadValue_ = false;
};

void Require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::string ShellQuote(const fs::path& path) {
  std::string quoted = "'";
  for (char character : path.string()) {
    if (character == '\'') {
      quoted += "'\\''";
    } else {
      quoted += character;
    }
  }
  quoted += "'";
  return quoted;
}

void Run(const std::string& command) {
  const int result = std::system(command.c_str());
  Require(result == 0, "Command failed: " + command);
}

std::string RunCapture(const std::string& command) {
  FILE* pipe = popen(command.c_str(), "r");
  Require(pipe != nullptr, "Cannot start command: " + command);
  std::string output;
  char buffer[4096] = {};
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    output += buffer;
  }
  const int result = pclose(pipe);
  Require(result == 0, "Command failed: " + command);
  return output;
}

std::string TrimLineEnding(std::string value) {
  while (!value.empty() &&
         (value.back() == '\n' || value.back() == '\r')) {
    value.pop_back();
  }
  return value;
}

void RunGit(const fs::path& repository, const std::string& arguments) {
  Run(
      "git -C " + ShellQuote(repository) + " " + arguments +
      " >/dev/null 2>&1");
}

void WriteFile(const fs::path& path, const std::string& content) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  Require(output.good(), "Cannot write fixture file: " + path.string());
  output << content;
  Require(output.good(), "Cannot finish fixture file: " + path.string());
}

std::string ReadFile(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  Require(input.good(), "Cannot read fixture file: " + path.string());
  return std::string(
      std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>());
}

const harmony_git::FileStatus* FindStatus(
    const harmony_git::RepositorySnapshot& snapshot,
    const std::string& path) {
  for (const harmony_git::FileStatus& status : snapshot.files) {
    if (status.path == path) {
      return &status;
    }
  }
  return nullptr;
}

bool Contains(
    const std::vector<std::string>& values,
    const std::string& expected) {
  for (const std::string& value : values) {
    if (value == expected) {
      return true;
    }
  }
  return false;
}

std::string JoinLines(
    const std::vector<std::string>& lines) {
  std::string output;
  for (const std::string& line : lines) {
    output += line;
    output.push_back('\n');
  }
  return output;
}

std::string ReflogFixtureLine(
    const std::string& oldObjectId,
    const std::string& newObjectId,
    int64_t timestamp,
    const std::string& message) {
  return oldObjectId + " " + newObjectId +
      " Harmony Expire <expire@example.invalid> " +
      std::to_string(timestamp) + " +0000\t" + message + "\n";
}

void AppendNullRecord(
    std::string* input,
    const std::string& value) {
  input->append(value);
  input->push_back('\0');
}

std::string PacketLine(const std::string& payload) {
  static const char digits[] = "0123456789abcdef";
  const size_t length = payload.size() + 4;
  Require(length <= 0xffff, "Packet-line fixture is too large.");
  std::string result(4, '0');
  result[0] = digits[(length >> 12) & 0x0f];
  result[1] = digits[(length >> 8) & 0x0f];
  result[2] = digits[(length >> 4) & 0x0f];
  result[3] = digits[length & 0x0f];
  return result + payload;
}

void AppendBigEndian32(
    std::string* output,
    uint32_t value) {
  output->push_back(static_cast<char>((value >> 24U) & 0xffU));
  output->push_back(static_cast<char>((value >> 16U) & 0xffU));
  output->push_back(static_cast<char>((value >> 8U) & 0xffU));
  output->push_back(static_cast<char>(value & 0xffU));
}

const harmony_git::ConfigEntry* FindConfig(
    const std::vector<harmony_git::ConfigEntry>& entries,
    const std::string& key) {
  for (const harmony_git::ConfigEntry& entry : entries) {
    if (entry.key == key) {
      return &entry;
    }
  }
  return nullptr;
}

const harmony_git::ConfigEntry* FindLastConfig(
    const std::vector<harmony_git::ConfigEntry>& entries,
    const std::string& key) {
  for (auto iterator = entries.rbegin();
       iterator != entries.rend();
       ++iterator) {
    if (iterator->key == key) {
      return &*iterator;
    }
  }
  return nullptr;
}

std::string ConfigValues(
    const std::vector<harmony_git::ConfigEntry>& entries,
    const std::string& key) {
  std::string output;
  for (const harmony_git::ConfigEntry& entry : entries) {
    if (entry.key == key) {
      output += entry.value;
      output.push_back('\n');
    }
  }
  return output;
}

const harmony_git::ReflogEntry* FindReflog(
    const std::vector<harmony_git::ReflogEntry>& entries,
    const std::string& message) {
  for (const harmony_git::ReflogEntry& entry : entries) {
    if (entry.message == message) {
      return &entry;
    }
  }
  return nullptr;
}

std::string FileUri(const fs::path& path) {
  std::string uri = "file://";
  for (char character : path.string()) {
    uri += character == ' ' ? "%20" : std::string(1, character);
  }
  return uri;
}

void TestRepositoryInspection(const fs::path& root) {
  const fs::path repository = root / "repository with spaces";
  Run(
      "git -c init.defaultBranch=main init " + ShellQuote(repository) +
      " >/dev/null 2>&1");
  RunGit(repository, "config user.name 'Harmony Test'");
  RunGit(repository, "config user.email 'harmony@example.invalid'");

  WriteFile(repository / "README.md", "baseline\n");
  WriteFile(repository / "docs/deleted.txt", "delete me\n");
  WriteFile(repository / "src/clean.txt", "clean\n");
  RunGit(repository, "add .");
  RunGit(repository, "commit -m baseline");
  RunGit(
      repository,
      "remote add origin https://example.invalid/harmony.git");
  RunGit(
      repository,
      "remote set-url --push origin ssh://example.invalid/harmony.git");
  RunGit(repository, "branch feature/native-reader");

  WriteFile(repository / "README.md", "modified content\n");
  fs::remove(repository / "docs/deleted.txt");
  WriteFile(repository / "notes.txt", "untracked\n");

  const harmony_git::RepositorySnapshot snapshot =
      harmony_git::InspectRepository(
          FileUri(repository / "src/clean.txt"));
  Require(snapshot.valid, snapshot.error);
  Require(snapshot.repositoryPath == repository.generic_string(),
          "Repository discovery did not return the work tree root.");
  Require(snapshot.branch == "main", "Current branch was not read.");
  Require(!snapshot.detached, "Main branch was reported as detached.");
  Require(snapshot.head.size() == 40, "HEAD object id was not resolved.");
  Require(Contains(snapshot.branches, "main"), "Main branch is missing.");
  Require(
      Contains(snapshot.branches, "feature/native-reader"),
      "Nested branch name is missing.");
  Require(snapshot.remotes.size() == 1, "Remote count is incorrect.");
  Require(
      snapshot.remotes[0].fetchUrl ==
          "https://example.invalid/harmony.git",
      "Fetch URL is incorrect.");
  Require(
      snapshot.remotes[0].pushUrl ==
          "ssh://example.invalid/harmony.git",
      "Push URL is incorrect.");

  const harmony_git::FileStatus* modified =
      FindStatus(snapshot, "README.md");
  Require(modified != nullptr, "Modified file was not reported.");
  Require(
      modified->tracked && modified->workTreeState == "M",
      "Modified file state is incorrect.");

  const harmony_git::FileStatus* deleted =
      FindStatus(snapshot, "docs/deleted.txt");
  Require(deleted != nullptr, "Deleted file was not reported.");
  Require(
      deleted->tracked && deleted->workTreeState == "D",
      "Deleted file state is incorrect.");

  const harmony_git::FileStatus* untracked =
      FindStatus(snapshot, "notes.txt");
  Require(untracked != nullptr, "Untracked file was not reported.");
  Require(!untracked->tracked, "Untracked file was marked as tracked.");
  Require(
      FindStatus(snapshot, "src/clean.txt") == nullptr,
      "Clean file should not appear in status output.");

  std::string listError;
  const std::vector<std::string> entries =
      harmony_git::ListDirectory(repository.string(), &listError);
  Require(listError.empty(), listError);
  Require(Contains(entries, ".git/"), "Git directory is missing from ls.");
  Require(Contains(entries, "src/"), "Source directory is missing from ls.");
}

void TestWorkspaceFileIO(const fs::path& root) {
  const fs::path repository = root / "repository with spaces";
  const fs::path sourceDirectory = repository / "src";
  harmony_git::RepositoryOperation written =
      harmony_git::WriteWorkspaceFile(
          sourceDirectory.string(),
          "../shell-output.txt",
          "first\n",
          false);
  Require(written.success, written.error);
  Require(
      written.changedCount == 1,
      "Workspace file write did not report one changed path.");

  std::string readError;
  std::string content =
      harmony_git::ReadWorkspaceFile(
          sourceDirectory.string(),
          "../shell-output.txt",
          &readError);
  Require(readError.empty(), readError);
  Require(content == "first\n", "Workspace file content is incorrect.");

  written = harmony_git::WriteWorkspaceFile(
      sourceDirectory.string(),
      FileUri(repository / "shell-output.txt"),
      "second\n",
      true);
  Require(written.success, written.error);
  content = harmony_git::ReadWorkspaceFile(
      repository.string(),
      "shell-output.txt",
      &readError);
  Require(readError.empty(), readError);
  Require(
      content == "first\nsecond\n",
      "Workspace file append did not preserve existing content.");

  written = harmony_git::WriteWorkspaceFile(
      repository.string(),
      "missing/directory/output.txt",
      "blocked\n",
      false);
  Require(
      !written.success && !written.error.empty(),
      "Workspace redirection unexpectedly created missing directories.");

  content = harmony_git::ReadWorkspaceFile(
      repository.string(),
      "missing.txt",
      &readError);
  Require(
      content.empty() && !readError.empty(),
      "Missing workspace file did not report an error.");
}

void TestRemoteAdvertisement() {
  std::string urlError;
  const std::string requestUrl =
      harmony_git::BuildRemoteAdvertisementUrl(
          "https://example.invalid/repository.git/",
          &urlError);
  Require(urlError.empty(), urlError);
  Require(
      requestUrl ==
          "https://example.invalid/repository.git/info/refs"
          "?service=git-upload-pack",
      "Remote advertisement URL is incorrect.");
  Require(
      harmony_git::BuildRemoteAdvertisementUrl(
          "ssh://example.invalid/repository.git",
          &urlError).empty() &&
          !urlError.empty(),
      "Unsupported remote scheme was accepted.");

  const std::string mainId =
      "1234567890abcdef1234567890abcdef12345678";
  const std::string tagId =
      "abcdef1234567890abcdef1234567890abcdef12";
  std::string firstReference = mainId + " HEAD";
  firstReference.push_back('\0');
  firstReference +=
      "multi_ack symref=HEAD:refs/heads/main agent=test\n";
  const std::string advertisementPayload =
      PacketLine("# service=git-upload-pack\n") +
      "0000" +
      PacketLine(firstReference) +
      PacketLine(mainId + " refs/heads/main\n") +
      PacketLine(tagId + " refs/tags/v1.0\n") +
      PacketLine(mainId + " refs/tags/v1.0^{}\n") +
      "0000";
  const harmony_git::RemoteAdvertisement advertisement =
      harmony_git::ParseRemoteAdvertisement(
          advertisementPayload);
  Require(advertisement.success, advertisement.error);
  Require(
      advertisement.headTarget == "refs/heads/main",
      "Remote HEAD symref was not parsed.");
  Require(
      Contains(advertisement.capabilities, "multi_ack"),
      "Remote capabilities were not retained.");
  Require(
      advertisement.references.size() == 4,
      "Remote reference count is incorrect.");

  const harmony_git::RemoteAdvertisement heads =
      harmony_git::SelectRemoteReferences(
          advertisement,
          true,
          false,
          false,
          {});
  Require(
      heads.references.size() == 1 &&
          heads.references[0].name == "refs/heads/main",
      "Remote head filtering is incorrect.");
  const harmony_git::RemoteAdvertisement tags =
      harmony_git::SelectRemoteReferences(
          advertisement,
          false,
          true,
          true,
          {"v1.*"});
  Require(
      tags.references.size() == 1 &&
          tags.references[0].name == "refs/tags/v1.0",
      "Remote tag and pattern filtering is incorrect.");

  const harmony_git::RemoteAdvertisement dumbAdvertisement =
      harmony_git::ParseRemoteAdvertisement(
          mainId + "\trefs/heads/main\n" +
          tagId + "\trefs/tags/v1.0\n");
  Require(
      dumbAdvertisement.success &&
          dumbAdvertisement.references.size() == 2,
      "Dumb HTTP reference advertisement was not parsed.");

  const harmony_git::RemoteAdvertisement malformed =
      harmony_git::ParseRemoteAdvertisement("0008bad");
  Require(
      !malformed.success && !malformed.error.empty(),
      "Malformed remote packet was accepted.");
}

void TestUploadPackProtocol() {
  std::string urlError;
  const std::string uploadUrl =
      harmony_git::BuildRemoteUploadPackUrl(
          "https://example.invalid/repository.git/",
          &urlError);
  Require(urlError.empty(), urlError);
  Require(
      uploadUrl ==
          "https://example.invalid/repository.git/git-upload-pack",
      "Remote upload-pack URL is incorrect.");

  const std::string firstId =
      "1234567890abcdef1234567890abcdef12345678";
  const std::string secondId =
      "abcdef1234567890abcdef1234567890abcdef12";
  const std::string localId =
      "1111111111111111111111111111111111111111";
  std::string requestError;
  const std::string request =
      harmony_git::BuildUploadPackRequest(
          {firstId, secondId, firstId},
          {localId, localId},
          {
              "multi_ack_detailed",
              "side-band-64k",
              "thin-pack",
              "ofs-delta",
              "agent=git/2.52.0"
          },
          &requestError);
  Require(requestError.empty(), requestError);
  const std::string expectedFirstWant =
      PacketLine(
          "want " + firstId +
          " multi_ack_detailed side-band-64k ofs-delta"
          " agent=Harmony-Git-Bash/0.1\n");
  Require(
      request.rfind(expectedFirstWant, 0) == 0,
      "Upload-pack first want and capabilities are incorrect.");
  Require(
      request.find(PacketLine("want " + secondId + "\n")) !=
          std::string::npos,
      "Upload-pack second want is missing.");
  Require(
      request.find(PacketLine("have " + localId + "\n")) !=
          std::string::npos &&
          request.find(PacketLine("done\n")) != std::string::npos,
      "Upload-pack have/done negotiation is incomplete.");
  Require(
      harmony_git::BuildUploadPackRequest(
          {"invalid"},
          {},
          {},
          &requestError).empty() &&
          !requestError.empty(),
      "Upload-pack accepted an invalid wanted object.");

  std::string pack = "PACK";
  AppendBigEndian32(&pack, 2U);
  AppendBigEndian32(&pack, 2U);
  pack.append(20U, '\0');
  std::string packChannel(1, '\x01');
  packChannel += pack;
  std::string progressChannel(1, '\x02');
  progressChannel += "Counting objects: 2\n";
  const std::string responsePayload =
      PacketLine("NAK\n") +
      PacketLine(progressChannel) +
      PacketLine(packChannel) +
      "0000";
  const harmony_git::RemotePackResponse response =
      harmony_git::ParseUploadPackResponse(
          responsePayload);
  Require(response.success, response.error);
  Require(
      response.objectCount == 2U && response.packData == pack,
      "Upload-pack side-band pack payload is incorrect.");
  Require(
      response.progress == "Counting objects: 2\n",
      "Upload-pack progress channel was not decoded.");

  const harmony_git::RemotePackResponse rawResponse =
      harmony_git::ParseUploadPackResponse(
          PacketLine("ACK " + firstId + " ready\n") +
          "0000" +
          pack);
  Require(rawResponse.success, rawResponse.error);
  Require(
      rawResponse.acknowledged && rawResponse.packData == pack,
      "Upload-pack raw pack response was not decoded.");
  const harmony_git::RemotePackResponse upToDate =
      harmony_git::ParseUploadPackResponse(
          PacketLine("ACK " + firstId + "\n") + "0000");
  Require(
      upToDate.success && upToDate.packData.empty(),
      "Upload-pack ACK-only response was not accepted.");

  std::string errorChannel(1, '\x03');
  errorChannel += "repository access denied\n";
  const harmony_git::RemotePackResponse remoteError =
      harmony_git::ParseUploadPackResponse(
          PacketLine(errorChannel));
  Require(
      !remoteError.success &&
          remoteError.error == "repository access denied",
      "Upload-pack fatal side-band channel was not reported.");

  const harmony_git::RemotePackResponse malformed =
      harmony_git::ParseUploadPackResponse("0008bad");
  Require(
      !malformed.success && !malformed.error.empty(),
      "Malformed upload-pack response was accepted.");
}

void TestReceivePackProtocol() {
  const std::string oldId =
      "1234567890abcdef1234567890abcdef12345678";
  const std::string newId =
      "abcdef1234567890abcdef1234567890abcdef12";
  std::string urlError;
  Require(
      harmony_git::BuildRemoteReceivePackAdvertisementUrl(
          "https://example.invalid/repository.git/",
          &urlError) ==
          "https://example.invalid/repository.git/info/refs"
          "?service=git-receive-pack" &&
          urlError.empty(),
      "Remote receive-pack advertisement URL is incorrect.");
  Require(
      harmony_git::BuildRemoteReceivePackUrl(
          "https://example.invalid/repository.git/",
          &urlError) ==
          "https://example.invalid/repository.git/git-receive-pack" &&
          urlError.empty(),
      "Remote receive-pack URL is incorrect.");

  harmony_git::RemotePushUpdate update {
    oldId,
    newId,
    "refs/heads/main"
  };
  std::string requestError;
  const std::string request =
      harmony_git::BuildReceivePackRequest(
          {update},
          "PACKDATA",
          {"report-status", "side-band-64k", "ofs-delta", "agent"},
          &requestError);
  Require(requestError.empty(), requestError);
  std::string firstLine =
      oldId + " " + newId + " refs/heads/main";
  firstLine.push_back('\0');
  firstLine +=
      "report-status side-band-64k ofs-delta "
      "agent=Harmony-Git-Bash/0.1\n";
  Require(
      request.rfind(PacketLine(firstLine), 0) == 0,
      "Receive-pack update packet is malformed.");
  Require(
      request.find("0000PACKDATA") != std::string::npos,
      "Receive-pack flush and pack data are missing.");
  Require(
      harmony_git::BuildReceivePackRequest(
          {},
          "",
          {},
          &requestError).empty() &&
          !requestError.empty(),
      "Receive-pack accepted an empty update list.");

  const std::string successResponse =
      PacketLine("unpack ok\n") +
      PacketLine("ok refs/heads/main\n") +
      "0000";
  const harmony_git::RemotePushResult success =
      harmony_git::ParseReceivePackResponse(
          successResponse,
          {"refs/heads/main"});
  Require(success.success && success.unpacked,
          "Successful receive-pack response was rejected.");
  Require(
      Contains(success.output, "ok refs/heads/main"),
      "Receive-pack accepted reference was not reported.");

  std::string progressChannel(1, '\x02');
  progressChannel += "Writing objects: 100%\n";
  const harmony_git::RemotePushResult withProgress =
      harmony_git::ParseReceivePackResponse(
          PacketLine(progressChannel) +
          PacketLine("unpack ok\n") +
          PacketLine("ok refs/heads/main\n"),
          {"refs/heads/main"});
  Require(
      withProgress.success &&
          Contains(withProgress.output, "remote: Writing objects: 100%"),
      "Receive-pack side-band progress was not decoded.");

  std::string errorChannel(1, '\x03');
  errorChannel += "permission denied\n";
  const harmony_git::RemotePushResult remoteError =
      harmony_git::ParseReceivePackResponse(
          PacketLine(errorChannel),
          {"refs/heads/main"});
  Require(
      !remoteError.success &&
          remoteError.error == "permission denied",
      "Receive-pack side-band error was not reported.");

  const harmony_git::RemotePushResult rejected =
      harmony_git::ParseReceivePackResponse(
          PacketLine("unpack ok\n") +
          PacketLine("ng refs/heads/main non-fast-forward\n"),
          {"refs/heads/main"});
  Require(
      !rejected.success &&
          rejected.error == "refs/heads/main non-fast-forward",
      "Receive-pack rejected update was accepted.");
}

void TestReceivePackPack(const fs::path& root) {
  const fs::path repository = root / "receive-pack source";
  const fs::path packPath = root / "receive-pack.pack";
  Run(
      "git -c init.defaultBranch=main init " + ShellQuote(repository) +
      " >/dev/null 2>&1");
  RunGit(repository, "config user.name 'Harmony Push Test'");
  RunGit(repository, "config user.email 'push@example.invalid'");
  WriteFile(repository / "README.md", "base\n");
  RunGit(repository, "add .");
  RunGit(repository, "commit -m base");
  const std::string baseId = TrimLineEnding(
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " rev-parse refs/heads/main"));
  WriteFile(repository / "README.md", "next\n");
  WriteFile(repository / "src/new.txt", "new object\n");
  RunGit(repository, "add .");
  RunGit(repository, "commit -m next");
  const std::string nextId = TrimLineEnding(
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " rev-parse refs/heads/main"));

  std::string packError;
  const std::string pack =
      harmony_git::BuildReceivePackPack(
          repository.string(),
          {nextId},
          {baseId},
          &packError);
  Require(packError.empty(), packError);
  Require(
      pack.size() > 32U &&
          pack.compare(0, 4, "PACK") == 0,
      "Receive-pack object pack was not generated.");
  WriteFile(packPath, pack);
  Run(
      "git index-pack " + ShellQuote(packPath) + " >/dev/null");

  const std::string emptyPack =
      harmony_git::BuildReceivePackPack(
          repository.string(),
          {},
          {baseId},
          &packError);
  Require(
      emptyPack.empty() && packError.empty(),
      "Receive-pack deletion unexpectedly generated objects.");
}

void TestRemotePackInstallation(const fs::path& root) {
  const fs::path source = root / "remote pack source";
  const fs::path target = root / "remote pack target";
  const fs::path packPath = root / "downloaded.pack";
  Run(
      "git -c init.defaultBranch=main init " + ShellQuote(source) +
      " >/dev/null 2>&1");
  RunGit(source, "config user.name 'Harmony Fetch Test'");
  RunGit(source, "config user.email 'fetch@example.invalid'");
  WriteFile(source / "README.md", "base\n");
  RunGit(source, "add .");
  RunGit(source, "commit -m base");
  RunGit(source, "checkout -b side");
  WriteFile(source / "side.txt", "side branch\n");
  RunGit(source, "add .");
  RunGit(source, "commit -m side");
  RunGit(source, "checkout main");
  WriteFile(source / "README.md", "main branch\n");
  RunGit(source, "commit -am main");

  const std::string mainId = TrimLineEnding(
      RunCapture(
          "git -C " + ShellQuote(source) +
          " rev-parse refs/heads/main"));
  const std::string sideId = TrimLineEnding(
      RunCapture(
          "git -C " + ShellQuote(source) +
          " rev-parse refs/heads/side"));
  Run(
      "printf '%s\\n%s\\n' " + mainId + " " + sideId +
      " | git -C " + ShellQuote(source) +
      " pack-objects --stdout --revs > " +
      ShellQuote(packPath));

  Run(
      "git -c init.defaultBranch=main init " + ShellQuote(target) +
      " >/dev/null 2>&1");
  const std::string packData = ReadFile(packPath);
  const harmony_git::RepositoryOperation installed =
      harmony_git::InstallRemotePack(
          target.string(),
          "origin",
          packData,
          {"refs/heads/main", "refs/heads/side"},
          {mainId, sideId},
          "refs/heads/main");
  Require(installed.success, installed.error);
  Require(
      installed.output.size() == 4U,
      "Remote pack installation output is incomplete.");
  Require(
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(target) +
              " rev-parse refs/remotes/origin/main")) == mainId &&
          TrimLineEnding(
              RunCapture(
                  "git -C " + ShellQuote(target) +
                  " rev-parse refs/remotes/origin/side")) == sideId,
      "Remote tracking references were not installed.");
  Require(
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(target) +
              " symbolic-ref refs/remotes/origin/HEAD")) ==
          "refs/remotes/origin/main",
      "Remote symbolic HEAD was not installed.");
  Run(
      "git -C " + ShellQuote(target) +
      " fsck --full >/dev/null 2>&1");

  std::string readError;
  Require(
      harmony_git::ReadObjectContent(
          target.string(),
          mainId,
          "type",
          &readError) == "commit" &&
          readError.empty(),
      "Installed pack objects are not readable.");
  Require(
      ReadFile(target / ".git" / "FETCH_HEAD").find(mainId) !=
          std::string::npos,
      "FETCH_HEAD was not written.");
  const harmony_git::RepositoryOperation upToDate =
      harmony_git::InstallRemotePack(
          target.string(),
          "mirror",
          "",
          {"refs/heads/main"},
          {mainId},
          "refs/heads/main");
  Require(upToDate.success, upToDate.error);
  Require(
      upToDate.output.back() ==
          "Remote objects are already available locally.",
      "Up-to-date fetch did not reuse local objects.");

  std::string corruptPack = packData;
  corruptPack[20] =
      static_cast<char>(
          static_cast<unsigned char>(corruptPack[20]) ^ 0x01U);
  const harmony_git::RepositoryOperation corrupt =
      harmony_git::InstallRemotePack(
          target.string(),
          "broken",
          corruptPack,
          {"refs/heads/main"},
          {mainId},
          "refs/heads/main");
  Require(
      !corrupt.success &&
          corrupt.error.find("checksum") != std::string::npos,
      "Corrupt downloaded pack was accepted.");
}

void TestLinkedWorktree(const fs::path& root) {
  const fs::path repository = root / "repository with spaces";
  const fs::path worktree = root / "linked worktree";
  RunGit(
      repository,
      "worktree add -b worktree/native-reader " +
          ShellQuote(worktree));
  WriteFile(worktree / "src/clean.txt", "worktree modification\n");

  const harmony_git::RepositorySnapshot snapshot =
      harmony_git::InspectRepository(worktree.string());
  Require(snapshot.valid, snapshot.error);
  Require(
      snapshot.repositoryPath == worktree.generic_string(),
      "Linked worktree root is incorrect.");
  Require(
      snapshot.gitDirectory != (worktree / ".git").generic_string(),
      "Linked worktree .git file was not resolved.");
  Require(
      snapshot.branch == "worktree/native-reader",
      "Linked worktree branch is incorrect.");
  Require(
      snapshot.head.size() == 40,
      "Linked worktree HEAD was not resolved through commondir.");
  Require(
      Contains(snapshot.branches, "main") &&
          Contains(snapshot.branches, "feature/native-reader") &&
          Contains(snapshot.branches, "worktree/native-reader"),
      "Linked worktree did not read common branches.");
  Require(
      snapshot.remotes.size() == 1 &&
          snapshot.remotes[0].name == "origin",
      "Linked worktree did not read common remote configuration.");

  const harmony_git::FileStatus* modified =
      FindStatus(snapshot, "src/clean.txt");
  Require(
      modified != nullptr && modified->workTreeState == "M",
      "Linked worktree modification was not reported.");

  const harmony_git::RepositorySnapshot reinitialized =
      harmony_git::InitializeRepository(
          (worktree / "src").string(),
          false);
  Require(reinitialized.valid, reinitialized.error);
  Require(
      reinitialized.repositoryPath == worktree.generic_string(),
      "Reinitializing inside a worktree created a nested repository.");
}

void TestRepositoryInitialization(const fs::path& root) {
  const fs::path repository = root / "initialized repository";
  const harmony_git::RepositorySnapshot snapshot =
      harmony_git::InitializeRepository(repository.string(), true);
  Require(snapshot.valid, snapshot.error);
  Require(snapshot.branch == "main", "Initialized branch should be main.");
  Require(snapshot.head.empty(), "New repository should have no HEAD object.");
  Require(
      FindStatus(snapshot, "README.md") != nullptr &&
          !FindStatus(snapshot, "README.md")->tracked,
      "Seed README should be untracked.");
  Require(
      FindStatus(snapshot, "docs/porting-notes.md") != nullptr,
      "Seed porting notes are missing.");

  const harmony_git::RepositorySnapshot invalid =
      harmony_git::InspectRepository((root / "missing").string());
  Require(!invalid.valid, "Missing repository should be invalid.");
}

void TestRepositoryOperations(const fs::path& root) {
  const fs::path repository = root / "native operation repository";
  Run(
      "git -c init.defaultBranch=main init " + ShellQuote(repository) +
      " >/dev/null 2>&1");
  RunGit(repository, "config user.name 'Harmony Native Test'");
  RunGit(repository, "config user.email 'native@example.invalid'");
  WriteFile(repository / "README.md", "baseline\n");
  WriteFile(repository / "docs/keep.txt", "keep\n");
  RunGit(repository, "add .");
  RunGit(repository, "commit -m baseline");

  WriteFile(repository / "README.md", "native change\n");
  WriteFile(repository / "new.txt", "new file\n");
  harmony_git::RepositoryOperation staged =
      harmony_git::StageRepository(repository.string(), {"."});
  Require(staged.success, staged.error);
  Require(staged.changedCount == 2, "Native add changed count is incorrect.");
  const harmony_git::FileStatus* stagedReadme =
      FindStatus(staged.snapshot, "README.md");
  Require(
      stagedReadme != nullptr && stagedReadme->indexState == "M" &&
          stagedReadme->workTreeState == " ",
      "Native staged modification state is incorrect.");
  const harmony_git::FileStatus* stagedNew =
      FindStatus(staged.snapshot, "new.txt");
  Require(
      stagedNew != nullptr && stagedNew->indexState == "A",
      "Native staged addition state is incorrect.");

  std::string diffError;
  const std::string stagedDiff =
      harmony_git::DiffRepository(repository.string(), true, &diffError);
  Require(diffError.empty(), diffError);
  Require(
      stagedDiff.find("native change") != std::string::npos &&
          stagedDiff.find("new file") != std::string::npos,
      "Native staged diff did not include staged content.");
  Require(
      harmony_git::DiffRepository(repository.string(), false, &diffError).empty(),
      "Native unstaged diff should be empty after add.");

  harmony_git::RepositoryOperation committed =
      harmony_git::CommitRepository(repository.string(), "native commit");
  Require(committed.success, committed.error);
  const std::string systemHead =
      RunCapture(
          "git -C " + ShellQuote(repository) + " rev-parse HEAD");
  Require(
      systemHead == committed.snapshot.head + "\n",
      "Native commit ref does not match system Git.");
  Require(
      RunCapture(
          "git -C " + ShellQuote(repository) + " show -s --format=%s HEAD") ==
          "native commit\n",
      "System Git could not read the native commit.");

  WriteFile(repository / "README.md", "unstaged change\n");
  const std::string workTreeDiff =
      harmony_git::DiffRepository(repository.string(), false, &diffError);
  Require(diffError.empty(), diffError);
  Require(
      workTreeDiff.find("-native change") != std::string::npos &&
          workTreeDiff.find("+unstaged change") != std::string::npos,
      "Native working-tree diff is incorrect.");

  harmony_git::RepositoryOperation restoredWorkingTree =
      harmony_git::RestoreWorkingTree(repository.string(), {"README.md"});
  Require(
      restoredWorkingTree.success,
      restoredWorkingTree.error);
  Require(
      RunCapture(
          "cat " + ShellQuote(repository / "README.md")) == "native change\n",
      "Native restore did not restore the indexed working-tree content.");
  Require(
      harmony_git::DiffRepository(repository.string(), false, &diffError).empty(),
      "Native restore left an unstaged diff.");

  harmony_git::RepositoryOperation restored =
      harmony_git::ResetHard(repository.string());
  Require(restored.success, restored.error);
  Require(
      RunCapture(
          "cat " + ShellQuote(repository / "README.md")) == "native change\n",
      "Native reset --hard did not restore the working tree.");

  harmony_git::RepositoryOperation created =
      harmony_git::CreateBranch(repository.string(), "feature/native", false);
  Require(created.success, created.error);
  harmony_git::RepositoryOperation switched =
      harmony_git::SwitchBranch(repository.string(), "feature/native");
  Require(switched.success, switched.error);
  WriteFile(repository / "feature.txt", "feature\n");
  Require(
      harmony_git::StageRepository(repository.string(), {"feature.txt"}).success,
      "Native add on feature branch failed.");
  harmony_git::RepositoryOperation featureCommit =
      harmony_git::CommitRepository(repository.string(), "feature commit");
  Require(featureCommit.success, featureCommit.error);
  Require(
      harmony_git::SwitchBranch(repository.string(), "main").success,
      "Native switch back to main failed.");
  Require(
      !fs::exists(repository / "feature.txt"),
      "Native branch switch left feature-only files in main.");

  harmony_git::RepositoryOperation unstage =
      harmony_git::StageRepository(repository.string(), {"README.md"});
  Require(unstage.success, unstage.error);
  harmony_git::RepositoryOperation restoreIndex =
      harmony_git::RestoreStaged(repository.string(), {"README.md"});
  Require(restoreIndex.success, restoreIndex.error);
  Require(
      FindStatus(restoreIndex.snapshot, "README.md") == nullptr,
      "Native restore --staged did not clear the index modification.");

  std::string logError;
  const std::vector<harmony_git::Commit> log =
      harmony_git::ReadLog(repository.string(), 10, &logError);
  Require(logError.empty(), logError);
  Require(
      log.size() >= 2 && log[0].subject == "native commit",
      "Native log did not read the latest commit.");

  harmony_git::RepositoryOperation deleteWithoutForce =
      harmony_git::DeleteBranch(repository.string(), "feature/native", false);
  Require(
      !deleteWithoutForce.success,
      "Native branch deletion should refuse an unmerged branch.");
  harmony_git::RepositoryOperation deleteForced =
      harmony_git::DeleteBranch(repository.string(), "feature/native", true);
  Require(deleteForced.success, deleteForced.error);
}

void TestMoveRemoveShowAndTags(const fs::path& root) {
  const fs::path repository = root / "move remove show tag repository";
  Run(
      "git -c init.defaultBranch=main init " + ShellQuote(repository) +
      " >/dev/null 2>&1");
  RunGit(repository, "config user.name 'Harmony Command Test'");
  RunGit(repository, "config user.email 'commands@example.invalid'");
  WriteFile(repository / "old.txt", "tracked baseline\n");
  WriteFile(repository / "clean.txt", "remove clean\n");
  WriteFile(repository / "cached.txt", "cached baseline\n");
  WriteFile(repository / "conflict.txt", "conflict baseline\n");
  WriteFile(repository / "force-delete.txt", "force baseline\n");
  WriteFile(repository / "dir/one.txt", "one\n");
  WriteFile(repository / "dir/two.txt", "two\n");
  WriteFile(repository / "show.txt", "show baseline\n");
  WriteFile(repository / "show-other.txt", "other baseline\n");
  RunGit(repository, "add .");
  RunGit(repository, "commit -m baseline");
  const std::string baselineHead = TrimLineEnding(
      RunCapture(
          "git -C " + ShellQuote(repository) + " rev-parse HEAD"));

  WriteFile(repository / "show.txt", "show current\n");
  WriteFile(repository / "show-other.txt", "other current\n");
  RunGit(repository, "add show.txt show-other.txt");
  RunGit(repository, "commit -m 'show command fixture'");
  const std::string currentHead = TrimLineEnding(
      RunCapture(
          "git -C " + ShellQuote(repository) + " rev-parse HEAD"));

  std::string showError;
  const std::string shown = harmony_git::ShowRevision(
      repository.string(),
      "",
      false,
      false,
      {},
      &showError);
  Require(showError.empty(), showError);
  Require(
      shown.find("commit " + currentHead) == 0 &&
          shown.find("show command fixture") != std::string::npos &&
          shown.find("-show baseline") != std::string::npos &&
          shown.find("+show current") != std::string::npos &&
          shown.find("show-other.txt") != std::string::npos,
      "Native show did not format commit metadata and patch content.");

  const std::string shownPath = harmony_git::ShowRevision(
      repository.string(),
      "HEAD",
      false,
      false,
      {"show.txt"},
      &showError);
  Require(showError.empty(), showError);
  Require(
      shownPath.find("show.txt") != std::string::npos &&
          shownPath.find("show-other.txt") == std::string::npos,
      "Native path-limited show included an unrelated file.");

  const std::string shownStat = harmony_git::ShowRevision(
      repository.string(),
      "HEAD",
      true,
      true,
      {},
      &showError);
  Require(showError.empty(), showError);
  Require(
      shownStat.find(currentHead.substr(0, 7) + " show command fixture") == 0 &&
          shownStat.find("show.txt") != std::string::npos &&
          shownStat.find("show-other.txt") != std::string::npos &&
          shownStat.find("2 insertions") != std::string::npos &&
          shownStat.find("2 deletions") != std::string::npos,
      "Native show --stat --oneline output is incomplete.");

  harmony_git::RepositoryOperation lightweight =
      harmony_git::CreateTag(
          repository.string(),
          "v1-light",
          "HEAD",
          false,
          false,
          "");
  Require(lightweight.success, lightweight.error);
  Require(
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " rev-parse refs/tags/v1-light")) == currentHead,
      "System Git could not read the native lightweight tag.");

  harmony_git::RepositoryOperation annotated =
      harmony_git::CreateTag(
          repository.string(),
          "v1-annotated",
          "HEAD",
          false,
          true,
          "Harmony annotated tag");
  Require(annotated.success, annotated.error);
  Require(
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " cat-file -t refs/tags/v1-annotated") == "tag\n",
      "System Git did not recognize the native annotated tag object.");
  Require(
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " tag -l v1-annotated --format='%(contents)'") ==
          "Harmony annotated tag\n\n",
      "System Git could not read the native annotated tag message.");

  harmony_git::RepositoryOperation moving =
      harmony_git::CreateTag(
          repository.string(),
          "moving",
          "HEAD~1",
          false,
          false,
          "");
  Require(moving.success, moving.error);
  RunGit(repository, "pack-refs --all");
  std::vector<std::string> packedTags =
      harmony_git::ReadTags(repository.string(), {}, &showError);
  Require(showError.empty(), showError);
  Require(
      Contains(packedTags, "v1-light") &&
          Contains(packedTags, "v1-annotated") &&
          Contains(packedTags, "moving"),
      "Native tag listing did not include packed tags.");
  const std::vector<std::string> filteredTags =
      harmony_git::ReadTags(
          repository.string(),
          {"v1-*"},
          &showError);
  Require(showError.empty(), showError);
  Require(
      filteredTags.size() == 2 &&
          Contains(filteredTags, "v1-light") &&
          Contains(filteredTags, "v1-annotated") &&
          !Contains(filteredTags, "moving"),
      "Native tag pattern listing did not filter packed tags.");

  moving = harmony_git::CreateTag(
      repository.string(),
      "moving",
      "HEAD",
      true,
      false,
      "");
  Require(moving.success, moving.error);
  Require(
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " rev-parse refs/tags/moving")) == currentHead,
      "Native forced tag update did not replace a packed tag.");

  const std::string shownTag = harmony_git::ShowRevision(
      repository.string(),
      "v1-annotated",
      false,
      true,
      {},
      &showError);
  Require(showError.empty(), showError);
  Require(
      shownTag.find(currentHead.substr(0, 7) + " show command fixture") == 0,
      "Native show did not peel an annotated tag to its commit.");

  harmony_git::RepositoryOperation deletedTags =
      harmony_git::DeleteTags(
          repository.string(),
          {"v1-light", "v1-annotated", "moving"});
  Require(deletedTags.success, deletedTags.error);
  Require(
      RunCapture(
          "git -C " + ShellQuote(repository) + " tag --list").empty(),
      "Native tag deletion left loose or packed tag refs.");

  const std::string originalBlob = TrimLineEnding(
      RunCapture(
          "git -C " + ShellQuote(repository) + " rev-parse HEAD:old.txt"));
  WriteFile(repository / "old.txt", "unstaged move content\n");
  harmony_git::RepositoryOperation moved =
      harmony_git::MoveRepositoryPath(
          repository.string(),
          "old.txt",
          "renamed.txt",
          false);
  Require(moved.success, moved.error);
  Require(
      !fs::exists(repository / "old.txt") &&
          RunCapture("cat " + ShellQuote(repository / "renamed.txt")) ==
              "unstaged move content\n",
      "Native move did not preserve the working-tree content.");
  Require(
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " rev-parse :renamed.txt")) == originalBlob,
      "Native move changed the indexed blob for an unstaged file.");
  const std::string moveStatus =
      RunCapture(
          "git -C " + ShellQuote(repository) + " status --short");
  Require(
      moveStatus.find("old.txt -> renamed.txt") != std::string::npos,
      "System Git did not recognize the native move.");
  RunGit(repository, "reset --hard HEAD");

  harmony_git::RepositoryOperation removed =
      harmony_git::RemoveRepositoryPaths(
          repository.string(),
          {"clean.txt"},
          false,
          false,
          false);
  Require(removed.success, removed.error);
  Require(
      !fs::exists(repository / "clean.txt") &&
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " ls-files -- clean.txt").empty(),
      "Native rm did not remove the file from the worktree and index.");

  WriteFile(repository / "cached.txt", "cached staged\n");
  Require(
      harmony_git::StageRepository(
          repository.string(),
          {"cached.txt"}).success,
      "Native cached-rm fixture staging failed.");
  harmony_git::RepositoryOperation cached =
      harmony_git::RemoveRepositoryPaths(
          repository.string(),
          {"cached.txt"},
          true,
          false,
          false);
  Require(cached.success, cached.error);
  Require(
      fs::exists(repository / "cached.txt") &&
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " ls-files -- cached.txt").empty(),
      "Native rm --cached removed worktree content or retained the index entry.");

  WriteFile(repository / "conflict.txt", "conflict staged\n");
  Require(
      harmony_git::StageRepository(
          repository.string(),
          {"conflict.txt"}).success,
      "Native forced cached-rm fixture staging failed.");
  WriteFile(repository / "conflict.txt", "conflict unstaged\n");
  harmony_git::RepositoryOperation refusedCached =
      harmony_git::RemoveRepositoryPaths(
          repository.string(),
          {"conflict.txt"},
          true,
          false,
          false);
  Require(
      !refusedCached.success,
      "Native rm --cached should refuse simultaneous index and worktree changes.");
  harmony_git::RepositoryOperation forcedCached =
      harmony_git::RemoveRepositoryPaths(
          repository.string(),
          {"conflict.txt"},
          true,
          true,
          false);
  Require(forcedCached.success, forcedCached.error);
  Require(
      RunCapture("cat " + ShellQuote(repository / "conflict.txt")) ==
          "conflict unstaged\n",
      "Forced native rm --cached changed worktree content.");

  WriteFile(repository / "force-delete.txt", "force modified\n");
  harmony_git::RepositoryOperation refusedDelete =
      harmony_git::RemoveRepositoryPaths(
          repository.string(),
          {"force-delete.txt"},
          false,
          false,
          false);
  Require(
      !refusedDelete.success && fs::exists(repository / "force-delete.txt"),
      "Native rm should refuse a modified worktree file without force.");
  harmony_git::RepositoryOperation forcedDelete =
      harmony_git::RemoveRepositoryPaths(
          repository.string(),
          {"force-delete.txt"},
          false,
          true,
          false);
  Require(forcedDelete.success, forcedDelete.error);
  Require(
      !fs::exists(repository / "force-delete.txt"),
      "Forced native rm did not remove the modified file.");

  harmony_git::RepositoryOperation refusedDirectory =
      harmony_git::RemoveRepositoryPaths(
          repository.string(),
          {"dir"},
          false,
          false,
          false);
  Require(
      !refusedDirectory.success,
      "Native rm should require recursive mode for directories.");
  harmony_git::RepositoryOperation removedDirectory =
      harmony_git::RemoveRepositoryPaths(
          repository.string(),
          {"dir"},
          false,
          false,
          true);
  Require(removedDirectory.success, removedDirectory.error);
  Require(
      !fs::exists(repository / "dir") &&
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " ls-files -- dir").empty(),
      "Native recursive rm did not remove all tracked directory entries.");

  Require(
      baselineHead != currentHead,
      "Show/tag fixture did not create distinct commits.");
}

void TestListFiles(const fs::path& root) {
  const fs::path repository = root / "list files repository";
  Run(
      "git -c init.defaultBranch=main init " + ShellQuote(repository) +
      " >/dev/null 2>&1");
  RunGit(repository, "config user.name 'Harmony List Files Test'");
  RunGit(repository, "config user.email 'list-files@example.invalid'");
  WriteFile(
      repository / ".gitignore",
      "*.log\nignored-dir/\n");
  WriteFile(repository / "-dash.txt", "dash\n");
  WriteFile(repository / "README.md", "root\n");
  WriteFile(repository / "src/clean.txt", "clean\n");
  WriteFile(repository / "src/modified.txt", "baseline\n");
  WriteFile(repository / "src/deleted.txt", "delete\n");
  WriteFile(repository / "docs/note.md", "note\n");
  RunGit(repository, "add .");
  RunGit(repository, "commit -m baseline");

  WriteFile(repository / "src/modified.txt", "changed\n");
  fs::remove(repository / "src/deleted.txt");
  WriteFile(repository / "src/untracked.txt", "untracked\n");
  WriteFile(repository / "root-untracked.txt", "root untracked\n");
  WriteFile(repository / "ignored.log", "ignored\n");
  WriteFile(repository / "ignored-dir/inside.txt", "ignored directory\n");

  const auto read =
      [](const fs::path& startPath,
         const harmony_git::ListFilesOptions& options) {
        std::string error;
        const std::vector<std::string> lines =
            harmony_git::ReadFiles(
                startPath.string(),
                options,
                &error);
        Require(error.empty(), error);
        return JoinLines(lines);
      };
  const auto system =
      [](const fs::path& startPath,
         const std::string& arguments) {
        return RunCapture(
            "git -C " + ShellQuote(startPath) +
            " ls-files " + arguments);
      };

  harmony_git::ListFilesOptions options;
  Require(
      read(repository, options) == system(repository, ""),
      "Native default ls-files does not agree with system Git.");

  options.cached = true;
  Require(
      read(repository, options) ==
          system(repository, "--cached"),
      "Native cached ls-files does not agree with system Git.");

  options = {};
  options.paths = {"src"};
  Require(
      read(repository, options) ==
          system(repository, "-- src"),
      "Native ls-files directory pathspec does not agree with system Git.");

  options.paths = {"*.txt"};
  Require(
      read(repository, options) ==
          system(repository, "-- '*.txt'"),
      "Native ls-files glob pathspec does not agree with system Git.");

  options.paths = {"-dash.txt"};
  Require(
      read(repository, options) ==
          system(repository, "-- '-dash.txt'"),
      "Native dash-prefixed pathspec does not agree with system Git.");

  options = {};
  options.stage = true;
  Require(
      read(repository, options) ==
          system(repository, "--stage"),
      "Native staged ls-files does not agree with system Git.");

  options = {};
  options.modified = true;
  Require(
      read(repository, options) ==
          system(repository, "--modified"),
      "Native modified ls-files does not agree with system Git.");

  options = {};
  options.deleted = true;
  Require(
      read(repository, options) ==
          system(repository, "--deleted"),
      "Native deleted ls-files does not agree with system Git.");

  options = {};
  options.cached = true;
  options.modified = true;
  Require(
      read(repository, options) ==
          system(repository, "--cached --modified"),
      "Native combined ls-files selectors do not agree with system Git.");

  options = {};
  options.others = true;
  Require(
      read(repository, options) ==
          system(repository, "--others"),
      "Native untracked ls-files does not agree with system Git.");

  options.excludeStandard = true;
  Require(
      read(repository, options) ==
          system(repository, "--others --exclude-standard"),
      "Native excluded untracked ls-files does not agree with system Git.");

  options.ignored = true;
  Require(
      read(repository, options) ==
          system(
              repository,
              "--others --ignored --exclude-standard"),
      "Native ignored ls-files does not agree with system Git.");

  const fs::path subdirectory = repository / "src";
  options = {};
  Require(
      read(subdirectory, options) ==
          system(subdirectory, ""),
      "Native subdirectory ls-files output is not command-relative.");

  options.paths = {"."};
  Require(
      read(subdirectory, options) ==
          system(subdirectory, "-- ."),
      "Native current-directory pathspec does not agree with system Git.");

  options.paths = {"../README.md"};
  Require(
      read(subdirectory, options) ==
          system(subdirectory, "-- ../README.md"),
      "Native parent pathspec does not agree with system Git.");

  options.paths.clear();
  options.fullName = true;
  Require(
      read(subdirectory, options) ==
          system(subdirectory, "--full-name"),
      "Native full-name ls-files output does not agree with system Git.");

  options = {};
  options.paths = {":(top)README.md"};
  Require(
      read(subdirectory, options) ==
          system(subdirectory, "-- ':(top)README.md'"),
      "Native top-level pathspec output does not agree with system Git.");

  options.fullName = true;
  Require(
      read(subdirectory, options) ==
          system(
              subdirectory,
              "--full-name -- ':(top)README.md'"),
      "Native full-name top-level pathspec does not agree with system Git.");

  const fs::path conflictRepository =
      root / "list files conflict repository";
  Run(
      "git -c init.defaultBranch=main init " +
      ShellQuote(conflictRepository) +
      " >/dev/null 2>&1");
  RunGit(
      conflictRepository,
      "config user.name 'Harmony Conflict Test'");
  RunGit(
      conflictRepository,
      "config user.email 'conflict@example.invalid'");
  WriteFile(conflictRepository / "conflict.txt", "baseline\n");
  RunGit(conflictRepository, "add .");
  RunGit(conflictRepository, "commit -m baseline");
  RunGit(conflictRepository, "checkout -b side");
  WriteFile(conflictRepository / "conflict.txt", "side\n");
  RunGit(conflictRepository, "commit -am side");
  RunGit(conflictRepository, "checkout main");
  WriteFile(conflictRepository / "conflict.txt", "main\n");
  RunGit(conflictRepository, "commit -am main");
  Run(
      "git -C " + ShellQuote(conflictRepository) +
      " merge side >/dev/null 2>&1; test $? -eq 1");

  options = {};
  Require(
      read(conflictRepository, options) ==
          system(conflictRepository, ""),
      "Native conflicted default ls-files does not agree with system Git.");

  options.cached = true;
  Require(
      read(conflictRepository, options) ==
          system(conflictRepository, "--cached"),
      "Native conflicted cached ls-files does not agree with system Git.");

  options = {};
  options.modified = true;
  Require(
      read(conflictRepository, options) ==
          system(conflictRepository, "--modified"),
      "Native conflicted modified ls-files does not agree with system Git.");

  options = {};
  options.stage = true;
  Require(
      read(conflictRepository, options) ==
          system(conflictRepository, "--stage"),
      "Native conflicted stage ls-files does not agree with system Git.");
}

void TestCatFileAndListTree(const fs::path& root) {
  const fs::path repository = root / "cat file and list tree repository";
  Run(
      "git -c init.defaultBranch=main init " + ShellQuote(repository) +
      " >/dev/null 2>&1");
  RunGit(repository, "config user.name 'Harmony Object Test'");
  RunGit(repository, "config user.email 'objects@example.invalid'");
  WriteFile(repository / "README.md", "baseline root\n");
  WriteFile(repository / "docs/guide.md", "guide\n");
  WriteFile(repository / "src/main.txt", "main\n");
  WriteFile(repository / "src/lib/helper.txt", "helper\n");
  RunGit(repository, "add .");
  RunGit(repository, "commit -m baseline");
  WriteFile(repository / "README.md", "current root\n");
  RunGit(repository, "add README.md");
  RunGit(repository, "commit -m current");
  RunGit(repository, "tag -a v1-annotated -m 'Harmony object tag'");

  const auto readObject =
      [&repository](
          const std::string& objectName,
          const std::string& mode) {
        std::string error;
        const std::string content =
            harmony_git::ReadObjectContent(
                repository.string(),
                objectName,
                mode,
                &error);
        Require(error.empty(), error);
        return content;
      };
  const auto systemObject =
      [&repository](
          const std::string& option,
          const std::string& objectName) {
        return RunCapture(
            "git -C " + ShellQuote(repository) +
            " cat-file " + option + " '" + objectName + "'");
      };

  Require(
      readObject("HEAD", "type") ==
          TrimLineEnding(systemObject("-t", "HEAD")),
      "Native cat-file type does not agree with system Git.");
  Require(
      readObject("HEAD", "size") ==
          TrimLineEnding(systemObject("-s", "HEAD")),
      "Native cat-file size does not agree with system Git.");
  Require(
      readObject("HEAD", "pretty") ==
          systemObject("-p", "HEAD"),
      "Native cat-file pretty commit does not agree with system Git.");
  Require(
      readObject("HEAD:README.md", "blob") ==
          systemObject("blob", "HEAD:README.md"),
      "Native typed blob read does not agree with system Git.");
  Require(
      readObject("HEAD:src", "pretty") ==
          TrimLineEnding(systemObject("-p", "HEAD:src")),
      "Native pretty tree output does not agree with system Git.");
  Require(
      readObject("v1-annotated", "type") == "tag",
      "Native cat-file did not resolve an annotated tag object.");
  Require(
      readObject("v1-annotated^{}", "type") == "commit" &&
          readObject("v1-annotated^{commit}", "type") == "commit" &&
          readObject("v1-annotated^{tree}", "type") == "tree",
      "Native cat-file did not peel annotated tag expressions.");
  Require(
      readObject("HEAD~1", "type") == "commit",
      "Native cat-file did not resolve an ancestor expression.");
  Require(
      readObject("HEAD:src/lib/helper.txt", "pretty") == "helper\n",
      "Native cat-file did not traverse a revision path.");
  Require(
      readObject("HEAD", "exists").empty(),
      "Native cat-file exists mode produced output.");

  const std::string head = TrimLineEnding(
      RunCapture(
          "git -C " + ShellQuote(repository) + " rev-parse HEAD"));
  RunGit(repository, "repack -ad");
  Require(
      readObject(head.substr(0, 12), "type") == "commit",
      "Native cat-file did not resolve a packed abbreviated object id.");

  std::string mismatchError;
  harmony_git::ReadObjectContent(
      repository.string(),
      "HEAD",
      "blob",
      &mismatchError);
  Require(
      !mismatchError.empty(),
      "Native typed cat-file did not reject an object type mismatch.");

  const auto readTree =
      [](const fs::path& startPath,
         const std::string& treeish,
         const harmony_git::ListTreeOptions& options) {
        std::string error;
        const std::vector<std::string> lines =
            harmony_git::ReadTree(
                startPath.string(),
                treeish,
                options,
                &error);
        Require(error.empty(), error);
        return JoinLines(lines);
      };
  const auto systemTree =
      [](const fs::path& startPath,
         const std::string& arguments) {
        return RunCapture(
            "git -C " + ShellQuote(startPath) +
            " ls-tree " + arguments);
      };

  harmony_git::ListTreeOptions options;
  Require(
      readTree(repository, "HEAD", options) ==
          systemTree(repository, "HEAD"),
      "Native default ls-tree does not agree with system Git.");

  options.recursive = true;
  Require(
      readTree(repository, "HEAD", options) ==
          systemTree(repository, "-r HEAD"),
      "Native recursive ls-tree does not agree with system Git.");

  options.includeTrees = true;
  Require(
      readTree(repository, "HEAD", options) ==
          systemTree(repository, "-rt HEAD"),
      "Native recursive tree inclusion does not agree with system Git.");

  options = {};
  options.recursive = true;
  options.directoriesOnly = true;
  Require(
      readTree(repository, "HEAD", options) ==
          systemTree(repository, "-rd HEAD"),
      "Native directory-only ls-tree does not agree with system Git.");

  options = {};
  options.longFormat = true;
  Require(
      readTree(repository, "HEAD", options) ==
          systemTree(repository, "-l HEAD"),
      "Native long ls-tree does not agree with system Git.");

  options = {};
  options.nameOnly = true;
  options.recursive = true;
  Require(
      readTree(repository, "HEAD", options) ==
          systemTree(repository, "-r --name-only HEAD"),
      "Native name-only ls-tree does not agree with system Git.");

  options = {};
  options.objectOnly = true;
  Require(
      readTree(repository, "HEAD", options) ==
          systemTree(repository, "--object-only HEAD"),
      "Native object-only ls-tree does not agree with system Git.");

  options = {};
  options.paths = {"src"};
  Require(
      readTree(repository, "HEAD", options) ==
          systemTree(repository, "HEAD -- src"),
      "Native ls-tree path filter does not agree with system Git.");

  const fs::path subdirectory = repository / "src";
  options = {};
  Require(
      readTree(subdirectory, "HEAD", options) ==
          systemTree(subdirectory, "HEAD"),
      "Native subdirectory ls-tree output is not command-relative.");

  options.fullName = true;
  Require(
      readTree(subdirectory, "HEAD", options) ==
          systemTree(subdirectory, "--full-name HEAD"),
      "Native full-name ls-tree output does not agree with system Git.");

  options = {};
  options.fullTree = true;
  Require(
      readTree(subdirectory, "HEAD", options) ==
          systemTree(subdirectory, "--full-tree HEAD"),
      "Native full-tree ls-tree output does not agree with system Git.");

  options = {};
  options.recursive = true;
  options.paths = {"lib"};
  Require(
      readTree(subdirectory, "HEAD", options) ==
          systemTree(subdirectory, "-r HEAD -- lib"),
      "Native subdirectory ls-tree path filter does not agree with system Git.");
}

void TestHashObjectAndCheckIgnore(const fs::path& root) {
  const fs::path repository = root / "hash and ignore repository";
  Run(
      "git -c init.defaultBranch=main init " + ShellQuote(repository) +
      " >/dev/null 2>&1");
  RunGit(repository, "config user.name 'Harmony Hash Test'");
  RunGit(repository, "config user.email 'hash@example.invalid'");
  WriteFile(
      repository / ".gitignore",
      "*.log\n!important.log\nbuild/\n");
  WriteFile(repository / "src/.gitignore", "generated.txt\n");
  WriteFile(repository / "README.md", "tracked root\n");
  WriteFile(repository / "tracked.log", "tracked ignored file\n");
  RunGit(repository, "add .gitignore src/.gitignore README.md");
  RunGit(repository, "add -f tracked.log");
  RunGit(repository, "commit -m baseline");

  const std::string binaryPayload("binary\0payload\n", 15);
  WriteFile(repository / "payload.bin", binaryPayload);
  WriteFile(repository / "write.txt", "write this object\n");
  WriteFile(repository / "ignored.log", "ignored\n");
  WriteFile(repository / "important.log", "included\n");
  WriteFile(repository / "build/output.txt", "ignored directory\n");
  WriteFile(repository / "src/generated.txt", "nested ignored\n");
  WriteFile(repository / "private.txt", "info excluded\n");
  WriteFile(repository / ".git/info/exclude", "private.txt\n");

  const auto hash =
      [](const fs::path& startPath,
         const std::vector<std::string>& paths,
         const std::string& type,
         bool write) {
        std::string error;
        const std::vector<std::string> objectIds =
            harmony_git::HashFiles(
                startPath.string(),
                paths,
                type,
                write,
                &error);
        Require(error.empty(), error);
        return JoinLines(objectIds);
      };
  const auto systemHash =
      [](const fs::path& startPath,
         const std::string& arguments) {
        return RunCapture(
            "git -C " + ShellQuote(startPath) +
            " hash-object " + arguments);
      };

  Require(
      hash(repository, {"payload.bin"}, "blob", false) ==
          systemHash(repository, "-- payload.bin"),
      "Native blob hash does not agree with system Git.");
  std::string stdinHashError;
  const std::string stdinPayload = "stdin payload\n";
  const std::string stdinHash = harmony_git::HashInput(
      repository.string(),
      stdinPayload,
      "blob",
      false,
      &stdinHashError);
  Require(stdinHashError.empty(), stdinHashError);
  Require(
      stdinHash ==
          TrimLineEnding(RunCapture(
              "printf 'stdin payload\\n' | git -C " +
              ShellQuote(repository) + " hash-object --stdin")),
      "Native stdin blob hash does not agree with system Git.");
  std::string writtenStdinHashError;
  const std::string writtenStdinHash = harmony_git::HashInput(
      repository.string(),
      stdinPayload,
      "blob",
      true,
      &writtenStdinHashError);
  Require(writtenStdinHashError.empty(), writtenStdinHashError);
  Require(
      writtenStdinHash == stdinHash,
      "Native written stdin blob ID changed unexpectedly.");
  Require(
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " cat-file blob " + writtenStdinHash) == stdinPayload,
      "System Git could not read the native stdin hash-object result.");
  Require(
      hash(
          repository / "src",
          {"generated.txt", "../README.md"},
          "blob",
          false) ==
          systemHash(
              repository / "src",
              "-- generated.txt ../README.md"),
      "Native multi-file subdirectory hashing does not agree with system Git.");

  const std::string writtenId = TrimLineEnding(
      hash(repository, {"write.txt"}, "blob", true));
  Require(
      writtenId ==
          TrimLineEnding(systemHash(repository, "-- write.txt")),
      "Native written blob ID does not agree with system Git.");
  Require(
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " cat-file blob " + writtenId) == "write this object\n",
      "System Git could not read the native hash-object -w result.");

  WriteFile(
      repository / "commit.payload",
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " cat-file commit HEAD"));
  Require(
      hash(repository, {"commit.payload"}, "commit", false) ==
          systemHash(repository, "-t commit -- commit.payload"),
      "Native typed commit hash does not agree with system Git.");

  const auto check =
      [](const fs::path& startPath,
         const std::vector<std::string>& paths,
         bool noIndex,
         bool verbose) {
        std::string error;
        const std::vector<std::string> lines =
            harmony_git::CheckIgnored(
                startPath.string(),
                paths,
                noIndex,
                verbose,
                &error);
        Require(error.empty(), error);
        return JoinLines(lines);
      };
  const auto systemCheck =
      [](const fs::path& startPath,
         const std::string& arguments) {
        return RunCapture(
            "git -C " + ShellQuote(startPath) +
            " check-ignore " + arguments);
      };
  const std::vector<std::string> paths = {
      "ignored.log",
      "important.log",
      "build/output.txt",
      "src/generated.txt",
      "private.txt",
      "tracked.log"};
  Require(
      check(repository, paths, false, false) ==
          systemCheck(
              repository,
              "-- ignored.log important.log build/output.txt "
              "src/generated.txt private.txt tracked.log"),
      "Native check-ignore output does not agree with system Git.");
  const std::string nativeVerbose =
      check(repository, paths, false, true);
  const std::string systemVerbose =
      systemCheck(
          repository,
          "-v -- ignored.log important.log build/output.txt "
          "src/generated.txt private.txt tracked.log");
  Require(
      nativeVerbose == systemVerbose,
      "Native verbose check-ignore output does not agree with system Git.\n"
      "Native:\n" + nativeVerbose +
      "System:\n" + systemVerbose);
  Require(
      check(repository, paths, true, false) ==
          systemCheck(
              repository,
              "--no-index -- ignored.log important.log build/output.txt "
              "src/generated.txt private.txt tracked.log"),
      "Native check-ignore --no-index does not agree with system Git.");

  Require(
      check(
          repository / "src",
          {"generated.txt", "../ignored.log"},
          false,
          false) ==
          systemCheck(
              repository / "src",
              "-- generated.txt ../ignored.log"),
      "Native subdirectory check-ignore output does not agree with system Git.");
}

void TestReferencePlumbing(const fs::path& root) {
  const fs::path repository = root / "reference-plumbing";
  Run(
      "git -c init.defaultBranch=main init " + ShellQuote(repository) +
      " >/dev/null 2>&1");
  RunGit(repository, "config user.name 'Harmony Ref Test'");
  RunGit(repository, "config user.email 'refs@example.invalid'");
  RunGit(repository, "commit --allow-empty -m first");
  const std::string first =
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " rev-parse HEAD"));
  RunGit(repository, "commit --allow-empty -m second");
  const std::string second =
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " rev-parse HEAD"));
  RunGit(repository, "branch feature HEAD~1");
  RunGit(repository, "tag light");
  RunGit(repository, "tag -a annotated -m 'annotated reference'");
  RunGit(
      repository,
      "update-ref refs/remotes/origin/main HEAD");
  RunGit(
      repository,
      "symbolic-ref refs/remotes/origin/HEAD refs/remotes/origin/main");
  RunGit(repository, "pack-refs --all");

  harmony_git::ShowRefOptions allOptions;
  std::string error;
  const std::vector<std::string> allReferences =
      harmony_git::ReadReferences(
          repository.string(),
          allOptions,
          &error);
  Require(error.empty(), error);
  Require(
      JoinLines(allReferences) ==
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " show-ref"),
      "Native show-ref output does not agree with system Git.");

  harmony_git::ShowRefOptions headsOptions;
  headsOptions.heads = true;
  headsOptions.includeHead = true;
  const std::vector<std::string> heads =
      harmony_git::ReadReferences(
          repository.string(),
          headsOptions,
          &error);
  Require(error.empty(), error);
  Require(
      JoinLines(heads) ==
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " show-ref --head --heads"),
      "Native show-ref HEAD/head filtering does not agree with system Git.");

  harmony_git::ShowRefOptions tagOptions;
  tagOptions.tags = true;
  tagOptions.dereference = true;
  const std::vector<std::string> tags =
      harmony_git::ReadReferences(
          repository.string(),
          tagOptions,
          &error);
  Require(error.empty(), error);
  Require(
      JoinLines(tags) ==
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " show-ref --tags --dereference"),
      "Native show-ref tag dereference output does not agree with system Git.");

  harmony_git::ShowRefOptions hashOptions;
  hashOptions.heads = true;
  hashOptions.hashOnly = true;
  hashOptions.abbreviation = 8;
  hashOptions.patterns = {"main"};
  const std::vector<std::string> hashes =
      harmony_git::ReadReferences(
          repository.string(),
          hashOptions,
          &error);
  Require(error.empty(), error);
  Require(
      JoinLines(hashes) ==
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " show-ref --heads --hash=8 main"),
      "Native abbreviated show-ref hash output does not agree with system Git.");

  harmony_git::ShowRefOptions verifyOptions;
  verifyOptions.verify = true;
  verifyOptions.patterns = {"refs/heads/main"};
  const std::vector<std::string> verified =
      harmony_git::ReadReferences(
          repository.string(),
          verifyOptions,
          &error);
  Require(error.empty(), error);
  Require(
      JoinLines(verified) ==
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " show-ref --verify refs/heads/main"),
      "Native show-ref verification does not agree with system Git.");
  verifyOptions.quiet = true;
  verifyOptions.patterns = {"refs/heads/missing"};
  const std::vector<std::string> quietMissing =
      harmony_git::ReadReferences(
          repository.string(),
          verifyOptions,
          &error);
  Require(
      error.empty() && quietMissing.empty(),
      "Quiet show-ref verification should suppress missing-ref output.");

  const std::string excludeInput =
      "refs/heads/main\n"
      "refs/heads/new\n"
      "0000000000000000000000000000000000000000 refs/tags/new^{}\n";
  const std::vector<std::string> excludedReferences =
      harmony_git::ExcludeExistingReferences(
          repository.string(),
          excludeInput,
          "",
          &error);
  Require(error.empty(), error);
  Require(
      JoinLines(excludedReferences) ==
          RunCapture(
              "printf 'refs/heads/main\\n"
              "refs/heads/new\\n"
              "0000000000000000000000000000000000000000 "
              "refs/tags/new^{}\\n' | git -C " +
              ShellQuote(repository) +
              " show-ref --exclude-existing"),
      "Native show-ref --exclude-existing output does not agree with system Git.");
  const std::vector<std::string> excludedTags =
      harmony_git::ExcludeExistingReferences(
          repository.string(),
          excludeInput + "invalid-ref\n",
          "refs/tags/",
          &error);
  Require(error.empty(), error);
  Require(
      excludedTags.size() == 1 &&
          excludedTags[0] ==
              "0000000000000000000000000000000000000000 refs/tags/new",
      "Native show-ref --exclude-existing pattern filtering is incorrect.");
  const std::vector<std::string> invalidExcludedReference =
      harmony_git::ExcludeExistingReferences(
          repository.string(),
          "invalid-ref\n",
          "",
          &error);
  Require(error.empty(), error);
  Require(
      invalidExcludedReference.size() == 1 &&
          invalidExcludedReference[0] ==
              "warning: ref 'invalid-ref' ignored",
      "Native show-ref --exclude-existing should warn for invalid refs.");

  const std::string symbolicHead =
      harmony_git::ReadSymbolicReference(
          repository.string(),
          "HEAD",
          false,
          true,
          &error);
  Require(error.empty(), error);
  Require(
      symbolicHead ==
          TrimLineEnding(
              RunCapture(
                  "git -C " + ShellQuote(repository) +
                  " symbolic-ref HEAD")),
      "Native symbolic-ref HEAD output does not agree with system Git.");
  const std::string shortHead =
      harmony_git::ReadSymbolicReference(
          repository.string(),
          "HEAD",
          true,
          true,
          &error);
  Require(error.empty(), error);
  Require(shortHead == "main", "Native symbolic-ref --short is incorrect.");

  RunGit(
      repository,
      "symbolic-ref refs/meta/current refs/remotes/origin/HEAD");
  const std::string recursiveTarget =
      harmony_git::ReadSymbolicReference(
          repository.string(),
          "refs/meta/current",
          false,
          true,
          &error);
  Require(error.empty(), error);
  Require(
      recursiveTarget == "refs/remotes/origin/main",
      "Native symbolic-ref did not follow a symbolic reference chain.");
  const std::string directTarget =
      harmony_git::ReadSymbolicReference(
          repository.string(),
          "refs/meta/current",
          false,
          false,
          &error);
  Require(error.empty(), error);
  Require(
      directTarget == "refs/remotes/origin/HEAD",
      "Native symbolic-ref --no-recurse did not return the direct target.");

  const harmony_git::RepositoryOperation symbolicCreated =
      harmony_git::UpdateSymbolicReference(
          repository.string(),
          "refs/meta/native",
          "refs/heads/main",
          false,
          "native symbolic ref");
  Require(symbolicCreated.success, symbolicCreated.error);
  Require(
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " symbolic-ref refs/meta/native")) ==
          "refs/heads/main",
      "System Git could not read the native symbolic reference.");
  const harmony_git::RepositoryOperation symbolicDeleted =
      harmony_git::UpdateSymbolicReference(
          repository.string(),
          "refs/meta/native",
          "",
          true,
          "");
  Require(symbolicDeleted.success, symbolicDeleted.error);
  Require(
      !fs::exists(repository / ".git" / "refs" / "meta" / "native"),
      "Native symbolic-ref deletion left the loose reference behind.");

  const std::string zeroId(40, '0');
  const harmony_git::RepositoryOperation created =
      harmony_git::UpdateReference(
          repository.string(),
          "refs/heads/native",
          first,
          zeroId,
          false,
          false,
          "create native");
  Require(created.success, created.error);
  Require(
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " rev-parse refs/heads/native")) == first,
      "System Git could not read the natively created reference.");

  const harmony_git::RepositoryOperation advanced =
      harmony_git::UpdateReference(
          repository.string(),
          "refs/heads/native",
          second,
          first,
          false,
          false,
          "advance native");
  Require(advanced.success, advanced.error);
  const std::vector<harmony_git::ReflogEntry> nativeLog =
      harmony_git::ReadReflog(
          repository.string(),
          "refs/heads/native",
          10,
          &error);
  Require(error.empty(), error);
  Require(
      FindReflog(nativeLog, "advance native") != nullptr,
      "Native update-ref did not append the requested reflog message.");

  const harmony_git::RepositoryOperation rejected =
      harmony_git::UpdateReference(
          repository.string(),
          "refs/heads/native",
          first,
          zeroId,
          false,
          false,
          "must fail");
  Require(
      !rejected.success,
      "Native update-ref did not enforce the expected old object ID.");
  Require(
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " rev-parse refs/heads/native")) == second,
      "A rejected update-ref changed the reference.");

  const harmony_git::RepositoryOperation packedAdvanced =
      harmony_git::UpdateReference(
          repository.string(),
          "refs/heads/feature",
          second,
          first,
          false,
          false,
          "advance packed feature");
  Require(packedAdvanced.success, packedAdvanced.error);
  Require(
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " rev-parse refs/heads/feature")) == second,
      "Native update-ref did not replace a packed reference.");

  const harmony_git::RepositoryOperation headBack =
      harmony_git::UpdateReference(
          repository.string(),
          "HEAD",
          first,
          second,
          false,
          false,
          "move HEAD back");
  Require(headBack.success, headBack.error);
  Require(
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " rev-parse refs/heads/main")) == first,
      "Native update-ref HEAD did not update its branch target.");

  const harmony_git::RepositoryOperation detached =
      harmony_git::UpdateReference(
          repository.string(),
          "HEAD",
          second,
          "",
          false,
          true,
          "detach HEAD");
  Require(detached.success, detached.error);
  Require(
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " rev-parse HEAD")) == second,
      "Native update-ref --no-deref did not update HEAD directly.");
  const harmony_git::RepositorySnapshot detachedSnapshot =
      harmony_git::InspectRepository(repository.string());
  Require(
      detachedSnapshot.valid && detachedSnapshot.detached,
      "Native update-ref --no-deref did not detach HEAD.");

  const harmony_git::RepositoryOperation restoredHead =
      harmony_git::UpdateSymbolicReference(
          repository.string(),
          "HEAD",
          "refs/heads/main",
          false,
          "restore symbolic HEAD");
  Require(restoredHead.success, restoredHead.error);
  const harmony_git::RepositoryOperation deleted =
      harmony_git::UpdateReference(
          repository.string(),
          "refs/heads/native",
          "",
          second,
          true,
          false,
          "delete native");
  Require(deleted.success, deleted.error);
  Require(
      !fs::exists(repository / ".git" / "refs" / "heads" / "native"),
      "Native update-ref deletion left the loose reference behind.");

  const harmony_git::RepositoryOperation batchCommitted =
      harmony_git::UpdateReferences(
          repository.string(),
          "start\n"
          "create refs/heads/batch-created \"ma\\151n\"\n"
          "update refs/heads/feature " + first + " " + second + "\n"
          "verify refs/heads/main " + first + "\n"
          "prepare\n"
          "commit\n",
          false,
          false,
          "native batch commit");
  Require(batchCommitted.success, batchCommitted.error);
  Require(
      batchCommitted.output ==
          std::vector<std::string>({
              "start: ok",
              "prepare: ok",
              "commit: ok"}),
      "Native update-ref transaction status output is incorrect.");
  Require(
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " rev-parse refs/heads/batch-created")) == first,
      "Native update-ref transaction did not parse an octal escape.");
  Require(
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " rev-parse refs/heads/feature")) == first,
      "Native update-ref transaction did not update an existing ref.");

  const harmony_git::RepositoryOperation validationRejected =
      harmony_git::UpdateReferences(
          repository.string(),
          "create refs/heads/batch-validation " + second + "\n"
          "update refs/heads/feature " + second + " " + second + "\n",
          false,
          false,
          "must not partially apply");
  Require(
      !validationRejected.success,
      "Native update-ref transaction accepted a stale old value.");
  Require(
      !fs::exists(
          repository / ".git" / "refs" / "heads" /
          "batch-validation"),
      "A rejected update-ref transaction created an earlier ref.");
  Require(
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " rev-parse refs/heads/feature")) == first,
      "A rejected update-ref transaction changed an existing ref.");

  RunGit(
      repository,
      "update-ref refs/heads/batch-regular " + first);
  RunGit(
      repository,
      "update-ref refs/heads/batch-path/child " + first);
  const std::string tree =
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " rev-parse HEAD^{tree}"));
  const std::string missingObject =
      "0000000000000000000000000000000000000001";
  const harmony_git::RepositoryOperation partialBatch =
      harmony_git::UpdateReferences(
          repository.string(),
          "create refs/heads/batch-accepted " + second + "\n"
          "create refs/heads/batch-invalid " + missingObject + "\n"
          "create refs/heads/batch-tree " + tree + "\n"
          "create refs/heads/batch-created " + second + "\n"
          "update refs/heads/batch-missing " + second + " " +
              first + "\n"
          "update refs/heads/feature " + second + " " + second + "\n"
          "option no-deref\n"
          "symref-update refs/heads/batch-regular "
              "refs/heads/main ref refs/heads/main\n"
          "create refs/heads/batch-path " + second + "\n",
          false,
          false,
          "allow partial reference updates",
          false,
          true);
  Require(partialBatch.success, partialBatch.error);
  Require(
      partialBatch.output ==
          std::vector<std::string>({
              "rejected refs/heads/batch-invalid " +
                  missingObject + " " + zeroId +
                  " invalid new value provided",
              "rejected refs/heads/batch-tree " +
                  tree + " " + zeroId +
                  " invalid new value provided",
              "rejected refs/heads/batch-created " +
                  second + " " + zeroId +
                  " reference already exists",
              "rejected refs/heads/batch-missing " +
                  second + " " + first +
                  " reference does not exist",
              "rejected refs/heads/feature " +
                  second + " " + second +
                  " incorrect old value provided",
              "rejected refs/heads/batch-regular " +
                  zeroId + " " + zeroId + " "
                  "expected symref but found regular ref",
              "rejected refs/heads/batch-path " +
                  second + " " + zeroId +
                  " refname conflict"}),
      "Native update-ref --batch-updates rejection output is incorrect.");
  Require(
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " rev-parse refs/heads/batch-accepted")) == second,
      "Native update-ref --batch-updates did not commit a valid item.");
  Require(
      !fs::exists(
          repository / ".git" / "refs" / "heads" /
          "batch-invalid"),
      "Native update-ref --batch-updates wrote an invalid object.");
  Require(
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " rev-parse refs/heads/feature")) == first,
      "Native update-ref --batch-updates changed a rejected ref.");
  Require(
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " rev-parse refs/heads/batch-regular")) == first,
      "Native update-ref --batch-updates rewrote a rejected regular ref.");

  RunGit(repository, "config core.ignorecase true");
  const harmony_git::RepositoryOperation casePartialBatch =
      harmony_git::UpdateReferences(
          repository.string(),
          "create refs/heads/batch-case " + first + "\n"
          "create refs/heads/BATCH-CASE " + second + "\n"
          "create refs/heads/batch-case-accepted " + second + "\n",
          false,
          false,
          "case-insensitive reference conflict",
          false,
          true);
  Require(casePartialBatch.success, casePartialBatch.error);
  Require(
      casePartialBatch.output ==
          std::vector<std::string>({
              "rejected refs/heads/BATCH-CASE " +
                  second + " " + zeroId +
                  " reference conflict due to case-insensitive filesystem"}),
      "Native update-ref case-conflict rejection output is incorrect.");
  Require(
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " rev-parse refs/heads/batch-case")) == first,
      "Native update-ref case-conflict handling changed the first ref.");
  Require(
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " rev-parse refs/heads/batch-case-accepted")) == second,
      "Native update-ref case-conflict handling lost a valid item.");

  const harmony_git::RepositoryOperation rolledBack =
      harmony_git::UpdateReferences(
          repository.string(),
          "create refs/heads/batch-parent " + first + "\n"
          "create refs/heads/batch-parent/child " + second + "\n",
          false,
          false,
          "force filesystem rollback");
  Require(
      !rolledBack.success,
      "Native update-ref transaction did not surface a ref path conflict.");
  Require(
      !fs::exists(
          repository / ".git" / "refs" / "heads" / "batch-parent"),
      "Native update-ref transaction did not roll back a loose ref.");
  Require(
      !fs::exists(
          repository / ".git" / "logs" / "refs" / "heads" /
          "batch-parent"),
      "Native update-ref transaction did not roll back a reflog.");

  const harmony_git::RepositoryOperation aborted =
      harmony_git::UpdateReferences(
          repository.string(),
          "start\n"
          "create refs/heads/batch-aborted " + second + "\n"
          "abort\n",
          false,
          false,
          "");
  Require(aborted.success, aborted.error);
  Require(
      aborted.output ==
          std::vector<std::string>({
              "start: ok",
              "abort: ok"}),
      "Native update-ref abort status output is incorrect.");
  Require(
      !fs::exists(
          repository / ".git" / "refs" / "heads" / "batch-aborted"),
      "Native update-ref abort wrote a queued ref.");

  const harmony_git::RepositoryOperation eofAborted =
      harmony_git::UpdateReferences(
          repository.string(),
          "start\n"
          "create refs/heads/batch-eof-aborted " + second + "\n"
          "prepare\n",
          false,
          false,
          "");
  Require(eofAborted.success, eofAborted.error);
  Require(
      !fs::exists(
          repository / ".git" / "refs" / "heads" /
          "batch-eof-aborted"),
      "Native update-ref committed an explicitly started transaction at EOF.");

  const harmony_git::RepositoryOperation detachedByBatch =
      harmony_git::UpdateReferences(
          repository.string(),
          "option no-deref\n"
          "update HEAD " + second + " " + first + "\n",
          false,
          false,
          "batch detach");
  Require(detachedByBatch.success, detachedByBatch.error);
  Require(
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " rev-parse HEAD")) == second,
      "Native update-ref option no-deref did not update HEAD directly.");
  Require(
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " rev-parse refs/heads/main")) == first,
      "Native update-ref option no-deref changed the branch target.");
  const harmony_git::RepositoryOperation batchRestoredHead =
      harmony_git::UpdateSymbolicReference(
          repository.string(),
          "HEAD",
          "refs/heads/main",
          false,
          "restore after batch detach");
  Require(batchRestoredHead.success, batchRestoredHead.error);

  const harmony_git::RepositoryOperation duplicateAlias =
      harmony_git::UpdateReferences(
          repository.string(),
          "update HEAD " + second + " " + first + "\n"
          "update refs/heads/main " + second + " " + first + "\n",
          false,
          false,
          "");
  Require(
      !duplicateAlias.success,
      "Native update-ref accepted duplicate dereferenced refs.");
  Require(
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " rev-parse refs/heads/main")) == first,
      "Duplicate dereferenced refs changed the branch.");

  RunGit(repository, "config core.logAllRefUpdates false");
  const harmony_git::RepositoryOperation forcedReflog =
      harmony_git::UpdateReferences(
          repository.string(),
          "create refs/meta/batch-log " + second + "\n",
          false,
          true,
          "forced transaction reflog");
  Require(forcedReflog.success, forcedReflog.error);
  Require(
      fs::is_regular_file(
          repository / ".git" / "logs" / "refs" / "meta" /
          "batch-log"),
      "Native update-ref --create-reflog did not create a reflog.");
  RunGit(repository, "config core.logAllRefUpdates true");

  std::string nullInput;
  AppendNullRecord(
      &nullInput,
      "create refs/heads/nul-created");
  AppendNullRecord(&nullInput, first);
  AppendNullRecord(
      &nullInput,
      "update refs/heads/feature");
  AppendNullRecord(&nullInput, second);
  AppendNullRecord(&nullInput, first);
  AppendNullRecord(
      &nullInput,
      "verify refs/heads/main");
  AppendNullRecord(&nullInput, first);
  const harmony_git::RepositoryOperation nullCommitted =
      harmony_git::UpdateReferences(
          repository.string(),
          nullInput,
          false,
          false,
          "native NUL batch",
          true);
  Require(nullCommitted.success, nullCommitted.error);
  Require(
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " rev-parse refs/heads/nul-created")) == first,
      "Native update-ref -z did not create a reference.");
  Require(
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " rev-parse refs/heads/feature")) == second,
      "Native update-ref -z did not update a reference.");

  std::string nullPartialInput;
  AppendNullRecord(
      &nullPartialInput,
      "create refs/heads/nul-batch-valid");
  AppendNullRecord(&nullPartialInput, first);
  AppendNullRecord(
      &nullPartialInput,
      "create refs/heads/nul-batch-invalid");
  AppendNullRecord(&nullPartialInput, missingObject);
  const harmony_git::RepositoryOperation nullPartialBatch =
      harmony_git::UpdateReferences(
          repository.string(),
          nullPartialInput,
          false,
          false,
          "native NUL partial batch",
          true,
          true);
  Require(nullPartialBatch.success, nullPartialBatch.error);
  Require(
      nullPartialBatch.output ==
          std::vector<std::string>({
              "rejected refs/heads/nul-batch-invalid " +
                  missingObject + " " + zeroId +
                  " invalid new value provided"}),
      "Native update-ref -z --batch-updates output is incorrect.");
  Require(
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " rev-parse refs/heads/nul-batch-valid")) == first,
      "Native update-ref -z --batch-updates lost a valid item.");

  std::string nullSymbolicInput;
  AppendNullRecord(&nullSymbolicInput, "start");
  AppendNullRecord(
      &nullSymbolicInput,
      "create refs/heads/nul-mixed");
  AppendNullRecord(&nullSymbolicInput, first);
  AppendNullRecord(
      &nullSymbolicInput,
      "symref-create refs/meta/nul-symbolic");
  AppendNullRecord(
      &nullSymbolicInput,
      "refs/heads/missing-target");
  AppendNullRecord(&nullSymbolicInput, "prepare");
  AppendNullRecord(&nullSymbolicInput, "commit");
  const harmony_git::RepositoryOperation nullSymbolicCreated =
      harmony_git::UpdateReferences(
          repository.string(),
          nullSymbolicInput,
          true,
          false,
          "native NUL symref",
          true);
  Require(
      nullSymbolicCreated.success,
      nullSymbolicCreated.error);
  Require(
      nullSymbolicCreated.output ==
          std::vector<std::string>({
              "start: ok",
              "prepare: ok",
              "commit: ok"}),
      "Native update-ref -z transaction status output is incorrect.");
  Require(
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " rev-parse refs/heads/nul-mixed")) == first,
      "Native update-ref -z mixed transaction lost a regular ref.");
  Require(
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " symbolic-ref refs/meta/nul-symbolic")) ==
          "refs/heads/missing-target",
      "Native update-ref -z did not create a dangling symref.");

  const harmony_git::RepositoryOperation symbolicVerified =
      harmony_git::UpdateReferences(
          repository.string(),
          "option no-deref\n"
          "symref-verify refs/meta/nul-symbolic "
          "refs/heads/missing-target\n",
          false,
          false,
          "");
  Require(symbolicVerified.success, symbolicVerified.error);
  const harmony_git::RepositoryOperation symbolicWrongTarget =
      harmony_git::UpdateReferences(
          repository.string(),
          "option no-deref\n"
          "symref-update refs/meta/nul-symbolic "
          "refs/heads/main ref refs/heads/feature\n",
          false,
          false,
          "");
  Require(
      !symbolicWrongTarget.success,
      "Native symref-update accepted a stale old target.");
  Require(
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " symbolic-ref refs/meta/nul-symbolic")) ==
          "refs/heads/missing-target",
      "Rejected symref-update changed the symbolic target.");

  const harmony_git::RepositoryOperation symbolicAdvanced =
      harmony_git::UpdateReferences(
          repository.string(),
          "option no-deref\n"
          "symref-update refs/meta/nul-symbolic "
          "refs/heads/feature ref refs/heads/missing-target\n",
          false,
          false,
          "advance symbolic target");
  Require(symbolicAdvanced.success, symbolicAdvanced.error);
  Require(
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " symbolic-ref refs/meta/nul-symbolic")) ==
          "refs/heads/feature",
      "Native symref-update did not compare the old target.");

  const harmony_git::RepositoryOperation oidSymbolicUpdate =
      harmony_git::UpdateReferences(
          repository.string(),
          "option no-deref\n"
          "symref-update refs/heads/nul-created "
          "refs/heads/main oid " + first + "\n",
          false,
          false,
          "convert regular ref");
  Require(oidSymbolicUpdate.success, oidSymbolicUpdate.error);
  Require(
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " symbolic-ref refs/heads/nul-created")) ==
          "refs/heads/main",
      "Native symref-update did not compare the old object ID.");

  RunGit(
      repository,
      "symbolic-ref refs/meta/outer refs/meta/inner");
  const harmony_git::RepositoryOperation dereferencedSymbolicUpdate =
      harmony_git::UpdateReferences(
          repository.string(),
          "symref-update refs/meta/outer refs/heads/main\n",
          false,
          false,
          "follow symbolic target");
  Require(
      dereferencedSymbolicUpdate.success,
      dereferencedSymbolicUpdate.error);
  Require(
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " symbolic-ref --no-recurse refs/meta/outer")) ==
          "refs/meta/inner",
      "Dereferenced symref-update changed the outer symbolic ref.");
  Require(
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " symbolic-ref --no-recurse refs/meta/inner")) ==
          "refs/heads/main",
      "Dereferenced symref-update did not update the final referent.");

  const harmony_git::RepositoryOperation symbolicDeleteRequiresNoDeref =
      harmony_git::UpdateReferences(
          repository.string(),
          "symref-delete refs/meta/nul-symbolic refs/heads/feature\n",
          false,
          false,
          "");
  Require(
      !symbolicDeleteRequiresNoDeref.success,
      "Native symref-delete accepted dereference mode.");
  const harmony_git::RepositoryOperation symbolicDeletedByBatch =
      harmony_git::UpdateReferences(
          repository.string(),
          "option no-deref\n"
          "symref-delete refs/meta/nul-symbolic refs/heads/feature\n",
          false,
          false,
          "delete symbolic target");
  Require(
      symbolicDeletedByBatch.success,
      symbolicDeletedByBatch.error);
  Require(
      !fs::exists(
          repository / ".git" / "refs" / "meta" /
          "nul-symbolic"),
      "Native symref-delete left the symbolic ref behind.");

  const harmony_git::RepositoryOperation missingSymbolicVerified =
      harmony_git::UpdateReferences(
          repository.string(),
          "option no-deref\n"
          "symref-verify refs/meta/missing-symbolic\n",
          false,
          false,
          "");
  Require(
      missingSymbolicVerified.success,
      missingSymbolicVerified.error);

  const harmony_git::RepositoryOperation duplicateHeadAlias =
      harmony_git::UpdateReferences(
          repository.string(),
          "option no-deref\n"
          "update HEAD " + second + " " + first + "\n"
          "update refs/heads/main " + second + " " + first + "\n",
          false,
          false,
          "");
  Require(
      !duplicateHeadAlias.success,
      "Native update-ref accepted HEAD and its referent together.");
  Require(
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " symbolic-ref HEAD")) ==
          "refs/heads/main",
      "Rejected HEAD alias transaction detached HEAD.");

  const harmony_git::RepositoryOperation symbolicRolledBack =
      harmony_git::UpdateReferences(
          repository.string(),
          "create refs/heads/symref-rollback " + first + "\n"
          "symref-create refs/heads/symref-rollback/child "
          "refs/heads/main\n",
          true,
          false,
          "force symbolic rollback");
  Require(
      !symbolicRolledBack.success,
      "Native symbolic transaction did not surface a ref path conflict.");
  Require(
      !fs::exists(
          repository / ".git" / "refs" / "heads" /
          "symref-rollback"),
      "Native symbolic transaction did not roll back a loose ref.");
}

void TestCommitGraphPlumbing(const fs::path& root) {
  const fs::path repository = root / "commit-graph-plumbing";
  Run(
      "git -c init.defaultBranch=main init " + ShellQuote(repository) +
      " >/dev/null 2>&1");
  RunGit(repository, "config user.name 'Harmony Graph Test'");
  RunGit(repository, "config user.email 'graph@example.invalid'");
  const auto commit =
      [&repository](const std::string& date, const std::string& message) {
        Run(
            "GIT_AUTHOR_DATE='" + date + "' "
            "GIT_COMMITTER_DATE='" + date + "' "
            "git -C " + ShellQuote(repository) +
            " commit --allow-empty -m '" + message +
            "' >/dev/null 2>&1");
      };
  commit("2001-01-01T00:00:00+0000", "graph root");
  const std::string rootCommit =
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " rev-parse HEAD"));
  commit("2001-01-02T00:00:00+0000", "main one");
  RunGit(repository, "branch feature " + rootCommit);
  RunGit(repository, "checkout feature");
  commit("2001-01-03T00:00:00+0000", "feature one");
  commit("2001-01-05T00:00:00+0000", "feature two");
  const std::string featureTip =
      TrimLineEnding(
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " rev-parse HEAD"));
  RunGit(repository, "checkout main");
  commit("2001-01-04T00:00:00+0000", "main two");
  Run(
      "GIT_AUTHOR_DATE='2001-01-06T00:00:00+0000' "
      "GIT_COMMITTER_DATE='2001-01-06T00:00:00+0000' "
      "git -C " + ShellQuote(repository) +
      " merge --no-ff feature -m 'merge feature' >/dev/null 2>&1");
  RunGit(repository, "tag light feature");
  RunGit(repository, "tag -a annotated -m 'graph tag' main");
  RunGit(
      repository,
      "update-ref refs/remotes/origin/main main");
  RunGit(
      repository,
      "symbolic-ref refs/remotes/origin/HEAD refs/remotes/origin/main");
  RunGit(repository, "pack-refs --all");

  const auto revList =
      [&repository](
          const harmony_git::RevListOptions& options,
          const std::string& arguments) {
        std::string error;
        const std::vector<std::string> lines =
            harmony_git::ReadRevisionList(
                repository.string(),
                options,
                &error);
        Require(
            error.empty(),
            "rev-list " + arguments + ": " + error);
        const std::string native = JoinLines(lines);
        const std::string system =
            RunCapture(
                "git -C " + ShellQuote(repository) +
                " rev-list " + arguments);
        Require(
            native == system,
            "Native rev-list output does not agree with system Git.\n"
            "Arguments: " + arguments +
            "\nNative:\n" + native +
            "System:\n" + system);
      };

  harmony_git::RevListOptions headOptions;
  headOptions.revisions = {"HEAD"};
  revList(headOptions, "HEAD");

  harmony_git::RevListOptions rangeOptions;
  rangeOptions.revisions = {rootCommit + "..HEAD"};
  revList(rangeOptions, rootCommit + "..HEAD");

  harmony_git::RevListOptions firstParentOptions;
  firstParentOptions.firstParent = true;
  firstParentOptions.revisions = {"HEAD"};
  revList(firstParentOptions, "--first-parent HEAD");

  harmony_git::RevListOptions formattedOptions;
  formattedOptions.parents = true;
  formattedOptions.abbreviate = true;
  formattedOptions.abbreviation = 12;
  formattedOptions.maxCount = 3;
  formattedOptions.revisions = {"HEAD"};
  revList(
      formattedOptions,
      "--parents --abbrev-commit --abbrev=12 -n 3 HEAD");

  harmony_git::RevListOptions countOptions;
  countOptions.noMerges = true;
  countOptions.count = true;
  countOptions.revisions = {"HEAD"};
  revList(countOptions, "--no-merges --count HEAD");

  harmony_git::RevListOptions allOptions;
  allOptions.all = true;
  allOptions.maxCount = 4;
  revList(allOptions, "--all -n 4");

  const auto mergeBase =
      [&repository](
          const harmony_git::MergeBaseOptions& options,
          const std::string& arguments) {
        std::string error;
        const std::vector<std::string> lines =
            harmony_git::ReadMergeBases(
                repository.string(),
                options,
                &error);
        Require(
            error.empty(),
            "merge-base " + arguments + ": " + error);
        const std::string native = JoinLines(lines);
        const std::string system =
            RunCapture(
                "git -C " + ShellQuote(repository) +
                " merge-base " + arguments);
        Require(
            native == system,
            "Native merge-base output does not agree with system Git.\n"
            "Arguments: " + arguments +
            "\nNative:\n" + native +
            "System:\n" + system);
      };

  harmony_git::MergeBaseOptions pairOptions;
  pairOptions.revisions = {"main", "feature"};
  mergeBase(pairOptions, "main feature");

  harmony_git::MergeBaseOptions octopusOptions;
  octopusOptions.all = true;
  octopusOptions.octopus = true;
  octopusOptions.revisions = {"main", "feature", rootCommit};
  mergeBase(
      octopusOptions,
      "--all --octopus main feature " + rootCommit);

  harmony_git::MergeBaseOptions independentOptions;
  independentOptions.independent = true;
  independentOptions.revisions = {"main", "feature", rootCommit};
  mergeBase(
      independentOptions,
      "--independent main feature " + rootCommit);

  const auto forEachRef =
      [&repository](
          const harmony_git::ForEachRefOptions& options,
          const std::string& arguments) {
        std::string error;
        const std::vector<std::string> lines =
            harmony_git::FormatReferences(
                repository.string(),
                options,
                &error);
        Require(
            error.empty(),
            "for-each-ref " + arguments + ": " + error);
        const std::string native = JoinLines(lines);
        const std::string system =
            RunCapture(
                "git -C " + ShellQuote(repository) +
                " for-each-ref " + arguments);
        Require(
            native == system,
            "Native for-each-ref output does not agree with system Git.\n"
            "Arguments: " + arguments +
            "\nNative:\n" + native +
            "System:\n" + system);
      };

  harmony_git::ForEachRefOptions defaultRefOptions;
  forEachRef(defaultRefOptions, "");

  harmony_git::ForEachRefOptions formatOptions;
  formatOptions.format =
      "%(HEAD)|%(refname:short)|%(objectname:short=12)|"
      "%(objecttype)|%(subject)|%(authorname)|%(authoremail)";
  formatOptions.sortKeys = {"-refname"};
  formatOptions.patterns = {"refs/heads"};
  forEachRef(
      formatOptions,
      "--format='%(HEAD)|%(refname:short)|%(objectname:short=12)|"
      "%(objecttype)|%(subject)|%(authorname)|%(authoremail)' "
      "--sort=-refname refs/heads");

  harmony_git::ForEachRefOptions countRefOptions;
  countRefOptions.count = 2;
  countRefOptions.sortKeys = {"-refname"};
  countRefOptions.format = "%(refname)";
  forEachRef(
      countRefOptions,
      "--count=2 --sort=-refname --format='%(refname)'");

  harmony_git::ForEachRefOptions pointsAtOptions;
  pointsAtOptions.pointsAt = featureTip;
  pointsAtOptions.format = "%(refname)";
  forEachRef(
      pointsAtOptions,
      "--points-at=" + featureTip + " --format='%(refname)'");

  harmony_git::ForEachRefOptions containsOptions;
  containsOptions.contains = featureTip;
  containsOptions.format = "%(refname)";
  containsOptions.patterns = {"refs/heads"};
  forEachRef(
      containsOptions,
      "--contains=" + featureTip +
      " --format='%(refname)' refs/heads");

  harmony_git::ForEachRefOptions mergedOptions;
  mergedOptions.merged = featureTip;
  mergedOptions.format = "%(refname)";
  mergedOptions.patterns = {"refs/heads"};
  forEachRef(
      mergedOptions,
      "--merged=" + featureTip +
      " --format='%(refname)' refs/heads");
}

void TestRevisionPathAndAncestor(const fs::path& root) {
  const fs::path repository = root / "revision path and ancestor";
  Run(
      "git -c init.defaultBranch=main init " + ShellQuote(repository) +
      " >/dev/null 2>&1");
  RunGit(repository, "config user.name 'Harmony Path Test'");
  RunGit(repository, "config user.email 'path@example.invalid'");

  WriteFile(repository / "src/a", "src baseline\n");
  WriteFile(repository / "docs/b", "docs baseline\n");
  WriteFile(repository / "README.md", "baseline\n");
  RunGit(repository, "add .");
  RunGit(repository, "commit -m baseline");

  WriteFile(repository / "src/a", "src change\n");
  RunGit(repository, "add src/a");
  RunGit(repository, "commit -m 'src change'");
  const std::string sourceCommit = TrimLineEnding(
      RunCapture(
          "git -C " + ShellQuote(repository) + " rev-parse HEAD"));

  WriteFile(repository / "docs/b", "docs change\n");
  RunGit(repository, "add docs/b");
  RunGit(repository, "commit -m 'docs change'");

  WriteFile(repository / "README.md", "unrelated change\n");
  RunGit(repository, "add README.md");
  RunGit(repository, "commit -m 'unrelated change'");
  RunGit(repository, "branch topic " + sourceCommit);

  const auto revList = [&repository](
      const std::vector<std::string>& paths,
      const std::string& arguments) {
    harmony_git::RevListOptions options;
    options.revisions = {"HEAD"};
    options.paths = paths;
    std::string error;
    const std::vector<std::string> lines =
        harmony_git::ReadRevisionList(
            repository.string(),
            options,
            &error);
    Require(error.empty(), "rev-list " + arguments + ": " + error);
    const std::string native = JoinLines(lines);
    const std::string system =
        RunCapture(
            "git -C " + ShellQuote(repository) +
            " rev-list " + arguments);
    Require(
        native == system,
        "Native path-limited rev-list disagrees with system Git.\n"
        "Arguments: " + arguments +
        "\nNative:\n" + native +
        "System:\n" + system);
  };

  revList({"src/a"}, "HEAD -- src/a");
  revList({"src"}, "HEAD -- src");
  revList({"docs/b"}, "HEAD -- docs/b");

  const fs::path mergeRepository = root / "revision path merge";
  Run(
      "git -c init.defaultBranch=main init " +
      ShellQuote(mergeRepository) +
      " >/dev/null 2>&1");
  RunGit(mergeRepository, "config user.name 'Harmony Merge Test'");
  RunGit(mergeRepository, "config user.email 'merge@example.invalid'");
  WriteFile(mergeRepository / "src/a", "baseline\n");
  WriteFile(mergeRepository / "docs/b", "baseline\n");
  RunGit(mergeRepository, "add .");
  RunGit(mergeRepository, "commit -m baseline");
  RunGit(mergeRepository, "branch topic");
  WriteFile(mergeRepository / "docs/b", "main docs\n");
  RunGit(mergeRepository, "add docs/b");
  RunGit(mergeRepository, "commit -m 'main docs'");
  RunGit(mergeRepository, "switch topic");
  WriteFile(mergeRepository / "src/a", "topic source\n");
  RunGit(mergeRepository, "add src/a");
  RunGit(mergeRepository, "commit -m 'topic source'");
  RunGit(mergeRepository, "switch main");
  RunGit(mergeRepository, "merge --no-ff topic -m merge");

  const auto mergeRevList = [&mergeRepository](
      const std::string& arguments,
      bool parents,
      bool firstParent) {
    harmony_git::RevListOptions options;
    options.parents = parents;
    options.firstParent = firstParent;
    options.revisions = {"HEAD"};
    options.paths = {"src/a"};
    std::string error;
    const std::vector<std::string> lines =
        harmony_git::ReadRevisionList(
            mergeRepository.string(),
            options,
            &error);
    Require(error.empty(), "merge rev-list " + arguments + ": " + error);
    const std::string native = JoinLines(lines);
    const std::string system =
        RunCapture(
            "git -C " + ShellQuote(mergeRepository) +
            " rev-list " + arguments);
    Require(
        native == system,
        "Native merge path history disagrees with system Git.\n"
        "Arguments: " + arguments +
        "\nNative:\n" + native +
        "System:\n" + system);
  };
  mergeRevList("HEAD -- src/a", false, false);
  mergeRevList("--parents HEAD -- src/a", true, false);
  mergeRevList("--first-parent HEAD -- src/a", false, true);

  std::string ancestorError;
  Require(
      harmony_git::IsAncestorRevision(
          repository.string(),
          "topic",
          "main",
          &ancestorError),
      "Native merge-base --is-ancestor returned false for an ancestor.");
  Require(ancestorError.empty(), ancestorError);
  Require(
      std::system(
          ("git -C " + ShellQuote(repository) +
           " merge-base --is-ancestor topic main >/dev/null 2>&1").c_str()) ==
          0,
      "System Git did not recognize topic as an ancestor of main.");

  ancestorError.clear();
  Require(
      !harmony_git::IsAncestorRevision(
          repository.string(),
          "main",
          "topic",
          &ancestorError),
      "Native merge-base --is-ancestor returned true for a non-ancestor.");
  Require(ancestorError.empty(), ancestorError);
  Require(
      std::system(
          ("git -C " + ShellQuote(repository) +
           " merge-base --is-ancestor main topic >/dev/null 2>&1").c_str()) !=
          0,
      "System Git incorrectly recognized main as an ancestor of topic.");

  const fs::path forkRepository = root / "fork point repository";
  Run(
      "git -c init.defaultBranch=main init " +
      ShellQuote(forkRepository) +
      " >/dev/null 2>&1");
  RunGit(forkRepository, "config user.name 'Harmony Fork Test'");
  RunGit(forkRepository, "config user.email 'fork@example.invalid'");
  WriteFile(forkRepository / "file", "first\n");
  RunGit(forkRepository, "add file");
  RunGit(forkRepository, "commit -m first");
  WriteFile(forkRepository / "file", "second\n");
  RunGit(forkRepository, "add file");
  RunGit(forkRepository, "commit -m second");
  const std::string rewriteBase = TrimLineEnding(
      RunCapture(
          "git -C " + ShellQuote(forkRepository) + " rev-parse HEAD"));
  WriteFile(forkRepository / "file", "old upstream tip\n");
  RunGit(forkRepository, "add file");
  RunGit(forkRepository, "commit -m 'old upstream tip'");
  const std::string expectedForkPoint = TrimLineEnding(
      RunCapture(
          "git -C " + ShellQuote(forkRepository) + " rev-parse HEAD"));
  RunGit(forkRepository, "branch topic");
  RunGit(forkRepository, "switch topic");
  WriteFile(forkRepository / "topic", "topic\n");
  RunGit(forkRepository, "add topic");
  RunGit(forkRepository, "commit -m topic");
  RunGit(forkRepository, "switch main");
  RunGit(forkRepository, "reset --hard " + rewriteBase);
  WriteFile(forkRepository / "main", "rewritten main\n");
  RunGit(forkRepository, "add main");
  RunGit(forkRepository, "commit -m 'rewritten main'");

  std::string forkError;
  const std::string nativeForkPoint =
      harmony_git::FindForkPointRevision(
          forkRepository.string(),
          "main",
          "topic",
          &forkError);
  Require(forkError.empty(), forkError);
  const std::string systemForkPoint = TrimLineEnding(
      RunCapture(
          "git -C " + ShellQuote(forkRepository) +
          " merge-base --fork-point main topic"));
  Require(
      nativeForkPoint == expectedForkPoint &&
          nativeForkPoint == systemForkPoint,
      "Native merge-base --fork-point disagrees with system Git.\n"
      "Native: " + nativeForkPoint +
      "\nSystem: " + systemForkPoint +
      "\nExpected: " + expectedForkPoint);
}

void TestConfigAndReflogs(const fs::path& root) {
  const fs::path repository = root / "config and reflog repository";
  Run(
      "git -c init.defaultBranch=main init " + ShellQuote(repository) +
      " >/dev/null 2>&1");
  RunGit(repository, "config user.name 'Harmony Config Test'");
  RunGit(repository, "config user.email 'config@example.invalid'");
  RunGit(
      repository,
      "remote add origin https://example.invalid/config.git");
  RunGit(
      repository,
      "config remote.origin.pushurl ssh://example.invalid/config.git");
  RunGit(repository, "config scope.value local");
  WriteFile(repository / "README.md", "config baseline\n");
  RunGit(repository, "add .");
  RunGit(repository, "commit -m baseline");
  const std::string baselineHead = TrimLineEnding(
      RunCapture(
          "git -C " + ShellQuote(repository) + " rev-parse HEAD"));

  const fs::path configRoot = root / "config scopes";
  const fs::path systemConfig = configRoot / "system.gitconfig";
  const fs::path globalConfig = configRoot / "global.gitconfig";
  const fs::path includedConfig = configRoot / "included.gitconfig";
  const fs::path gitDirectoryConfig =
      configRoot / "git-directory.gitconfig";
  const fs::path caseInsensitiveConfig =
      configRoot / "git-directory-case.gitconfig";
  const fs::path branchConfig =
      configRoot / "branch.gitconfig";
  const fs::path explicitConfig =
      configRoot / "explicit.gitconfig";
  const fs::path canonicalRepository =
      fs::weakly_canonical(repository);
  WriteFile(
      systemConfig,
      "[scope]\n"
      "\tvalue = system\n"
      "[system]\n"
      "\tmarker = configured\n");
  WriteFile(
      globalConfig,
      "[scope]\n"
      "\tvalue = global\n"
      "[include]\n"
      "\tpath = included.gitconfig\n"
      "[includeIf \"gitdir:" +
          canonicalRepository.generic_string() + "/\"]\n"
      "\tpath = git-directory.gitconfig\n"
      "[includeIf \"gitdir/i:CONFIG AND REFLOG REPOSITORY/\"]\n"
      "\tpath = git-directory-case.gitconfig\n"
      "[includeIf \"onbranch:main\"]\n"
      "\tpath = branch.gitconfig\n"
      "[user]\n"
      "\tname = Harmony Global Config Test\n"
      "\temail = global-config@example.invalid\n");
  WriteFile(
      includedConfig,
      "[included]\n"
      "\tvalue = relative-path\n");
  WriteFile(
      gitDirectoryConfig,
      "[conditional]\n"
      "\tgitdir = matched\n");
  WriteFile(
      caseInsensitiveConfig,
      "[conditional]\n"
      "\tgitdir-case = matched\n");
  WriteFile(
      branchConfig,
      "[conditional]\n"
      "\tbranch = main\n");
  WriteFile(
      explicitConfig,
      "[explicit]\n"
      "\tvalue = initial\n"
      "\tremove = present\n");
  ScopedEnvironmentVariable scopedSystemConfig(
      "GIT_CONFIG_SYSTEM",
      systemConfig.string());
  ScopedEnvironmentVariable scopedGlobalConfig(
      "GIT_CONFIG_GLOBAL",
      globalConfig.string());

  std::string configError;
  const std::vector<harmony_git::ConfigEntry> initialConfig =
      harmony_git::ReadConfig(
          repository.string(),
          "all",
          true,
          "",
          &configError);
  Require(configError.empty(), configError);
  Require(
      ConfigValues(initialConfig, "scope.value") ==
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " config --includes --get-all scope.value"),
      "Native config scope ordering disagrees with system Git.");
  const harmony_git::ConfigEntry* userName =
      FindLastConfig(initialConfig, "user.name");
  Require(
      userName != nullptr && userName->value == "Harmony Config Test",
      "Native config reader did not apply local user.name precedence.");
  const harmony_git::ConfigEntry* fetchUrl =
      FindConfig(initialConfig, "remote.origin.url");
  Require(
      fetchUrl != nullptr &&
          fetchUrl->value == "https://example.invalid/config.git",
      "Native config reader did not read remote subsection values.");
  const harmony_git::ConfigEntry* effectiveScope =
      FindLastConfig(initialConfig, "scope.value");
  Require(
      effectiveScope != nullptr && effectiveScope->value == "local",
      "Native config scope ordering did not prefer local config.");

  const std::vector<harmony_git::ConfigEntry> globalWithoutIncludes =
      harmony_git::ReadConfig(
          repository.string(),
          "global",
          false,
          "",
          &configError);
  Require(configError.empty(), configError);
  Require(
      FindConfig(globalWithoutIncludes, "include.path") != nullptr &&
          FindConfig(globalWithoutIncludes, "included.value") == nullptr,
      "Native --no-includes config read did not suppress include.path.");
  const std::vector<harmony_git::ConfigEntry> globalWithIncludes =
      harmony_git::ReadConfig(
          repository.string(),
          "global",
          true,
          "",
          &configError);
  Require(configError.empty(), configError);
  const harmony_git::ConfigEntry* includedValue =
      FindConfig(globalWithIncludes, "included.value");
  Require(
      includedValue != nullptr &&
          includedValue->value == "relative-path",
      "Native config reader did not resolve a relative include.path.");
  Require(
      FindConfig(globalWithIncludes, "conditional.gitdir") != nullptr &&
          FindConfig(
              globalWithIncludes,
              "conditional.gitdir-case") != nullptr &&
          FindConfig(globalWithIncludes, "conditional.branch") != nullptr,
      "Native config reader did not apply includeIf conditions.");
  Require(
      ConfigValues(globalWithIncludes, "conditional.gitdir") ==
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " config --global --includes --get-all conditional.gitdir") &&
          ConfigValues(globalWithIncludes, "conditional.gitdir-case") ==
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " config --global --includes --get-all "
              "conditional.gitdir-case") &&
          ConfigValues(globalWithIncludes, "conditional.branch") ==
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " config --global --includes --get-all conditional.branch"),
      "Native conditional config includes disagree with system Git.");

  const std::vector<harmony_git::ConfigEntry> explicitEntries =
      harmony_git::ReadConfig(
          repository.string(),
          "file",
          true,
          explicitConfig.string(),
          &configError);
  Require(configError.empty(), configError);
  const harmony_git::ConfigEntry* explicitValue =
      FindLastConfig(explicitEntries, "explicit.value");
  Require(
      explicitValue != nullptr && explicitValue->value == "initial",
      "Native config --file read did not use the explicit file.");
  Require(
      harmony_git::SetConfigValue(
          repository.string(),
          "explicit.value",
          "updated",
          "file",
          false,
          explicitConfig.string()).success,
      "Native config --file write failed.");
  Require(
      harmony_git::UnsetConfigValue(
          repository.string(),
          "explicit.remove",
          "file",
          false,
          explicitConfig.string()).success,
      "Native config --file unset failed.");
  Require(
      RunCapture(
          "git config --file " + ShellQuote(explicitConfig) +
          " --get explicit.value") == "updated\n" &&
          RunCapture(
              "git config --file " + ShellQuote(explicitConfig) +
              " --get explicit.remove 2>/dev/null || true").empty(),
      "Native explicit config file mutations disagree with system Git.");

  Require(
      harmony_git::SetCommandConfig(
          {
            "user.name=Harmony Command Config",
            "user.email=command-config@example.invalid",
            "feature.enabled"
          },
          &configError),
      configError);
  const std::vector<harmony_git::ConfigEntry> commandConfig =
      harmony_git::ReadConfig(
          repository.string(),
          "all",
          true,
          "",
          &configError);
  Require(configError.empty(), configError);
  const harmony_git::ConfigEntry* commandName =
      FindLastConfig(commandConfig, "user.name");
  const harmony_git::ConfigEntry* commandBoolean =
      FindLastConfig(commandConfig, "feature.enabled");
  Require(
      commandName != nullptr &&
          commandName->value == "Harmony Command Config" &&
          commandBoolean != nullptr &&
          commandBoolean->value == "true",
      "Native command-scoped config did not override effective config.");
  const std::vector<harmony_git::ConfigEntry> localWithCommandConfig =
      harmony_git::ReadConfig(
          repository.string(),
          "local",
          true,
          "",
          &configError);
  const harmony_git::ConfigEntry* localCommandName =
      FindLastConfig(localWithCommandConfig, "user.name");
  Require(
      configError.empty() &&
          localCommandName != nullptr &&
          localCommandName->value ==
              "Harmony Config Test",
      "Native local config unexpectedly included command-scoped values.");
  WriteFile(repository / "command-config.txt", "command config\n");
  Require(
      harmony_git::StageRepository(
          repository.string(),
          {"command-config.txt"}).success,
      "Native command config fixture staging failed.");
  const harmony_git::RepositoryOperation commandCommit =
      harmony_git::CommitRepository(
          repository.string(),
          "command config identity");
  Require(commandCommit.success, commandCommit.error);
  Require(
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " show -s --format='%an <%ae>' HEAD") ==
          "Harmony Command Config <command-config@example.invalid>\n",
      "Native commit did not honor command-scoped identity config.");
  Require(
      harmony_git::SetCommandConfig({}, &configError),
      configError);
  const std::vector<harmony_git::ConfigEntry> clearedCommandConfig =
      harmony_git::ReadConfig(
          repository.string(),
          "all",
          true,
          "",
          &configError);
  const harmony_git::ConfigEntry* clearedCommandName =
      FindLastConfig(clearedCommandConfig, "user.name");
  Require(
      configError.empty() &&
          clearedCommandName != nullptr &&
          clearedCommandName->value ==
              "Harmony Config Test",
      "Native command-scoped config was not cleared.");

  harmony_git::RepositoryOperation setCustom =
      harmony_git::SetConfigValue(
          repository.string(),
          "remote.upstream.url",
          "https://example.invalid/upstream.git",
          "local",
          false,
          "");
  Require(setCustom.success, setCustom.error);
  Require(
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " config --local --get remote.upstream.url") ==
          "https://example.invalid/upstream.git\n",
      "Native config write did not agree with system Git.");

  harmony_git::RepositoryOperation setValue =
      harmony_git::SetConfigValue(
          repository.string(),
          "core.editor",
          "Harmony Editor",
          "local",
          false,
          "");
  Require(setValue.success, setValue.error);
  Require(
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " config --local --get core.editor") == "Harmony Editor\n",
      "Native config write did not preserve spaces.");

  harmony_git::RepositoryOperation setGlobal =
      harmony_git::SetConfigValue(
          repository.string(),
          "global.marker",
          "native",
          "global",
          false,
          "");
  Require(setGlobal.success, setGlobal.error);
  Require(
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " config --global --get global.marker") == "native\n",
      "Native global config write did not agree with system Git.");

  harmony_git::RepositoryOperation setSystem =
      harmony_git::SetConfigValue(
          repository.string(),
          "system.write",
          "native",
          "system",
          false,
          "");
  Require(setSystem.success, setSystem.error);
  Require(
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " config --system --get system.write") == "native\n",
      "Native system config write did not agree with system Git.");

  Require(
      harmony_git::SetConfigValue(
          repository.string(),
          "alias.repeat",
          "first",
          "local",
          true,
          "").success,
      "Native config --add did not append the first value.");
  Require(
      harmony_git::SetConfigValue(
          repository.string(),
          "alias.repeat",
          "second",
          "local",
          true,
          "").success,
      "Native config --add did not append the second value.");
  Require(
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " config --local --get-all alias.repeat") ==
          "first\nsecond\n",
      "Native config multivars disagree with system Git.");
  const harmony_git::RepositoryOperation replaceMultiple =
      harmony_git::SetConfigValue(
          repository.string(),
          "alias.repeat",
          "replacement",
          "local",
          false,
          "");
  Require(
      !replaceMultiple.success &&
          replaceMultiple.error.find("multiple values") != std::string::npos,
      "Native config replacement did not protect duplicate values.");
  const harmony_git::RepositoryOperation unsetMultiple =
      harmony_git::UnsetConfigValue(
          repository.string(),
          "alias.repeat",
          "local",
          false,
          "");
  Require(
      !unsetMultiple.success &&
          unsetMultiple.error.find("multiple values") != std::string::npos,
      "Native config --unset did not protect duplicate values.");
  Require(
      harmony_git::UnsetConfigValue(
          repository.string(),
          "alias.repeat",
          "local",
          true,
          "").success,
      "Native config --unset-all did not remove duplicate values.");
  Require(
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " config --local --get-all alias.repeat 2>/dev/null || true")
          .empty(),
      "Native config --unset-all left duplicate values behind.");

  harmony_git::RepositoryOperation unsetValue =
      harmony_git::UnsetConfigValue(
          repository.string(),
          "remote.origin.pushurl",
          "local",
          false,
          "");
  Require(unsetValue.success, unsetValue.error);
  Require(
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " config --local --get remote.origin.pushurl 2>/dev/null || true")
          .empty(),
      "Native config unset did not remove the value.");

  Require(
      harmony_git::UnsetConfigValue(
          repository.string(),
          "user.name",
          "local",
          false,
          "").success &&
          harmony_git::UnsetConfigValue(
              repository.string(),
              "user.email",
              "local",
              false,
              "").success,
      "Native local identity cleanup failed.");
  WriteFile(repository / "README.md", "config commit\n");
  Require(
      harmony_git::StageRepository(
          repository.string(),
          {"README.md"}).success,
      "Native config fixture staging failed.");
  harmony_git::RepositoryOperation committed =
      harmony_git::CommitRepository(repository.string(), "config commit");
  Require(committed.success, committed.error);
  Require(
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " show -s --format='%an <%ae>' HEAD") ==
          "Harmony Global Config Test <global-config@example.invalid>\n",
      "Native commit did not use effective global identity config.");
  const std::string nativeCommitId = committed.snapshot.head;
  Require(
      nativeCommitId != baselineHead,
      "Native config fixture commit did not advance HEAD.");

  harmony_git::RepositoryOperation created =
      harmony_git::CreateBranch(repository.string(), "feature/config", false);
  Require(created.success, created.error);
  std::vector<harmony_git::ReflogEntry> featureLog =
      harmony_git::ReadReflog(
          repository.string(),
          "feature/config",
          10,
          &configError);
  Require(configError.empty(), configError);
  const harmony_git::ReflogEntry* createdEntry =
      FindReflog(featureLog, "branch: Created from HEAD");
  Require(createdEntry != nullptr, "Native branch reflog entry is missing.");
  Require(
      createdEntry->oldId == std::string(40, '0') &&
          createdEntry->newId == nativeCommitId,
      "Native branch reflog old/new object ids are incorrect.");

  harmony_git::RepositoryOperation switched =
      harmony_git::SwitchBranch(repository.string(), "feature/config");
  Require(switched.success, switched.error);
  std::vector<harmony_git::ReflogEntry> headLog =
      harmony_git::ReadReflog(repository.string(), "HEAD", 10, &configError);
  Require(configError.empty(), configError);
  const harmony_git::ReflogEntry* switchEntry =
      FindReflog(
          headLog,
          "checkout: moving from main to feature/config");
  Require(switchEntry != nullptr, "Native checkout reflog entry is missing.");
  Require(
      switchEntry->oldId == nativeCommitId &&
          switchEntry->newId == nativeCommitId,
      "Native checkout reflog object ids are incorrect.");

  WriteFile(repository / "feature.txt", "feature config\n");
  Require(
      harmony_git::StageRepository(
          repository.string(),
          {"feature.txt"}).success,
      "Native feature staging failed.");
  harmony_git::RepositoryOperation featureCommit =
      harmony_git::CommitRepository(repository.string(), "feature config");
  Require(featureCommit.success, featureCommit.error);
  featureLog = harmony_git::ReadReflog(
      repository.string(),
      "feature/config",
      10,
      &configError);
  Require(configError.empty(), configError);
  const harmony_git::ReflogEntry* commitEntry =
      FindReflog(featureLog, "commit: feature config");
  Require(commitEntry != nullptr, "Native commit reflog entry is missing.");
  Require(
      commitEntry->newId == featureCommit.snapshot.head,
      "Native commit reflog new object id is incorrect.");

  Require(
      harmony_git::SwitchBranch(repository.string(), "main").success,
      "Native config fixture switch back failed.");
  harmony_git::RepositoryOperation reset =
      harmony_git::ResetHard(repository.string());
  Require(reset.success, reset.error);
  headLog = harmony_git::ReadReflog(
      repository.string(),
      "HEAD",
      20,
      &configError);
  Require(configError.empty(), configError);
  const harmony_git::ReflogEntry* resetEntry =
      FindReflog(headLog, "reset: moving to HEAD");
  Require(resetEntry != nullptr, "Native reset reflog entry is missing.");
  Require(
      resetEntry->oldId == reset.snapshot.head &&
          resetEntry->newId == reset.snapshot.head,
      "Native reset reflog object ids are incorrect.");

  const std::string systemHeadReflog =
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " reflog --format='%H|%gs' -1 HEAD");
  Require(
      systemHeadReflog.find(reset.snapshot.head + "|reset: moving to HEAD") ==
          0,
      "System Git could not read the native HEAD reflog entry.");

  const std::vector<std::string> nativeReflogs =
      harmony_git::ListReflogs(
          repository.string(),
          &configError);
  Require(configError.empty(), configError);
  Require(
      JoinLines(nativeReflogs) ==
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " reflog list"),
      "Native reflog list disagrees with system Git.");
  Require(
      harmony_git::ReflogExists(
          repository.string(),
          "refs/heads/main",
          &configError),
      "Native reflog exists did not find an exact branch ref.");
  Require(configError.empty(), configError);
  Require(
      !harmony_git::ReflogExists(
          repository.string(),
          "main",
          &configError),
      "Native reflog exists incorrectly applied DWIM to a short ref.");
  Require(configError.empty(), configError);

  const std::string zeroObjectId(40, '0');
  RunGit(repository, "config core.logAllRefUpdates false");
  harmony_git::RepositoryOperation writeReflog =
      harmony_git::WriteReflog(
          repository.string(),
          "refs/heads/manual-native",
          zeroObjectId,
          baselineHead,
          " first\n\tmanual  entry ");
  Require(writeReflog.success, writeReflog.error);
  Require(
      fs::is_regular_file(
          repository / ".git/logs/refs/heads/manual-native"),
      "Native reflog write respected core.logAllRefUpdates=false.");
  RunGit(repository, "config core.logAllRefUpdates true");
  writeReflog = harmony_git::WriteReflog(
      repository.string(),
      "refs/heads/manual-native",
      baselineHead,
      nativeCommitId,
      "second manual entry");
  Require(writeReflog.success, writeReflog.error);
  writeReflog = harmony_git::WriteReflog(
      repository.string(),
      "refs/heads/manual-native",
      nativeCommitId,
      featureCommit.snapshot.head,
      "third manual entry");
  Require(writeReflog.success, writeReflog.error);
  const fs::path nativeRef =
      repository / ".git/refs/heads/manual-native";
  const fs::path systemRef =
      repository / ".git/refs/heads/manual-system";
  const fs::path nativeLog =
      repository / ".git/logs/refs/heads/manual-native";
  const fs::path systemLog =
      repository / ".git/logs/refs/heads/manual-system";
  WriteFile(nativeRef, featureCommit.snapshot.head + "\n");
  Require(
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " reflog show --format='%gs' refs/heads/manual-native") ==
          "third manual entry\n"
          "second manual entry\n"
          "first manual entry\n",
      "System Git could not read normalized native reflog writes.");
  Require(
      !harmony_git::WriteReflog(
          repository.string(),
          "manual-short",
          zeroObjectId,
          baselineHead,
          "invalid ref").success,
      "Native reflog write accepted an unqualified ref.");
  Require(
      !harmony_git::WriteReflog(
          repository.string(),
          "refs/heads/manual-invalid",
          baselineHead.substr(0, 8),
          baselineHead,
          "invalid object").success,
      "Native reflog write accepted an abbreviated object ID.");

  WriteFile(systemRef, featureCommit.snapshot.head + "\n");
  WriteFile(systemLog, ReadFile(nativeLog));
  WriteFile(nativeRef, baselineHead + "\n");
  WriteFile(systemRef, baselineHead + "\n");

  const std::string logBeforeDryRun = ReadFile(nativeLog);
  const harmony_git::RepositoryOperation dryRunDelete =
      harmony_git::DeleteReflogEntries(
          repository.string(),
          {"refs/heads/manual-native@{0}"},
          false,
          false,
          true,
          true);
  Require(dryRunDelete.success, dryRunDelete.error);
  Require(
      ReadFile(nativeLog) == logBeforeDryRun,
      "Native reflog delete --dry-run modified the reflog.");
  Require(
      JoinLines(dryRunDelete.output) ==
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " reflog delete --dry-run --verbose "
              "refs/heads/manual-system@{0}"),
      "Native reflog delete verbose dry-run output disagrees with system Git.");

  const harmony_git::RepositoryOperation nativeDelete =
      harmony_git::DeleteReflogEntries(
          repository.string(),
          {"refs/heads/manual-native@{1}"},
          true,
          true,
          false,
          false);
  Require(nativeDelete.success, nativeDelete.error);
  RunGit(
      repository,
      "reflog delete --rewrite --updateref "
      "refs/heads/manual-system@{1}");
  Require(
      ReadFile(nativeLog) == ReadFile(systemLog),
      "Native reflog delete --rewrite disagrees with system Git.\nNATIVE:\n" +
          ReadFile(nativeLog) + "SYSTEM:\n" + ReadFile(systemLog));
  Require(
      ReadFile(nativeRef) == ReadFile(systemRef),
      "Native reflog delete --updateref disagrees with system Git.");
  Require(
      TrimLineEnding(ReadFile(nativeRef)) == featureCommit.snapshot.head,
      "Native reflog delete --updateref did not follow the new reflog tip.");

  const harmony_git::RepositoryOperation nativeDrop =
      harmony_git::DropReflogs(
          repository.string(),
          {"manual-native", "manual-system"},
          false,
          false);
  Require(nativeDrop.success, nativeDrop.error);
  Require(
      !fs::exists(nativeLog) && !fs::exists(systemLog),
      "Native reflog drop did not remove all requested reflogs.");

  const fs::path linkedWorktree = root / "config reflog linked worktree";
  RunGit(repository, "branch reflog-linked");
  RunGit(
      repository,
      "worktree add " + ShellQuote(linkedWorktree) + " reflog-linked");
  RunGit(
      repository,
      "update-ref --create-reflog "
      "refs/worktree/main " + baselineHead);
  RunGit(
      linkedWorktree,
      "update-ref --create-reflog "
      "refs/worktree/linked " + featureCommit.snapshot.head);
  const std::vector<std::string> linkedReflogs =
      harmony_git::ListReflogs(
          linkedWorktree.string(),
          &configError);
  Require(configError.empty(), configError);
  Require(
      JoinLines(linkedReflogs) ==
          RunCapture(
              "git -C " + ShellQuote(linkedWorktree) +
              " reflog list"),
      "Native linked-worktree reflog list disagrees with system Git.");
  const fs::path linkedGitDirectory = TrimLineEnding(
      RunCapture(
          "git -C " + ShellQuote(linkedWorktree) +
          " rev-parse --absolute-git-dir"));
  Require(
      fs::is_regular_file(
          repository / ".git/logs/refs/worktree/main"),
      "System Git did not create the main-worktree ref log fixture.");
  Require(
      fs::is_regular_file(
          linkedGitDirectory / "logs/refs/worktree/linked"),
      "System Git did not create the linked-worktree ref log fixture.");
  const harmony_git::RepositoryOperation dropSingleWorktree =
      harmony_git::DropReflogs(
          linkedWorktree.string(),
          {},
          true,
          true);
  Require(dropSingleWorktree.success, dropSingleWorktree.error);
  Require(
      fs::is_regular_file(repository / ".git/logs/HEAD"),
      "Linked-worktree reflog drop removed the main HEAD reflog.");
  Require(
      fs::is_regular_file(
          repository / ".git/logs/refs/worktree/main"),
      "Linked-worktree reflog drop removed a main-worktree ref log.");
  Require(
      !fs::exists(linkedGitDirectory / "logs/HEAD") &&
          !fs::exists(
              linkedGitDirectory / "logs/refs/worktree/linked"),
      "Linked-worktree reflog drop left current-worktree logs behind.");
  Require(
      harmony_git::ListReflogs(
          linkedWorktree.string(),
          &configError).empty(),
      "Linked-worktree reflog drop left shared or current logs visible.");
  Require(configError.empty(), configError);

  const harmony_git::RepositoryOperation dropAll =
      harmony_git::DropReflogs(
          repository.string(),
          {},
          true,
          false);
  Require(dropAll.success, dropAll.error);
  Require(
      harmony_git::ListReflogs(
          repository.string(),
          &configError).empty(),
      "Native reflog drop --all left reflogs behind.");
  Require(configError.empty(), configError);
  Require(
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " reflog list").empty(),
      "System Git still sees reflogs after native drop --all.");
}

void TestReflogWalkOptions(const fs::path& root) {
  const fs::path repository = root / "reflog walk repository";
  Run(
      "git -c init.defaultBranch=main init " + ShellQuote(repository) +
      " >/dev/null 2>&1");
  RunGit(repository, "config user.name 'Harmony Reflog Test'");
  RunGit(repository, "config user.email 'reflog@example.invalid'");
  const auto commit =
      [&repository](const std::string& date, const std::string& message) {
        Run(
            "GIT_AUTHOR_DATE='" + date + "' "
            "GIT_COMMITTER_DATE='" + date + "' "
            "git -C " + ShellQuote(repository) +
            " commit --allow-empty -m '" + message +
            "' >/dev/null 2>&1");
      };
  commit("2020-01-01T12:00:00+0000", "reflog 2020");
  commit("2021-01-01T12:00:00+0000", "reflog 2021");
  commit("2022-01-01T12:00:00+0000", "reflog 2022");

  const auto read =
      [&repository](
          const std::string& ref,
          uint32_t maxCount,
          uint32_t skip,
          const std::string& since,
          const std::string& until) {
        std::string error;
        const std::vector<harmony_git::ReflogEntry> entries =
            harmony_git::ReadReflog(
                repository.string(),
                ref,
                maxCount,
                skip,
                since,
                until,
                &error);
        Require(error.empty(), error);
        return entries;
      };
  const auto reflogLines =
      [](const std::vector<harmony_git::ReflogEntry>& entries) {
        std::string output;
        for (const harmony_git::ReflogEntry& entry : entries) {
          output += entry.newId + "|" + entry.selector + "|" +
              entry.message + "\n";
        }
        return output;
      };
  const auto commitLines =
      [](const std::vector<harmony_git::ReflogEntry>& entries) {
        std::string output;
        for (const harmony_git::ReflogEntry& entry : entries) {
          const size_t separator = entry.commitTimestamp.find(' ');
          const std::string seconds = separator == std::string::npos
              ? entry.commitTimestamp
              : entry.commitTimestamp.substr(0, separator);
          output += entry.newId + "|" + entry.subject + "|" +
              entry.author + "|" + seconds + "\n";
        }
        return output;
      };

  const std::vector<harmony_git::ReflogEntry> all =
      read("HEAD", 100, 0, "", "");
  Require(
      reflogLines(all) == RunCapture(
          "git -C " + ShellQuote(repository) +
          " reflog show --format='%H|%gD|%gs' HEAD"),
      "Native reflog selectors disagree with system Git.");
  Require(
      commitLines(all) == RunCapture(
          "git -C " + ShellQuote(repository) +
          " reflog show --format='%H|%s|%an <%ae>|%ct' HEAD"),
      "Native reflog commit metadata disagrees with system Git.");

  const std::vector<harmony_git::ReflogEntry> indexed =
      read("HEAD@{1}", 100, 0, "", "");
  Require(
      reflogLines(indexed) == RunCapture(
          "git -C " + ShellQuote(repository) +
          " reflog show --format='%H|%gD|%gs' 'HEAD@{1}'"),
      "Native numeric reflog selector disagrees with system Git.");

  const std::vector<harmony_git::ReflogEntry> dated =
      read("HEAD@{2021-06-01}", 100, 0, "", "");
  Require(
      reflogLines(dated) == RunCapture(
          "git -C " + ShellQuote(repository) +
          " reflog show --format='%H|%gD|%gs' 'HEAD@{2021-06-01}'"),
      "Native date reflog selector disagrees with system Git.");

  const std::vector<harmony_git::ReflogEntry> filtered =
      read(
          "HEAD",
          100,
          1,
          "@1577836800",
          "@1672531199");
  Require(
      reflogLines(filtered) == RunCapture(
          "git -C " + ShellQuote(repository) +
          " reflog show --skip=1 --since=@1577836800 "
          "--until=@1672531199 --format='%H|%gD|%gs' HEAD"),
      "Native reflog skip and date filters disagree with system Git.");
}

void TestReflogExpire(const fs::path& root) {
  const fs::path repository = root / "reflog expire repository";
  Run(
      "git -c init.defaultBranch=main init " + ShellQuote(repository) +
      " >/dev/null 2>&1");
  RunGit(repository, "config user.name 'Harmony Expire Test'");
  RunGit(repository, "config user.email 'expire@example.invalid'");
  WriteFile(repository / "README.md", "expire baseline\n");
  RunGit(repository, "add .");
  RunGit(repository, "commit -m baseline");
  const std::string baselineHead = TrimLineEnding(
      RunCapture(
          "git -C " + ShellQuote(repository) + " rev-parse HEAD"));
  WriteFile(repository / "README.md", "expire second\n");
  RunGit(repository, "add .");
  RunGit(repository, "commit -m second");
  const std::string secondHead = TrimLineEnding(
      RunCapture(
          "git -C " + ShellQuote(repository) + " rev-parse HEAD"));
  WriteFile(repository / "README.md", "expire third\n");
  RunGit(repository, "add .");
  RunGit(repository, "commit -m third");
  const std::string thirdHead = TrimLineEnding(
      RunCapture(
          "git -C " + ShellQuote(repository) + " rev-parse HEAD"));
  const std::string zeroObjectId(40, '0');
  const std::string fixture =
      ReflogFixtureLine(
          zeroObjectId,
          baselineHead,
          1500000000,
          "expire: first") +
      ReflogFixtureLine(
          baselineHead,
          secondHead,
          1600000000,
          "expire: second") +
      ReflogFixtureLine(
          secondHead,
          thirdHead,
          1700000000,
          "expire: third");

  const auto writeRefAndLog =
      [&repository, &fixture, &thirdHead](
          const std::string& name) {
        WriteFile(
            repository / ".git/refs/heads" / name,
            thirdHead + "\n");
        WriteFile(
            repository / ".git/logs/refs/heads" / name,
            fixture);
      };
  writeRefAndLog("expire-native");
  writeRefAndLog("expire-system");
  const harmony_git::RepositoryOperation nativeDryRun =
      harmony_git::ExpireReflogs(
          repository.string(),
          {"refs/heads/expire-native"},
          "@1650000000",
          "never",
          true,
          true,
          false,
          true,
          true,
          false,
          false);
  Require(nativeDryRun.success, nativeDryRun.error);
  const std::string systemDryRun =
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " reflog expire --expire=@1650000000 "
          "--expire-unreachable=never --rewrite --updateref "
          "--dry-run --verbose refs/heads/expire-system");
  Require(
      JoinLines(nativeDryRun.output) == systemDryRun,
      "Native reflog expire dry-run output disagrees with system Git.\n"
      "NATIVE:\n" + JoinLines(nativeDryRun.output) +
      "SYSTEM:\n" + systemDryRun);
  Require(
      ReadFile(repository / ".git/logs/refs/heads/expire-native") == fixture,
      "Native reflog expire --dry-run modified the reflog.");

  const harmony_git::RepositoryOperation nativeExpire =
      harmony_git::ExpireReflogs(
          repository.string(),
          {"refs/heads/expire-native"},
          "@1650000000",
          "never",
          true,
          true,
          false,
          false,
          false,
          false,
          false);
  Require(nativeExpire.success, nativeExpire.error);
  RunGit(
      repository,
      "reflog expire --expire=@1650000000 "
      "--expire-unreachable=never --rewrite --updateref "
      "refs/heads/expire-system");
  Require(
      ReadFile(
          repository / ".git/logs/refs/heads/expire-native") ==
          ReadFile(
              repository / ".git/logs/refs/heads/expire-system"),
      "Native reflog expire rewrite disagrees with system Git.");
  Require(
      ReadFile(repository / ".git/refs/heads/expire-native") ==
          ReadFile(repository / ".git/refs/heads/expire-system"),
      "Native reflog expire --updateref disagrees with system Git.");
  Require(
      TrimLineEnding(
          ReadFile(repository / ".git/refs/heads/expire-native")) ==
          thirdHead,
      "Native reflog expire --updateref did not preserve the new tip.");
  Require(nativeExpire.changedCount == 2,
          "Native reflog expire changed count is incorrect.");

  const std::string unreachableFixture =
      ReflogFixtureLine(
          zeroObjectId,
          secondHead,
          1500000000,
          "unreachable: old") +
      ReflogFixtureLine(
          secondHead,
          baselineHead,
          1600000000,
          "unreachable: mixed") +
      ReflogFixtureLine(
          baselineHead,
          baselineHead,
          1700000000,
          "unreachable: keep");
  const auto writeUnreachableRefAndLog =
      [&repository, &unreachableFixture, &baselineHead](
          const std::string& name) {
        WriteFile(
            repository / ".git/refs/heads" / name,
            baselineHead + "\n");
        WriteFile(
            repository / ".git/logs/refs/heads" / name,
            unreachableFixture);
      };
  writeUnreachableRefAndLog("unreachable-native");
  writeUnreachableRefAndLog("unreachable-system");
  const harmony_git::RepositoryOperation nativeUnreachable =
      harmony_git::ExpireReflogs(
          repository.string(),
          {"refs/heads/unreachable-native"},
          "never",
          "@1650000000",
          false,
          false,
          false,
          false,
          false,
          false,
          false);
  Require(nativeUnreachable.success, nativeUnreachable.error);
  RunGit(
      repository,
      "reflog expire --expire=never "
      "--expire-unreachable=@1650000000 refs/heads/unreachable-system");
  Require(
      ReadFile(
          repository / ".git/logs/refs/heads/unreachable-native") ==
          ReadFile(
              repository / ".git/logs/refs/heads/unreachable-system"),
      "Native reflog expire-unreachable disagrees with system Git.");

  const std::string missingObject(40, 'a');
  const std::string staleFixture =
      ReflogFixtureLine(
          zeroObjectId,
          missingObject,
          1700000000,
          "stale: broken") +
      ReflogFixtureLine(
          missingObject,
          baselineHead,
          1700000001,
          "stale: recovery");
  const auto writeStaleRefAndLog =
      [&repository, &staleFixture, &baselineHead](
          const std::string& name) {
        WriteFile(
            repository / ".git/refs/heads" / name,
            baselineHead + "\n");
        WriteFile(
            repository / ".git/logs/refs/heads" / name,
            staleFixture);
      };
  writeStaleRefAndLog("stale-native");
  writeStaleRefAndLog("stale-system");
  const harmony_git::RepositoryOperation nativeStale =
      harmony_git::ExpireReflogs(
          repository.string(),
          {"refs/heads/stale-native"},
          "never",
          "never",
          false,
          false,
          true,
          false,
          true,
          false,
          false);
  Require(nativeStale.success, nativeStale.error);
  const std::string systemStale =
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " reflog expire --expire=never "
          "--expire-unreachable=never --stale-fix --verbose "
          "refs/heads/stale-system");
  Require(
      JoinLines(nativeStale.output) == systemStale,
      "Native reflog expire --stale-fix output disagrees with system Git.");
  Require(
      ReadFile(repository / ".git/logs/refs/heads/stale-native") ==
          ReadFile(repository / ".git/logs/refs/heads/stale-system"),
      "Native reflog expire --stale-fix disagrees with system Git.");

  const fs::path linkedWorktree = root / "reflog expire linked worktree";
  RunGit(repository, "branch expire-linked");
  RunGit(
      repository,
      "worktree add " + ShellQuote(linkedWorktree) + " expire-linked");
  const harmony_git::RepositoryOperation nativeLinkedDryRun =
      harmony_git::ExpireReflogs(
          linkedWorktree.string(),
          {},
          "all",
          "never",
          false,
          false,
          false,
          true,
          true,
          true,
          true);
  Require(nativeLinkedDryRun.success, nativeLinkedDryRun.error);
  const std::string systemLinkedDryRun =
      RunCapture(
          "git -C " + ShellQuote(linkedWorktree) +
          " reflog expire --expire=all "
          "--expire-unreachable=never --dry-run --verbose "
          "--all --single-worktree");
  Require(
      JoinLines(nativeLinkedDryRun.output) == systemLinkedDryRun,
      "Native linked-worktree reflog expire scope disagrees with system Git.");
}

void TestBranchAndRemoteManagement(const fs::path& root) {
  const fs::path repository = root / "branch remote repository";
  const fs::path linkedWorktree = root / "branch remote linked worktree";
  const fs::path checkedOutTarget = root / "branch remote target worktree";
  Run(
      "git -c init.defaultBranch=main init " + ShellQuote(repository) +
      " >/dev/null 2>&1");
  RunGit(repository, "config user.name 'Harmony Branch Remote Test'");
  RunGit(repository, "config user.email 'branch-remote@example.invalid'");
  WriteFile(repository / "README.md", "branch remote baseline\n");
  RunGit(repository, "add .");
  RunGit(repository, "commit -m baseline");
  const std::string baselineHead = TrimLineEnding(
      RunCapture(
          "git -C " + ShellQuote(repository) + " rev-parse HEAD"));

  harmony_git::RepositoryOperation added =
      harmony_git::AddRemote(
          repository.string(),
          "origin",
          "https://example.invalid/origin.git");
  Require(added.success, added.error);
  Require(
      harmony_git::GetRemoteUrl(
          repository.string(),
          "origin",
          false,
          &added.error) == "https://example.invalid/origin.git",
      "Native remote add did not expose its fetch URL.");
  Require(
      harmony_git::GetRemoteUrl(
          repository.string(),
          "origin",
          true,
          &added.error) == "https://example.invalid/origin.git",
      "Native remote push URL fallback is incorrect.");
  harmony_git::RepositoryOperation pushUrl =
      harmony_git::SetRemoteUrl(
          repository.string(),
          "origin",
          "ssh://example.invalid/origin.git",
          true);
  Require(pushUrl.success, pushUrl.error);
  Require(
      harmony_git::GetRemoteUrl(
          repository.string(),
          "origin",
          true,
          &added.error) == "ssh://example.invalid/origin.git",
      "Native remote set-url --push did not add pushurl.");
  harmony_git::RepositoryOperation fetchUrl =
      harmony_git::SetRemoteUrl(
          repository.string(),
          "origin",
          "https://example.invalid/updated-origin.git",
          false);
  Require(fetchUrl.success, fetchUrl.error);
  Require(
      harmony_git::GetRemoteUrl(
          repository.string(),
          "origin",
          false,
          &added.error) == "https://example.invalid/updated-origin.git",
      "Native remote set-url did not update url.");

  harmony_git::RepositoryOperation dottedRemote =
      harmony_git::AddRemote(
          repository.string(),
          "Release.Team",
          "https://example.invalid/release.git");
  Require(dottedRemote.success, dottedRemote.error);
  const harmony_git::RepositorySnapshot remoteSnapshot =
      harmony_git::InspectRepository(repository.string());
  Require(remoteSnapshot.valid, remoteSnapshot.error);
  Require(
      Contains(
          [&remoteSnapshot]() {
            std::vector<std::string> names;
            for (const harmony_git::Remote& remote : remoteSnapshot.remotes) {
              names.push_back(remote.name);
            }
            return names;
          }(),
          "Release.Team"),
      "Native remote reader lost a remote name containing a dot.");

  RunGit(repository, "branch feature/old");
  RunGit(
      repository,
      "config branch.feature/old.remote origin");
  RunGit(
      repository,
      "config branch.feature/old.merge refs/heads/feature/old");
  Run(
      "git -C " + ShellQuote(repository) +
      " update-ref --create-reflog -m setup refs/remotes/origin/main " +
      baselineHead);
  Run(
      "git -C " + ShellQuote(repository) +
      " symbolic-ref refs/remotes/origin/HEAD refs/remotes/origin/main");
  RunGit(repository, "pack-refs --all --prune");
  Run(
      "git -C " + ShellQuote(repository) +
      " worktree add " + ShellQuote(linkedWorktree) +
      " feature/old >/dev/null 2>&1");

  harmony_git::RepositoryOperation moved =
      harmony_git::MoveBranch(
          repository.string(),
          "feature/old",
          "feature/moved",
          false);
  Require(moved.success, moved.error);
  Require(
      RunCapture(
          "git -C " + ShellQuote(linkedWorktree) + " symbolic-ref HEAD") ==
          "refs/heads/feature/moved\n",
      "Native branch rename did not update the linked worktree HEAD.");
  Require(
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " config --local --get branch.feature/moved.remote") ==
          "origin\n",
      "Native branch rename did not move branch remote config.");
  Require(
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " config --local --get branch.feature/moved.merge") ==
          "refs/heads/feature/old\n",
      "Native branch rename unexpectedly changed branch merge config.");
  std::string reflogError;
  const std::vector<harmony_git::ReflogEntry> movedLog =
      harmony_git::ReadReflog(
          repository.string(),
          "feature/moved",
          10,
          &reflogError);
  Require(reflogError.empty(), reflogError);
  Require(
      FindReflog(
          movedLog,
          "Branch: renamed refs/heads/feature/old to refs/heads/feature/moved") !=
          nullptr,
      "Native branch rename reflog message is missing.");

  harmony_git::RepositoryOperation copied =
      harmony_git::CopyBranch(
          repository.string(),
          "feature/moved",
          "feature/copied",
          false);
  Require(copied.success, copied.error);
  Require(
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " rev-parse refs/heads/feature/copied") == baselineHead + "\n",
      "Native branch copy did not preserve the branch tip.");
  Require(
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " config --local --get branch.feature/copied.remote") ==
          "origin\n",
      "Native branch copy did not copy branch remote config.");
  const std::vector<harmony_git::ReflogEntry> copiedLog =
      harmony_git::ReadReflog(
          repository.string(),
          "feature/copied",
          10,
          &reflogError);
  Require(reflogError.empty(), reflogError);
  Require(
      FindReflog(
          copiedLog,
          "Branch: copied refs/heads/feature/moved to refs/heads/feature/copied") !=
          nullptr,
      "Native branch copy reflog message is missing.");

  RunGit(repository, "branch feature/target");
  harmony_git::RepositoryOperation copyWithoutForce =
      harmony_git::CopyBranch(
          repository.string(),
          "feature/moved",
          "feature/target",
          false);
  Require(
      !copyWithoutForce.success,
      "Native branch copy should reject an existing target without force.");
  harmony_git::RepositoryOperation copyForced =
      harmony_git::CopyBranch(
          repository.string(),
          "feature/moved",
          "feature/target",
          true);
  Require(copyForced.success, copyForced.error);
  Require(
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " rev-parse refs/heads/feature/target") == baselineHead + "\n",
      "Native forced branch copy did not replace the target.");

  harmony_git::RepositoryOperation sourceBranch =
      harmony_git::CreateBranch(
          repository.string(),
          "feature/source",
          false);
  Require(sourceBranch.success, sourceBranch.error);
  Run(
      "git -C " + ShellQuote(repository) +
      " worktree add " + ShellQuote(checkedOutTarget) +
      " feature/target >/dev/null 2>&1");
  harmony_git::RepositoryOperation renameCheckedOutTarget =
      harmony_git::MoveBranch(
          repository.string(),
          "feature/source",
          "feature/target",
          true);
  Require(
      !renameCheckedOutTarget.success,
      "Native forced branch rename should reject a linked-worktree target.");

  harmony_git::RepositoryOperation renamedRemote =
      harmony_git::RenameRemote(
          repository.string(),
          "origin",
          "upstream");
  Require(renamedRemote.success, renamedRemote.error);
  Require(
      harmony_git::GetRemoteUrl(
          repository.string(),
          "upstream",
          false,
          &reflogError) == "https://example.invalid/updated-origin.git",
      "Native remote rename did not preserve the fetch URL.");
  Require(
      harmony_git::GetRemoteUrl(
          repository.string(),
          "upstream",
          true,
          &reflogError) == "ssh://example.invalid/origin.git",
      "Native remote rename did not preserve the push URL.");
  Require(
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " rev-parse refs/remotes/upstream/main") == baselineHead + "\n",
      "Native remote rename did not rewrite packed remote refs.");
  Require(
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " symbolic-ref refs/remotes/upstream/HEAD") ==
          "refs/remotes/upstream/main\n",
      "Native remote rename did not rewrite the remote symbolic HEAD.");
  Require(
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " config --local --get branch.feature/moved.remote") ==
          "upstream\n",
      "Native remote rename did not update branch remote config.");
  const std::vector<harmony_git::ReflogEntry> remoteLog =
      harmony_git::ReadReflog(
          repository.string(),
          "refs/remotes/upstream/main",
          10,
          &reflogError);
  Require(reflogError.empty(), reflogError);
  Require(
      !remoteLog.empty(),
      "Native remote rename did not preserve remote reflog entries.");

  harmony_git::RepositoryOperation removed =
      harmony_git::RemoveRemote(
          repository.string(),
          "upstream");
  Require(removed.success, removed.error);
  Require(
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " config --local --get branch.feature/moved.remote "
          "2>/dev/null || true").empty(),
      "Native remote remove did not delete associated branch config.");
  Require(
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " show-ref --verify refs/remotes/upstream/main "
          "2>/dev/null || true").empty(),
      "Native remote remove did not delete remote refs.");
  Require(
      !fs::exists(
          repository / ".git/logs/refs/remotes/upstream/main"),
      "Native remote remove did not delete the remote reflog.");
  harmony_git::RepositoryOperation removedDotted =
      harmony_git::RemoveRemote(
          repository.string(),
          "Release.Team");
  Require(removedDotted.success, removedDotted.error);
}

void TestSourceRestoreAndForcedCheckout(const fs::path& root) {
  const fs::path repository = root / "source restore repository";
  Run(
      "git -c init.defaultBranch=main init " + ShellQuote(repository) +
      " >/dev/null 2>&1");
  RunGit(repository, "config user.name 'Harmony Restore Test'");
  RunGit(repository, "config user.email 'restore@example.invalid'");

  WriteFile(repository / "README.md", "baseline\n");
  WriteFile(repository / "legacy.txt", "legacy\n");
  RunGit(repository, "add .");
  RunGit(repository, "commit -m baseline");
  const std::string baselineHead = TrimLineEnding(
      RunCapture(
          "git -C " + ShellQuote(repository) + " rev-parse HEAD"));

  WriteFile(repository / "README.md", "main revision\n");
  fs::remove(repository / "legacy.txt");
  WriteFile(repository / "modern.txt", "modern\n");
  RunGit(repository, "add -A");
  RunGit(repository, "commit -m main-revision");
  const std::string mainHead = TrimLineEnding(
      RunCapture(
          "git -C " + ShellQuote(repository) + " rev-parse HEAD"));

  harmony_git::RepositoryOperation invalidRevision =
      harmony_git::RestoreFromSource(
          repository.string(),
          "HEAD~1invalid",
          {"README.md"},
          false,
          true);
  Require(
      !invalidRevision.success,
      "Malformed first-parent revision was accepted.");

  harmony_git::RepositoryOperation worktreeRestore =
      harmony_git::RestoreFromSource(
          repository.string(),
          "HEAD~",
          {"README.md"},
          false,
          true);
  Require(worktreeRestore.success, worktreeRestore.error);
  Require(
      RunCapture("cat " + ShellQuote(repository / "README.md")) ==
          "baseline\n",
      "Source restore did not update the working tree.");
  Require(
      FindStatus(worktreeRestore.snapshot, "README.md") != nullptr &&
          FindStatus(worktreeRestore.snapshot, "README.md")->indexState == " " &&
          FindStatus(worktreeRestore.snapshot, "README.md")->workTreeState == "M",
      "Working-tree source restore changed the index.");

  harmony_git::RepositoryOperation combinedRestore =
      harmony_git::RestoreFromSource(
          repository.string(),
          baselineHead,
          {"."},
          true,
          true);
  Require(combinedRestore.success, combinedRestore.error);
  Require(
      RunCapture("cat " + ShellQuote(repository / "README.md")) ==
          "baseline\n",
      "Combined source restore did not restore file content.");
  Require(
      fs::exists(repository / "legacy.txt") &&
          !fs::exists(repository / "modern.txt"),
      "Combined source restore did not add and remove source paths.");
  Require(
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " diff --cached --name-status") ==
          "M\tREADME.md\nA\tlegacy.txt\nD\tmodern.txt\n",
      "Combined source restore does not agree with system Git.");

  harmony_git::RepositoryOperation reset =
      harmony_git::ResetHard(repository.string());
  Require(reset.success, reset.error);
  harmony_git::RepositoryOperation checkout =
      harmony_git::CheckoutBranch(
          repository.string(),
          "feature/reset",
          "HEAD~1");
  Require(checkout.success, checkout.error);
  Require(
      checkout.snapshot.branch == "feature/reset" &&
          checkout.snapshot.head == baselineHead,
      "Forced checkout did not create the branch at the requested source.");
  Require(
      RunCapture("cat " + ShellQuote(repository / "README.md")) ==
          "baseline\n" &&
          fs::exists(repository / "legacy.txt") &&
          !fs::exists(repository / "modern.txt"),
      "Forced checkout did not materialize the requested source tree.");

  harmony_git::RepositoryOperation resetExisting =
      harmony_git::CheckoutBranch(
          repository.string(),
          "feature/reset",
          "main");
  Require(resetExisting.success, resetExisting.error);
  Require(
      resetExisting.snapshot.branch == "feature/reset" &&
          resetExisting.snapshot.head == mainHead,
      "Forced checkout did not reset the existing branch.");
  Require(
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " rev-parse feature/reset") ==
          mainHead + "\n",
      "System Git did not observe the reset branch reference.");
  Require(
      RunCapture("cat " + ShellQuote(repository / "README.md")) ==
          "main revision\n" &&
          !fs::exists(repository / "legacy.txt") &&
          fs::exists(repository / "modern.txt"),
      "Reset branch checkout did not materialize the target tree.");
}

void TestIndexV4(const fs::path& root) {
  const fs::path repository = root / "index v4 repository";
  Run(
      "git -c init.defaultBranch=main init " + ShellQuote(repository) +
      " >/dev/null 2>&1");
  RunGit(repository, "config user.name 'Harmony Index Test'");
  RunGit(repository, "config user.email 'index@example.invalid'");

  const std::string longAlphaPath =
      "long/" + std::string(140, 'a') + "/alpha.txt";
  const std::string longBetaPath =
      "long/" + std::string(140, 'b') + "/beta.txt";
  WriteFile(repository / fs::path(longAlphaPath), "long alpha baseline\n");
  WriteFile(repository / fs::path(longBetaPath), "long beta baseline\n");
  WriteFile(repository / "src/components/alpha.txt", "alpha baseline\n");
  WriteFile(repository / "src/components/alpine.txt", "alpine baseline\n");
  WriteFile(repository / "src/components/beta.txt", "beta baseline\n");
  WriteFile(repository / "src/config/application.txt", "config baseline\n");
  RunGit(repository, "add .");
  RunGit(repository, "commit -m baseline");

  WriteFile(repository / "src/components/alpine.txt", "alpine staged v4\n");
  RunGit(repository, "add src/components/alpine.txt");
  RunGit(repository, "update-index --index-version 4");
  Require(
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " update-index --show-index-version") == "4\n",
      "System Git did not create an index v4 fixture.");

  WriteFile(repository / fs::path(longBetaPath), "long beta unstaged v4\n");
  WriteFile(repository / "src/components/beta.txt", "beta unstaged v4\n");
  WriteFile(repository / "src/components/gamma.txt", "gamma untracked\n");
  const harmony_git::RepositorySnapshot snapshot =
      harmony_git::InspectRepository(repository.string());
  Require(snapshot.valid, snapshot.error);
  Require(snapshot.indexVersion == 4, "Native reader did not report index v4.");
  const harmony_git::FileStatus* staged =
      FindStatus(snapshot, "src/components/alpine.txt");
  Require(
      staged != nullptr && staged->indexState == "M" &&
          staged->workTreeState == " ",
      "Native status did not read the staged index v4 entry.");
  const harmony_git::FileStatus* unstaged =
      FindStatus(snapshot, "src/components/beta.txt");
  Require(
      unstaged != nullptr && unstaged->indexState == " " &&
          unstaged->workTreeState == "M",
      "Native status did not read the unstaged index v4 entry.");
  const harmony_git::FileStatus* longUnstaged =
      FindStatus(snapshot, longBetaPath);
  Require(
      longUnstaged != nullptr && longUnstaged->indexState == " " &&
          longUnstaged->workTreeState == "M",
      "Native status did not decode a multibyte index v4 strip length.");
  Require(
      FindStatus(snapshot, "src/components/gamma.txt") != nullptr &&
          !FindStatus(snapshot, "src/components/gamma.txt")->tracked,
      "Native status did not preserve untracked discovery with index v4.");

  std::string diffError;
  const std::string stagedDiff =
      harmony_git::DiffRepository(repository.string(), true, &diffError);
  Require(diffError.empty(), diffError);
  Require(
      stagedDiff.find("alpine staged v4") != std::string::npos,
      "Native staged diff did not read index v4.");
  const std::string unstagedDiff =
      harmony_git::DiffRepository(repository.string(), false, &diffError);
  Require(diffError.empty(), diffError);
  Require(
      unstagedDiff.find("beta unstaged v4") != std::string::npos &&
          unstagedDiff.find("long beta unstaged v4") != std::string::npos,
      "Native working-tree diff did not read index v4.");

  const harmony_git::RepositoryOperation nativeStage =
      harmony_git::StageRepository(
          repository.string(),
          {"src/components/beta.txt"});
  Require(nativeStage.success, nativeStage.error);
  Require(nativeStage.changedCount == 1, "Native index v4 add count is incorrect.");
  Require(
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " diff --cached --name-status") ==
          "M\tsrc/components/alpine.txt\n"
          "M\tsrc/components/beta.txt\n",
      "Native add from index v4 does not agree with system Git.");
  Require(
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " update-index --show-index-version") == "2\n",
      "Native index rewrite did not normalize the index to version 2.");
}

std::string PackedFixtureContent(int revision) {
  std::string content;
  content.reserve(96U * 1024U);
  for (int line = 0; line < 4096; ++line) {
    content += "stable line ";
    content += std::to_string(line);
    content += " with enough repeated content for pack deltas\n";
  }
  content += "revision ";
  content += std::to_string(revision);
  content += "\n";
  return content;
}

void TestPackedObjects(const fs::path& root) {
  const fs::path repository = root / "packed repository";
  Run(
      "git -c init.defaultBranch=main init " + ShellQuote(repository) +
      " >/dev/null 2>&1");
  RunGit(repository, "config user.name 'Harmony Packed Test'");
  RunGit(repository, "config user.email 'packed@example.invalid'");
  for (int revision = 0; revision < 16; ++revision) {
    WriteFile(
        repository / "large.txt",
        PackedFixtureContent(revision));
    WriteFile(
        repository / "docs/history.txt",
        "history revision " + std::to_string(revision) + "\n");
    RunGit(repository, "add .");
    RunGit(
        repository,
        "commit -m packed-" + std::to_string(revision));
  }
  RunGit(repository, "repack -adf --window=50 --depth=50");
  const std::string countObjects =
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " count-objects -v");
  Require(
      countObjects.find("count: 0") != std::string::npos,
      "Packed fixture still has loose objects.");

  std::string packedObject;
  int packedDistance = -1;
  for (int distance = 0; distance < 16; ++distance) {
    const std::string objectId =
        RunCapture(
            "git -C " + ShellQuote(repository) +
            " rev-parse HEAD~" + std::to_string(distance) +
            ":large.txt");
    const std::string verifyLine =
        RunCapture(
            "git verify-pack -v " +
            ShellQuote(repository / ".git/objects/pack") +
            "/*.idx 2>/dev/null | "
            "grep '^" + objectId.substr(0, 40) + " ' || true");
    size_t spaces = 0;
    for (char character : verifyLine) {
      if (character == ' ') {
        ++spaces;
      }
    }
    if (spaces >= 6) {
      packedObject = objectId.substr(0, 40);
      packedDistance = distance;
      break;
    }
  }
  Require(
      !packedObject.empty(),
      "Packed fixture did not produce a delta-compressed blob.");

  RunGit(
      repository,
      "checkout --detach HEAD~" + std::to_string(packedDistance));
  const harmony_git::RepositorySnapshot clean =
      harmony_git::InspectRepository(repository.string());
  Require(clean.valid, clean.error);
  Require(clean.detached, "Packed fixture checkout should be detached.");

  WriteFile(repository / "large.txt", "packed working-tree change\n");
  std::string diffError;
  const std::string workingTreeDiff =
      harmony_git::DiffRepository(repository.string(), false, &diffError);
  Require(diffError.empty(), diffError);
  Require(
      workingTreeDiff.find("packed working-tree change") != std::string::npos &&
          workingTreeDiff.find("stable line") != std::string::npos,
      "Native diff did not read the packed blob.");

  harmony_git::RepositoryOperation staged =
      harmony_git::StageRepository(repository.string(), {"large.txt"});
  Require(staged.success, staged.error);
  const std::string stagedDiff =
      harmony_git::DiffRepository(repository.string(), true, &diffError);
  Require(diffError.empty(), diffError);
  Require(
      stagedDiff.find("packed working-tree change") != std::string::npos,
      "Native staged diff did not read the packed commit tree.");

  std::string logError;
  const std::vector<harmony_git::Commit> log =
      harmony_git::ReadLog(repository.string(), 20, &logError);
  Require(logError.empty(), logError);
  Require(
      log.size() == static_cast<size_t>(16 - packedDistance) &&
          log[0].subject ==
              "packed-" + std::to_string(15 - packedDistance),
      "Native log did not resolve packed commit history.");
}

void TestCleanRepository(const fs::path& root) {
  const fs::path repository = root / "clean repository";
  const fs::path globalIgnore = root / "clean global.ignore";
  Run(
      "git -c init.defaultBranch=main init " + ShellQuote(repository) +
      " >/dev/null 2>&1");
  RunGit(repository, "config user.name 'Harmony Clean Test'");
  RunGit(repository, "config user.email 'clean@example.invalid'");
  RunGit(
      repository,
      "config core.excludesFile " + ShellQuote(globalIgnore));
  WriteFile(globalIgnore, "*.global\n");
  WriteFile(
      repository / ".gitignore",
      "*.log\n"
      "ignored-dir/\n");
  WriteFile(repository / "README.md", "tracked\n");
  WriteFile(repository / "tracked/keep.txt", "keep\n");
  RunGit(repository, "add .");
  RunGit(repository, "commit -m baseline");
  WriteFile(repository / "visible.txt", "visible\n");
  WriteFile(repository / "plain-dir/inside.txt", "plain directory\n");
  WriteFile(repository / "ignored.log", "ignored\n");
  WriteFile(repository / "ignored-dir/inside.txt", "ignored directory\n");
  WriteFile(repository / "local-exclude.txt", "local exclude\n");
  WriteFile(repository / "global-file.global", "global exclude\n");
  WriteFile(repository / "command-keep.txt", "command exclude\n");
  WriteFile(
      repository / ".git/info/exclude",
      "local-exclude.txt\n");
  const fs::path nestedRepository = repository / "nested-repository";
  Run(
      "git init -q " + ShellQuote(nestedRepository) +
      " >/dev/null 2>&1");
  WriteFile(nestedRepository / "nested.txt", "nested\n");

  const auto formatResult =
      [](const harmony_git::CleanResult& result, bool dryRun) {
        std::string output;
        for (const std::string& path : result.cleanedPaths) {
          output += dryRun ? "Would remove " : "Removing ";
          output += path;
          output.push_back('\n');
        }
        for (const std::string& path : result.skippedRepositories) {
          output += dryRun
              ? "Would skip repository "
              : "Skipping repository ";
          output += path;
          output.push_back('\n');
        }
        return output;
      };
  const auto systemClean =
      [](const fs::path& startPath, const std::string& arguments) {
        return RunCapture(
            "git -C " + ShellQuote(startPath) +
            " clean " + arguments);
      };

  harmony_git::CleanOptions options;
  options.dryRun = true;
  harmony_git::CleanResult dryRun =
      harmony_git::CleanRepository(repository.string(), options);
  Require(dryRun.success, dryRun.error);
  const std::string nativeDefaultDryRun =
      formatResult(dryRun, true);
  const std::string systemDefaultDryRun =
      systemClean(repository, "-n");
  Require(
      nativeDefaultDryRun == systemDefaultDryRun,
      "Native git clean dry-run does not agree with system Git.\n"
      "Native:\n" + nativeDefaultDryRun +
      "System:\n" + systemDefaultDryRun);
  Require(
      Contains(dryRun.cleanedPaths, "visible.txt") &&
          !Contains(dryRun.cleanedPaths, "plain-dir/"),
      "Native git clean default directory selection is incorrect.");

  options.dryRun = false;
  harmony_git::CleanResult refused =
      harmony_git::CleanRepository(repository.string(), options);
  Require(
      !refused.success &&
          refused.error.find("clean.requireForce") != std::string::npos &&
          fs::exists(repository / "visible.txt"),
      "Native git clean should require force before deleting files.");

  RunGit(repository, "config clean.requireForce false");
  harmony_git::CleanResult allowed =
      harmony_git::CleanRepository(repository.string(), options);
  Require(allowed.success, allowed.error);
  Require(
      !fs::exists(repository / "visible.txt") &&
          fs::exists(repository / "plain-dir/inside.txt"),
      "clean.requireForce=false did not allow file-only cleanup.");

  options = {};
  options.dryRun = true;
  options.directories = true;
  options.force = 1;
  harmony_git::CleanResult directoryDryRun =
      harmony_git::CleanRepository(repository.string(), options);
  Require(directoryDryRun.success, directoryDryRun.error);
  harmony_git::CleanResult directoryRemovals = directoryDryRun;
  directoryRemovals.skippedRepositories.clear();
  const std::string nativeDirectoryDryRun =
      formatResult(directoryRemovals, true);
  const std::string systemDirectoryDryRun =
      systemClean(repository, "-ndf");
  Require(
      nativeDirectoryDryRun == systemDirectoryDryRun,
      "Native git clean -d dry-run does not agree with system Git.\n"
      "Native:\n" + nativeDirectoryDryRun +
      "System:\n" + systemDirectoryDryRun);
  Require(
      Contains(directoryDryRun.cleanedPaths, "plain-dir/") &&
          Contains(
              directoryDryRun.skippedRepositories,
              "nested-repository/"),
      "Native git clean did not protect the nested repository.");

  options.dryRun = false;
  harmony_git::CleanResult removedDirectory =
      harmony_git::CleanRepository(repository.string(), options);
  Require(removedDirectory.success, removedDirectory.error);
  Require(
      !fs::exists(repository / "plain-dir") &&
          fs::exists(nestedRepository),
      "Native git clean -d removed the wrong directories.");

  options.dryRun = true;
  options.force = 2;
  harmony_git::CleanResult nestedDryRun =
      harmony_git::CleanRepository(repository.string(), options);
  Require(nestedDryRun.success, nestedDryRun.error);
  Require(
      formatResult(nestedDryRun, true) ==
          systemClean(repository, "-ndff"),
      "Native double-force nested-repository cleanup disagrees with system Git.");
  Require(
      Contains(nestedDryRun.cleanedPaths, "nested-repository/") &&
          nestedDryRun.skippedRepositories.empty(),
      "Native double-force clean did not select the nested repository.");

  options.dryRun = false;
  harmony_git::CleanResult removedNested =
      harmony_git::CleanRepository(repository.string(), options);
  Require(removedNested.success, removedNested.error);
  Require(
      !fs::exists(nestedRepository),
      "Native double-force clean did not remove the nested repository.");

  WriteFile(repository / "visible-again.txt", "visible\n");
  WriteFile(repository / "ignored-again.log", "ignored\n");
  WriteFile(repository / "ignored-dir/again.txt", "ignored directory\n");
  WriteFile(repository / "local-again.txt", "local exclude\n");
  WriteFile(repository / "global-again.global", "global exclude\n");
  options = {};
  options.dryRun = true;
  options.directories = true;
  options.ignoredOnly = true;
  options.force = 1;
  harmony_git::CleanResult ignoredDryRun =
      harmony_git::CleanRepository(repository.string(), options);
  Require(ignoredDryRun.success, ignoredDryRun.error);
  Require(
      formatResult(ignoredDryRun, true) ==
          systemClean(repository, "-ndfX"),
      "Native git clean -X dry-run does not agree with system Git.");
  Require(
      Contains(ignoredDryRun.cleanedPaths, "ignored-again.log") &&
          Contains(ignoredDryRun.cleanedPaths, "ignored-dir/") &&
          !Contains(ignoredDryRun.cleanedPaths, "visible-again.txt"),
      "Native git clean -X selected the wrong files.");

  options.dryRun = false;
  harmony_git::CleanResult removedIgnored =
      harmony_git::CleanRepository(repository.string(), options);
  Require(removedIgnored.success, removedIgnored.error);
  Require(
      !fs::exists(repository / "ignored-again.log") &&
          !fs::exists(repository / "ignored-dir") &&
          fs::exists(repository / "visible-again.txt"),
      "Native git clean -X removed non-ignored files.");

  WriteFile(repository / "visible-x.txt", "visible\n");
  WriteFile(repository / "ignored-x.log", "ignored\n");
  WriteFile(repository / "ignored-dir/x.txt", "ignored directory\n");
  WriteFile(repository / "local-x.txt", "local exclude\n");
  WriteFile(repository / "global-x.global", "global exclude\n");
  WriteFile(repository / "command-keep.txt", "command exclude\n");
  options = {};
  options.dryRun = true;
  options.directories = true;
  options.removeIgnored = true;
  options.force = 1;
  options.excludes = {"command-keep.txt"};
  harmony_git::CleanResult removeAllDryRun =
      harmony_git::CleanRepository(repository.string(), options);
  Require(removeAllDryRun.success, removeAllDryRun.error);
  Require(
      formatResult(removeAllDryRun, true) ==
          systemClean(
              repository,
              "-ndfx -e " + ShellQuote("command-keep.txt")),
      "Native git clean -x/-e dry-run does not agree with system Git.");
  Require(
      Contains(removeAllDryRun.cleanedPaths, "ignored-x.log") &&
          Contains(removeAllDryRun.cleanedPaths, "local-x.txt") &&
          Contains(removeAllDryRun.cleanedPaths, "global-x.global") &&
          !Contains(removeAllDryRun.cleanedPaths, "command-keep.txt"),
      "Native git clean -x did not apply command excludes last.");

  options.dryRun = false;
  harmony_git::CleanResult removedAll =
      harmony_git::CleanRepository(repository.string(), options);
  Require(removedAll.success, removedAll.error);
  Require(
      !fs::exists(repository / "visible-x.txt") &&
          !fs::exists(repository / "ignored-x.log") &&
          !fs::exists(repository / "ignored-dir") &&
          fs::exists(repository / "command-keep.txt"),
      "Native git clean -x did not remove the expected files.\n"
      "visible-x=" +
          std::to_string(fs::exists(repository / "visible-x.txt")) +
      " ignored-x=" +
          std::to_string(fs::exists(repository / "ignored-x.log")) +
      " ignored-dir=" +
          std::to_string(fs::exists(repository / "ignored-dir")) +
      " command-keep=" +
          std::to_string(fs::exists(repository / "command-keep.txt")));

  const fs::path pathRepository = root / "clean path repository";
  Run(
      "git -c init.defaultBranch=main init " +
          ShellQuote(pathRepository) +
          " >/dev/null 2>&1");
  RunGit(pathRepository, "config user.name 'Harmony Clean Path Test'");
  RunGit(pathRepository, "config user.email 'clean-path@example.invalid'");
  RunGit(pathRepository, "config clean.requireForce false");
  WriteFile(pathRepository / "README.md", "tracked\n");
  RunGit(pathRepository, "add README.md");
  RunGit(pathRepository, "commit -m baseline");
  WriteFile(pathRepository / "src/remove.tmp", "remove\n");
  WriteFile(pathRepository / "src/keep.txt", "keep\n");
  WriteFile(pathRepository / "docs/remove.tmp", "remove\n");
  WriteFile(pathRepository / "docs/absolute.bin", "remove\n");
  WriteFile(pathRepository / "root.tmp", "remove\n");

  options = {};
  options.dryRun = true;
  options.paths = {"*.tmp"};
  harmony_git::CleanResult subdirectoryGlob =
      harmony_git::CleanRepository(
          (pathRepository / "src").string(),
          options);
  Require(subdirectoryGlob.success, subdirectoryGlob.error);
  Require(
      formatResult(subdirectoryGlob, true) ==
          systemClean(
              pathRepository / "src",
              "-n -- '*.tmp'"),
      "Native subdirectory clean glob does not agree with system Git.");
  Require(
      subdirectoryGlob.cleanedPaths.size() == 1 &&
          subdirectoryGlob.cleanedPaths[0] == "remove.tmp",
      "Native subdirectory clean did not use command-relative paths.");

  options.paths = {"src/*.tmp"};
  harmony_git::CleanResult rootGlob =
      harmony_git::CleanRepository(pathRepository.string(), options);
  Require(rootGlob.success, rootGlob.error);
  Require(
      formatResult(rootGlob, true) ==
          systemClean(pathRepository, "-n -- 'src/*.tmp'"),
      "Native root clean glob does not agree with system Git.");

  const fs::path absoluteTarget = pathRepository / "docs/absolute.bin";
  options.paths = {absoluteTarget.string()};
  harmony_git::CleanResult absolutePath =
      harmony_git::CleanRepository(pathRepository.string(), options);
  Require(absolutePath.success, absolutePath.error);
  Require(
      formatResult(absolutePath, true) ==
          systemClean(
              pathRepository,
              "-n -- " + ShellQuote(absoluteTarget)),
      "Native absolute clean path does not agree with system Git.");
}

void TestLargeAndUnusualRepository(const fs::path& root) {
  const fs::path repository = root / "large unusual repository";
  Run(
      "git -c init.defaultBranch=main init " + ShellQuote(repository) +
      " >/dev/null 2>&1");
  RunGit(repository, "config user.name 'Harmony Large Fixture Test'");
  RunGit(
      repository,
      "config user.email 'large-fixture@example.invalid'");

  constexpr int kFileCount = 640;
  for (int index = 0; index < kFileCount; ++index) {
    char path[80] = {};
    std::snprintf(
        path,
        sizeof(path),
        "bulk/segment-%02d/file-%04d.txt",
        index / 40,
        index);
    WriteFile(
        repository / path,
        "fixture " + std::to_string(index) + "\n");
  }
  WriteFile(repository / "-leading-dash.txt", "dash\n");
  WriteFile(
      repository / "folder with spaces/file name with spaces.txt",
      "spaces\n");

  fs::path longRelativePath = "long-path";
  for (int index = 0; index < 7; ++index) {
    longRelativePath /=
        "segment-" + std::string(18, static_cast<char>('a' + index));
  }
  longRelativePath /= "payload-with-a-very-long-name-0001.txt";
  Require(
      longRelativePath.generic_string().size() >= 220U,
      "Long-path fixture is shorter than intended.");
  WriteFile(repository / longRelativePath, "long path\n");

  RunGit(repository, "add .");
  RunGit(repository, "commit -m 'large baseline'");
  for (int index = 1; index <= 4; ++index) {
    WriteFile(
        repository / "history.txt",
        "history " + std::to_string(index) + "\n");
    RunGit(repository, "add history.txt");
    RunGit(
        repository,
        "commit -m 'large fixture history " +
            std::to_string(index) + "'");
    RunGit(
        repository,
        "branch fixture/packed-" + std::to_string(index));
  }
  RunGit(repository, "branch fixture/packed-base HEAD~4");
  RunGit(repository, "pack-refs --all --prune");
  RunGit(repository, "repack -ad");

  Require(
      fs::exists(repository / ".git/packed-refs"),
      "Large fixture did not create packed refs.");
  std::error_code packError;
  uint32_t packFiles = 0;
  for (fs::directory_iterator iterator(
           repository / ".git/objects/pack",
           packError);
       !packError && iterator != fs::directory_iterator();
       iterator.increment(packError)) {
    if (iterator->path().extension() == ".pack") {
      ++packFiles;
    }
  }
  Require(
      !packError && packFiles > 0U,
      "Large fixture did not create a packed object store.");

  WriteFile(
      repository / "bulk/segment-00/file-0000.txt",
      "modified fixture\n");
  fs::remove(repository / "bulk/segment-00/file-0001.txt");
  WriteFile(repository / "scratch-untracked.txt", "untracked\n");

  std::string error;
  harmony_git::ListFilesOptions filesOptions;
  const std::vector<std::string> nativeFiles =
      harmony_git::ReadFiles(
          repository.string(),
          filesOptions,
          &error);
  Require(error.empty(), error);
  Require(
      JoinLines(nativeFiles) ==
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " ls-files"),
      "Native large-repository ls-files does not agree with system Git.");
  Require(
      nativeFiles.size() == static_cast<size_t>(kFileCount + 4),
      "Native large-repository ls-files count is incorrect.");

  harmony_git::ListTreeOptions treeOptions;
  treeOptions.recursive = true;
  treeOptions.nameOnly = true;
  const std::vector<std::string> nativeTree =
      harmony_git::ReadTree(
          repository.string(),
          "HEAD",
          treeOptions,
          &error);
  Require(error.empty(), error);
  Require(
      JoinLines(nativeTree) ==
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " ls-tree -r --name-only HEAD"),
      "Native packed large-tree listing does not agree with system Git.");

  const harmony_git::RepositorySnapshot snapshot =
      harmony_git::InspectRepository(repository.string());
  Require(snapshot.valid, snapshot.error);
  std::string nativeStatus;
  for (const harmony_git::FileStatus& status : snapshot.files) {
    nativeStatus += status.indexState + status.workTreeState + " " +
        status.path + "\n";
  }
  Require(
      nativeStatus ==
          RunCapture(
              "git -c core.quotePath=false -C " +
              ShellQuote(repository) +
              " status --short --untracked-files=all --no-renames"),
      "Native large-repository status does not agree with system Git.");
  Require(
      JoinLines(snapshot.branches) ==
          RunCapture(
              "git -C " + ShellQuote(repository) +
              " for-each-ref --format='%(refname:short)' refs/heads"),
      "Native packed branch listing does not agree with system Git.");

  Require(
      harmony_git::ReadObjectContent(
          repository.string(),
          "HEAD",
          "type",
          &error) == "commit" &&
          error.empty(),
      "Native object reader could not read packed HEAD.");
  Require(
      harmony_git::ReadObjectContent(
          repository.string(),
          "HEAD:" + longRelativePath.generic_string(),
          "blob",
          &error) == "long path\n" &&
          error.empty(),
      "Native object reader could not traverse the packed long path.");
  Require(
      harmony_git::ReadObjectContent(
          repository.string(),
          "HEAD:folder with spaces/file name with spaces.txt",
          "blob",
          &error) == "spaces\n" &&
          error.empty(),
      "Native object reader could not traverse a path with spaces.");
}

void TestIgnoreRules(const fs::path& root) {
  const fs::path repository = root / "ignore repository";
  const fs::path globalIgnore = root / "global.ignore";
  Run(
      "git -c init.defaultBranch=main init " + ShellQuote(repository) +
      " >/dev/null 2>&1");
  RunGit(
      repository,
      "config core.excludesFile " + ShellQuote(globalIgnore));
  WriteFile(globalIgnore, "*.global\n");
  WriteFile(
      repository / ".gitignore",
      "*.log\n"
      "cache/\n"
      "!cache/\n"
      "cache/*.tmp\n");
  WriteFile(
      repository / "docs/.gitignore",
      "*.tmp\n"
      "!keep.tmp\n");
  WriteFile(
      repository / ".git/info/exclude",
      "# local excludes\nexcluded.txt\n");
  WriteFile(repository / "visible.txt", "visible\n");
  WriteFile(repository / "ignored.log", "ignored\n");
  WriteFile(repository / "build.global", "ignored globally\n");
  WriteFile(repository / "global-file.global", "ignored globally\n");
  WriteFile(repository / "cache/drop.tmp", "ignored cache file\n");
  WriteFile(repository / "cache/keep.txt", "visible cache file\n");
  WriteFile(repository / "docs/drop.tmp", "ignored docs file\n");
  WriteFile(repository / "docs/keep.tmp", "visible docs file\n");
  WriteFile(repository / "excluded.txt", "ignored local file\n");

  const harmony_git::RepositorySnapshot snapshot =
      harmony_git::InspectRepository(repository.string());
  Require(snapshot.valid, snapshot.error);
  Require(FindStatus(snapshot, "visible.txt") != nullptr,
          "Visible file was incorrectly ignored.");
  Require(FindStatus(snapshot, "cache/keep.txt") != nullptr,
          "Negated cache directory rule did not work.");
  Require(FindStatus(snapshot, "docs/keep.tmp") != nullptr,
          "Negated nested ignore rule did not work.");
  Require(FindStatus(snapshot, "ignored.log") == nullptr,
          "Root .gitignore rule was not applied.");
  Require(FindStatus(snapshot, "cache/drop.tmp") == nullptr,
          "Nested cache ignore rule was not applied.");
  Require(FindStatus(snapshot, "docs/drop.tmp") == nullptr,
          "Nested .gitignore rule was not applied.");
  Require(FindStatus(snapshot, "excluded.txt") == nullptr,
          "Repository exclude rule was not applied.");
  Require(FindStatus(snapshot, "build.global") == nullptr,
          "Global ignore rule was not applied.");
  Require(FindStatus(snapshot, "global-file.global") == nullptr,
          "Global excludes were not applied.");
  std::string nativeUntracked;
  for (const harmony_git::FileStatus& file : snapshot.files) {
    if (!file.tracked) {
      nativeUntracked += file.path;
      nativeUntracked.push_back('\n');
    }
  }
  const std::string systemUntracked =
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " ls-files --others --exclude-standard");
  Require(
      nativeUntracked == systemUntracked,
      "Native ignore matching does not agree with system Git.");

  const harmony_git::RepositoryOperation staged =
      harmony_git::StageRepository(repository.string(), {"."});
  Require(staged.success, staged.error);
  const std::string indexed =
      RunCapture(
          "git -C " + ShellQuote(repository) +
          " ls-files");
  Require(
      indexed.find("visible.txt") != std::string::npos &&
          indexed.find("cache/keep.txt") != std::string::npos &&
          indexed.find("docs/keep.tmp") != std::string::npos,
      "Native add did not stage visible files.");
  Require(
      indexed.find("ignored.log") == std::string::npos &&
          indexed.find("cache/drop.tmp") == std::string::npos &&
          indexed.find("docs/drop.tmp") == std::string::npos &&
          indexed.find("excluded.txt") == std::string::npos &&
          indexed.find(".global") == std::string::npos,
      "Native add staged ignored files.");
}

}  // namespace

int main() {
  try {
    TemporaryDirectory temporaryDirectory;
    TestRepositoryInspection(temporaryDirectory.path());
    TestWorkspaceFileIO(temporaryDirectory.path());
    TestRemoteAdvertisement();
    TestUploadPackProtocol();
    TestReceivePackProtocol();
    TestReceivePackPack(temporaryDirectory.path());
    TestRemotePackInstallation(temporaryDirectory.path());
    TestLinkedWorktree(temporaryDirectory.path());
    TestRepositoryInitialization(temporaryDirectory.path());
    TestRepositoryOperations(temporaryDirectory.path());
    TestMoveRemoveShowAndTags(temporaryDirectory.path());
    TestListFiles(temporaryDirectory.path());
    TestCatFileAndListTree(temporaryDirectory.path());
    TestHashObjectAndCheckIgnore(temporaryDirectory.path());
    TestReferencePlumbing(temporaryDirectory.path());
    TestCommitGraphPlumbing(temporaryDirectory.path());
    TestRevisionPathAndAncestor(temporaryDirectory.path());
    TestConfigAndReflogs(temporaryDirectory.path());
    TestReflogWalkOptions(temporaryDirectory.path());
    TestReflogExpire(temporaryDirectory.path());
    TestBranchAndRemoteManagement(temporaryDirectory.path());
    TestSourceRestoreAndForcedCheckout(temporaryDirectory.path());
    TestIndexV4(temporaryDirectory.path());
    TestPackedObjects(temporaryDirectory.path());
    TestCleanRepository(temporaryDirectory.path());
    TestLargeAndUnusualRepository(temporaryDirectory.path());
    TestIgnoreRules(temporaryDirectory.path());
    std::cout << "Native repository fixture tests passed.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Native repository fixture test failed: "
              << error.what() << '\n';
    return 1;
  }
}
