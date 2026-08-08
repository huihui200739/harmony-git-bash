#include "git_repository.h"

#include <node_api.h>

#include <string>
#include <vector>

namespace {

napi_value CreateString(napi_env env, const std::string& value) {
  napi_value result = nullptr;
  napi_create_string_utf8(env, value.c_str(), value.size(), &result);
  return result;
}

napi_value CreateBoolean(napi_env env, bool value) {
  napi_value result = nullptr;
  napi_get_boolean(env, value, &result);
  return result;
}

napi_value CreateUint32(napi_env env, uint32_t value) {
  napi_value result = nullptr;
  napi_create_uint32(env, value, &result);
  return result;
}

void SetProperty(
    napi_env env,
    napi_value object,
    const char* name,
    napi_value value) {
  napi_set_named_property(env, object, name, value);
}

std::string ReadStringArgument(
    napi_env env,
    napi_callback_info info,
    size_t argumentIndex,
    bool* present) {
  size_t argumentCount = 5;
  napi_value arguments[5] = {
      nullptr,
      nullptr,
      nullptr,
      nullptr,
      nullptr};
  napi_get_cb_info(env, info, &argumentCount, arguments, nullptr, nullptr);
  if (argumentIndex >= argumentCount) {
    *present = false;
    return "";
  }
  napi_valuetype type = napi_undefined;
  napi_typeof(env, arguments[argumentIndex], &type);
  if (type != napi_string) {
    *present = false;
    return "";
  }
  size_t length = 0;
  napi_get_value_string_utf8(
      env,
      arguments[argumentIndex],
      nullptr,
      0,
      &length);
  std::string value(length + 1, '\0');
  napi_get_value_string_utf8(
      env,
      arguments[argumentIndex],
      value.data(),
      length + 1,
      &length);
  value.resize(length);
  *present = true;
  return value;
}

bool ReadBooleanArgument(
    napi_env env,
    napi_callback_info info,
    size_t argumentIndex,
    bool defaultValue) {
  size_t argumentCount = 5;
  napi_value arguments[5] = {
      nullptr,
      nullptr,
      nullptr,
      nullptr,
      nullptr};
  napi_get_cb_info(env, info, &argumentCount, arguments, nullptr, nullptr);
  if (argumentIndex >= argumentCount) {
    return defaultValue;
  }
  napi_valuetype type = napi_undefined;
  napi_typeof(env, arguments[argumentIndex], &type);
  if (type != napi_boolean) {
    return defaultValue;
  }
  bool value = defaultValue;
  napi_get_value_bool(env, arguments[argumentIndex], &value);
  return value;
}

uint32_t ReadUint32Argument(
    napi_env env,
    napi_callback_info info,
    size_t argumentIndex,
    uint32_t defaultValue) {
  size_t argumentCount = 5;
  napi_value arguments[5] = {
      nullptr,
      nullptr,
      nullptr,
      nullptr,
      nullptr};
  napi_get_cb_info(env, info, &argumentCount, arguments, nullptr, nullptr);
  if (argumentIndex >= argumentCount) {
    return defaultValue;
  }
  napi_valuetype type = napi_undefined;
  napi_typeof(env, arguments[argumentIndex], &type);
  if (type != napi_number) {
    return defaultValue;
  }
  uint32_t value = defaultValue;
  napi_get_value_uint32(env, arguments[argumentIndex], &value);
  return value;
}

std::vector<std::string> ReadStringArrayArgument(
    napi_env env,
    napi_callback_info info,
    size_t argumentIndex,
    bool* present) {
  size_t argumentCount = 5;
  napi_value arguments[5] = {
      nullptr,
      nullptr,
      nullptr,
      nullptr,
      nullptr};
  napi_get_cb_info(env, info, &argumentCount, arguments, nullptr, nullptr);
  if (argumentIndex >= argumentCount) {
    *present = false;
    return {};
  }
  bool isArray = false;
  napi_is_array(env, arguments[argumentIndex], &isArray);
  if (!isArray) {
    *present = false;
    return {};
  }
  uint32_t length = 0;
  napi_get_array_length(env, arguments[argumentIndex], &length);
  std::vector<std::string> result;
  result.reserve(length);
  for (uint32_t index = 0; index < length; ++index) {
    napi_value element = nullptr;
    napi_get_element(env, arguments[argumentIndex], index, &element);
    napi_valuetype type = napi_undefined;
    napi_typeof(env, element, &type);
    if (type != napi_string) {
      *present = false;
      return {};
    }
    size_t stringLength = 0;
    napi_get_value_string_utf8(
        env,
        element,
        nullptr,
        0,
        &stringLength);
    std::string value(stringLength + 1, '\0');
    napi_get_value_string_utf8(
        env,
        element,
        value.data(),
        stringLength + 1,
        &stringLength);
    value.resize(stringLength);
    result.push_back(value);
  }
  *present = true;
  return result;
}

napi_value FileStatusToValue(
    napi_env env,
    const harmony_git::FileStatus& status) {
  napi_value result = nullptr;
  napi_create_object(env, &result);
  SetProperty(env, result, "path", CreateString(env, status.path));
  SetProperty(
      env,
      result,
      "indexState",
      CreateString(env, status.indexState));
  SetProperty(
      env,
      result,
      "workTreeState",
      CreateString(env, status.workTreeState));
  SetProperty(env, result, "tracked", CreateBoolean(env, status.tracked));
  SetProperty(env, result, "staged", CreateBoolean(env, status.staged));
  return result;
}

napi_value RemoteToValue(
    napi_env env,
    const harmony_git::Remote& remote) {
  napi_value result = nullptr;
  napi_create_object(env, &result);
  SetProperty(env, result, "name", CreateString(env, remote.name));
  SetProperty(env, result, "fetchUrl", CreateString(env, remote.fetchUrl));
  SetProperty(env, result, "pushUrl", CreateString(env, remote.pushUrl));
  return result;
}

napi_value SnapshotToValue(
    napi_env env,
    const harmony_git::RepositorySnapshot& snapshot) {
  napi_value result = nullptr;
  napi_create_object(env, &result);
  SetProperty(env, result, "valid", CreateBoolean(env, snapshot.valid));
  SetProperty(
      env,
      result,
      "repositoryPath",
      CreateString(env, snapshot.repositoryPath));
  SetProperty(
      env,
      result,
      "gitDirectory",
      CreateString(env, snapshot.gitDirectory));
  SetProperty(env, result, "branch", CreateString(env, snapshot.branch));
  SetProperty(env, result, "head", CreateString(env, snapshot.head));
  SetProperty(env, result, "detached", CreateBoolean(env, snapshot.detached));
  SetProperty(
      env,
      result,
      "indexVersion",
      CreateUint32(env, snapshot.indexVersion));
  SetProperty(env, result, "error", CreateString(env, snapshot.error));

  napi_value files = nullptr;
  napi_create_array_with_length(env, snapshot.files.size(), &files);
  for (size_t index = 0; index < snapshot.files.size(); ++index) {
    napi_set_element(
        env,
        files,
        index,
        FileStatusToValue(env, snapshot.files[index]));
  }
  SetProperty(env, result, "files", files);

  napi_value branches = nullptr;
  napi_create_array_with_length(env, snapshot.branches.size(), &branches);
  for (size_t index = 0; index < snapshot.branches.size(); ++index) {
    napi_set_element(
        env,
        branches,
        index,
        CreateString(env, snapshot.branches[index]));
  }
  SetProperty(env, result, "branches", branches);

  napi_value remotes = nullptr;
  napi_create_array_with_length(env, snapshot.remotes.size(), &remotes);
  for (size_t index = 0; index < snapshot.remotes.size(); ++index) {
    napi_set_element(
        env,
        remotes,
        index,
        RemoteToValue(env, snapshot.remotes[index]));
  }
  SetProperty(env, result, "remotes", remotes);
  return result;
}

napi_value OperationToValue(
    napi_env env,
    const harmony_git::RepositoryOperation& operation) {
  napi_value result = nullptr;
  napi_create_object(env, &result);
  SetProperty(env, result, "success", CreateBoolean(env, operation.success));
  SetProperty(
      env,
      result,
      "changedCount",
      CreateUint32(env, operation.changedCount));
  SetProperty(
      env,
      result,
      "snapshot",
      SnapshotToValue(env, operation.snapshot));
  SetProperty(env, result, "error", CreateString(env, operation.error));
  return result;
}

napi_value CommitToValue(
    napi_env env,
    const harmony_git::Commit& commit) {
  napi_value result = nullptr;
  napi_create_object(env, &result);
  SetProperty(env, result, "id", CreateString(env, commit.id));
  SetProperty(env, result, "subject", CreateString(env, commit.subject));
  SetProperty(env, result, "author", CreateString(env, commit.author));
  SetProperty(env, result, "timestamp", CreateString(env, commit.timestamp));
  return result;
}

napi_value InspectRepository(napi_env env, napi_callback_info info) {
  bool present = false;
  const std::string path = ReadStringArgument(env, info, 0, &present);
  if (!present) {
    napi_throw_type_error(env, nullptr, "inspectRepository expects a path.");
    return nullptr;
  }
  return SnapshotToValue(env, harmony_git::InspectRepository(path));
}

napi_value InitializeRepository(napi_env env, napi_callback_info info) {
  bool present = false;
  const std::string path = ReadStringArgument(env, info, 0, &present);
  if (!present) {
    napi_throw_type_error(env, nullptr, "initializeRepository expects a path.");
    return nullptr;
  }
  const bool seedDemoFiles = ReadBooleanArgument(env, info, 1, false);
  return SnapshotToValue(
      env,
      harmony_git::InitializeRepository(path, seedDemoFiles));
}

napi_value DirectoryExists(napi_env env, napi_callback_info info) {
  bool present = false;
  const std::string path = ReadStringArgument(env, info, 0, &present);
  if (!present) {
    napi_throw_type_error(env, nullptr, "directoryExists expects a path.");
    return nullptr;
  }
  return CreateBoolean(env, harmony_git::DirectoryExists(path));
}

napi_value ListDirectory(napi_env env, napi_callback_info info) {
  bool present = false;
  const std::string path = ReadStringArgument(env, info, 0, &present);
  if (!present) {
    napi_throw_type_error(env, nullptr, "listDirectory expects a path.");
    return nullptr;
  }
  std::string error;
  const std::vector<std::string> entries =
      harmony_git::ListDirectory(path, &error);
  if (!error.empty()) {
    napi_throw_error(env, nullptr, error.c_str());
    return nullptr;
  }
  napi_value result = nullptr;
  napi_create_array_with_length(env, entries.size(), &result);
  for (size_t index = 0; index < entries.size(); ++index) {
    napi_set_element(env, result, index, CreateString(env, entries[index]));
  }
  return result;
}

napi_value StageRepository(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool pathsPresent = false;
  const std::vector<std::string> paths =
      ReadStringArrayArgument(env, info, 1, &pathsPresent);
  if (!pathPresent || !pathsPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "stageRepository expects a path and a string array.");
    return nullptr;
  }
  return OperationToValue(
      env,
      harmony_git::StageRepository(path, paths));
}

napi_value RestoreStaged(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool pathsPresent = false;
  const std::vector<std::string> paths =
      ReadStringArrayArgument(env, info, 1, &pathsPresent);
  if (!pathPresent || !pathsPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "restoreStaged expects a path and a string array.");
    return nullptr;
  }
  return OperationToValue(
      env,
      harmony_git::RestoreStaged(path, paths));
}

napi_value RestoreWorkingTree(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool pathsPresent = false;
  const std::vector<std::string> paths =
      ReadStringArrayArgument(env, info, 1, &pathsPresent);
  if (!pathPresent || !pathsPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "restoreWorkingTree expects a path and a string array.");
    return nullptr;
  }
  return OperationToValue(
      env,
      harmony_git::RestoreWorkingTree(path, paths));
}

napi_value RestoreFromSource(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool sourcePresent = false;
  const std::string source =
      ReadStringArgument(env, info, 1, &sourcePresent);
  bool pathsPresent = false;
  const std::vector<std::string> paths =
      ReadStringArrayArgument(env, info, 2, &pathsPresent);
  const bool staged = ReadBooleanArgument(env, info, 3, false);
  const bool worktree = ReadBooleanArgument(env, info, 4, false);
  if (!pathPresent || !sourcePresent || !pathsPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "restoreFromSource expects a path, source, and string array.");
    return nullptr;
  }
  return OperationToValue(
      env,
      harmony_git::RestoreFromSource(
          path,
          source,
          paths,
          staged,
          worktree));
}

napi_value ResetHard(napi_env env, napi_callback_info info) {
  bool present = false;
  const std::string path = ReadStringArgument(env, info, 0, &present);
  if (!present) {
    napi_throw_type_error(env, nullptr, "resetHard expects a path.");
    return nullptr;
  }
  return OperationToValue(env, harmony_git::ResetHard(path));
}

napi_value CommitRepository(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool messagePresent = false;
  const std::string message =
      ReadStringArgument(env, info, 1, &messagePresent);
  if (!pathPresent || !messagePresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "commitRepository expects a path and message.");
    return nullptr;
  }
  return OperationToValue(
      env,
      harmony_git::CommitRepository(path, message));
}

napi_value CreateBranch(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool namePresent = false;
  const std::string name = ReadStringArgument(env, info, 1, &namePresent);
  const bool checkout = ReadBooleanArgument(env, info, 2, false);
  if (!pathPresent || !namePresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "createBranch expects a path and branch name.");
    return nullptr;
  }
  return OperationToValue(
      env,
      harmony_git::CreateBranch(path, name, checkout));
}

napi_value SwitchBranch(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool namePresent = false;
  const std::string name = ReadStringArgument(env, info, 1, &namePresent);
  if (!pathPresent || !namePresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "switchBranch expects a path and branch name.");
    return nullptr;
  }
  return OperationToValue(
      env,
      harmony_git::SwitchBranch(path, name));
}

napi_value CheckoutBranch(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool namePresent = false;
  const std::string name = ReadStringArgument(env, info, 1, &namePresent);
  bool sourcePresent = false;
  const std::string source = ReadStringArgument(env, info, 2, &sourcePresent);
  if (!pathPresent || !namePresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "checkoutBranch expects a path and branch name.");
    return nullptr;
  }
  return OperationToValue(
      env,
      harmony_git::CheckoutBranch(
          path,
          name,
          sourcePresent ? source : ""));
}

napi_value DeleteBranch(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool namePresent = false;
  const std::string name = ReadStringArgument(env, info, 1, &namePresent);
  const bool force = ReadBooleanArgument(env, info, 2, false);
  if (!pathPresent || !namePresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "deleteBranch expects a path and branch name.");
    return nullptr;
  }
  return OperationToValue(
      env,
      harmony_git::DeleteBranch(path, name, force));
}

napi_value DiffRepository(napi_env env, napi_callback_info info) {
  bool present = false;
  const std::string path = ReadStringArgument(env, info, 0, &present);
  if (!present) {
    napi_throw_type_error(env, nullptr, "diffRepository expects a path.");
    return nullptr;
  }
  const bool staged = ReadBooleanArgument(env, info, 1, false);
  std::string error;
  const std::string diff =
      harmony_git::DiffRepository(path, staged, &error);
  if (!error.empty()) {
    napi_throw_error(env, nullptr, error.c_str());
    return nullptr;
  }
  return CreateString(env, diff);
}

napi_value ReadLog(napi_env env, napi_callback_info info) {
  bool present = false;
  const std::string path = ReadStringArgument(env, info, 0, &present);
  if (!present) {
    napi_throw_type_error(env, nullptr, "readLog expects a path.");
    return nullptr;
  }
  const uint32_t maxCount = ReadUint32Argument(env, info, 1, 100);
  std::string error;
  const std::vector<harmony_git::Commit> commits =
      harmony_git::ReadLog(path, maxCount, &error);
  if (!error.empty()) {
    napi_throw_error(env, nullptr, error.c_str());
    return nullptr;
  }
  napi_value result = nullptr;
  napi_create_array_with_length(env, commits.size(), &result);
  for (size_t index = 0; index < commits.size(); ++index) {
    napi_set_element(
        env,
        result,
        index,
        CommitToValue(env, commits[index]));
  }
  return result;
}

napi_value Initialize(napi_env env, napi_value exports) {
  napi_property_descriptor descriptors[] = {
      {"inspectRepository",
       nullptr,
       InspectRepository,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"initializeRepository",
       nullptr,
       InitializeRepository,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"directoryExists",
       nullptr,
       DirectoryExists,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"listDirectory",
       nullptr,
       ListDirectory,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"stageRepository",
       nullptr,
       StageRepository,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"restoreStaged",
       nullptr,
       RestoreStaged,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"restoreWorkingTree",
       nullptr,
       RestoreWorkingTree,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"restoreFromSource",
       nullptr,
       RestoreFromSource,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"resetHard",
       nullptr,
       ResetHard,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"commitRepository",
       nullptr,
       CommitRepository,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"createBranch",
       nullptr,
       CreateBranch,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"switchBranch",
       nullptr,
       SwitchBranch,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"checkoutBranch",
       nullptr,
       CheckoutBranch,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"deleteBranch",
       nullptr,
       DeleteBranch,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"diffRepository",
       nullptr,
       DiffRepository,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"readLog",
       nullptr,
       ReadLog,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
  };
  napi_define_properties(
      env,
      exports,
      sizeof(descriptors) / sizeof(descriptors[0]),
      descriptors);
  return exports;
}

napi_module gitNativeModule = {
    NAPI_MODULE_VERSION,
    0,
    nullptr,
    Initialize,
    "git_native",
    nullptr,
    {nullptr, nullptr, nullptr, nullptr},
};

}  // namespace

extern "C" __attribute__((constructor)) void RegisterGitNativeModule() {
  napi_module_register(&gitNativeModule);
}
