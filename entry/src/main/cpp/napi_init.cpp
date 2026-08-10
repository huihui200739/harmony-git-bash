#include "git_repository.h"
#include "git_transport.h"

#include <node_api.h>

#include <cstring>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr size_t kMaximumArguments = 16;

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

napi_value CreateArrayBuffer(
    napi_env env,
    const std::string& value) {
  napi_value result = nullptr;
  void* data = nullptr;
  napi_create_arraybuffer(env, value.size(), &data, &result);
  if (data != nullptr && !value.empty()) {
    std::memcpy(data, value.data(), value.size());
  }
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
  size_t argumentCount = kMaximumArguments;
  napi_value arguments[kMaximumArguments] = {
      nullptr,
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
  size_t argumentCount = kMaximumArguments;
  napi_value arguments[kMaximumArguments] = {
      nullptr,
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
  size_t argumentCount = kMaximumArguments;
  napi_value arguments[kMaximumArguments] = {
      nullptr,
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
  size_t argumentCount = kMaximumArguments;
  napi_value arguments[kMaximumArguments] = {
      nullptr,
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

std::string ReadArrayBufferArgument(
    napi_env env,
    napi_callback_info info,
    size_t argumentIndex,
    bool* present) {
  size_t argumentCount = kMaximumArguments;
  napi_value arguments[kMaximumArguments] = {
      nullptr,
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
  bool isArrayBuffer = false;
  napi_is_arraybuffer(
      env,
      arguments[argumentIndex],
      &isArrayBuffer);
  if (!isArrayBuffer) {
    *present = false;
    return "";
  }
  void* data = nullptr;
  size_t length = 0;
  napi_get_arraybuffer_info(
      env,
      arguments[argumentIndex],
      &data,
      &length);
  *present = true;
  return data == nullptr || length == 0
      ? ""
      : std::string(static_cast<const char*>(data), length);
}

harmony_git::RemoteTransportOptions ReadRemoteTransportOptions(
    napi_env env,
    napi_callback_info info) {
  harmony_git::RemoteTransportOptions options;
  options.connectTimeout =
      ReadUint32Argument(env, info, 6, options.connectTimeout);
  options.readTimeout =
      ReadUint32Argument(env, info, 7, options.readTimeout);
  bool proxyModePresent = false;
  const std::string proxyMode =
      ReadStringArgument(env, info, 8, &proxyModePresent);
  if (proxyModePresent) {
    options.proxyMode = proxyMode;
  }
  bool proxyHostPresent = false;
  const std::string proxyHost =
      ReadStringArgument(env, info, 9, &proxyHostPresent);
  if (proxyHostPresent) {
    options.proxyHost = proxyHost;
  }
  options.proxyPort =
      ReadUint32Argument(env, info, 10, options.proxyPort);
  bool exclusionsPresent = false;
  options.proxyExclusions =
      ReadStringArrayArgument(env, info, 11, &exclusionsPresent);
  if (!exclusionsPresent) {
    options.proxyExclusions.clear();
  }
  return options;
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

napi_value RemoteAdvertisementToValue(
    napi_env env,
    const harmony_git::RemoteAdvertisement& advertisement) {
  napi_value result = nullptr;
  napi_create_object(env, &result);
  SetProperty(
      env,
      result,
      "success",
      CreateBoolean(env, advertisement.success));
  SetProperty(
      env,
      result,
      "responseCode",
      CreateUint32(
          env,
          advertisement.responseCode < 0
              ? 0
              : static_cast<uint32_t>(
                    advertisement.responseCode)));
  SetProperty(
      env,
      result,
      "headTarget",
      CreateString(env, advertisement.headTarget));
  napi_value references = nullptr;
  napi_create_array_with_length(
      env,
      advertisement.references.size(),
      &references);
  for (size_t index = 0;
       index < advertisement.references.size();
       ++index) {
    napi_value reference = nullptr;
    napi_create_object(env, &reference);
    SetProperty(
        env,
        reference,
        "objectId",
        CreateString(
            env,
            advertisement.references[index].objectId));
    SetProperty(
        env,
        reference,
        "name",
        CreateString(
            env,
            advertisement.references[index].name));
    napi_set_element(
        env,
        references,
        static_cast<uint32_t>(index),
        reference);
  }
  SetProperty(env, result, "references", references);
  napi_value capabilities = nullptr;
  napi_create_array_with_length(
      env,
      advertisement.capabilities.size(),
      &capabilities);
  for (size_t index = 0;
       index < advertisement.capabilities.size();
       ++index) {
    napi_set_element(
        env,
        capabilities,
        static_cast<uint32_t>(index),
        CreateString(env, advertisement.capabilities[index]));
  }
  SetProperty(env, result, "capabilities", capabilities);
  SetProperty(
      env,
      result,
      "error",
      CreateString(env, advertisement.error));
  return result;
}

napi_value RemotePackResponseToValue(
    napi_env env,
    const harmony_git::RemotePackResponse& response) {
  napi_value result = nullptr;
  napi_create_object(env, &result);
  SetProperty(
      env,
      result,
      "success",
      CreateBoolean(env, response.success));
  SetProperty(
      env,
      result,
      "acknowledged",
      CreateBoolean(env, response.acknowledged));
  SetProperty(
      env,
      result,
      "objectCount",
      CreateUint32(env, response.objectCount));
  SetProperty(
      env,
      result,
      "packData",
      CreateArrayBuffer(env, response.packData));
  SetProperty(
      env,
      result,
      "progress",
      CreateString(env, response.progress));
  SetProperty(
      env,
      result,
      "error",
      CreateString(env, response.error));
  return result;
}

napi_value RemotePushResultToValue(
    napi_env env,
    const harmony_git::RemotePushResult& response) {
  napi_value result = nullptr;
  napi_create_object(env, &result);
  SetProperty(
      env,
      result,
      "success",
      CreateBoolean(env, response.success));
  SetProperty(
      env,
      result,
      "unpacked",
      CreateBoolean(env, response.unpacked));
  napi_value output = nullptr;
  napi_create_array_with_length(
      env,
      response.output.size(),
      &output);
  for (size_t index = 0; index < response.output.size(); ++index) {
    napi_set_element(
        env,
        output,
        static_cast<uint32_t>(index),
        CreateString(env, response.output[index]));
  }
  SetProperty(env, result, "output", output);
  SetProperty(
      env,
      result,
      "error",
      CreateString(env, response.error));
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
  napi_value output = nullptr;
  napi_create_array_with_length(env, operation.output.size(), &output);
  for (size_t index = 0; index < operation.output.size(); ++index) {
    napi_set_element(
        env,
        output,
        index,
        CreateString(env, operation.output[index]));
  }
  SetProperty(env, result, "output", output);
  SetProperty(env, result, "error", CreateString(env, operation.error));
  SetProperty(
      env,
      result,
      "responseCode",
      CreateUint32(
          env,
          operation.responseCode < 0
              ? 0
              : static_cast<uint32_t>(operation.responseCode)));
  return result;
}

napi_value CleanResultToValue(
    napi_env env,
    const harmony_git::CleanResult& clean) {
  napi_value result = nullptr;
  napi_create_object(env, &result);
  SetProperty(env, result, "success", CreateBoolean(env, clean.success));
  SetProperty(
      env,
      result,
      "changedCount",
      CreateUint32(env, clean.changedCount));
  SetProperty(env, result, "error", CreateString(env, clean.error));

  napi_value cleanedPaths = nullptr;
  napi_create_array_with_length(
      env,
      clean.cleanedPaths.size(),
      &cleanedPaths);
  for (size_t index = 0; index < clean.cleanedPaths.size(); ++index) {
    napi_set_element(
        env,
        cleanedPaths,
        index,
        CreateString(env, clean.cleanedPaths[index]));
  }
  SetProperty(env, result, "cleanedPaths", cleanedPaths);

  napi_value skippedRepositories = nullptr;
  napi_create_array_with_length(
      env,
      clean.skippedRepositories.size(),
      &skippedRepositories);
  for (size_t index = 0;
       index < clean.skippedRepositories.size();
       ++index) {
    napi_set_element(
        env,
        skippedRepositories,
        index,
        CreateString(env, clean.skippedRepositories[index]));
  }
  SetProperty(
      env,
      result,
      "skippedRepositories",
      skippedRepositories);
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

napi_value ConfigEntryToValue(
    napi_env env,
    const harmony_git::ConfigEntry& entry) {
  napi_value result = nullptr;
  napi_create_object(env, &result);
  SetProperty(env, result, "key", CreateString(env, entry.key));
  SetProperty(env, result, "value", CreateString(env, entry.value));
  return result;
}

napi_value ReflogEntryToValue(
    napi_env env,
    const harmony_git::ReflogEntry& entry) {
  napi_value result = nullptr;
  napi_create_object(env, &result);
  SetProperty(env, result, "oldId", CreateString(env, entry.oldId));
  SetProperty(env, result, "newId", CreateString(env, entry.newId));
  SetProperty(env, result, "actor", CreateString(env, entry.actor));
  SetProperty(env, result, "timestamp", CreateString(env, entry.timestamp));
  SetProperty(env, result, "message", CreateString(env, entry.message));
  SetProperty(env, result, "index", CreateUint32(env, entry.index));
  SetProperty(env, result, "selector", CreateString(env, entry.selector));
  SetProperty(env, result, "subject", CreateString(env, entry.subject));
  SetProperty(
      env,
      result,
      "author",
      CreateString(env, entry.author));
  SetProperty(
      env,
      result,
      "committer",
      CreateString(env, entry.committer));
  SetProperty(
      env,
      result,
      "authorTimestamp",
      CreateString(env, entry.authorTimestamp));
  SetProperty(
      env,
      result,
      "commitTimestamp",
      CreateString(env, entry.commitTimestamp));
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

napi_value ReadWorkspaceFile(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool filePathPresent = false;
  const std::string filePath =
      ReadStringArgument(env, info, 1, &filePathPresent);
  if (!pathPresent || !filePathPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "readWorkspaceFile expects a path and file path.");
    return nullptr;
  }
  std::string error;
  const std::string content =
      harmony_git::ReadWorkspaceFile(path, filePath, &error);
  if (!error.empty()) {
    napi_throw_error(env, nullptr, error.c_str());
    return nullptr;
  }
  return CreateString(env, content);
}

napi_value WriteWorkspaceFile(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool filePathPresent = false;
  const std::string filePath =
      ReadStringArgument(env, info, 1, &filePathPresent);
  bool contentPresent = false;
  const std::string content =
      ReadStringArgument(env, info, 2, &contentPresent);
  if (!pathPresent || !filePathPresent || !contentPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "writeWorkspaceFile expects a path, file path, and content.");
    return nullptr;
  }
  return OperationToValue(
      env,
      harmony_git::WriteWorkspaceFile(
          path,
          filePath,
          content,
          ReadBooleanArgument(env, info, 3, false)));
}

napi_value CleanRepository(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool excludesPresent = false;
  const std::vector<std::string> excludes =
      ReadStringArrayArgument(env, info, 7, &excludesPresent);
  bool pathsPresent = false;
  const std::vector<std::string> paths =
      ReadStringArrayArgument(env, info, 8, &pathsPresent);
  if (!pathPresent) {
    napi_throw_type_error(env, nullptr, "cleanRepository expects a path.");
    return nullptr;
  }
  harmony_git::CleanOptions options;
  options.dryRun = ReadBooleanArgument(env, info, 1, false);
  options.directories = ReadBooleanArgument(env, info, 2, false);
  options.quiet = ReadBooleanArgument(env, info, 3, false);
  options.removeIgnored = ReadBooleanArgument(env, info, 4, false);
  options.ignoredOnly = ReadBooleanArgument(env, info, 5, false);
  options.force = ReadUint32Argument(env, info, 6, 0);
  options.excludes =
      excludesPresent ? excludes : std::vector<std::string> {};
  options.paths =
      pathsPresent ? paths : std::vector<std::string> {};
  return CleanResultToValue(
      env,
      harmony_git::CleanRepository(path, options));
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

napi_value RemoveRepositoryPaths(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool pathsPresent = false;
  const std::vector<std::string> paths =
      ReadStringArrayArgument(env, info, 1, &pathsPresent);
  const bool cached = ReadBooleanArgument(env, info, 2, false);
  const bool force = ReadBooleanArgument(env, info, 3, false);
  const bool recursive = ReadBooleanArgument(env, info, 4, false);
  if (!pathPresent || !pathsPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "removeRepositoryPaths expects a path and a string array.");
    return nullptr;
  }
  return OperationToValue(
      env,
      harmony_git::RemoveRepositoryPaths(
          path,
          paths,
          cached,
          force,
          recursive));
}

napi_value MoveRepositoryPath(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool sourcePresent = false;
  const std::string source =
      ReadStringArgument(env, info, 1, &sourcePresent);
  bool destinationPresent = false;
  const std::string destination =
      ReadStringArgument(env, info, 2, &destinationPresent);
  const bool force = ReadBooleanArgument(env, info, 3, false);
  if (!pathPresent || !sourcePresent || !destinationPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "moveRepositoryPath expects a path, source, and destination.");
    return nullptr;
  }
  return OperationToValue(
      env,
      harmony_git::MoveRepositoryPath(
          path,
          source,
          destination,
          force));
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

napi_value MoveBranch(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool oldPresent = false;
  const std::string oldName = ReadStringArgument(env, info, 1, &oldPresent);
  bool newPresent = false;
  const std::string newName = ReadStringArgument(env, info, 2, &newPresent);
  const bool force = ReadBooleanArgument(env, info, 3, false);
  if (!pathPresent || !oldPresent || !newPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "moveBranch expects a path, old branch, and new branch.");
    return nullptr;
  }
  return OperationToValue(
      env,
      harmony_git::MoveBranch(path, oldName, newName, force));
}

napi_value CopyBranch(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool oldPresent = false;
  const std::string oldName = ReadStringArgument(env, info, 1, &oldPresent);
  bool newPresent = false;
  const std::string newName = ReadStringArgument(env, info, 2, &newPresent);
  const bool force = ReadBooleanArgument(env, info, 3, false);
  if (!pathPresent || !oldPresent || !newPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "copyBranch expects a path, old branch, and new branch.");
    return nullptr;
  }
  return OperationToValue(
      env,
      harmony_git::CopyBranch(path, oldName, newName, force));
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

napi_value ShowRevision(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool revisionPresent = false;
  const std::string revision =
      ReadStringArgument(env, info, 1, &revisionPresent);
  const bool statOnly = ReadBooleanArgument(env, info, 2, false);
  const bool oneLine = ReadBooleanArgument(env, info, 3, false);
  bool pathsPresent = false;
  const std::vector<std::string> paths =
      ReadStringArrayArgument(env, info, 4, &pathsPresent);
  if (!pathPresent) {
    napi_throw_type_error(env, nullptr, "showRevision expects a path.");
    return nullptr;
  }
  std::string error;
  const std::string output = harmony_git::ShowRevision(
      path,
      revisionPresent ? revision : "",
      statOnly,
      oneLine,
      pathsPresent ? paths : std::vector<std::string> {},
      &error);
  if (!error.empty()) {
    napi_throw_error(env, nullptr, error.c_str());
    return nullptr;
  }
  return CreateString(env, output);
}

napi_value ReadTags(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool patternsPresent = false;
  const std::vector<std::string> patterns =
      ReadStringArrayArgument(env, info, 1, &patternsPresent);
  if (!pathPresent) {
    napi_throw_type_error(env, nullptr, "readTags expects a path.");
    return nullptr;
  }
  std::string error;
  const std::vector<std::string> tags =
      harmony_git::ReadTags(
          path,
          patternsPresent
              ? patterns
              : std::vector<std::string> {},
          &error);
  if (!error.empty()) {
    napi_throw_error(env, nullptr, error.c_str());
    return nullptr;
  }
  napi_value result = nullptr;
  napi_create_array_with_length(env, tags.size(), &result);
  for (size_t index = 0; index < tags.size(); ++index) {
    napi_set_element(env, result, index, CreateString(env, tags[index]));
  }
  return result;
}

napi_value ReadFiles(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool pathsPresent = false;
  const std::vector<std::string> paths =
      ReadStringArrayArgument(env, info, 9, &pathsPresent);
  if (!pathPresent) {
    napi_throw_type_error(env, nullptr, "readFiles expects a path.");
    return nullptr;
  }
  harmony_git::ListFilesOptions options;
  options.cached = ReadBooleanArgument(env, info, 1, false);
  options.modified = ReadBooleanArgument(env, info, 2, false);
  options.deleted = ReadBooleanArgument(env, info, 3, false);
  options.others = ReadBooleanArgument(env, info, 4, false);
  options.ignored = ReadBooleanArgument(env, info, 5, false);
  options.excludeStandard = ReadBooleanArgument(env, info, 6, false);
  options.stage = ReadBooleanArgument(env, info, 7, false);
  options.fullName = ReadBooleanArgument(env, info, 8, false);
  options.paths =
      pathsPresent ? paths : std::vector<std::string> {};
  std::string error;
  const std::vector<std::string> files =
      harmony_git::ReadFiles(path, options, &error);
  if (!error.empty()) {
    napi_throw_error(env, nullptr, error.c_str());
    return nullptr;
  }
  napi_value result = nullptr;
  napi_create_array_with_length(env, files.size(), &result);
  for (size_t index = 0; index < files.size(); ++index) {
    napi_set_element(env, result, index, CreateString(env, files[index]));
  }
  return result;
}

napi_value HashFiles(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool pathsPresent = false;
  const std::vector<std::string> paths =
      ReadStringArrayArgument(env, info, 1, &pathsPresent);
  bool typePresent = false;
  const std::string type =
      ReadStringArgument(env, info, 2, &typePresent);
  const bool write = ReadBooleanArgument(env, info, 3, false);
  if (!pathPresent || !pathsPresent || !typePresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "hashFiles expects a repository path, paths, and object type.");
    return nullptr;
  }
  std::string error;
  const std::vector<std::string> objectIds =
      harmony_git::HashFiles(path, paths, type, write, &error);
  if (!error.empty()) {
    napi_throw_error(env, nullptr, error.c_str());
    return nullptr;
  }
  napi_value result = nullptr;
  napi_create_array_with_length(env, objectIds.size(), &result);
  for (size_t index = 0; index < objectIds.size(); ++index) {
    napi_set_element(
        env,
        result,
        index,
        CreateString(env, objectIds[index]));
  }
  return result;
}

napi_value HashInput(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool inputPresent = false;
  const std::string input =
      ReadStringArgument(env, info, 1, &inputPresent);
  bool typePresent = false;
  const std::string type =
      ReadStringArgument(env, info, 2, &typePresent);
  const bool write = ReadBooleanArgument(env, info, 3, false);
  if (!pathPresent || !inputPresent || !typePresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "hashInput expects a repository path, input, and object type.");
    return nullptr;
  }
  std::string error;
  const std::string objectId =
      harmony_git::HashInput(path, input, type, write, &error);
  if (!error.empty()) {
    napi_throw_error(env, nullptr, error.c_str());
    return nullptr;
  }
  return CreateString(env, objectId);
}

napi_value CheckIgnored(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool pathsPresent = false;
  const std::vector<std::string> paths =
      ReadStringArrayArgument(env, info, 1, &pathsPresent);
  const bool noIndex = ReadBooleanArgument(env, info, 2, false);
  const bool verbose = ReadBooleanArgument(env, info, 3, false);
  if (!pathPresent || !pathsPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "checkIgnored expects a repository path and paths.");
    return nullptr;
  }
  std::string error;
  const std::vector<std::string> lines =
      harmony_git::CheckIgnored(
          path,
          paths,
          noIndex,
          verbose,
          &error);
  if (!error.empty()) {
    napi_throw_error(env, nullptr, error.c_str());
    return nullptr;
  }
  napi_value result = nullptr;
  napi_create_array_with_length(env, lines.size(), &result);
  for (size_t index = 0; index < lines.size(); ++index) {
    napi_set_element(env, result, index, CreateString(env, lines[index]));
  }
  return result;
}

napi_value ReadObjectContent(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool objectPresent = false;
  const std::string objectName =
      ReadStringArgument(env, info, 1, &objectPresent);
  bool modePresent = false;
  const std::string mode =
      ReadStringArgument(env, info, 2, &modePresent);
  if (!pathPresent || !objectPresent || !modePresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "readObjectContent expects a path, object name, and mode.");
    return nullptr;
  }
  std::string error;
  const std::string output =
      harmony_git::ReadObjectContent(
          path,
          objectName,
          mode,
          &error);
  if (!error.empty()) {
    napi_throw_error(env, nullptr, error.c_str());
    return nullptr;
  }
  return CreateString(env, output);
}

napi_value ReadTree(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool treeishPresent = false;
  const std::string treeish =
      ReadStringArgument(env, info, 1, &treeishPresent);
  bool pathsPresent = false;
  const std::vector<std::string> paths =
      ReadStringArrayArgument(env, info, 10, &pathsPresent);
  if (!pathPresent || !treeishPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "readTree expects a path and tree-ish.");
    return nullptr;
  }
  harmony_git::ListTreeOptions options;
  options.recursive = ReadBooleanArgument(env, info, 2, false);
  options.directoriesOnly = ReadBooleanArgument(env, info, 3, false);
  options.includeTrees = ReadBooleanArgument(env, info, 4, false);
  options.nameOnly = ReadBooleanArgument(env, info, 5, false);
  options.objectOnly = ReadBooleanArgument(env, info, 6, false);
  options.longFormat = ReadBooleanArgument(env, info, 7, false);
  options.fullName = ReadBooleanArgument(env, info, 8, false);
  options.fullTree = ReadBooleanArgument(env, info, 9, false);
  options.paths =
      pathsPresent ? paths : std::vector<std::string> {};
  std::string error;
  const std::vector<std::string> lines =
      harmony_git::ReadTree(path, treeish, options, &error);
  if (!error.empty()) {
    napi_throw_error(env, nullptr, error.c_str());
    return nullptr;
  }
  napi_value result = nullptr;
  napi_create_array_with_length(env, lines.size(), &result);
  for (size_t index = 0; index < lines.size(); ++index) {
    napi_set_element(env, result, index, CreateString(env, lines[index]));
  }
  return result;
}

napi_value ReadReferences(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool patternsPresent = false;
  const std::vector<std::string> patterns =
      ReadStringArrayArgument(env, info, 9, &patternsPresent);
  if (!pathPresent) {
    napi_throw_type_error(env, nullptr, "readReferences expects a path.");
    return nullptr;
  }
  harmony_git::ShowRefOptions options;
  options.heads = ReadBooleanArgument(env, info, 1, false);
  options.tags = ReadBooleanArgument(env, info, 2, false);
  options.includeHead = ReadBooleanArgument(env, info, 3, false);
  options.dereference = ReadBooleanArgument(env, info, 4, false);
  options.verify = ReadBooleanArgument(env, info, 5, false);
  options.quiet = ReadBooleanArgument(env, info, 6, false);
  options.hashOnly = ReadBooleanArgument(env, info, 7, false);
  options.abbreviation = ReadUint32Argument(env, info, 8, 40);
  options.patterns =
      patternsPresent ? patterns : std::vector<std::string> {};
  std::string error;
  const std::vector<std::string> references =
      harmony_git::ReadReferences(path, options, &error);
  if (!error.empty()) {
    napi_throw_error(env, nullptr, error.c_str());
    return nullptr;
  }
  napi_value result = nullptr;
  napi_create_array_with_length(env, references.size(), &result);
  for (size_t index = 0; index < references.size(); ++index) {
    napi_set_element(
        env,
        result,
        index,
        CreateString(env, references[index]));
  }
  return result;
}

napi_value ExcludeExistingReferences(
    napi_env env,
    napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool inputPresent = false;
  const std::string input =
      ReadStringArgument(env, info, 1, &inputPresent);
  bool patternPresent = false;
  const std::string pattern =
      ReadStringArgument(env, info, 2, &patternPresent);
  if (!pathPresent || !inputPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "excludeExistingReferences expects a path and input.");
    return nullptr;
  }
  std::string error;
  const std::vector<std::string> lines =
      harmony_git::ExcludeExistingReferences(
          path,
          input,
          patternPresent ? pattern : "",
          &error);
  if (!error.empty()) {
    napi_throw_error(env, nullptr, error.c_str());
    return nullptr;
  }
  napi_value result = nullptr;
  napi_create_array_with_length(env, lines.size(), &result);
  for (size_t index = 0; index < lines.size(); ++index) {
    napi_set_element(env, result, index, CreateString(env, lines[index]));
  }
  return result;
}

napi_value ReadRevisionList(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool revisionsPresent = false;
  const std::vector<std::string> revisions =
      ReadStringArrayArgument(env, info, 14, &revisionsPresent);
  bool pathsPresent = false;
  const std::vector<std::string> paths =
      ReadStringArrayArgument(env, info, 15, &pathsPresent);
  if (!pathPresent) {
    napi_throw_type_error(env, nullptr, "readRevisionList expects a path.");
    return nullptr;
  }
  harmony_git::RevListOptions options;
  options.all = ReadBooleanArgument(env, info, 1, false);
  options.branches = ReadBooleanArgument(env, info, 2, false);
  options.tags = ReadBooleanArgument(env, info, 3, false);
  options.remotes = ReadBooleanArgument(env, info, 4, false);
  options.parents = ReadBooleanArgument(env, info, 5, false);
  options.count = ReadBooleanArgument(env, info, 6, false);
  options.reverse = ReadBooleanArgument(env, info, 7, false);
  options.firstParent = ReadBooleanArgument(env, info, 8, false);
  options.noMerges = ReadBooleanArgument(env, info, 9, false);
  options.merges = ReadBooleanArgument(env, info, 10, false);
  options.abbreviate = ReadBooleanArgument(env, info, 11, false);
  options.abbreviation = ReadUint32Argument(env, info, 12, 7);
  options.maxCount = ReadUint32Argument(
      env,
      info,
      13,
      std::numeric_limits<uint32_t>::max());
  options.revisions =
      revisionsPresent ? revisions : std::vector<std::string> {};
  options.paths =
      pathsPresent ? paths : std::vector<std::string> {};
  std::string error;
  const std::vector<std::string> lines =
      harmony_git::ReadRevisionList(path, options, &error);
  if (!error.empty()) {
    napi_throw_error(env, nullptr, error.c_str());
    return nullptr;
  }
  napi_value result = nullptr;
  napi_create_array_with_length(env, lines.size(), &result);
  for (size_t index = 0; index < lines.size(); ++index) {
    napi_set_element(env, result, index, CreateString(env, lines[index]));
  }
  return result;
}

napi_value IsAncestor(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool ancestorPresent = false;
  const std::string ancestor =
      ReadStringArgument(env, info, 1, &ancestorPresent);
  bool descendantPresent = false;
  const std::string descendant =
      ReadStringArgument(env, info, 2, &descendantPresent);
  if (!pathPresent || !ancestorPresent || !descendantPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "isAncestor expects a path, ancestor, and descendant.");
    return nullptr;
  }
  std::string error;
  const bool result = harmony_git::IsAncestorRevision(
      path,
      ancestor,
      descendant,
      &error);
  if (!error.empty()) {
    napi_throw_error(env, nullptr, error.c_str());
    return nullptr;
  }
  return CreateBoolean(env, result);
}

napi_value FindForkPoint(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool referencePresent = false;
  const std::string reference =
      ReadStringArgument(env, info, 1, &referencePresent);
  bool derivedPresent = false;
  const std::string derived =
      ReadStringArgument(env, info, 2, &derivedPresent);
  if (!pathPresent || !referencePresent || !derivedPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "findForkPoint expects a path, reference, and derived commit.");
    return nullptr;
  }
  std::string error;
  const std::string result = harmony_git::FindForkPointRevision(
      path,
      reference,
      derived,
      &error);
  if (!error.empty()) {
    napi_throw_error(env, nullptr, error.c_str());
    return nullptr;
  }
  return CreateString(env, result);
}

napi_value ReadMergeBases(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool revisionsPresent = false;
  const std::vector<std::string> revisions =
      ReadStringArrayArgument(env, info, 4, &revisionsPresent);
  if (!pathPresent || !revisionsPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "readMergeBases expects a path and revisions.");
    return nullptr;
  }
  harmony_git::MergeBaseOptions options;
  options.all = ReadBooleanArgument(env, info, 1, false);
  options.octopus = ReadBooleanArgument(env, info, 2, false);
  options.independent = ReadBooleanArgument(env, info, 3, false);
  options.revisions = revisions;
  std::string error;
  const std::vector<std::string> lines =
      harmony_git::ReadMergeBases(path, options, &error);
  if (!error.empty()) {
    napi_throw_error(env, nullptr, error.c_str());
    return nullptr;
  }
  napi_value result = nullptr;
  napi_create_array_with_length(env, lines.size(), &result);
  for (size_t index = 0; index < lines.size(); ++index) {
    napi_set_element(env, result, index, CreateString(env, lines[index]));
  }
  return result;
}

napi_value FormatReferences(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool formatPresent = false;
  const std::string format =
      ReadStringArgument(env, info, 2, &formatPresent);
  bool sortKeysPresent = false;
  const std::vector<std::string> sortKeys =
      ReadStringArrayArgument(env, info, 3, &sortKeysPresent);
  bool patternsPresent = false;
  const std::vector<std::string> patterns =
      ReadStringArrayArgument(env, info, 4, &patternsPresent);
  bool excludesPresent = false;
  const std::vector<std::string> excludes =
      ReadStringArrayArgument(env, info, 5, &excludesPresent);
  if (!pathPresent) {
    napi_throw_type_error(env, nullptr, "formatReferences expects a path.");
    return nullptr;
  }
  harmony_git::ForEachRefOptions options;
  options.count = ReadUint32Argument(
      env,
      info,
      1,
      std::numeric_limits<uint32_t>::max());
  options.format = formatPresent ? format : "";
  options.sortKeys =
      sortKeysPresent ? sortKeys : std::vector<std::string> {};
  options.patterns =
      patternsPresent ? patterns : std::vector<std::string> {};
  options.excludes =
      excludesPresent ? excludes : std::vector<std::string> {};
  bool pointsAtPresent = false;
  options.pointsAt =
      ReadStringArgument(env, info, 6, &pointsAtPresent);
  if (!pointsAtPresent) {
    options.pointsAt.clear();
  }
  bool mergedPresent = false;
  options.merged =
      ReadStringArgument(env, info, 7, &mergedPresent);
  if (!mergedPresent) {
    options.merged.clear();
  }
  bool noMergedPresent = false;
  options.noMerged =
      ReadStringArgument(env, info, 8, &noMergedPresent);
  if (!noMergedPresent) {
    options.noMerged.clear();
  }
  bool containsPresent = false;
  options.contains =
      ReadStringArgument(env, info, 9, &containsPresent);
  if (!containsPresent) {
    options.contains.clear();
  }
  bool noContainsPresent = false;
  options.noContains =
      ReadStringArgument(env, info, 10, &noContainsPresent);
  if (!noContainsPresent) {
    options.noContains.clear();
  }
  options.ignoreCase = ReadBooleanArgument(env, info, 11, false);
  options.includeRootRefs = ReadBooleanArgument(env, info, 12, false);
  std::string error;
  const std::vector<std::string> lines =
      harmony_git::FormatReferences(path, options, &error);
  if (!error.empty()) {
    napi_throw_error(env, nullptr, error.c_str());
    return nullptr;
  }
  napi_value result = nullptr;
  napi_create_array_with_length(env, lines.size(), &result);
  for (size_t index = 0; index < lines.size(); ++index) {
    napi_set_element(env, result, index, CreateString(env, lines[index]));
  }
  return result;
}

napi_value ReadSymbolicReference(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool namePresent = false;
  const std::string name = ReadStringArgument(env, info, 1, &namePresent);
  const bool shortName = ReadBooleanArgument(env, info, 2, false);
  const bool recurse = ReadBooleanArgument(env, info, 3, true);
  if (!pathPresent || !namePresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "readSymbolicReference expects a path and reference name.");
    return nullptr;
  }
  std::string error;
  const std::string target =
      harmony_git::ReadSymbolicReference(
          path,
          name,
          shortName,
          recurse,
          &error);
  if (!error.empty()) {
    napi_throw_error(env, nullptr, error.c_str());
    return nullptr;
  }
  return CreateString(env, target);
}

napi_value UpdateSymbolicReference(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool namePresent = false;
  const std::string name = ReadStringArgument(env, info, 1, &namePresent);
  bool targetPresent = false;
  const std::string target =
      ReadStringArgument(env, info, 2, &targetPresent);
  const bool deleteReference = ReadBooleanArgument(env, info, 3, false);
  bool messagePresent = false;
  const std::string message =
      ReadStringArgument(env, info, 4, &messagePresent);
  if (!pathPresent || !namePresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "updateSymbolicReference expects a path and reference name.");
    return nullptr;
  }
  return OperationToValue(
      env,
      harmony_git::UpdateSymbolicReference(
          path,
          name,
          targetPresent ? target : "",
          deleteReference,
          messagePresent ? message : ""));
}

napi_value UpdateReference(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool namePresent = false;
  const std::string name = ReadStringArgument(env, info, 1, &namePresent);
  bool newValuePresent = false;
  const std::string newValue =
      ReadStringArgument(env, info, 2, &newValuePresent);
  bool oldValuePresent = false;
  const std::string oldValue =
      ReadStringArgument(env, info, 3, &oldValuePresent);
  const bool deleteReference = ReadBooleanArgument(env, info, 4, false);
  const bool noDeref = ReadBooleanArgument(env, info, 5, false);
  bool messagePresent = false;
  const std::string message =
      ReadStringArgument(env, info, 6, &messagePresent);
  const bool createReflog = ReadBooleanArgument(env, info, 7, false);
  if (!pathPresent || !namePresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "updateReference expects a path and reference name.");
    return nullptr;
  }
  return OperationToValue(
      env,
      harmony_git::UpdateReference(
          path,
          name,
          newValuePresent ? newValue : "",
          oldValuePresent ? oldValue : "",
          deleteReference,
          noDeref,
          messagePresent ? message : "",
          createReflog));
}

napi_value UpdateReferences(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool inputPresent = false;
  const std::string input =
      ReadStringArgument(env, info, 1, &inputPresent);
  const bool noDeref = ReadBooleanArgument(env, info, 2, false);
  const bool createReflog = ReadBooleanArgument(env, info, 3, false);
  bool messagePresent = false;
  const std::string message =
      ReadStringArgument(env, info, 4, &messagePresent);
  const bool nullTerminated =
      ReadBooleanArgument(env, info, 5, false);
  const bool batchUpdates =
      ReadBooleanArgument(env, info, 6, false);
  if (!pathPresent || !inputPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "updateReferences expects a path and transaction input.");
    return nullptr;
  }
  return OperationToValue(
      env,
      harmony_git::UpdateReferences(
          path,
          input,
          noDeref,
          createReflog,
          messagePresent ? message : "",
          nullTerminated,
          batchUpdates));
}

napi_value CreateTag(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool namePresent = false;
  const std::string name = ReadStringArgument(env, info, 1, &namePresent);
  bool targetPresent = false;
  const std::string target =
      ReadStringArgument(env, info, 2, &targetPresent);
  const bool force = ReadBooleanArgument(env, info, 3, false);
  const bool annotated = ReadBooleanArgument(env, info, 4, false);
  bool messagePresent = false;
  const std::string message =
      ReadStringArgument(env, info, 5, &messagePresent);
  if (!pathPresent || !namePresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "createTag expects a path and tag name.");
    return nullptr;
  }
  return OperationToValue(
      env,
      harmony_git::CreateTag(
          path,
          name,
          targetPresent ? target : "",
          force,
          annotated,
          messagePresent ? message : ""));
}

napi_value DeleteTags(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool namesPresent = false;
  const std::vector<std::string> names =
      ReadStringArrayArgument(env, info, 1, &namesPresent);
  if (!pathPresent || !namesPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "deleteTags expects a path and a string array.");
    return nullptr;
  }
  return OperationToValue(
      env,
      harmony_git::DeleteTags(path, names));
}

napi_value ReadConfig(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool scopePresent = false;
  const std::string scope =
      ReadStringArgument(env, info, 1, &scopePresent);
  const bool includes = ReadBooleanArgument(env, info, 2, true);
  bool filePresent = false;
  const std::string explicitFile =
      ReadStringArgument(env, info, 3, &filePresent);
  if (!pathPresent) {
    napi_throw_type_error(env, nullptr, "readConfig expects a path.");
    return nullptr;
  }
  std::string error;
  const std::vector<harmony_git::ConfigEntry> entries =
      harmony_git::ReadConfig(
          path,
          scopePresent ? scope : "all",
          includes,
          filePresent ? explicitFile : "",
          &error);
  if (!error.empty()) {
    napi_throw_error(env, nullptr, error.c_str());
    return nullptr;
  }
  napi_value result = nullptr;
  napi_create_array_with_length(env, entries.size(), &result);
  for (size_t index = 0; index < entries.size(); ++index) {
    napi_set_element(
        env,
        result,
        index,
        ConfigEntryToValue(env, entries[index]));
  }
  return result;
}

napi_value SetConfigValue(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool keyPresent = false;
  const std::string key = ReadStringArgument(env, info, 1, &keyPresent);
  bool valuePresent = false;
  const std::string value = ReadStringArgument(env, info, 2, &valuePresent);
  bool scopePresent = false;
  const std::string scope =
      ReadStringArgument(env, info, 3, &scopePresent);
  const bool append = ReadBooleanArgument(env, info, 4, false);
  bool filePresent = false;
  const std::string explicitFile =
      ReadStringArgument(env, info, 5, &filePresent);
  if (!pathPresent || !keyPresent || !valuePresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "setConfigValue expects a path, key, and value.");
    return nullptr;
  }
  return OperationToValue(
      env,
      harmony_git::SetConfigValue(
          path,
          key,
          value,
          scopePresent ? scope : "local",
          append,
          filePresent ? explicitFile : ""));
}

napi_value UnsetConfigValue(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool keyPresent = false;
  const std::string key = ReadStringArgument(env, info, 1, &keyPresent);
  bool scopePresent = false;
  const std::string scope =
      ReadStringArgument(env, info, 2, &scopePresent);
  const bool all = ReadBooleanArgument(env, info, 3, false);
  bool filePresent = false;
  const std::string explicitFile =
      ReadStringArgument(env, info, 4, &filePresent);
  if (!pathPresent || !keyPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "unsetConfigValue expects a path and key.");
    return nullptr;
  }
  return OperationToValue(
      env,
      harmony_git::UnsetConfigValue(
          path,
          key,
          scopePresent ? scope : "local",
          all,
          filePresent ? explicitFile : ""));
}

napi_value SetCommandConfig(napi_env env, napi_callback_info info) {
  bool assignmentsPresent = false;
  const std::vector<std::string> assignments =
      ReadStringArrayArgument(env, info, 0, &assignmentsPresent);
  if (!assignmentsPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "setCommandConfig expects a string array.");
    return nullptr;
  }
  std::string error;
  if (!harmony_git::SetCommandConfig(assignments, &error)) {
    napi_throw_error(env, nullptr, error.c_str());
    return nullptr;
  }
  napi_value result = nullptr;
  napi_get_boolean(env, true, &result);
  return result;
}

napi_value AddRemote(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool namePresent = false;
  const std::string name = ReadStringArgument(env, info, 1, &namePresent);
  bool urlPresent = false;
  const std::string url = ReadStringArgument(env, info, 2, &urlPresent);
  if (!pathPresent || !namePresent || !urlPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "addRemote expects a path, remote name, and URL.");
    return nullptr;
  }
  return OperationToValue(
      env,
      harmony_git::AddRemote(path, name, url));
}

napi_value RemoveRemote(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool namePresent = false;
  const std::string name = ReadStringArgument(env, info, 1, &namePresent);
  if (!pathPresent || !namePresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "removeRemote expects a path and remote name.");
    return nullptr;
  }
  return OperationToValue(
      env,
      harmony_git::RemoveRemote(path, name));
}

napi_value RenameRemote(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool oldPresent = false;
  const std::string oldName = ReadStringArgument(env, info, 1, &oldPresent);
  bool newPresent = false;
  const std::string newName = ReadStringArgument(env, info, 2, &newPresent);
  if (!pathPresent || !oldPresent || !newPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "renameRemote expects a path, old remote, and new remote.");
    return nullptr;
  }
  return OperationToValue(
      env,
      harmony_git::RenameRemote(path, oldName, newName));
}

napi_value GetRemoteUrl(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool namePresent = false;
  const std::string name = ReadStringArgument(env, info, 1, &namePresent);
  const bool push = ReadBooleanArgument(env, info, 2, false);
  if (!pathPresent || !namePresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "getRemoteUrl expects a path and remote name.");
    return nullptr;
  }
  std::string error;
  const std::string url = harmony_git::GetRemoteUrl(
      path,
      name,
      push,
      &error);
  if (!error.empty()) {
    napi_throw_error(env, nullptr, error.c_str());
    return nullptr;
  }
  return CreateString(env, url);
}

napi_value SetRemoteUrl(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool namePresent = false;
  const std::string name = ReadStringArgument(env, info, 1, &namePresent);
  bool urlPresent = false;
  const std::string url = ReadStringArgument(env, info, 2, &urlPresent);
  const bool push = ReadBooleanArgument(env, info, 3, false);
  if (!pathPresent || !namePresent || !urlPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "setRemoteUrl expects a path, remote name, and URL.");
    return nullptr;
  }
  return OperationToValue(
      env,
      harmony_git::SetRemoteUrl(path, name, url, push));
}

napi_value ListRemoteReferences(
    napi_env env,
    napi_callback_info info) {
  bool urlPresent = false;
  const std::string url =
      ReadStringArgument(env, info, 0, &urlPresent);
  bool patternsPresent = false;
  const std::vector<std::string> patterns =
      ReadStringArrayArgument(env, info, 4, &patternsPresent);
  bool authorizationPresent = false;
  const std::string authorization =
      ReadStringArgument(env, info, 5, &authorizationPresent);
  (void)authorizationPresent;
  if (!urlPresent || !patternsPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "listRemoteReferences expects a URL and patterns.");
    return nullptr;
  }
  const harmony_git::RemoteTransportOptions transportOptions =
      ReadRemoteTransportOptions(env, info);
  std::string transportError;
  if (!harmony_git::ValidateRemoteTransportOptions(
          transportOptions,
          &transportError)) {
    napi_throw_type_error(env, nullptr, transportError.c_str());
    return nullptr;
  }
  return RemoteAdvertisementToValue(
      env,
      harmony_git::ListRemoteReferences(
          url,
          ReadBooleanArgument(env, info, 1, false),
          ReadBooleanArgument(env, info, 2, false),
          ReadBooleanArgument(env, info, 3, false),
          patterns,
          authorization,
          transportOptions));
}

napi_value ListRemotePushReferences(
    napi_env env,
    napi_callback_info info) {
  bool urlPresent = false;
  const std::string url =
      ReadStringArgument(env, info, 0, &urlPresent);
  bool patternsPresent = false;
  const std::vector<std::string> patterns =
      ReadStringArrayArgument(env, info, 4, &patternsPresent);
  bool authorizationPresent = false;
  const std::string authorization =
      ReadStringArgument(env, info, 5, &authorizationPresent);
  (void)authorizationPresent;
  if (!urlPresent || !patternsPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "listRemotePushReferences expects a URL and patterns.");
    return nullptr;
  }
  const harmony_git::RemoteTransportOptions transportOptions =
      ReadRemoteTransportOptions(env, info);
  std::string transportError;
  if (!harmony_git::ValidateRemoteTransportOptions(
          transportOptions,
          &transportError)) {
    napi_throw_type_error(env, nullptr, transportError.c_str());
    return nullptr;
  }
  return RemoteAdvertisementToValue(
      env,
      harmony_git::ListRemoteReceivePackReferences(
          url,
          ReadBooleanArgument(env, info, 1, false),
          ReadBooleanArgument(env, info, 2, false),
          ReadBooleanArgument(env, info, 3, false),
          patterns,
          authorization,
          transportOptions));
}

napi_value BuildRemoteUploadPackUrl(
    napi_env env,
    napi_callback_info info) {
  bool urlPresent = false;
  const std::string url =
      ReadStringArgument(env, info, 0, &urlPresent);
  if (!urlPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "buildRemoteUploadPackUrl expects a URL.");
    return nullptr;
  }
  std::string error;
  const std::string result =
      harmony_git::BuildRemoteUploadPackUrl(url, &error);
  if (!error.empty()) {
    napi_throw_error(env, nullptr, error.c_str());
    return nullptr;
  }
  return CreateString(env, result);
}

napi_value BuildRemoteReceivePackUrl(
    napi_env env,
    napi_callback_info info) {
  bool urlPresent = false;
  const std::string url =
      ReadStringArgument(env, info, 0, &urlPresent);
  if (!urlPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "buildRemoteReceivePackUrl expects a URL.");
    return nullptr;
  }
  std::string error;
  const std::string result =
      harmony_git::BuildRemoteReceivePackUrl(url, &error);
  if (!error.empty()) {
    napi_throw_error(env, nullptr, error.c_str());
    return nullptr;
  }
  return CreateString(env, result);
}

napi_value BuildUploadPackRequest(
    napi_env env,
    napi_callback_info info) {
  bool wantsPresent = false;
  const std::vector<std::string> wants =
      ReadStringArrayArgument(env, info, 0, &wantsPresent);
  bool havesPresent = false;
  const std::vector<std::string> haves =
      ReadStringArrayArgument(env, info, 1, &havesPresent);
  bool capabilitiesPresent = false;
  const std::vector<std::string> capabilities =
      ReadStringArrayArgument(
          env,
          info,
          2,
          &capabilitiesPresent);
  if (!wantsPresent || !havesPresent || !capabilitiesPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "buildUploadPackRequest expects wants, haves, and capabilities.");
    return nullptr;
  }
  std::string error;
  const std::string request =
      harmony_git::BuildUploadPackRequest(
          wants,
          haves,
          capabilities,
          &error);
  if (!error.empty()) {
    napi_throw_error(env, nullptr, error.c_str());
    return nullptr;
  }
  return CreateArrayBuffer(env, request);
}

napi_value BuildReceivePackRequest(
    napi_env env,
    napi_callback_info info) {
  bool pathPresent = false;
  const std::string path =
      ReadStringArgument(env, info, 0, &pathPresent);
  bool updatesPresent = false;
  const std::vector<std::string> updateLines =
      ReadStringArrayArgument(env, info, 1, &updatesPresent);
  bool havesPresent = false;
  const std::vector<std::string> haves =
      ReadStringArrayArgument(env, info, 2, &havesPresent);
  bool capabilitiesPresent = false;
  const std::vector<std::string> capabilities =
      ReadStringArrayArgument(env, info, 3, &capabilitiesPresent);
  if (!pathPresent || !updatesPresent ||
      !havesPresent || !capabilitiesPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "buildReceivePackRequest expects a path, updates, haves, and capabilities.");
    return nullptr;
  }

  std::vector<harmony_git::RemotePushUpdate> updates;
  std::vector<std::string> newObjectIds;
  updates.reserve(updateLines.size());
  newObjectIds.reserve(updateLines.size());
  for (const std::string& line : updateLines) {
    std::istringstream input(line);
    harmony_git::RemotePushUpdate update;
    std::string trailing;
    if (!(input >> update.oldObjectId >>
          update.newObjectId >> update.name) ||
        (input >> trailing)) {
      napi_throw_type_error(
          env,
          nullptr,
          "buildReceivePackRequest contains a malformed ref update.");
      return nullptr;
    }
    updates.push_back(update);
    if (update.newObjectId != std::string(40, '0')) {
      newObjectIds.push_back(update.newObjectId);
    }
  }

  std::string error;
  const std::string packData =
      harmony_git::BuildReceivePackPack(
          path,
          newObjectIds,
          haves,
          &error);
  if (!error.empty()) {
    napi_throw_error(env, nullptr, error.c_str());
    return nullptr;
  }
  const std::string request =
      harmony_git::BuildReceivePackRequest(
          updates,
          packData,
          capabilities,
          &error);
  if (!error.empty()) {
    napi_throw_error(env, nullptr, error.c_str());
    return nullptr;
  }
  return CreateArrayBuffer(env, request);
}

napi_value ParseUploadPackResponse(
    napi_env env,
    napi_callback_info info) {
  bool payloadPresent = false;
  const std::string payload =
      ReadArrayBufferArgument(env, info, 0, &payloadPresent);
  if (!payloadPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "parseUploadPackResponse expects an ArrayBuffer.");
    return nullptr;
  }
  return RemotePackResponseToValue(
      env,
      harmony_git::ParseUploadPackResponse(payload));
}

napi_value ParseReceivePackResponse(
    napi_env env,
    napi_callback_info info) {
  bool payloadPresent = false;
  const std::string payload =
      ReadArrayBufferArgument(env, info, 0, &payloadPresent);
  bool referencesPresent = false;
  const std::vector<std::string> references =
      ReadStringArrayArgument(env, info, 1, &referencesPresent);
  if (!payloadPresent || !referencesPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "parseReceivePackResponse expects a payload and references.");
    return nullptr;
  }
  return RemotePushResultToValue(
      env,
      harmony_git::ParseReceivePackResponse(
          payload,
          references));
}

napi_value InstallRemotePack(
    napi_env env,
    napi_callback_info info) {
  bool pathPresent = false;
  const std::string path =
      ReadStringArgument(env, info, 0, &pathPresent);
  bool remotePresent = false;
  const std::string remoteName =
      ReadStringArgument(env, info, 1, &remotePresent);
  bool packPresent = false;
  const std::string packData =
      ReadArrayBufferArgument(env, info, 2, &packPresent);
  bool namesPresent = false;
  const std::vector<std::string> referenceNames =
      ReadStringArrayArgument(env, info, 3, &namesPresent);
  bool idsPresent = false;
  const std::vector<std::string> objectIds =
      ReadStringArrayArgument(env, info, 4, &idsPresent);
  bool headPresent = false;
  const std::string headTarget =
      ReadStringArgument(env, info, 5, &headPresent);
  if (!pathPresent || !remotePresent || !packPresent ||
      !namesPresent || !idsPresent || !headPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "installRemotePack expects a path, remote, pack, references, object ids, and HEAD target.");
    return nullptr;
  }
  return OperationToValue(
      env,
      harmony_git::InstallRemotePack(
          path,
          remoteName,
          packData,
          referenceNames,
          objectIds,
          headTarget));
}

napi_value ReadReflog(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path = ReadStringArgument(env, info, 0, &pathPresent);
  bool refPresent = false;
  const std::string ref = ReadStringArgument(env, info, 1, &refPresent);
  if (!pathPresent) {
    napi_throw_type_error(env, nullptr, "readReflog expects a path.");
    return nullptr;
  }
  const uint32_t maxCount = ReadUint32Argument(env, info, 2, 100);
  const uint32_t skip = ReadUint32Argument(env, info, 3, 0);
  bool sincePresent = false;
  const std::string since =
      ReadStringArgument(env, info, 4, &sincePresent);
  bool untilPresent = false;
  const std::string until =
      ReadStringArgument(env, info, 5, &untilPresent);
  std::string error;
  const std::vector<harmony_git::ReflogEntry> entries =
      harmony_git::ReadReflog(
          path,
          refPresent ? ref : "",
          maxCount,
          skip,
          sincePresent ? since : "",
          untilPresent ? until : "",
          &error);
  if (!error.empty()) {
    napi_throw_error(env, nullptr, error.c_str());
    return nullptr;
  }
  napi_value result = nullptr;
  napi_create_array_with_length(env, entries.size(), &result);
  for (size_t index = 0; index < entries.size(); ++index) {
    napi_set_element(
        env,
        result,
        index,
        ReflogEntryToValue(env, entries[index]));
  }
  return result;
}

napi_value ListReflogs(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path =
      ReadStringArgument(env, info, 0, &pathPresent);
  if (!pathPresent) {
    napi_throw_type_error(env, nullptr, "listReflogs expects a path.");
    return nullptr;
  }
  std::string error;
  const std::vector<std::string> reflogs =
      harmony_git::ListReflogs(path, &error);
  if (!error.empty()) {
    napi_throw_error(env, nullptr, error.c_str());
    return nullptr;
  }
  napi_value result = nullptr;
  napi_create_array_with_length(env, reflogs.size(), &result);
  for (size_t index = 0; index < reflogs.size(); ++index) {
    napi_set_element(
        env,
        result,
        index,
        CreateString(env, reflogs[index]));
  }
  return result;
}

napi_value ReflogExists(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path =
      ReadStringArgument(env, info, 0, &pathPresent);
  bool refPresent = false;
  const std::string ref =
      ReadStringArgument(env, info, 1, &refPresent);
  if (!pathPresent || !refPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "reflogExists expects a path and reference.");
    return nullptr;
  }
  std::string error;
  const bool exists =
      harmony_git::ReflogExists(path, ref, &error);
  if (!error.empty()) {
    napi_throw_error(env, nullptr, error.c_str());
    return nullptr;
  }
  return CreateBoolean(env, exists);
}

napi_value WriteReflog(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path =
      ReadStringArgument(env, info, 0, &pathPresent);
  bool refPresent = false;
  const std::string ref =
      ReadStringArgument(env, info, 1, &refPresent);
  bool oldObjectIdPresent = false;
  const std::string oldObjectId =
      ReadStringArgument(env, info, 2, &oldObjectIdPresent);
  bool newObjectIdPresent = false;
  const std::string newObjectId =
      ReadStringArgument(env, info, 3, &newObjectIdPresent);
  bool messagePresent = false;
  const std::string message =
      ReadStringArgument(env, info, 4, &messagePresent);
  if (!pathPresent || !refPresent ||
      !oldObjectIdPresent || !newObjectIdPresent ||
      !messagePresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "writeReflog expects a path, reference, old object ID, new object ID, and message.");
    return nullptr;
  }
  return OperationToValue(
      env,
      harmony_git::WriteReflog(
          path,
          ref,
          oldObjectId,
          newObjectId,
          message));
}

napi_value DeleteReflogEntries(
    napi_env env,
    napi_callback_info info) {
  bool pathPresent = false;
  const std::string path =
      ReadStringArgument(env, info, 0, &pathPresent);
  bool selectorsPresent = false;
  const std::vector<std::string> selectors =
      ReadStringArrayArgument(env, info, 1, &selectorsPresent);
  if (!pathPresent || !selectorsPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "deleteReflogEntries expects a path and selector array.");
    return nullptr;
  }
  return OperationToValue(
      env,
      harmony_git::DeleteReflogEntries(
          path,
          selectors,
          ReadBooleanArgument(env, info, 2, false),
          ReadBooleanArgument(env, info, 3, false),
          ReadBooleanArgument(env, info, 4, false),
          ReadBooleanArgument(env, info, 5, false)));
}

napi_value DropReflogs(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path =
      ReadStringArgument(env, info, 0, &pathPresent);
  bool refsPresent = false;
  const std::vector<std::string> refs =
      ReadStringArrayArgument(env, info, 1, &refsPresent);
  if (!pathPresent || !refsPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "dropReflogs expects a path and reference array.");
    return nullptr;
  }
  return OperationToValue(
      env,
      harmony_git::DropReflogs(
          path,
          refs,
          ReadBooleanArgument(env, info, 2, false),
          ReadBooleanArgument(env, info, 3, false)));
}

napi_value ExpireReflogs(napi_env env, napi_callback_info info) {
  bool pathPresent = false;
  const std::string path =
      ReadStringArgument(env, info, 0, &pathPresent);
  bool refsPresent = false;
  const std::vector<std::string> refs =
      ReadStringArrayArgument(env, info, 1, &refsPresent);
  bool expirePresent = false;
  const std::string expire =
      ReadStringArgument(env, info, 2, &expirePresent);
  bool expireUnreachablePresent = false;
  const std::string expireUnreachable =
      ReadStringArgument(env, info, 3, &expireUnreachablePresent);
  if (!pathPresent || !refsPresent) {
    napi_throw_type_error(
        env,
        nullptr,
        "expireReflogs expects a path and reference array.");
    return nullptr;
  }
  return OperationToValue(
      env,
      harmony_git::ExpireReflogs(
          path,
          refs,
          expirePresent ? expire : "",
          expireUnreachablePresent ? expireUnreachable : "",
          ReadBooleanArgument(env, info, 4, false),
          ReadBooleanArgument(env, info, 5, false),
          ReadBooleanArgument(env, info, 6, false),
          ReadBooleanArgument(env, info, 7, false),
          ReadBooleanArgument(env, info, 8, false),
          ReadBooleanArgument(env, info, 9, false),
          ReadBooleanArgument(env, info, 10, false)));
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
      {"readWorkspaceFile",
       nullptr,
       ReadWorkspaceFile,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"writeWorkspaceFile",
       nullptr,
       WriteWorkspaceFile,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"cleanRepository",
       nullptr,
       CleanRepository,
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
      {"removeRepositoryPaths",
       nullptr,
       RemoveRepositoryPaths,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"moveRepositoryPath",
       nullptr,
       MoveRepositoryPath,
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
      {"moveBranch",
       nullptr,
       MoveBranch,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"copyBranch",
       nullptr,
       CopyBranch,
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
      {"showRevision",
       nullptr,
       ShowRevision,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"readTags",
       nullptr,
       ReadTags,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"readFiles",
       nullptr,
       ReadFiles,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"hashFiles",
       nullptr,
       HashFiles,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"hashInput",
       nullptr,
       HashInput,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"checkIgnored",
       nullptr,
       CheckIgnored,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"readObjectContent",
       nullptr,
       ReadObjectContent,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"readTree",
       nullptr,
       ReadTree,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"readReferences",
       nullptr,
       ReadReferences,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"excludeExistingReferences",
       nullptr,
       ExcludeExistingReferences,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"readRevisionList",
       nullptr,
       ReadRevisionList,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"readMergeBases",
       nullptr,
       ReadMergeBases,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"isAncestor",
       nullptr,
       IsAncestor,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"findForkPoint",
       nullptr,
       FindForkPoint,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"formatReferences",
       nullptr,
       FormatReferences,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"readSymbolicReference",
       nullptr,
       ReadSymbolicReference,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"updateSymbolicReference",
       nullptr,
       UpdateSymbolicReference,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"updateReference",
       nullptr,
       UpdateReference,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"updateReferences",
       nullptr,
       UpdateReferences,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"createTag",
       nullptr,
       CreateTag,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"deleteTags",
       nullptr,
       DeleteTags,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"readConfig",
       nullptr,
       ReadConfig,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"setConfigValue",
       nullptr,
       SetConfigValue,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"unsetConfigValue",
       nullptr,
       UnsetConfigValue,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"setCommandConfig",
       nullptr,
       SetCommandConfig,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"addRemote",
       nullptr,
       AddRemote,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"removeRemote",
       nullptr,
       RemoveRemote,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"renameRemote",
       nullptr,
       RenameRemote,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"getRemoteUrl",
       nullptr,
       GetRemoteUrl,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"setRemoteUrl",
       nullptr,
       SetRemoteUrl,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"listRemoteReferences",
       nullptr,
       ListRemoteReferences,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"listRemotePushReferences",
       nullptr,
       ListRemotePushReferences,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"buildRemoteUploadPackUrl",
       nullptr,
       BuildRemoteUploadPackUrl,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"buildRemoteReceivePackUrl",
       nullptr,
       BuildRemoteReceivePackUrl,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"buildUploadPackRequest",
       nullptr,
       BuildUploadPackRequest,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"buildReceivePackRequest",
       nullptr,
       BuildReceivePackRequest,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"parseUploadPackResponse",
       nullptr,
       ParseUploadPackResponse,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"parseReceivePackResponse",
       nullptr,
       ParseReceivePackResponse,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"installRemotePack",
       nullptr,
       InstallRemotePack,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"readReflog",
       nullptr,
       ReadReflog,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"listReflogs",
       nullptr,
       ListReflogs,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"reflogExists",
       nullptr,
       ReflogExists,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"writeReflog",
       nullptr,
       WriteReflog,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"deleteReflogEntries",
       nullptr,
       DeleteReflogEntries,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"dropReflogs",
       nullptr,
       DropReflogs,
       nullptr,
       nullptr,
       nullptr,
       napi_default,
       nullptr},
      {"expireReflogs",
       nullptr,
       ExpireReflogs,
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
