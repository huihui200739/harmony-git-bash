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
  size_t argumentCount = 2;
  napi_value arguments[2] = {nullptr, nullptr};
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
  std::string value(length, '\0');
  napi_get_value_string_utf8(
      env,
      arguments[argumentIndex],
      value.data(),
      length + 1,
      &length);
  *present = true;
  return value;
}

bool ReadBooleanArgument(
    napi_env env,
    napi_callback_info info,
    size_t argumentIndex,
    bool defaultValue) {
  size_t argumentCount = 2;
  napi_value arguments[2] = {nullptr, nullptr};
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
