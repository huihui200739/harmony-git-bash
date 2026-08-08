#include "git_repository.h"

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

}  // namespace

int main() {
  try {
    TemporaryDirectory temporaryDirectory;
    TestRepositoryInspection(temporaryDirectory.path());
    TestLinkedWorktree(temporaryDirectory.path());
    TestRepositoryInitialization(temporaryDirectory.path());
    TestRepositoryOperations(temporaryDirectory.path());
    std::cout << "Native repository fixture tests passed.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Native repository fixture test failed: "
              << error.what() << '\n';
    return 1;
  }
}
