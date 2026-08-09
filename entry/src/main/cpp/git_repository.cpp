#include "git_repository.h"

#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
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
  std::array<uint8_t, 20> objectId {};
  uint32_t ctimeSeconds = 0;
  uint32_t ctimeNanoseconds = 0;
  uint32_t mtimeSeconds = 0;
  uint32_t mtimeNanoseconds = 0;
  uint32_t device = 0;
  uint32_t inode = 0;
  uint32_t mode = 0;
  uint32_t userId = 0;
  uint32_t groupId = 0;
  uint32_t size = 0;
  uint16_t stage = 0;
};

struct TreeEntry {
  std::string path;
  std::string mode;
  std::array<uint8_t, 20> objectId {};
};

struct TreeNode {
  std::map<std::string, TreeNode> directories;
  std::map<std::string, TreeEntry> files;
};

struct ObjectData {
  std::string type;
  std::string payload;
};

struct DiffFile {
  std::string path;
  std::string oldContent;
  std::string newContent;
  std::string oldObjectId;
  std::string newObjectId;
  std::string oldMode = "100644";
  std::string newMode = "100644";
};

struct RepositoryContext {
  fs::path repositoryPath;
  fs::path gitDirectory;
  fs::path commonGitDirectory;
  std::string headText;
  std::string headObjectId;
};

struct ConfigSection {
  std::string name;
  std::string subsection;
  bool hasSubsection = false;
};

struct IgnoreRule {
  std::string basePath;
  std::string pattern;
  std::string sourcePath;
  std::string displayPattern;
  size_t lineNumber = 0;
  bool negated = false;
  bool directoryOnly = false;
  bool anchored = false;
  bool hasSlash = false;
};

bool IsHexCharacter(char value);
int HexValue(char value);
std::string RelativePathOrEmpty(
    const fs::path& repositoryPath,
    const fs::path& candidate);
std::vector<std::string> ReadParents(const std::string& payload);
fs::path CommandBasePath(const std::string& startPath);

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

uint32_t RotateLeft(uint32_t value, uint32_t bits) {
  return (value << bits) | (value >> (32U - bits));
}

std::array<uint8_t, 20> Sha1(const std::string& input) {
  std::vector<uint8_t> data(input.begin(), input.end());
  const uint64_t bitLength =
      static_cast<uint64_t>(data.size()) * static_cast<uint64_t>(8);
  data.push_back(0x80);
  while ((data.size() % 64) != 56) {
    data.push_back(0);
  }
  for (int shift = 56; shift >= 0; shift -= 8) {
    data.push_back(
        static_cast<uint8_t>((bitLength >> static_cast<uint32_t>(shift)) & 0xff));
  }

  uint32_t h0 = 0x67452301;
  uint32_t h1 = 0xefcdab89;
  uint32_t h2 = 0x98badcfe;
  uint32_t h3 = 0x10325476;
  uint32_t h4 = 0xc3d2e1f0;

  for (size_t block = 0; block < data.size(); block += 64) {
    uint32_t words[80] = {};
    for (size_t index = 0; index < 16; ++index) {
      const size_t offset = block + index * 4;
      words[index] =
          (static_cast<uint32_t>(data[offset]) << 24) |
          (static_cast<uint32_t>(data[offset + 1]) << 16) |
          (static_cast<uint32_t>(data[offset + 2]) << 8) |
          static_cast<uint32_t>(data[offset + 3]);
    }
    for (size_t index = 16; index < 80; ++index) {
      words[index] = RotateLeft(
          words[index - 3] ^ words[index - 8] ^
              words[index - 14] ^ words[index - 16],
          1);
    }

    uint32_t a = h0;
    uint32_t b = h1;
    uint32_t c = h2;
    uint32_t d = h3;
    uint32_t e = h4;
    for (size_t index = 0; index < 80; ++index) {
      uint32_t function = 0;
      uint32_t constant = 0;
      if (index < 20) {
        function = (b & c) | ((~b) & d);
        constant = 0x5a827999;
      } else if (index < 40) {
        function = b ^ c ^ d;
        constant = 0x6ed9eba1;
      } else if (index < 60) {
        function = (b & c) | (b & d) | (c & d);
        constant = 0x8f1bbcdc;
      } else {
        function = b ^ c ^ d;
        constant = 0xca62c1d6;
      }
      const uint32_t temporary =
          RotateLeft(a, 5) + function + e + constant + words[index];
      e = d;
      d = c;
      c = RotateLeft(b, 30);
      b = a;
      a = temporary;
    }
    h0 += a;
    h1 += b;
    h2 += c;
    h3 += d;
    h4 += e;
  }

  std::array<uint8_t, 20> result {};
  const uint32_t words[] = {h0, h1, h2, h3, h4};
  for (size_t index = 0; index < 5; ++index) {
    result[index * 4] =
        static_cast<uint8_t>((words[index] >> 24) & 0xff);
    result[index * 4 + 1] =
        static_cast<uint8_t>((words[index] >> 16) & 0xff);
    result[index * 4 + 2] =
        static_cast<uint8_t>((words[index] >> 8) & 0xff);
    result[index * 4 + 3] =
        static_cast<uint8_t>(words[index] & 0xff);
  }
  return result;
}

std::string ObjectIdToHex(const std::array<uint8_t, 20>& objectId) {
  static const char* const digits = "0123456789abcdef";
  std::string result;
  result.reserve(40);
  for (uint8_t value : objectId) {
    result.push_back(digits[(value >> 4) & 0x0f]);
    result.push_back(digits[value & 0x0f]);
  }
  return result;
}

bool HexToObjectId(
    const std::string& value,
    std::array<uint8_t, 20>* objectId) {
  if (value.size() != 40) {
    return false;
  }
  for (size_t index = 0; index < 20; ++index) {
    if (!IsHexCharacter(value[index * 2]) ||
        !IsHexCharacter(value[index * 2 + 1])) {
      return false;
    }
    (*objectId)[index] = static_cast<uint8_t>(
        HexValue(value[index * 2]) * 16 + HexValue(value[index * 2 + 1]));
  }
  return true;
}

std::string HashObjectId(
    const std::string& type,
    const std::string& payload) {
  return ObjectIdToHex(Sha1(
      type + " " + std::to_string(payload.size()) + '\0' + payload));
}

void AppendBigEndian32(std::string* data, uint32_t value) {
  data->push_back(static_cast<char>((value >> 24) & 0xff));
  data->push_back(static_cast<char>((value >> 16) & 0xff));
  data->push_back(static_cast<char>((value >> 8) & 0xff));
  data->push_back(static_cast<char>(value & 0xff));
}

void AppendBigEndian16(std::string* data, uint16_t value) {
  data->push_back(static_cast<char>((value >> 8) & 0xff));
  data->push_back(static_cast<char>(value & 0xff));
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

bool ReadBinaryFile(
    const fs::path& path,
    std::string* content,
    std::string* error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    if (error != nullptr) {
      *error = "Cannot read " + path.string();
    }
    return false;
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  if (!input.good() && !input.eof()) {
    if (error != nullptr) {
      *error = "Failed while reading " + path.string();
    }
    return false;
  }
  *content = buffer.str();
  return true;
}

bool WriteBinaryFile(
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
  output.write(content.data(), static_cast<std::streamsize>(content.size()));
  if (!output.good()) {
    if (error != nullptr) {
      *error = "Failed while writing " + path.string();
    }
    return false;
  }
  return true;
}

bool EnsureDirectory(const fs::path& path, std::string* error);

bool WriteAtomicFile(
    const fs::path& path,
    const std::string& content,
    std::string* error) {
  if (!EnsureDirectory(path.parent_path(), error)) {
    return false;
  }
  fs::path temporary = path;
  temporary += ".harmony-" + std::to_string(getpid()) + ".tmp";
  if (!WriteBinaryFile(temporary, content, error)) {
    return false;
  }
  std::error_code renameError;
  fs::rename(temporary, path, renameError);
  if (renameError) {
    std::error_code cleanupError;
    fs::remove(temporary, cleanupError);
    if (error != nullptr) {
      *error =
          "Cannot replace " + path.string() + ": " + renameError.message();
    }
    return false;
  }
  return true;
}

bool Inflate(
    const std::string& compressed,
    std::string* decompressed,
    std::string* error) {
  z_stream stream {};
  stream.next_in = reinterpret_cast<Bytef*>(
      const_cast<char*>(compressed.data()));
  stream.avail_in = static_cast<uInt>(compressed.size());
  if (inflateInit(&stream) != Z_OK) {
    if (error != nullptr) {
      *error = "Cannot initialize zlib inflater.";
    }
    return false;
  }

  std::string output;
  std::array<char, 16384> buffer {};
  int result = Z_OK;
  while (result == Z_OK) {
    stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
    stream.avail_out = static_cast<uInt>(buffer.size());
    result = inflate(&stream, Z_NO_FLUSH);
    const size_t produced = buffer.size() - stream.avail_out;
    output.append(buffer.data(), produced);
  }
  inflateEnd(&stream);
  if (result != Z_STREAM_END) {
    if (error != nullptr) {
      *error = "Git object compression is corrupt.";
    }
    return false;
  }
  *decompressed = output;
  return true;
}

bool Deflate(
    const std::string& input,
    std::string* compressed,
    std::string* error) {
  uLongf outputSize = compressBound(static_cast<uLong>(input.size()));
  std::string output(outputSize, '\0');
  const int result = compress2(
      reinterpret_cast<Bytef*>(output.data()),
      &outputSize,
      reinterpret_cast<const Bytef*>(input.data()),
      static_cast<uLong>(input.size()),
      Z_BEST_SPEED);
  if (result != Z_OK) {
    if (error != nullptr) {
      *error = "Cannot compress Git object.";
    }
    return false;
  }
  output.resize(outputSize);
  *compressed = output;
  return true;
}

bool ReadLooseObject(
    const fs::path& commonGitDirectory,
    const std::string& objectId,
    ObjectData* object,
    std::string* error) {
  if (objectId.size() != 40) {
    if (error != nullptr) {
      *error = "Unsupported Git object id.";
    }
    return false;
  }
  const fs::path objectPath =
      commonGitDirectory / "objects" / objectId.substr(0, 2) /
      objectId.substr(2);
  std::string compressed;
  if (!ReadBinaryFile(objectPath, &compressed, error)) {
    if (error != nullptr) {
      *error = "Git object " + objectId + " is not available as a loose object.";
    }
    return false;
  }
  std::string raw;
  if (!Inflate(compressed, &raw, error)) {
    return false;
  }
  const size_t headerEnd = raw.find('\0');
  if (headerEnd == std::string::npos) {
    if (error != nullptr) {
      *error = "Git object " + objectId + " has no header.";
    }
    return false;
  }
  const std::string header = raw.substr(0, headerEnd);
  const size_t separator = header.find(' ');
  if (separator == std::string::npos) {
    if (error != nullptr) {
      *error = "Git object " + objectId + " has an invalid header.";
    }
    return false;
  }
  const std::string sizeText = header.substr(separator + 1);
  size_t parsedSize = 0;
  try {
    parsedSize = static_cast<size_t>(std::stoull(sizeText));
  } catch (...) {
    if (error != nullptr) {
      *error = "Git object " + objectId + " has an invalid size.";
    }
    return false;
  }
  if (parsedSize != raw.size() - headerEnd - 1) {
    if (error != nullptr) {
      *error = "Git object " + objectId + " has a truncated payload.";
    }
    return false;
  }
  object->type = header.substr(0, separator);
  object->payload = raw.substr(headerEnd + 1);
  return true;
}

bool ReadBigEndian32(
    const std::string& data,
    size_t offset,
    uint32_t* value) {
  if (offset + 4 > data.size()) {
    return false;
  }
  *value =
      (static_cast<uint32_t>(
           static_cast<unsigned char>(data[offset])) << 24) |
      (static_cast<uint32_t>(
           static_cast<unsigned char>(data[offset + 1])) << 16) |
      (static_cast<uint32_t>(
           static_cast<unsigned char>(data[offset + 2])) << 8) |
      static_cast<uint32_t>(
          static_cast<unsigned char>(data[offset + 3]));
  return true;
}

bool ReadBigEndian64(
    const std::string& data,
    size_t offset,
    uint64_t* value) {
  if (offset + 8 > data.size()) {
    return false;
  }
  uint64_t result = 0;
  for (size_t index = 0; index < 8; ++index) {
    result =
        (result << 8) |
        static_cast<uint64_t>(
            static_cast<unsigned char>(data[offset + index]));
  }
  *value = result;
  return true;
}

bool InflateFromOffset(
    const std::string& compressed,
    size_t offset,
    std::string* decompressed,
    size_t* consumed,
    std::string* error) {
  if (offset >= compressed.size()) {
    if (error != nullptr) {
      *error = "Packed Git object has no compressed payload.";
    }
    return false;
  }

  z_stream stream {};
  if (inflateInit(&stream) != Z_OK) {
    if (error != nullptr) {
      *error = "Cannot initialize packed Git object inflater.";
    }
    return false;
  }

  std::string output;
  std::array<char, 16384> buffer {};
  size_t inputOffset = offset;
  int result = Z_OK;
  while (result == Z_OK) {
    if (stream.avail_in == 0 && inputOffset < compressed.size()) {
      const size_t chunkSize = std::min<size_t>(
          compressed.size() - inputOffset,
          std::numeric_limits<uInt>::max());
      stream.next_in = reinterpret_cast<Bytef*>(
          const_cast<char*>(compressed.data() + inputOffset));
      stream.avail_in = static_cast<uInt>(chunkSize);
      inputOffset += chunkSize;
    }

    stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
    stream.avail_out = static_cast<uInt>(buffer.size());
    result = inflate(&stream, Z_NO_FLUSH);
    output.append(
        buffer.data(),
        buffer.size() - stream.avail_out);
    if (result == Z_BUF_ERROR && stream.avail_in == 0 &&
        inputOffset >= compressed.size()) {
      break;
    }
  }

  const size_t remainingInput = stream.avail_in;
  inflateEnd(&stream);
  if (result != Z_STREAM_END) {
    if (error != nullptr) {
      *error = "Packed Git object compression is corrupt.";
    }
    return false;
  }
  *consumed = inputOffset - remainingInput - offset;
  *decompressed = std::move(output);
  return true;
}

bool LookupPackIndex(
    const std::string& indexData,
    const std::array<uint8_t, 20>& objectId,
    uint64_t* packOffset,
    bool* found,
    std::string* error) {
  *found = false;
  if (indexData.size() < 4U * 256U + 40U) {
    if (error != nullptr) {
      *error = "Git pack index is truncated.";
    }
    return false;
  }

  const std::array<uint8_t, 20> checksum =
      Sha1(indexData.substr(0, indexData.size() - 20));
  if (std::memcmp(
          checksum.data(),
          indexData.data() + indexData.size() - 20,
          checksum.size()) != 0) {
    if (error != nullptr) {
      *error = "Git pack index checksum does not match its contents.";
    }
    return false;
  }

  const bool versionTwo =
      indexData.size() >= 8 &&
      static_cast<unsigned char>(indexData[0]) == 0xff &&
      indexData[1] == 't' &&
      indexData[2] == 'O' &&
      indexData[3] == 'c';
  size_t fanoutOffset = 0;
  uint32_t version = 1;
  if (versionTwo) {
    if (!ReadBigEndian32(indexData, 4, &version) || version != 2) {
      if (error != nullptr) {
        *error =
            "Unsupported Git pack index version " +
            std::to_string(version) + ".";
      }
      return false;
    }
    fanoutOffset = 8;
  }

  uint32_t objectCount = 0;
  if (!ReadBigEndian32(
          indexData,
          fanoutOffset + 255U * 4U,
          &objectCount)) {
    if (error != nullptr) {
      *error = "Git pack index fanout table is truncated.";
    }
    return false;
  }
  const uint8_t firstByte = objectId[0];
  uint32_t lower = 0;
  if (firstByte != 0 &&
      !ReadBigEndian32(
          indexData,
          fanoutOffset +
              static_cast<size_t>(firstByte - 1U) * 4U,
          &lower)) {
    if (error != nullptr) {
      *error = "Git pack index fanout table is truncated.";
    }
    return false;
  }
  uint32_t upper = 0;
  if (!ReadBigEndian32(
          indexData,
          fanoutOffset + static_cast<size_t>(firstByte) * 4U,
          &upper) ||
      lower > upper || upper > objectCount) {
    if (error != nullptr) {
      *error = "Git pack index fanout table is corrupt.";
    }
    return false;
  }

  const size_t namesOffset = fanoutOffset + 256U * 4U;
  const size_t entrySize = versionTwo ? 20U : 24U;
  const size_t nameAdjustment = versionTwo ? 0U : 4U;
  const size_t namesEnd =
      namesOffset + static_cast<size_t>(objectCount) * entrySize;
  if (namesEnd + 40U > indexData.size()) {
    if (error != nullptr) {
      *error = "Git pack index object table is truncated.";
    }
    return false;
  }

  uint32_t left = lower;
  uint32_t right = upper;
  while (left < right) {
    const uint32_t middle = left + (right - left) / 2U;
    const size_t objectOffset =
        namesOffset + static_cast<size_t>(middle) * entrySize +
        nameAdjustment;
    const int comparison = std::memcmp(
        indexData.data() + objectOffset,
        objectId.data(),
        objectId.size());
    if (comparison < 0) {
      left = middle + 1U;
    } else {
      right = middle;
    }
  }
  if (left >= upper) {
    return true;
  }
  const size_t matchedNameOffset =
      namesOffset + static_cast<size_t>(left) * entrySize +
      nameAdjustment;
  if (std::memcmp(
          indexData.data() + matchedNameOffset,
          objectId.data(),
          objectId.size()) != 0) {
    return true;
  }

  if (!versionTwo) {
    uint32_t offset = 0;
    if (!ReadBigEndian32(
            indexData,
            namesOffset + static_cast<size_t>(left) * entrySize,
            &offset)) {
      if (error != nullptr) {
        *error = "Git pack index offset is truncated.";
      }
      return false;
    }
    *packOffset = offset;
    *found = true;
    return true;
  }

  const size_t crcOffset =
      namesOffset + static_cast<size_t>(objectCount) * 20U;
  const size_t offsetsOffset =
      crcOffset + static_cast<size_t>(objectCount) * 4U;
  const size_t largeOffsetsOffset =
      offsetsOffset + static_cast<size_t>(objectCount) * 4U;
  if (largeOffsetsOffset + 40U > indexData.size()) {
    if (error != nullptr) {
      *error = "Git pack index offset tables are truncated.";
    }
    return false;
  }
  uint32_t offset = 0;
  if (!ReadBigEndian32(
          indexData,
          offsetsOffset + static_cast<size_t>(left) * 4U,
          &offset)) {
    if (error != nullptr) {
      *error = "Git pack index offset is truncated.";
    }
    return false;
  }
  if ((offset & 0x80000000U) == 0) {
    *packOffset = offset;
    *found = true;
    return true;
  }

  const uint32_t largeIndex = offset & 0x7fffffffU;
  const size_t availableLargeOffsets =
      (indexData.size() - largeOffsetsOffset - 40U) / 8U;
  if (largeIndex >= availableLargeOffsets ||
      !ReadBigEndian64(
          indexData,
          largeOffsetsOffset + static_cast<size_t>(largeIndex) * 8U,
          packOffset)) {
    if (error != nullptr) {
      *error = "Git pack index large offset is truncated.";
    }
    return false;
  }
  *found = true;
  return true;
}

bool FindPackedObject(
    const fs::path& commonGitDirectory,
    const std::string& objectId,
    fs::path* packPath,
    uint64_t* packOffset,
    bool* found,
    std::string* error) {
  *found = false;
  std::array<uint8_t, 20> binaryObjectId {};
  if (!HexToObjectId(objectId, &binaryObjectId)) {
    if (error != nullptr) {
      *error = "Unsupported Git object id.";
    }
    return false;
  }

  const fs::path packDirectory = commonGitDirectory / "objects" / "pack";
  std::error_code directoryError;
  if (!fs::is_directory(packDirectory, directoryError)) {
    return true;
  }
  std::vector<fs::path> indexes;
  fs::directory_iterator iterator(
      packDirectory,
      fs::directory_options::skip_permission_denied,
      directoryError);
  const fs::directory_iterator end;
  while (!directoryError && iterator != end) {
    if (iterator->is_regular_file(directoryError) &&
        iterator->path().extension() == ".idx") {
      indexes.push_back(iterator->path());
    }
    directoryError.clear();
    iterator.increment(directoryError);
  }
  if (directoryError) {
    if (error != nullptr) {
      *error =
          "Cannot enumerate Git pack indexes: " +
          directoryError.message();
    }
    return false;
  }
  std::sort(indexes.begin(), indexes.end());

  for (const fs::path& indexPath : indexes) {
    std::string indexData;
    if (!ReadBinaryFile(indexPath, &indexData, error)) {
      return false;
    }
    bool indexFound = false;
    if (!LookupPackIndex(
            indexData,
            binaryObjectId,
            packOffset,
            &indexFound,
            error)) {
      if (error != nullptr) {
        *error = indexPath.string() + ": " + *error;
      }
      return false;
    }
    if (!indexFound) {
      continue;
    }
    *packPath = indexPath;
    packPath->replace_extension(".pack");
    *found = true;
    return true;
  }
  return true;
}

bool ReadObjectInternal(
    const fs::path& commonGitDirectory,
    const std::string& objectId,
    ObjectData* object,
    uint32_t depth,
    std::string* error);

bool ReadDeltaInteger(
    const std::string& delta,
    size_t* offset,
    size_t* value) {
  uint64_t result = 0;
  uint32_t shift = 0;
  while (*offset < delta.size() && shift < 64U) {
    const uint8_t byte =
        static_cast<uint8_t>(delta[(*offset)++]);
    result |= static_cast<uint64_t>(byte & 0x7fU) << shift;
    if ((byte & 0x80U) == 0) {
      if (result > std::numeric_limits<size_t>::max()) {
        return false;
      }
      *value = static_cast<size_t>(result);
      return true;
    }
    shift += 7U;
  }
  return false;
}

bool ApplyPackDelta(
    const std::string& base,
    const std::string& delta,
    std::string* result,
    std::string* error) {
  size_t offset = 0;
  size_t baseSize = 0;
  size_t resultSize = 0;
  if (!ReadDeltaInteger(delta, &offset, &baseSize) ||
      !ReadDeltaInteger(delta, &offset, &resultSize) ||
      baseSize != base.size()) {
    if (error != nullptr) {
      *error = "Packed Git delta has an invalid size header.";
    }
    return false;
  }

  std::string output;
  output.reserve(resultSize);
  const auto readByte = [&delta, &offset](
                            uint8_t* byte) -> bool {
    if (offset >= delta.size()) {
      return false;
    }
    *byte = static_cast<uint8_t>(delta[offset++]);
    return true;
  };
  while (offset < delta.size()) {
    const uint8_t instruction =
        static_cast<uint8_t>(delta[offset++]);
    if ((instruction & 0x80U) != 0) {
      uint32_t copyOffset = 0;
      uint32_t copySize = 0;
      uint8_t byte = 0;
      if ((instruction & 0x01U) != 0) {
        if (!readByte(&byte)) {
          if (error != nullptr) {
            *error = "Packed Git delta copy offset is truncated.";
          }
          return false;
        }
        copyOffset |= byte;
      }
      if ((instruction & 0x02U) != 0) {
        if (!readByte(&byte)) {
          if (error != nullptr) {
            *error = "Packed Git delta copy offset is truncated.";
          }
          return false;
        }
        copyOffset |= static_cast<uint32_t>(byte) << 8;
      }
      if ((instruction & 0x04U) != 0) {
        if (!readByte(&byte)) {
          if (error != nullptr) {
            *error = "Packed Git delta copy offset is truncated.";
          }
          return false;
        }
        copyOffset |= static_cast<uint32_t>(byte) << 16;
      }
      if ((instruction & 0x08U) != 0) {
        if (!readByte(&byte)) {
          if (error != nullptr) {
            *error = "Packed Git delta copy offset is truncated.";
          }
          return false;
        }
        copyOffset |= static_cast<uint32_t>(byte) << 24;
      }
      if ((instruction & 0x10U) != 0) {
        if (!readByte(&byte)) {
          if (error != nullptr) {
            *error = "Packed Git delta copy size is truncated.";
          }
          return false;
        }
        copySize |= byte;
      }
      if ((instruction & 0x20U) != 0) {
        if (!readByte(&byte)) {
          if (error != nullptr) {
            *error = "Packed Git delta copy size is truncated.";
          }
          return false;
        }
        copySize |= static_cast<uint32_t>(byte) << 8;
      }
      if ((instruction & 0x40U) != 0) {
        if (!readByte(&byte)) {
          if (error != nullptr) {
            *error = "Packed Git delta copy size is truncated.";
          }
          return false;
        }
        copySize |= static_cast<uint32_t>(byte) << 16;
      }
      if (copySize == 0) {
        copySize = 0x10000U;
      }
      if (static_cast<uint64_t>(copyOffset) + copySize >
          base.size()) {
        if (error != nullptr) {
          *error = "Packed Git delta copies beyond its base object.";
        }
        return false;
      }
      output.append(base, copyOffset, copySize);
    } else if (instruction != 0) {
      const size_t insertSize = instruction;
      if (offset + insertSize > delta.size()) {
        if (error != nullptr) {
          *error = "Packed Git delta insert is truncated.";
        }
        return false;
      }
      output.append(delta, offset, insertSize);
      offset += insertSize;
    } else {
      if (error != nullptr) {
        *error = "Packed Git delta contains an invalid instruction.";
      }
      return false;
    }
    if (output.size() > resultSize) {
      if (error != nullptr) {
        *error = "Packed Git delta produced too much data.";
      }
      return false;
    }
  }
  if (output.size() != resultSize) {
    if (error != nullptr) {
      *error = "Packed Git delta produced a truncated object.";
    }
    return false;
  }
  *result = std::move(output);
  return true;
}

bool ReadPackedObjectAt(
    const fs::path& commonGitDirectory,
    const fs::path& packPath,
    const std::string& packData,
    uint64_t packOffset,
    ObjectData* object,
    uint32_t depth,
    std::string* error) {
  constexpr uint32_t kMaximumDeltaDepth = 128;
  if (depth > kMaximumDeltaDepth) {
    if (error != nullptr) {
      *error = "Packed Git delta chain is too deep.";
    }
    return false;
  }
  if (packData.size() < 32 ||
      packData.compare(0, 4, "PACK") != 0) {
    if (error != nullptr) {
      *error = "Git pack file has an invalid header.";
    }
    return false;
  }
  uint32_t version = 0;
  if (!ReadBigEndian32(packData, 4, &version) ||
      (version != 2 && version != 3)) {
    if (error != nullptr) {
      *error =
          "Unsupported Git pack version " +
          std::to_string(version) + ".";
    }
    return false;
  }
  if (packOffset < 12U ||
      packOffset >= packData.size() - 20U ||
      packOffset > std::numeric_limits<size_t>::max()) {
    if (error != nullptr) {
      *error = "Git pack index points outside its pack file.";
    }
    return false;
  }

  size_t offset = static_cast<size_t>(packOffset);
  const uint8_t first = static_cast<uint8_t>(packData[offset++]);
  const uint8_t type = (first >> 4U) & 0x07U;
  uint64_t encodedSize = first & 0x0fU;
  uint32_t shift = 4;
  uint8_t sizeByte = first;
  while ((sizeByte & 0x80U) != 0) {
    if (offset >= packData.size() - 20U || shift >= 64U) {
      if (error != nullptr) {
        *error = "Packed Git object header is truncated.";
      }
      return false;
    }
    sizeByte = static_cast<uint8_t>(packData[offset++]);
    encodedSize |=
        static_cast<uint64_t>(sizeByte & 0x7fU) << shift;
    shift += 7U;
  }
  if (encodedSize > std::numeric_limits<size_t>::max()) {
    if (error != nullptr) {
      *error = "Packed Git object is too large for this device.";
    }
    return false;
  }

  ObjectData base;
  if (type == 6) {
    if (offset >= packData.size() - 20U) {
      if (error != nullptr) {
        *error = "Packed Git OFS_DELTA base is truncated.";
      }
      return false;
    }
    uint8_t byte = static_cast<uint8_t>(packData[offset++]);
    uint64_t distance = byte & 0x7fU;
    while ((byte & 0x80U) != 0) {
      if (offset >= packData.size() - 20U ||
          distance >
              (std::numeric_limits<uint64_t>::max() >> 7U) - 1U) {
        if (error != nullptr) {
          *error = "Packed Git OFS_DELTA base offset is corrupt.";
        }
        return false;
      }
      byte = static_cast<uint8_t>(packData[offset++]);
      distance = ((distance + 1U) << 7U) | (byte & 0x7fU);
    }
    if (distance > packOffset ||
        !ReadPackedObjectAt(
            commonGitDirectory,
            packPath,
            packData,
            packOffset - distance,
            &base,
            depth + 1U,
            error)) {
      if (error != nullptr && error->empty()) {
        *error = "Packed Git OFS_DELTA base offset is invalid.";
      }
      return false;
    }
  } else if (type == 7) {
    if (offset + 20U > packData.size() - 20U) {
      if (error != nullptr) {
        *error = "Packed Git REF_DELTA base is truncated.";
      }
      return false;
    }
    std::array<uint8_t, 20> baseId {};
    std::memcpy(
        baseId.data(),
        packData.data() + offset,
        baseId.size());
    offset += baseId.size();
    if (!ReadObjectInternal(
            commonGitDirectory,
            ObjectIdToHex(baseId),
            &base,
            depth + 1U,
            error)) {
      return false;
    }
  } else if (type < 1 || type > 4) {
    if (error != nullptr) {
      *error = "Packed Git object has an unsupported type.";
    }
    return false;
  }

  std::string inflated;
  size_t consumed = 0;
  if (!InflateFromOffset(
          packData,
          offset,
          &inflated,
          &consumed,
          error)) {
    return false;
  }
  if (inflated.size() != static_cast<size_t>(encodedSize)) {
    if (error != nullptr) {
      *error = "Packed Git object size does not match its header.";
    }
    return false;
  }

  if (type == 6 || type == 7) {
    object->type = base.type;
    return ApplyPackDelta(
        base.payload,
        inflated,
        &object->payload,
        error);
  }
  static const char* const kObjectTypes[] = {
      "", "commit", "tree", "blob", "tag"};
  object->type = kObjectTypes[type];
  object->payload = std::move(inflated);
  return true;
}

bool ReadObjectInternal(
    const fs::path& commonGitDirectory,
    const std::string& objectId,
    ObjectData* object,
    uint32_t depth,
    std::string* error) {
  if (objectId.size() != 40) {
    if (error != nullptr) {
      *error = "Unsupported Git object id.";
    }
    return false;
  }
  const fs::path loosePath =
      commonGitDirectory / "objects" / objectId.substr(0, 2) /
      objectId.substr(2);
  std::error_code existsError;
  if (fs::is_regular_file(loosePath, existsError)) {
    return ReadLooseObject(
        commonGitDirectory,
        objectId,
        object,
        error);
  }

  fs::path packPath;
  uint64_t packOffset = 0;
  bool found = false;
  if (!FindPackedObject(
          commonGitDirectory,
          objectId,
          &packPath,
          &packOffset,
          &found,
          error)) {
    return false;
  }
  if (!found) {
    if (error != nullptr) {
      *error =
          "Git object " + objectId +
          " is not available as a loose or packed object.";
    }
    return false;
  }
  std::string packData;
  if (!ReadBinaryFile(packPath, &packData, error)) {
    return false;
  }
  if (!ReadPackedObjectAt(
          commonGitDirectory,
          packPath,
          packData,
          packOffset,
          object,
          depth,
          error)) {
    if (error != nullptr) {
      *error = packPath.string() + ": " + *error;
    }
    return false;
  }
  if (HashObjectId(object->type, object->payload) != objectId) {
    if (error != nullptr) {
      *error = "Packed Git object " + objectId + " failed hash validation.";
    }
    return false;
  }
  return true;
}

bool ReadObject(
    const fs::path& commonGitDirectory,
    const std::string& objectId,
    ObjectData* object,
    std::string* error) {
  return ReadObjectInternal(
      commonGitDirectory,
      objectId,
      object,
      0,
      error);
}

bool ReadPackIndexObjectIds(
    const std::string& indexData,
    std::vector<std::string>* objectIds,
    std::string* error) {
  objectIds->clear();
  if (indexData.size() < 4U * 256U + 40U) {
    if (error != nullptr) {
      *error = "Git pack index is truncated.";
    }
    return false;
  }
  const std::array<uint8_t, 20> checksum =
      Sha1(indexData.substr(0, indexData.size() - 20));
  if (std::memcmp(
          checksum.data(),
          indexData.data() + indexData.size() - 20,
          checksum.size()) != 0) {
    if (error != nullptr) {
      *error = "Git pack index checksum does not match its contents.";
    }
    return false;
  }

  const bool versionTwo =
      indexData.size() >= 8 &&
      static_cast<unsigned char>(indexData[0]) == 0xff &&
      indexData[1] == 't' &&
      indexData[2] == 'O' &&
      indexData[3] == 'c';
  size_t fanoutOffset = 0;
  uint32_t version = 1;
  if (versionTwo) {
    if (!ReadBigEndian32(indexData, 4, &version) || version != 2) {
      if (error != nullptr) {
        *error =
            "Unsupported Git pack index version " +
            std::to_string(version) + ".";
      }
      return false;
    }
    fanoutOffset = 8;
  }

  uint32_t objectCount = 0;
  if (!ReadBigEndian32(
          indexData,
          fanoutOffset + 255U * 4U,
          &objectCount)) {
    if (error != nullptr) {
      *error = "Git pack index fanout table is truncated.";
    }
    return false;
  }
  const size_t namesOffset = fanoutOffset + 256U * 4U;
  const size_t entrySize = versionTwo ? 20U : 24U;
  const size_t nameAdjustment = versionTwo ? 0U : 4U;
  const size_t namesEnd =
      namesOffset + static_cast<size_t>(objectCount) * entrySize;
  if (namesEnd + 40U > indexData.size()) {
    if (error != nullptr) {
      *error = "Git pack index object table is truncated.";
    }
    return false;
  }

  objectIds->reserve(objectCount);
  for (uint32_t index = 0; index < objectCount; ++index) {
    const size_t objectOffset =
        namesOffset + static_cast<size_t>(index) * entrySize +
        nameAdjustment;
    std::array<uint8_t, 20> objectId {};
    std::memcpy(
        objectId.data(),
        indexData.data() + objectOffset,
        objectId.size());
    objectIds->push_back(ObjectIdToHex(objectId));
  }
  return true;
}

std::string ResolveAbbreviatedObject(
    const fs::path& commonGitDirectory,
    const std::string& prefix,
    std::string* error) {
  std::set<std::string> matches;
  const fs::path looseDirectory =
      commonGitDirectory / "objects" / prefix.substr(0, 2);
  std::error_code directoryError;
  if (fs::is_directory(looseDirectory, directoryError)) {
    fs::directory_iterator iterator(
        looseDirectory,
        fs::directory_options::skip_permission_denied,
        directoryError);
    const fs::directory_iterator end;
    while (!directoryError && iterator != end) {
      const std::string name =
          iterator->path().filename().generic_string();
      const std::string objectId = prefix.substr(0, 2) + name;
      if (iterator->is_regular_file(directoryError) &&
          objectId.size() == 40 &&
          objectId.rfind(prefix, 0) == 0 &&
          std::all_of(
              objectId.begin(),
              objectId.end(),
              [](char value) {
                return IsHexCharacter(value);
              })) {
        matches.insert(objectId);
      }
      directoryError.clear();
      iterator.increment(directoryError);
    }
    if (directoryError) {
      if (error != nullptr) {
        *error =
            "Cannot enumerate loose Git objects: " +
            directoryError.message();
      }
      return "";
    }
  }

  const fs::path packDirectory =
      commonGitDirectory / "objects" / "pack";
  directoryError.clear();
  if (fs::is_directory(packDirectory, directoryError)) {
    std::vector<fs::path> indexes;
    fs::directory_iterator iterator(
        packDirectory,
        fs::directory_options::skip_permission_denied,
        directoryError);
    const fs::directory_iterator end;
    while (!directoryError && iterator != end) {
      if (iterator->is_regular_file(directoryError) &&
          iterator->path().extension() == ".idx") {
        indexes.push_back(iterator->path());
      }
      directoryError.clear();
      iterator.increment(directoryError);
    }
    if (directoryError) {
      if (error != nullptr) {
        *error =
            "Cannot enumerate Git pack indexes: " +
            directoryError.message();
      }
      return "";
    }
    std::sort(indexes.begin(), indexes.end());
    for (const fs::path& indexPath : indexes) {
      std::string indexData;
      if (!ReadBinaryFile(indexPath, &indexData, error)) {
        return "";
      }
      std::vector<std::string> objectIds;
      if (!ReadPackIndexObjectIds(
              indexData,
              &objectIds,
              error)) {
        if (error != nullptr) {
          *error = indexPath.string() + ": " + *error;
        }
        return "";
      }
      for (const std::string& objectId : objectIds) {
        if (objectId.rfind(prefix, 0) == 0) {
          matches.insert(objectId);
        }
      }
    }
  }

  if (matches.size() == 1) {
    return *matches.begin();
  }
  if (error != nullptr) {
    *error = matches.empty()
        ? "Invalid object name: " + prefix
        : "Short object ID " + prefix + " is ambiguous.";
  }
  return "";
}

bool WriteLooseObject(
    const fs::path& commonGitDirectory,
    const std::string& type,
    const std::string& payload,
    std::string* objectId,
    std::string* error) {
  const std::string id = HashObjectId(type, payload);
  const fs::path objectDirectory =
      commonGitDirectory / "objects" / id.substr(0, 2);
  if (!EnsureDirectory(objectDirectory, error)) {
    return false;
  }
  const fs::path objectPath = objectDirectory / id.substr(2);
  std::error_code existsError;
  if (!fs::exists(objectPath, existsError)) {
    const std::string raw =
        type + " " + std::to_string(payload.size()) + '\0' + payload;
    std::string compressed;
    if (!Deflate(raw, &compressed, error) ||
        !WriteBinaryFile(objectPath, compressed, error)) {
      return false;
    }
  }
  *objectId = id;
  return true;
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

bool ReadIndexV4Integer(
    const std::vector<uint8_t>& data,
    size_t end,
    size_t* offset,
    size_t* value) {
  if (*offset >= end) {
    return false;
  }
  uint8_t byte = data[(*offset)++];
  uint64_t result = static_cast<uint64_t>(byte & 0x7fU);
  while ((byte & 0x80U) != 0) {
    if (result == std::numeric_limits<uint64_t>::max()) {
      return false;
    }
    ++result;
    if (result > (std::numeric_limits<uint64_t>::max() >> 7U) ||
        *offset >= end) {
      return false;
    }
    byte = data[(*offset)++];
    result =
        (result << 7U) + static_cast<uint64_t>(byte & 0x7fU);
  }
  if (result > std::numeric_limits<size_t>::max()) {
    return false;
  }
  *value = static_cast<size_t>(result);
  return true;
}

bool ReadIndex(
    const fs::path& indexPath,
    std::vector<IndexEntry>* entries,
    uint32_t* version,
    std::string* error) {
  entries->clear();
  std::ifstream input(indexPath, std::ios::binary);
  if (!input) {
    return true;
  }
  std::vector<uint8_t> data(
      (std::istreambuf_iterator<char>(input)),
      std::istreambuf_iterator<char>());
  if (data.size() < 32 ||
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

  const uint32_t count = ReadBigEndian32(data, 8);
  const size_t indexEnd = data.size() - 20;
  const std::array<uint8_t, 20> expectedChecksum = Sha1(
      std::string(
          reinterpret_cast<const char*>(data.data()),
          indexEnd));
  if (!std::equal(
          expectedChecksum.begin(),
          expectedChecksum.end(),
          data.begin() + static_cast<std::ptrdiff_t>(indexEnd))) {
    *error = "Git index checksum does not match its contents.";
    return false;
  }
  size_t offset = 12;
  std::string previousPath;
  for (uint32_t entryIndex = 0; entryIndex < count; ++entryIndex) {
    const size_t entryStart = offset;
    if (entryStart + 62 > indexEnd) {
      *error = "Git index entry is truncated.";
      return false;
    }
    IndexEntry entry;
    entry.ctimeSeconds = ReadBigEndian32(data, entryStart);
    entry.ctimeNanoseconds = ReadBigEndian32(data, entryStart + 4);
    entry.mtimeSeconds = ReadBigEndian32(data, entryStart + 8);
    entry.mtimeNanoseconds = ReadBigEndian32(data, entryStart + 12);
    entry.device = ReadBigEndian32(data, entryStart + 16);
    entry.inode = ReadBigEndian32(data, entryStart + 20);
    entry.mode = ReadBigEndian32(data, entryStart + 24);
    entry.userId = ReadBigEndian32(data, entryStart + 28);
    entry.groupId = ReadBigEndian32(data, entryStart + 32);
    entry.size = ReadBigEndian32(data, entryStart + 36);
    std::copy_n(
        data.begin() + static_cast<std::ptrdiff_t>(entryStart + 40),
        entry.objectId.size(),
        entry.objectId.begin());
    const uint16_t flags = ReadBigEndian16(data, entryStart + 60);
    entry.stage = static_cast<uint16_t>((flags >> 12) & 0x3);

    size_t pathStart = entryStart + 62;
    if ((flags & 0x4000) != 0 && *version >= 3) {
      pathStart += 2;
    }
    if (pathStart >= indexEnd) {
      *error = "Git index path is truncated.";
      return false;
    }
    size_t stripLength = 0;
    if (*version == 4 &&
        !ReadIndexV4Integer(
            data,
            indexEnd,
            &pathStart,
            &stripLength)) {
      *error = "Git index v4 path compression integer is invalid.";
      return false;
    }
    if (stripLength > previousPath.size()) {
      *error = "Git index v4 path removes more bytes than the previous path.";
      return false;
    }
    size_t pathEnd = pathStart;
    while (pathEnd < indexEnd && data[pathEnd] != 0) {
      ++pathEnd;
    }
    if (pathEnd >= indexEnd) {
      *error = "Git index path is missing its terminator.";
      return false;
    }
    const size_t suffixLength = pathEnd - pathStart;
    const size_t prefixLength = previousPath.size() - stripLength;
    if (suffixLength >
        std::numeric_limits<size_t>::max() - prefixLength) {
      *error = "Git index path length overflows the native reader.";
      return false;
    }
    if (*version == 4) {
      entry.path.assign(previousPath.data(), prefixLength);
      entry.path.append(
          reinterpret_cast<const char*>(data.data() + pathStart),
          suffixLength);
    } else {
      entry.path.assign(
          reinterpret_cast<const char*>(data.data() + pathStart),
          suffixLength);
    }
    const size_t declaredPathLength =
        static_cast<size_t>(flags & 0x0fffU);
    if (declaredPathLength != 0x0fffU &&
        declaredPathLength != entry.path.size()) {
      *error = "Git index path length does not match its entry flags.";
      return false;
    }
    entries->push_back(entry);

    if (*version == 4) {
      offset = pathEnd + 1;
      previousPath = entry.path;
    } else {
      const size_t entryLength = pathEnd - entryStart + 1;
      offset = entryStart + ((entryLength + 7) / 8) * 8;
    }
    if (offset > indexEnd && entryIndex + 1 < count) {
      *error = "Git index padding is truncated.";
      return false;
    }
  }
  return true;
}

uint32_t StatMtimeNanoseconds(const struct stat& fileStat);

bool WriteIndex(
    const fs::path& indexPath,
    std::vector<IndexEntry> entries,
    std::string* error) {
  std::sort(
      entries.begin(),
      entries.end(),
      [](const IndexEntry& left, const IndexEntry& right) {
        if (left.path != right.path) {
          return left.path < right.path;
        }
        return left.stage < right.stage;
      });

  std::string data;
  data.append("DIRC", 4);
  AppendBigEndian32(&data, 2);
  AppendBigEndian32(&data, static_cast<uint32_t>(entries.size()));
  for (const IndexEntry& entry : entries) {
    const size_t entryStart = data.size();
    AppendBigEndian32(&data, entry.ctimeSeconds);
    AppendBigEndian32(&data, entry.ctimeNanoseconds);
    AppendBigEndian32(&data, entry.mtimeSeconds);
    AppendBigEndian32(&data, entry.mtimeNanoseconds);
    AppendBigEndian32(&data, entry.device);
    AppendBigEndian32(&data, entry.inode);
    AppendBigEndian32(&data, entry.mode);
    AppendBigEndian32(&data, entry.userId);
    AppendBigEndian32(&data, entry.groupId);
    AppendBigEndian32(&data, entry.size);
    data.append(
        reinterpret_cast<const char*>(entry.objectId.data()),
        entry.objectId.size());
    const uint16_t pathLength = static_cast<uint16_t>(
        std::min<size_t>(entry.path.size(), 0x0fff));
    AppendBigEndian16(
        &data,
        static_cast<uint16_t>((entry.stage << 12) | pathLength));
    data.append(entry.path);
    data.push_back('\0');
    while ((data.size() - entryStart) % 8 != 0) {
      data.push_back('\0');
    }
  }
  const std::array<uint8_t, 20> checksum = Sha1(data);
  data.append(
      reinterpret_cast<const char*>(checksum.data()),
      checksum.size());
  return WriteAtomicFile(indexPath, data, error);
}

uint32_t ModeFromStat(const struct stat& fileStat) {
  if (S_ISLNK(fileStat.st_mode)) {
    return 0120000U;
  }
  if (S_ISREG(fileStat.st_mode)) {
    return (fileStat.st_mode & 0111U) != 0 ? 0100755U : 0100644U;
  }
  if (S_ISDIR(fileStat.st_mode)) {
    return 0040000U;
  }
  return 0;
}

bool ReadWorkingTreeFile(
    const fs::path& path,
    std::string* content,
    uint32_t* mode,
    struct stat* fileStat,
    std::string* error) {
  struct stat currentStat {};
  if (lstat(path.c_str(), &currentStat) != 0) {
    if (error != nullptr) {
      *error = "Cannot stat " + path.string();
    }
    return false;
  }
  if (!S_ISREG(currentStat.st_mode) && !S_ISLNK(currentStat.st_mode)) {
    if (error != nullptr) {
      *error = "Unsupported Git path type: " + path.string();
    }
    return false;
  }
  if (S_ISLNK(currentStat.st_mode)) {
    std::vector<char> target(4096, '\0');
    const ssize_t length = readlink(
        path.c_str(),
        target.data(),
        target.size() - 1);
    if (length < 0) {
      if (error != nullptr) {
        *error = "Cannot read symbolic link " + path.string();
      }
      return false;
    }
    content->assign(target.data(), static_cast<size_t>(length));
  } else if (!ReadBinaryFile(path, content, error)) {
    return false;
  }
  *mode = ModeFromStat(currentStat);
  if (fileStat != nullptr) {
    *fileStat = currentStat;
  }
  return true;
}

IndexEntry IndexEntryFromStat(
    const std::string& path,
    const struct stat& fileStat,
    const std::array<uint8_t, 20>& objectId) {
  IndexEntry entry;
  entry.path = path;
  entry.objectId = objectId;
  entry.ctimeSeconds = static_cast<uint32_t>(fileStat.st_ctime);
  entry.mtimeSeconds = static_cast<uint32_t>(fileStat.st_mtime);
  entry.ctimeNanoseconds = 0;
  entry.mtimeNanoseconds = StatMtimeNanoseconds(fileStat);
  entry.device = static_cast<uint32_t>(fileStat.st_dev);
  entry.inode = static_cast<uint32_t>(fileStat.st_ino);
  entry.mode = ModeFromStat(fileStat);
  entry.userId = static_cast<uint32_t>(fileStat.st_uid);
  entry.groupId = static_cast<uint32_t>(fileStat.st_gid);
  entry.size = static_cast<uint32_t>(fileStat.st_size);
  return entry;
}

IndexEntry IndexEntryFromTree(
    const TreeEntry& treeEntry,
    const fs::path& repositoryPath) {
  IndexEntry entry;
  entry.path = treeEntry.path;
  entry.objectId = treeEntry.objectId;
  try {
    struct stat fileStat {};
    if (lstat(
            (repositoryPath / fs::path(entry.path)).c_str(),
            &fileStat) == 0) {
      const std::string path = entry.path;
      const std::array<uint8_t, 20> objectId = entry.objectId;
      entry = IndexEntryFromStat(path, fileStat, objectId);
      entry.mode = static_cast<uint32_t>(
          std::stoul(treeEntry.mode, nullptr, 8));
    } else {
      entry.mode = static_cast<uint32_t>(
          std::stoul(treeEntry.mode, nullptr, 8));
    }
  } catch (...) {
    entry.mode = 0100644U;
  }
  return entry;
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

  const uint32_t currentMode = ModeFromStat(fileStat);
  const uint32_t indexType = entry.mode & 0170000U;
  const uint32_t currentType = currentMode & 0170000U;
  if (indexType != currentType) {
    *state = "T";
    return true;
  }
  if (indexType == 0160000U) {
    return false;
  }

  if (indexType == 0100000U &&
      (entry.mode & 0111U) != (currentMode & 0111U)) {
    *state = "M";
    return true;
  }

  std::string content;
  uint32_t ignoredMode = 0;
  std::string error;
  if (!ReadWorkingTreeFile(
          filePath,
          &content,
          &ignoredMode,
          nullptr,
          &error)) {
    *state = "M";
    return true;
  }
  std::array<uint8_t, 20> currentObjectId {};
  HexToObjectId(HashObjectId("blob", content), &currentObjectId);
  if (currentObjectId != entry.objectId) {
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

bool IsEscapedAt(
    const std::string& value,
    size_t index) {
  if (index == 0 || index > value.size()) {
    return false;
  }
  size_t backslashes = 0;
  for (size_t cursor = index; cursor > 0 && value[cursor - 1] == '\\';) {
    ++backslashes;
    --cursor;
  }
  return (backslashes % 2U) != 0;
}

std::string TrimIgnoreLine(
    const std::string& line) {
  std::string value = line;
  while (!value.empty() &&
         (value.back() == '\n' || value.back() == '\r')) {
    value.pop_back();
  }
  while (!value.empty() &&
         value.back() == ' ' &&
         !IsEscapedAt(value, value.size() - 1)) {
    value.pop_back();
  }
  return value;
}

bool IsGlobSlash(
    const std::string& pattern,
    size_t index) {
  return index < pattern.size() &&
      pattern[index] == '/' &&
      !IsEscapedAt(pattern, index);
}

bool IsGlobCharacter(
    const std::string& pattern,
    size_t index,
    char candidate,
    size_t* nextIndex) {
  if (index >= pattern.size()) {
    return false;
  }
  if (pattern[index] == '\\' && index + 1 < pattern.size()) {
    *nextIndex = index + 2;
    return pattern[index + 1] == candidate;
  }
  if (pattern[index] != '[') {
    *nextIndex = index + 1;
    return pattern[index] == candidate;
  }

  size_t cursor = index + 1;
  bool negated = false;
  if (cursor < pattern.size() &&
      (pattern[cursor] == '!' || pattern[cursor] == '^')) {
    negated = true;
    ++cursor;
  }
  bool matched = false;
  bool hasClosingBracket = false;
  char previous = '\0';
  bool havePrevious = false;
  for (; cursor < pattern.size(); ++cursor) {
    if (pattern[cursor] == ']' && cursor > index + 1) {
      hasClosingBracket = true;
      break;
    }
    char current = pattern[cursor];
    if (current == '\\' && cursor + 1 < pattern.size()) {
      current = pattern[++cursor];
    }
    if (current == '-' &&
        havePrevious &&
        cursor + 1 < pattern.size() &&
        pattern[cursor + 1] != ']') {
      char rangeEnd = pattern[++cursor];
      if (rangeEnd == '\\' && cursor + 1 < pattern.size()) {
        rangeEnd = pattern[++cursor];
      }
      if (previous <= candidate && candidate <= rangeEnd) {
        matched = true;
      }
      previous = rangeEnd;
      havePrevious = true;
      continue;
    }
    if (current == candidate) {
      matched = true;
    }
    previous = current;
    havePrevious = true;
  }
  if (!hasClosingBracket) {
    *nextIndex = index + 1;
    return pattern[index] == candidate;
  }
  *nextIndex = cursor + 1;
  return negated ? !matched : matched;
}

bool GlobMatch(
    const std::string& pattern,
    const std::string& value) {
  std::vector<std::vector<int8_t>> memo(
      pattern.size() + 1,
      std::vector<int8_t>(value.size() + 1, -1));
  const auto match = [&pattern, &value, &memo](
                         const auto& self,
                         size_t patternIndex,
                         size_t valueIndex) -> bool {
    int8_t& cached = memo[patternIndex][valueIndex];
    if (cached != -1) {
      return cached != 0;
    }
    bool matched = false;
    if (patternIndex == pattern.size()) {
      matched = valueIndex == value.size();
    } else if (pattern[patternIndex] == '*') {
      size_t starEnd = patternIndex;
      while (starEnd < pattern.size() &&
             pattern[starEnd] == '*') {
        ++starEnd;
      }
      const bool doubleStar = starEnd - patternIndex >= 2;
      if (doubleStar && IsGlobSlash(pattern, starEnd)) {
        matched = self(self, starEnd + 1, valueIndex);
        for (size_t cursor = valueIndex;
             !matched && cursor < value.size();
             ++cursor) {
          if (value[cursor] == '/') {
            matched = self(self, starEnd + 1, cursor + 1);
          }
        }
      } else {
        matched = self(self, starEnd, valueIndex);
        for (size_t cursor = valueIndex;
             !matched && cursor < value.size();
             ++cursor) {
          if (doubleStar || value[cursor] != '/') {
            matched = self(self, starEnd, cursor + 1);
          } else {
            break;
          }
        }
      }
    } else if (pattern[patternIndex] == '?' &&
               valueIndex < value.size() &&
               value[valueIndex] != '/') {
      matched = self(self, patternIndex + 1, valueIndex + 1);
    } else if (valueIndex < value.size() &&
               pattern[patternIndex] != '/' &&
               IsGlobCharacter(
                   pattern,
                   patternIndex,
                   value[valueIndex],
                   &patternIndex)) {
      matched = self(self, patternIndex, valueIndex + 1);
    } else if (valueIndex < value.size() &&
               pattern[patternIndex] == '/' &&
               value[valueIndex] == '/') {
      matched = self(self, patternIndex + 1, valueIndex + 1);
    }
    cached = matched ? 1 : 0;
    return matched;
  };
  return match(match, 0, 0);
}

bool GlobMatchPathspec(
    const std::string& pattern,
    const std::string& value) {
  std::vector<std::vector<int8_t>> memo(
      pattern.size() + 1,
      std::vector<int8_t>(value.size() + 1, -1));
  const auto match = [&pattern, &value, &memo](
                         const auto& self,
                         size_t patternIndex,
                         size_t valueIndex) -> bool {
    int8_t& cached = memo[patternIndex][valueIndex];
    if (cached != -1) {
      return cached != 0;
    }
    bool matched = false;
    if (patternIndex == pattern.size()) {
      matched = valueIndex == value.size();
    } else if (pattern[patternIndex] == '*') {
      size_t starEnd = patternIndex;
      while (starEnd < pattern.size() &&
             pattern[starEnd] == '*') {
        ++starEnd;
      }
      matched = self(self, starEnd, valueIndex);
      for (size_t cursor = valueIndex;
           !matched && cursor < value.size();
           ++cursor) {
        matched = self(self, starEnd, cursor + 1);
      }
    } else if (pattern[patternIndex] == '?' &&
               valueIndex < value.size()) {
      matched = self(self, patternIndex + 1, valueIndex + 1);
    } else if (valueIndex < value.size()) {
      size_t nextPatternIndex = patternIndex;
      if (IsGlobCharacter(
              pattern,
              patternIndex,
              value[valueIndex],
              &nextPatternIndex)) {
        matched = self(
            self,
            nextPatternIndex,
            valueIndex + 1);
      }
    }
    cached = matched ? 1 : 0;
    return matched;
  };
  return match(match, 0, 0);
}

bool HasGlobCharacters(
    const std::string& value) {
  for (size_t index = 0; index < value.size(); ++index) {
    if ((value[index] == '*' ||
         value[index] == '?' ||
         value[index] == '[') &&
        !IsEscapedAt(value, index)) {
      return true;
    }
  }
  return false;
}

bool ParseIgnoreRule(
    const std::string& line,
    const std::string& basePath,
    IgnoreRule* rule) {
  std::string value = TrimIgnoreLine(line);
  if (value.empty() || value[0] == '#') {
    return false;
  }
  rule->basePath = basePath;
  rule->negated = value[0] == '!' && !IsEscapedAt(value, 0);
  if (rule->negated) {
    value.erase(0, 1);
  }
  if (value.empty()) {
    return false;
  }
  if (!value.empty() &&
      value.back() == '/' &&
      !IsEscapedAt(value, value.size() - 1)) {
    rule->directoryOnly = true;
    value.pop_back();
  }
  if (!value.empty() &&
      value.front() == '/' &&
      !IsEscapedAt(value, 0)) {
    rule->anchored = true;
    value.erase(0, 1);
  }
  if (value.empty()) {
    return false;
  }
  for (size_t index = 0; index < value.size(); ++index) {
    if (value[index] == '/' && !IsEscapedAt(value, index)) {
      rule->hasSlash = true;
      break;
    }
    if (value[index] == '\\' && index + 1 < value.size()) {
      ++index;
    }
  }
  rule->pattern = value;
  return true;
}

void ReadIgnoreRulesFile(
    const fs::path& path,
    const std::string& basePath,
    std::vector<IgnoreRule>* rules) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return;
  }
  std::string line;
  size_t lineNumber = 0;
  while (std::getline(input, line)) {
    ++lineNumber;
    IgnoreRule rule;
    if (ParseIgnoreRule(line, basePath, &rule)) {
      rule.sourcePath = path.lexically_normal().generic_string();
      rule.displayPattern = TrimIgnoreLine(line);
      rule.lineNumber = lineNumber;
      rules->push_back(rule);
    }
  }
}

std::string LowercaseAscii(std::string value) {
  std::transform(
      value.begin(),
      value.end(),
      value.begin(),
      [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
      });
  return value;
}

std::string UnescapeConfigSubsection(std::string value) {
  std::string unescaped;
  unescaped.reserve(value.size());
  for (size_t index = 0; index < value.size(); ++index) {
    if (value[index] == '\\' && index + 1 < value.size()) {
      unescaped.push_back(value[++index]);
    } else {
      unescaped.push_back(value[index]);
    }
  }
  return unescaped;
}

bool ParseConfigSectionHeader(
    const std::string& line,
    ConfigSection* section) {
  const std::string trimmed = Trim(line);
  if (trimmed.size() < 3 ||
      trimmed.front() != '[' ||
      trimmed.back() != ']') {
    return false;
  }
  const std::string body =
      Trim(trimmed.substr(1, trimmed.size() - 2));
  const size_t separator = body.find_first_of(" \t");
  if (separator == std::string::npos) {
    section->name = LowercaseAscii(body);
    section->subsection.clear();
    section->hasSubsection = false;
    return !section->name.empty();
  }
  section->name = LowercaseAscii(Trim(body.substr(0, separator)));
  std::string subsection = Trim(body.substr(separator + 1));
  if (subsection.size() >= 2 &&
      subsection.front() == '"' &&
      subsection.back() == '"') {
    subsection = subsection.substr(1, subsection.size() - 2);
  }
  section->subsection = UnescapeConfigSubsection(subsection);
  section->hasSubsection = !section->subsection.empty();
  return !section->name.empty() && section->hasSubsection;
}

std::string ConfigSectionKey(const ConfigSection& section) {
  return section.name +
      (section.hasSubsection ? "." + section.subsection : "");
}

std::string NormalizeConfigSection(const std::string& value) {
  const std::string trimmed = Trim(value);
  const size_t separator = trimmed.find('.');
  if (separator == std::string::npos) {
    return LowercaseAscii(trimmed);
  }
  return LowercaseAscii(trimmed.substr(0, separator)) +
      "." + trimmed.substr(separator + 1);
}

std::string EscapeConfigSubsection(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (char character : value) {
    if (character == '\\' || character == '"') {
      escaped.push_back('\\');
    }
    escaped.push_back(character);
  }
  return escaped;
}

std::string ConfigSectionHeader(const std::string& section) {
  const size_t separator = section.find('.');
  if (separator == std::string::npos) {
    return "[" + section + "]";
  }
  return "[" + section.substr(0, separator) + " \"" +
      EscapeConfigSubsection(section.substr(separator + 1)) + "\"]";
}

std::string ConfigSectionFromHeader(const std::string& line) {
  ConfigSection section;
  return ParseConfigSectionHeader(line, &section)
      ? ConfigSectionKey(section)
      : "";
}

bool ParseConfigKey(
    const std::string& key,
    std::string* section,
    std::string* localKey) {
  const std::string trimmed = Trim(key);
  const size_t firstSeparator = trimmed.find('.');
  const size_t lastSeparator = trimmed.rfind('.');
  if (firstSeparator == std::string::npos ||
      firstSeparator == 0 ||
      lastSeparator == 0 ||
      lastSeparator + 1 >= trimmed.size()) {
    return false;
  }
  *section = LowercaseAscii(trimmed.substr(0, firstSeparator));
  *localKey = LowercaseAscii(trimmed.substr(lastSeparator + 1));
  if (lastSeparator != firstSeparator) {
    *section += "." +
        trimmed.substr(
            firstSeparator + 1,
            lastSeparator - firstSeparator - 1);
  }
  return !section->empty() && !localKey->empty();
}

bool ParseConfigAssignment(
    const std::string& line,
    std::string* key,
    std::string* value) {
  const std::string trimmed = Trim(line);
  if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';' ||
      trimmed.front() == '[') {
    return false;
  }
  size_t separator = trimmed.find('=');
  if (separator == std::string::npos) {
    separator = trimmed.find_first_of(" \t");
  }
  if (separator == std::string::npos) {
    return false;
  }
  *key = LowercaseAscii(Trim(trimmed.substr(0, separator)));
  *value = Trim(trimmed.substr(separator + 1));
  if (key->empty()) {
    return false;
  }
  if (value->size() >= 2 &&
      value->front() == '"' &&
      value->back() == '"') {
    value->erase(0, 1);
    value->pop_back();
    std::string unescaped;
    unescaped.reserve(value->size());
    for (size_t index = 0; index < value->size(); ++index) {
      if (value->at(index) != '\\' || index + 1 >= value->size()) {
        unescaped.push_back(value->at(index));
        continue;
      }
      const char escaped = value->at(++index);
      if (escaped == 'n') {
        unescaped.push_back('\n');
      } else if (escaped == 't') {
        unescaped.push_back('\t');
      } else {
        unescaped.push_back(escaped);
      }
    }
    *value = unescaped;
  }
  return true;
}

std::vector<ConfigEntry> ReadConfigEntriesFromFile(
    const fs::path& configPath) {
  std::vector<ConfigEntry> entries;
  std::istringstream input(ReadTextFile(configPath));
  std::string currentSection;
  std::string line;
  while (std::getline(input, line)) {
    const std::string trimmed = Trim(line);
    if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
      continue;
    }
    if (trimmed.front() == '[' && trimmed.back() == ']') {
      currentSection = ConfigSectionFromHeader(trimmed);
      continue;
    }
    if (currentSection.empty()) {
      continue;
    }
    std::string key;
    std::string value;
    if (ParseConfigAssignment(line, &key, &value)) {
      entries.push_back({currentSection + "." + key, value});
    }
  }
  return entries;
}

std::string ConfigValueFromFile(
    const fs::path& configPath,
    const std::string& section,
    const std::string& key) {
  const std::string expected =
      NormalizeConfigSection(section) + "." + LowercaseAscii(Trim(key));
  std::string result;
  for (const ConfigEntry& entry : ReadConfigEntriesFromFile(configPath)) {
    if (entry.key == expected) {
      result = entry.value;
    }
  }
  return result;
}

bool RewriteLocalConfig(
    const fs::path& configPath,
    const std::string& key,
    const std::string& value,
    bool unset,
    bool* changed,
    std::string* error) {
  if (key.find_first_of("\r\n") != std::string::npos ||
      value.find_first_of("\r\n") != std::string::npos) {
    if (error != nullptr) {
      *error = "Git config keys and values cannot contain newlines.";
    }
    return false;
  }
  std::string targetSection;
  std::string targetKey;
  if (!ParseConfigKey(key, &targetSection, &targetKey)) {
    if (error != nullptr) {
      *error = "Invalid key: " + key;
    }
    return false;
  }

  std::vector<std::string> lines;
  std::istringstream input(ReadTextFile(configPath));
  std::string line;
  while (std::getline(input, line)) {
    lines.push_back(line);
  }
  if (lines.empty() && fs::exists(configPath)) {
    lines.push_back("");
  }

  std::string currentSection;
  std::vector<size_t> matchingLines;
  size_t sectionEnd = lines.size();
  size_t targetHeader = std::string::npos;
  for (size_t index = 0; index < lines.size(); ++index) {
    const std::string trimmed = Trim(lines[index]);
    if (!trimmed.empty() &&
        trimmed.front() == '[' &&
        trimmed.back() == ']') {
      if (!currentSection.empty() &&
          currentSection == targetSection &&
          sectionEnd == lines.size()) {
        sectionEnd = index;
      }
      currentSection = ConfigSectionFromHeader(trimmed);
      if (currentSection == targetSection) {
        targetHeader = index;
      }
      continue;
    }
    if (currentSection != targetSection) {
      continue;
    }
    std::string actualKey;
    std::string ignoredValue;
    if (ParseConfigAssignment(lines[index], &actualKey, &ignoredValue) &&
        actualKey == targetKey) {
      matchingLines.push_back(index);
    }
  }
  if (!currentSection.empty() &&
      currentSection == targetSection &&
      sectionEnd == lines.size()) {
    sectionEnd = lines.size();
  }

  *changed = !matchingLines.empty();
  if (unset) {
    for (auto iterator = matchingLines.rbegin();
         iterator != matchingLines.rend();
         ++iterator) {
      lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(*iterator));
    }
  } else if (!matchingLines.empty()) {
    lines[matchingLines.back()] = "\t" + targetKey + " = " + value;
    *changed = true;
  } else {
    const std::string sectionHeader = ConfigSectionHeader(targetSection);
    const size_t insertAt =
        targetHeader == std::string::npos ? lines.size() : sectionEnd;
    if (targetHeader == std::string::npos) {
      if (!lines.empty() && !Trim(lines.back()).empty()) {
        lines.push_back("");
      }
      lines.push_back(sectionHeader);
      lines.push_back("\t" + targetKey + " = " + value);
    } else {
      lines.insert(
          lines.begin() + static_cast<std::ptrdiff_t>(insertAt),
          "\t" + targetKey + " = " + value);
    }
    *changed = true;
  }

  if (!*changed) {
    return true;
  }
  std::string content;
  for (const std::string& outputLine : lines) {
    content += outputLine;
    content.push_back('\n');
  }
  return WriteAtomicFile(configPath, content, error);
}

struct ConfigSectionRange {
  ConfigSection section;
  size_t begin = 0;
  size_t end = 0;
};

std::vector<ConfigSectionRange> ConfigSectionRanges(
    const std::vector<std::string>& lines) {
  std::vector<ConfigSectionRange> ranges;
  for (size_t index = 0; index < lines.size(); ++index) {
    ConfigSection section;
    if (!ParseConfigSectionHeader(lines[index], &section)) {
      continue;
    }
    if (!ranges.empty()) {
      ranges.back().end = index;
    }
    ranges.push_back({section, index, lines.size()});
  }
  return ranges;
}

std::vector<std::string> ReadConfigLines(const fs::path& configPath) {
  std::vector<std::string> lines;
  std::istringstream input(ReadTextFile(configPath));
  std::string line;
  while (std::getline(input, line)) {
    lines.push_back(line);
  }
  return lines;
}

std::string ConfigLinesContent(const std::vector<std::string>& lines) {
  std::string content;
  for (const std::string& line : lines) {
    content += line;
    content.push_back('\n');
  }
  return content;
}

bool ConfigSectionMatches(
    const ConfigSection& section,
    const std::string& name,
    const std::string& subsection) {
  return section.name == LowercaseAscii(name) &&
      section.hasSubsection &&
      section.subsection == subsection;
}

bool ConfigSectionHasAssignment(
    const std::vector<std::string>& lines,
    const ConfigSectionRange& range,
    const std::string& key,
    const std::string& expectedValue) {
  for (size_t index = range.begin + 1; index < range.end; ++index) {
    std::string actualKey;
    std::string actualValue;
    if (ParseConfigAssignment(lines[index], &actualKey, &actualValue) &&
        actualKey == LowercaseAscii(key) &&
        actualValue == expectedValue) {
      return true;
    }
  }
  return false;
}

bool UpdateRemoteUrlConfig(
    const fs::path& configPath,
    const std::string& name,
    const std::string& url,
    bool push,
    bool* changed,
    std::string* error) {
  if (url.find_first_of("\r\n") != std::string::npos) {
    if (error != nullptr) {
      *error = "Remote URLs cannot contain newlines.";
    }
    return false;
  }
  std::vector<std::string> lines = ReadConfigLines(configPath);
  const std::vector<ConfigSectionRange> ranges =
      ConfigSectionRanges(lines);
  std::vector<ConfigSectionRange> remoteRanges;
  for (const ConfigSectionRange& range : ranges) {
    if (ConfigSectionMatches(range.section, "remote", name)) {
      remoteRanges.push_back(range);
    }
  }
  if (remoteRanges.empty()) {
    if (error != nullptr) {
      *error = "No such remote '" + name + "'.";
    }
    return false;
  }

  const std::string targetKey = push ? "pushurl" : "url";
  std::vector<size_t> matchingLines;
  for (const ConfigSectionRange& range : remoteRanges) {
    for (size_t index = range.begin + 1; index < range.end; ++index) {
      std::string actualKey;
      std::string ignoredValue;
      if (ParseConfigAssignment(lines[index], &actualKey, &ignoredValue) &&
          actualKey == targetKey) {
        matchingLines.push_back(index);
      }
    }
  }
  if (matchingLines.size() > 1) {
    if (error != nullptr) {
      *error = "Remote '" + name + "' has multiple " + targetKey +
          " values.";
    }
    return false;
  }

  *changed = false;
  if (matchingLines.size() == 1) {
    const size_t lineIndex = matchingLines.front();
    std::string actualKey;
    std::string currentValue;
    ParseConfigAssignment(lines[lineIndex], &actualKey, &currentValue);
    const size_t separator = lines[lineIndex].find('=');
    if (separator != std::string::npos) {
      lines[lineIndex] =
          lines[lineIndex].substr(0, separator + 1) + " " + url;
    } else {
      const size_t whitespace =
          lines[lineIndex].find_first_of(" \t");
      lines[lineIndex] =
          lines[lineIndex].substr(0, whitespace + 1) + url;
    }
    *changed = currentValue != url;
  } else if (push) {
    const ConfigSectionRange& destination = remoteRanges.back();
    lines.insert(
        lines.begin() + static_cast<std::ptrdiff_t>(destination.end),
        "\tpushurl = " + url);
    *changed = true;
  } else {
    if (error != nullptr) {
      *error = "Remote '" + name + "' has no url configured.";
    }
    return false;
  }

  if (!*changed) {
    return true;
  }
  return WriteAtomicFile(
      configPath,
      ConfigLinesContent(lines),
      error);
}

bool RewriteBranchConfigSections(
    const fs::path& configPath,
    const std::string& oldName,
    const std::string& newName,
    bool copy,
    bool* changed,
    std::string* error) {
  std::vector<std::string> lines = ReadConfigLines(configPath);
  const std::vector<ConfigSectionRange> ranges =
      ConfigSectionRanges(lines);
  const std::string oldSection = "branch";
  std::vector<ConfigSectionRange> matching;
  for (const ConfigSectionRange& range : ranges) {
    if (ConfigSectionMatches(range.section, oldSection, oldName)) {
      matching.push_back(range);
    }
  }
  *changed = !matching.empty();
  if (matching.empty()) {
    return true;
  }

  const std::string destination = ConfigSectionHeader(
      "branch." + newName);
  if (copy) {
    std::vector<std::string> additions;
    for (const ConfigSectionRange& range : matching) {
      if (!additions.empty()) {
        additions.push_back("");
      }
      additions.push_back(destination);
      for (size_t index = range.begin + 1; index < range.end; ++index) {
        additions.push_back(lines[index]);
      }
    }
    if (!lines.empty() && !Trim(lines.back()).empty()) {
      lines.push_back("");
    }
    lines.insert(lines.end(), additions.begin(), additions.end());
  } else {
    for (const ConfigSectionRange& range : matching) {
      lines[range.begin] = destination;
    }
  }
  return WriteAtomicFile(
      configPath,
      ConfigLinesContent(lines),
      error);
}

bool RewriteRemoteConfigSections(
    const fs::path& configPath,
    const std::string& oldName,
    const std::string& newName,
    bool remove,
    bool* changed,
    std::string* error) {
  std::vector<std::string> lines = ReadConfigLines(configPath);
  const std::vector<ConfigSectionRange> ranges =
      ConfigSectionRanges(lines);
  std::vector<ConfigSectionRange> remoteRanges;
  std::vector<ConfigSectionRange> branchRangesToRemove;
  for (const ConfigSectionRange& range : ranges) {
    if (ConfigSectionMatches(range.section, "remote", oldName)) {
      remoteRanges.push_back(range);
    }
    if (remove &&
        ConfigSectionMatches(range.section, "branch", "") == false &&
        range.section.name == "branch" &&
        ConfigSectionHasAssignment(
            lines,
            range,
            "remote",
            oldName)) {
      branchRangesToRemove.push_back(range);
    }
  }
  *changed = !remoteRanges.empty();
  if (remoteRanges.empty()) {
    return true;
  }

  if (remove) {
    std::vector<std::pair<size_t, size_t>> removals;
    for (const ConfigSectionRange& range : remoteRanges) {
      removals.push_back({range.begin, range.end});
    }
    for (const ConfigSectionRange& range : branchRangesToRemove) {
      removals.push_back({range.begin, range.end});
    }
    std::sort(
        removals.begin(),
        removals.end(),
        [](const auto& left, const auto& right) {
          return left.first > right.first;
        });
    for (const auto& removal : removals) {
      lines.erase(
          lines.begin() + static_cast<std::ptrdiff_t>(removal.first),
          lines.begin() + static_cast<std::ptrdiff_t>(removal.second));
    }
  } else {
    const std::string oldRefPrefix = "refs/remotes/" + oldName + "/";
    const std::string newRefPrefix = "refs/remotes/" + newName + "/";
    for (const ConfigSectionRange& range : ranges) {
      if (ConfigSectionMatches(range.section, "remote", oldName)) {
        lines[range.begin] = ConfigSectionHeader(
            "remote." + newName);
        for (size_t index = range.begin + 1; index < range.end; ++index) {
          std::string key;
          std::string value;
          if (!ParseConfigAssignment(lines[index], &key, &value) ||
              key != "fetch") {
            continue;
          }
          const size_t prefix = value.find(oldRefPrefix);
          if (prefix != std::string::npos) {
            lines[index].replace(
                lines[index].find(oldRefPrefix),
                oldRefPrefix.size(),
                newRefPrefix);
          }
        }
      } else if (range.section.name == "branch") {
        for (size_t index = range.begin + 1; index < range.end; ++index) {
          std::string key;
          std::string value;
          if (!ParseConfigAssignment(lines[index], &key, &value) ||
              key != "remote" ||
              value != oldName) {
            continue;
          }
          const size_t separator = lines[index].find('=');
          if (separator != std::string::npos) {
            lines[index] =
                lines[index].substr(0, separator + 1) + " " + newName;
          } else {
            const size_t whitespace =
                lines[index].find_first_of(" \t");
            lines[index] =
                lines[index].substr(0, whitespace + 1) + newName;
          }
        }
      }
    }
  }
  return WriteAtomicFile(
      configPath,
      ConfigLinesContent(lines),
      error);
}

bool AddRemoteConfig(
    const fs::path& configPath,
    const std::string& name,
    const std::string& url,
    std::string* error) {
  if (url.find_first_of("\r\n") != std::string::npos) {
    if (error != nullptr) {
      *error = "Remote URLs cannot contain newlines.";
    }
    return false;
  }
  std::vector<std::string> lines = ReadConfigLines(configPath);
  if (!lines.empty() && !Trim(lines.back()).empty()) {
    lines.push_back("");
  }
  lines.push_back(ConfigSectionHeader("remote." + name));
  lines.push_back("\turl = " + url);
  lines.push_back(
      "\tfetch = +refs/heads/*:refs/remotes/" + name + "/*");
  return WriteAtomicFile(
      configPath,
      ConfigLinesContent(lines),
      error);
}

fs::path ExpandUserPath(
    const std::string& value) {
  if (value.rfind("~/", 0) != 0 && value != "~") {
    return fs::path(value);
  }
  const char* home = std::getenv("HOME");
  if (home == nullptr || *home == '\0') {
    return fs::path(value);
  }
  return fs::path(home) / value.substr(value == "~" ? 1 : 2);
}

fs::path GlobalIgnoreFile(
    const fs::path& commonGitDirectory) {
  const fs::path repositoryConfig = commonGitDirectory / "config";
  const std::string repositoryConfigured =
      ConfigValueFromFile(repositoryConfig, "core", "excludesFile");
  if (!repositoryConfigured.empty()) {
    const fs::path expanded = ExpandUserPath(repositoryConfigured);
    return expanded.is_absolute()
        ? expanded
        : repositoryConfig.parent_path() / expanded;
  }

  const char* explicitConfig = std::getenv("GIT_CONFIG_GLOBAL");
  fs::path configPath;
  if (explicitConfig != nullptr && *explicitConfig != '\0') {
    configPath = ExpandUserPath(explicitConfig);
  } else {
    const char* xdgConfig = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    if (xdgConfig != nullptr && *xdgConfig != '\0') {
      configPath = fs::path(xdgConfig) / "git" / "config";
    } else if (home != nullptr && *home != '\0') {
      configPath = fs::path(home) / ".config" / "git" / "config";
    }
    if (!configPath.empty() &&
        !fs::is_regular_file(configPath)) {
      configPath =
          home != nullptr && *home != '\0'
              ? fs::path(home) / ".gitconfig"
              : fs::path();
    }
  }
  if (configPath.empty()) {
    return {};
  }

  const std::string configured =
      ConfigValueFromFile(configPath, "core", "excludesFile");
  if (!configured.empty()) {
    return ExpandUserPath(configured).is_absolute()
        ? ExpandUserPath(configured)
        : configPath.parent_path() / ExpandUserPath(configured);
  }

  const char* home = std::getenv("HOME");
  if (home == nullptr || *home == '\0') {
    return {};
  }
  const char* xdgConfig = std::getenv("XDG_CONFIG_HOME");
  const fs::path defaultPath =
      (xdgConfig != nullptr && *xdgConfig != '\0')
          ? fs::path(xdgConfig) / "git" / "ignore"
          : fs::path(home) / ".config" / "git" / "ignore";
  return defaultPath;
}

void LoadGlobalIgnoreRules(
    const fs::path& commonGitDirectory,
    std::vector<IgnoreRule>* rules) {
  const fs::path globalIgnore = GlobalIgnoreFile(commonGitDirectory);
  if (!globalIgnore.empty()) {
    ReadIgnoreRulesFile(globalIgnore, "", rules);
  }
}

bool IsIgnoredByRule(
    const IgnoreRule& rule,
    const std::string& relativePath,
    bool directory) {
  if (rule.directoryOnly && !directory) {
    return false;
  }
  if (!rule.basePath.empty() &&
      relativePath != rule.basePath &&
      relativePath.rfind(rule.basePath + "/", 0) != 0) {
    return false;
  }
  std::string candidate = relativePath;
  if (!rule.basePath.empty()) {
    candidate = relativePath.substr(rule.basePath.size());
    if (!candidate.empty() && candidate.front() == '/') {
      candidate.erase(0, 1);
    }
  }
  if (candidate.empty()) {
    return false;
  }
  if (rule.hasSlash || rule.anchored) {
    return GlobMatch(rule.pattern, candidate);
  }
  size_t start = 0;
  while (start < candidate.size()) {
    const size_t end = candidate.find('/', start);
    const std::string component = candidate.substr(
        start,
        end == std::string::npos ? std::string::npos : end - start);
    if (GlobMatch(rule.pattern, component)) {
      return true;
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return false;
}

bool IsIgnored(
    const std::string& relativePath,
    bool directory,
    std::vector<IgnoreRule> const& rules,
    bool inherited = false) {
  bool ignored = inherited;
  for (const IgnoreRule& rule : rules) {
    if (IsIgnoredByRule(rule, relativePath, directory)) {
      ignored = !rule.negated;
    }
  }
  return ignored;
}

struct IgnoreDecision {
  bool ignored = false;
  bool matched = false;
  IgnoreRule rule;
};

IgnoreDecision EvaluateIgnoreRules(
    const std::string& relativePath,
    bool directory,
    const std::vector<IgnoreRule>& rules,
    const IgnoreDecision& inherited) {
  IgnoreDecision decision = inherited;
  for (const IgnoreRule& rule : rules) {
    if (!IsIgnoredByRule(rule, relativePath, directory)) {
      continue;
    }
    if (rule.negated && inherited.ignored) {
      continue;
    }
    decision.ignored = !rule.negated;
    decision.matched = true;
    decision.rule = rule;
  }
  return decision;
}

std::string DisplayIgnoreSource(
    const fs::path& repositoryPath,
    const IgnoreRule& rule) {
  const fs::path source(rule.sourcePath);
  const std::string relative =
      RelativePathOrEmpty(repositoryPath, source);
  return relative.empty() ? rule.sourcePath : relative;
}

void AppendUntrackedFilesRecursive(
    const fs::path& directory,
    const std::string& relativeDirectory,
    const std::set<std::string>& trackedPaths,
    std::vector<IgnoreRule> rules,
    bool includeIgnored,
    bool onlyIgnored,
    bool parentIgnored,
    std::vector<FileStatus>* files) {
  const fs::path ignoreFile = directory / ".gitignore";
  ReadIgnoreRulesFile(ignoreFile, relativeDirectory, &rules);

  std::error_code iteratorError;
  fs::directory_iterator iterator(
      directory,
      fs::directory_options::skip_permission_denied,
      iteratorError);
  const fs::directory_iterator end;
  while (!iteratorError && iterator != end) {
    const fs::path path = iterator->path();
    const std::string name = path.filename().generic_string();
    const std::string relative =
        relativeDirectory.empty()
            ? name
            : relativeDirectory + "/" + name;
    if (relative == ".git" || relative.rfind(".git/", 0) == 0) {
      iterator.increment(iteratorError);
      continue;
    }

    std::error_code typeError;
    const fs::file_status status = fs::symlink_status(path, typeError);
    if (typeError) {
      iterator.increment(iteratorError);
      continue;
    }
    const bool directory = fs::is_directory(status);
    const bool ignored =
        IsIgnored(relative, directory, rules, parentIgnored);
    if (directory) {
      if (includeIgnored || onlyIgnored || !ignored) {
        AppendUntrackedFilesRecursive(
            path,
            relative,
            trackedPaths,
            rules,
            includeIgnored,
            onlyIgnored,
            ignored,
            files);
      }
    } else if (trackedPaths.find(relative) == trackedPaths.end() &&
               (onlyIgnored ? ignored : includeIgnored || !ignored)) {
      files->push_back({relative, "?", "?", false, false});
    }
    iterator.increment(iteratorError);
  }
}

void AppendUntrackedFiles(
    const fs::path& repositoryPath,
    const fs::path& commonGitDirectory,
    const std::set<std::string>& trackedPaths,
    std::vector<FileStatus>* files) {
  std::vector<IgnoreRule> rules;
  LoadGlobalIgnoreRules(commonGitDirectory, &rules);
  ReadIgnoreRulesFile(commonGitDirectory / "info" / "exclude", "", &rules);
  AppendUntrackedFilesRecursive(
      repositoryPath,
      "",
      trackedPaths,
      rules,
      false,
      false,
      false,
      files);
}

void AppendAllUntrackedFiles(
    const fs::path& repositoryPath,
    const fs::path& commonGitDirectory,
    const std::set<std::string>& trackedPaths,
    std::vector<FileStatus>* files) {
  std::vector<IgnoreRule> rules;
  LoadGlobalIgnoreRules(commonGitDirectory, &rules);
  ReadIgnoreRulesFile(commonGitDirectory / "info" / "exclude", "", &rules);
  AppendUntrackedFilesRecursive(
      repositoryPath,
      "",
      trackedPaths,
      rules,
      true,
      false,
      false,
      files);
}

void AppendIgnoredFiles(
    const fs::path& repositoryPath,
    const fs::path& commonGitDirectory,
    const std::set<std::string>& trackedPaths,
    std::vector<FileStatus>* files) {
  std::vector<IgnoreRule> rules;
  LoadGlobalIgnoreRules(commonGitDirectory, &rules);
  ReadIgnoreRulesFile(commonGitDirectory / "info" / "exclude", "", &rules);
  AppendUntrackedFilesRecursive(
      repositoryPath,
      "",
      trackedPaths,
      rules,
      false,
      true,
      false,
      files);
}

std::string ResolveHeadObject(
    const fs::path& gitDirectory,
    const fs::path& commonGitDirectory,
    const std::string& headText) {
  const std::string prefix = "ref:";
  std::string current = Trim(headText);
  std::set<std::string> visited;
  while (current.rfind(prefix, 0) == 0) {
    const std::string refName = Trim(current.substr(prefix.size()));
    if (refName.empty() || !visited.insert(refName).second) {
      return "";
    }
    std::string referenceValue =
        Trim(ReadTextFile(gitDirectory / refName));
    if (referenceValue.empty() && commonGitDirectory != gitDirectory) {
      referenceValue =
          Trim(ReadTextFile(commonGitDirectory / refName));
    }
    if (referenceValue.empty()) {
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
          referenceValue = Trim(line.substr(0, separator));
          break;
        }
      }
    }
    if (referenceValue.empty()) {
      return "";
    }
    current = referenceValue;
  }
  return current;
}

std::string CommitHeaderValue(
    const std::string& payload,
    const std::string& name) {
  const std::string prefix = name + " ";
  std::istringstream lines(payload);
  std::string line;
  while (std::getline(lines, line)) {
    if (line.empty()) {
      break;
    }
    if (line.rfind(prefix, 0) == 0) {
      return line.substr(prefix.size());
    }
  }
  return "";
}

std::string CommitMessage(const std::string& payload) {
  const size_t separator = payload.find("\n\n");
  return separator == std::string::npos
      ? std::string()
      : payload.substr(separator + 2);
}

bool ReadCommitObject(
    const fs::path& commonGitDirectory,
    const std::string& objectId,
    ObjectData* commit,
    std::string* error) {
  if (!ReadObject(commonGitDirectory, objectId, commit, error)) {
    return false;
  }
  if (commit->type != "commit") {
    if (error != nullptr) {
      *error = "Git object " + objectId + " is not a commit.";
    }
    return false;
  }
  return true;
}

bool PeelToCommit(
    const fs::path& commonGitDirectory,
    std::string* objectId,
    std::string* error) {
  std::set<std::string> visited;
  while (visited.insert(*objectId).second) {
    ObjectData object;
    if (!ReadObject(
            commonGitDirectory,
            *objectId,
            &object,
            error)) {
      return false;
    }
    if (object.type == "commit") {
      return true;
    }
    if (object.type != "tag") {
      if (error != nullptr) {
        *error =
            "Git object " + *objectId + " is not a commit or annotated tag.";
      }
      return false;
    }
    const std::string target =
        CommitHeaderValue(object.payload, "object");
    if (target.empty()) {
      if (error != nullptr) {
        *error = "Annotated tag " + *objectId + " has no target object.";
      }
      return false;
    }
    *objectId = target;
  }
  if (error != nullptr) {
    *error = "Annotated tag chain contains a cycle.";
  }
  return false;
}

std::string ResolveRevision(
    const RepositoryContext& context,
    const std::string& source,
    std::string* error) {
  std::string value = Trim(source);
  if (value.empty()) {
    if (error != nullptr) {
      *error = "A source revision is required.";
    }
    return "";
  }

  uint32_t parentCount = 0;
  const size_t tilde = value.find('~');
  if (tilde != std::string::npos) {
    const std::string countText = value.substr(tilde + 1);
    if (countText.empty()) {
      parentCount = 1;
    } else {
      try {
        size_t parsedLength = 0;
        const unsigned long parsed =
            std::stoul(countText, &parsedLength);
        if (parsedLength != countText.size() ||
            parsed > std::numeric_limits<uint32_t>::max()) {
          throw std::out_of_range("revision parent count");
        }
        parentCount = static_cast<uint32_t>(parsed);
      } catch (...) {
        if (error != nullptr) {
          *error = "Invalid revision: " + value;
        }
        return "";
      }
    }
    value = value.substr(0, tilde);
    if (value.empty()) {
      if (error != nullptr) {
        *error = "Invalid revision: " + source;
      }
      return "";
    }
  }

  std::string objectId;
  if (value == "HEAD") {
    objectId = context.headObjectId;
  } else if (value.size() == 40) {
    std::array<uint8_t, 20> ignoredObjectId {};
    if (HexToObjectId(value, &ignoredObjectId)) {
      objectId = value;
    }
  } else if (value.rfind("refs/", 0) == 0) {
    objectId = ResolveHeadObject(
        context.gitDirectory,
        context.commonGitDirectory,
        "ref: " + value);
  } else {
    objectId = ResolveHeadObject(
        context.gitDirectory,
        context.commonGitDirectory,
        "ref: refs/heads/" + value);
    if (objectId.empty()) {
      objectId = ResolveHeadObject(
          context.gitDirectory,
          context.commonGitDirectory,
          "ref: refs/remotes/" + value);
    }
    if (objectId.empty()) {
      objectId = ResolveHeadObject(
          context.gitDirectory,
          context.commonGitDirectory,
          "ref: refs/tags/" + value);
    }
  }
  if (objectId.empty()) {
    if (error != nullptr) {
      *error = "Invalid reference: " + source;
    }
    return "";
  }
  if (!PeelToCommit(
          context.commonGitDirectory,
          &objectId,
          error)) {
    return "";
  }

  for (uint32_t index = 0; index < parentCount; ++index) {
    ObjectData commit;
    if (!ReadCommitObject(
            context.commonGitDirectory,
            objectId,
            &commit,
            error)) {
      return "";
    }
    objectId = CommitHeaderValue(commit.payload, "parent");
    if (objectId.empty()) {
      if (error != nullptr) {
        *error = "Revision " + source + " does not have enough parents.";
      }
      return "";
    }
  }
  return objectId;
}

bool ReadTreeEntries(
    const fs::path& commonGitDirectory,
    const std::string& treeObjectId,
    std::vector<TreeEntry>* entries,
    std::string* error) {
  entries->clear();
  ObjectData tree;
  if (!ReadObject(
          commonGitDirectory,
          treeObjectId,
          &tree,
          error)) {
    return false;
  }
  if (tree.type != "tree") {
    if (error != nullptr) {
      *error = "Git object " + treeObjectId + " is not a tree.";
    }
    return false;
  }

  size_t offset = 0;
  while (offset < tree.payload.size()) {
    const size_t modeEnd = tree.payload.find(' ', offset);
    const size_t nameEnd =
        modeEnd == std::string::npos
            ? std::string::npos
            : tree.payload.find('\0', modeEnd + 1);
    if (modeEnd == std::string::npos ||
        nameEnd == std::string::npos ||
        nameEnd + 21 > tree.payload.size()) {
      if (error != nullptr) {
        *error = "Git tree " + treeObjectId + " is truncated.";
      }
      entries->clear();
      return false;
    }
    std::array<uint8_t, 20> childObjectId {};
    std::copy_n(
        tree.payload.begin() + static_cast<std::ptrdiff_t>(nameEnd + 1),
        childObjectId.size(),
        childObjectId.begin());
    entries->push_back({
        tree.payload.substr(modeEnd + 1, nameEnd - modeEnd - 1),
        tree.payload.substr(offset, modeEnd - offset),
        childObjectId});
    offset = nameEnd + 21;
  }
  return true;
}

std::string ResolveDirectObject(
    const RepositoryContext& context,
    const std::string& source,
    std::string* error) {
  std::string value = Trim(source);
  if (value.empty()) {
    if (error != nullptr) {
      *error = "An object name is required.";
    }
    return "";
  }

  std::string objectId;
  if (value == "HEAD" || value == "@") {
    objectId = context.headObjectId;
  } else if (value.rfind("refs/", 0) == 0) {
    objectId = ResolveHeadObject(
        context.gitDirectory,
        context.commonGitDirectory,
        "ref: " + value);
  } else {
    objectId = ResolveHeadObject(
        context.gitDirectory,
        context.commonGitDirectory,
        "ref: refs/heads/" + value);
    if (objectId.empty()) {
      objectId = ResolveHeadObject(
          context.gitDirectory,
          context.commonGitDirectory,
          "ref: refs/remotes/" + value);
    }
    if (objectId.empty()) {
      objectId = ResolveHeadObject(
          context.gitDirectory,
          context.commonGitDirectory,
          "ref: refs/tags/" + value);
    }
  }
  if (!objectId.empty()) {
    return objectId;
  }

  const bool hexadecimal =
      std::all_of(
          value.begin(),
          value.end(),
          [](char character) {
            return IsHexCharacter(character);
          });
  if (hexadecimal && value.size() == 40) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
          return static_cast<char>(std::tolower(character));
        });
    return value;
  }
  if (hexadecimal && value.size() >= 4 && value.size() < 40) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
          return static_cast<char>(std::tolower(character));
        });
    return ResolveAbbreviatedObject(
        context.commonGitDirectory,
        value,
        error);
  }
  if (error != nullptr) {
    *error = "Invalid object name: " + source;
  }
  return "";
}

bool PeelAnnotatedTags(
    const fs::path& commonGitDirectory,
    std::string* objectId,
    ObjectData* object,
    std::string* error) {
  std::set<std::string> visited;
  while (visited.insert(*objectId).second) {
    if (!ReadObject(
            commonGitDirectory,
            *objectId,
            object,
            error)) {
      return false;
    }
    if (object->type != "tag") {
      return true;
    }
    const std::string target =
        CommitHeaderValue(object->payload, "object");
    if (target.empty()) {
      if (error != nullptr) {
        *error = "Annotated tag " + *objectId + " has no target object.";
      }
      return false;
    }
    *objectId = target;
  }
  if (error != nullptr) {
    *error = "Annotated tag chain contains a cycle.";
  }
  return false;
}

bool ResolveTreeObject(
    const fs::path& commonGitDirectory,
    std::string* objectId,
    std::string* error) {
  ObjectData object;
  if (!PeelAnnotatedTags(
          commonGitDirectory,
          objectId,
          &object,
          error)) {
    return false;
  }
  if (object.type == "commit") {
    const std::string tree =
        CommitHeaderValue(object.payload, "tree");
    if (tree.empty()) {
      if (error != nullptr) {
        *error = "Commit " + *objectId + " has no tree.";
      }
      return false;
    }
    *objectId = tree;
    return true;
  }
  if (object.type == "tree") {
    return true;
  }
  if (error != nullptr) {
    *error =
        "Git object " + *objectId +
        " is not a commit, tree, or annotated tag.";
  }
  return false;
}

std::string ResolveTreePath(
    const fs::path& commonGitDirectory,
    const std::string& treeObjectId,
    const std::string& path,
    std::string* error) {
  fs::path normalized = fs::path(path).lexically_normal();
  if (normalized.empty() || normalized == ".") {
    return treeObjectId;
  }
  if (normalized.is_absolute()) {
    normalized = normalized.relative_path();
  }

  std::string current = treeObjectId;
  for (const fs::path& componentPath : normalized) {
    const std::string component =
        componentPath.generic_string();
    if (component.empty() || component == ".") {
      continue;
    }
    if (component == "..") {
      if (error != nullptr) {
        *error = "Object paths cannot traverse above the tree root.";
      }
      return "";
    }
    std::vector<TreeEntry> entries;
    if (!ReadTreeEntries(
            commonGitDirectory,
            current,
            &entries,
            error)) {
      return "";
    }
    const auto found = std::find_if(
        entries.begin(),
        entries.end(),
        [&component](const TreeEntry& entry) {
          return entry.path == component;
        });
    if (found == entries.end()) {
      if (error != nullptr) {
        *error = "Path '" + path + "' does not exist in the object.";
      }
      return "";
    }
    current = ObjectIdToHex(found->objectId);
  }
  return current;
}

std::string ResolveObjectName(
    const RepositoryContext& context,
    const std::string& source,
    std::string* error) {
  std::string expression = Trim(source);
  if (expression.empty()) {
    if (error != nullptr) {
      *error = "An object name is required.";
    }
    return "";
  }

  std::string objectPath;
  const size_t pathSeparator = expression.find(':');
  if (pathSeparator != std::string::npos) {
    objectPath = expression.substr(pathSeparator + 1);
    expression = expression.substr(0, pathSeparator);
  }

  std::string requestedType;
  static const std::vector<std::string> suffixes = {
      "^{commit}", "^{tree}", "^{blob}", "^{}"};
  for (const std::string& suffix : suffixes) {
    if (expression.size() >= suffix.size() &&
        expression.compare(
            expression.size() - suffix.size(),
            suffix.size(),
            suffix) == 0) {
      requestedType = suffix == "^{}"
          ? "peel"
          : suffix.substr(2, suffix.size() - 3);
      expression.erase(expression.size() - suffix.size());
      break;
    }
  }

  std::string objectId;
  if (expression.find('~') != std::string::npos) {
    objectId = ResolveRevision(context, expression, error);
  } else {
    objectId = ResolveDirectObject(context, expression, error);
  }
  if (objectId.empty()) {
    return "";
  }

  if (!requestedType.empty()) {
    ObjectData object;
    if (!PeelAnnotatedTags(
            context.commonGitDirectory,
            &objectId,
            &object,
            error)) {
      return "";
    }
    if (requestedType == "tree" && object.type == "commit") {
      objectId = CommitHeaderValue(object.payload, "tree");
      if (objectId.empty()) {
        if (error != nullptr) {
          *error = "Commit has no tree.";
        }
        return "";
      }
    } else if (requestedType != "peel" &&
               object.type != requestedType) {
      if (error != nullptr) {
        *error =
            "Git object " + objectId +
            " is not a " + requestedType + ".";
      }
      return "";
    }
  }

  if (!objectPath.empty() || pathSeparator != std::string::npos) {
    if (!ResolveTreeObject(
            context.commonGitDirectory,
            &objectId,
            error)) {
      return "";
    }
    objectId = ResolveTreePath(
        context.commonGitDirectory,
        objectId,
        objectPath,
        error);
  }
  return objectId;
}

std::string NormalizedTreeMode(
    const std::string& mode) {
  return mode == "40000" ? "040000" : mode;
}

std::string TreeEntryType(
    const std::string& mode) {
  if (mode == "40000" || mode == "040000") {
    return "tree";
  }
  if (mode == "160000") {
    return "commit";
  }
  return "blob";
}

bool ReadTreeRecursive(
    const fs::path& commonGitDirectory,
    const std::string& treeObjectId,
    const std::string& prefix,
    std::map<std::string, TreeEntry>* entries,
    std::string* error) {
  std::vector<TreeEntry> children;
  if (!ReadTreeEntries(
          commonGitDirectory,
          treeObjectId,
          &children,
          error)) {
    return false;
  }
  for (const TreeEntry& child : children) {
    const std::string path =
        prefix.empty() ? child.path : prefix + "/" + child.path;
    if (child.mode == "40000" || child.mode == "040000") {
      if (!ReadTreeRecursive(
              commonGitDirectory,
              ObjectIdToHex(child.objectId),
              path,
              entries,
              error)) {
        return false;
      }
    } else {
      (*entries)[path] = {path, child.mode, child.objectId};
    }
  }
  return true;
}

bool ReadHeadTree(
    const fs::path& commonGitDirectory,
    const std::string& headObjectId,
    std::map<std::string, TreeEntry>* entries,
    std::string* error) {
  entries->clear();
  if (headObjectId.empty()) {
    return true;
  }
  ObjectData commit;
  if (!ReadCommitObject(
          commonGitDirectory,
          headObjectId,
          &commit,
          error)) {
    return false;
  }
  const std::string treeObjectId =
      CommitHeaderValue(commit.payload, "tree");
  if (treeObjectId.empty()) {
    if (error != nullptr) {
      *error = "Commit " + headObjectId + " has no tree.";
    }
    return false;
  }
  return ReadTreeRecursive(
      commonGitDirectory,
      treeObjectId,
      "",
      entries,
      error);
}

bool LoadRepositoryContext(
    const std::string& startPath,
    RepositoryContext* context,
    std::string* error) {
  if (!DiscoverRepository(
          startPath,
          &context->repositoryPath,
          &context->gitDirectory)) {
    if (error != nullptr) {
      *error = "Not a Git repository: " + NormalizeInputPath(startPath);
    }
    return false;
  }
  context->commonGitDirectory =
      ResolveCommonGitDirectory(context->gitDirectory);
  context->headText = Trim(ReadTextFile(context->gitDirectory / "HEAD"));
  if (context->headText.empty()) {
    if (error != nullptr) {
      *error = "Git repository has no readable HEAD.";
    }
    return false;
  }
  context->headObjectId = ResolveHeadObject(
      context->gitDirectory,
      context->commonGitDirectory,
      context->headText);
  return true;
}

bool ReadIndexEntries(
    const RepositoryContext& context,
    std::vector<IndexEntry>* entries,
    uint32_t* version,
    std::string* error) {
  entries->clear();
  *version = 0;
  return ReadIndex(
      context.gitDirectory / "index",
      entries,
      version,
      error);
}

std::string ReadConfigValue(
    const fs::path& gitDirectory,
    const std::string& section,
    const std::string& key) {
  return ConfigValueFromFile(gitDirectory / "config", section, key);
}

std::string ModeString(uint32_t mode) {
  std::ostringstream output;
  output << std::oct << mode;
  return output.str();
}

std::string ReadBlob(
    const fs::path& commonGitDirectory,
    const std::array<uint8_t, 20>& objectId,
    std::string* error) {
  ObjectData object;
  if (!ReadObject(
          commonGitDirectory,
          ObjectIdToHex(objectId),
          &object,
          error)) {
    return "";
  }
  if (object.type != "blob") {
    if (error != nullptr) {
      *error = "Git object " + ObjectIdToHex(objectId) + " is not a blob.";
    }
    return "";
  }
  return object.payload;
}

bool IsPathInside(
    const fs::path& repositoryPath,
    const fs::path& candidate) {
  const fs::path repository = repositoryPath.lexically_normal();
  const fs::path normalizedCandidate = candidate.lexically_normal();
  if (normalizedCandidate == repository) {
    return true;
  }
  const fs::path relative =
      normalizedCandidate.lexically_relative(repository);
  const std::string value = relative.generic_string();
  return !value.empty() &&
      value != ".." &&
      value.rfind("../", 0) != 0;
}

std::string RelativePathOrEmpty(
    const fs::path& repositoryPath,
    const fs::path& candidate) {
  if (!IsPathInside(repositoryPath, candidate)) {
    return "";
  }
  if (candidate.lexically_normal() == repositoryPath.lexically_normal()) {
    return ".";
  }
  return candidate.lexically_normal()
      .lexically_relative(repositoryPath.lexically_normal())
      .generic_string();
}

bool PathMatchesSpec(
    const std::string& relative,
    const fs::path& basePath,
    const fs::path& repositoryPath,
    const std::string& spec) {
  if (spec.empty() || spec[0] == '-') {
    return false;
  }
  fs::path requested(spec);
  if (!requested.is_absolute()) {
    requested = (basePath / requested).lexically_normal();
  } else {
    requested = requested.lexically_normal();
  }
  const std::string requestedRelative =
      RelativePathOrEmpty(repositoryPath, requested);
  if (requestedRelative.empty()) {
    return false;
  }
  if (requestedRelative == ".") {
    return true;
  }
  return relative == requestedRelative ||
      relative.rfind(requestedRelative + "/", 0) == 0;
}

bool PathRequested(
    const std::string& relative,
    const fs::path& basePath,
    const fs::path& repositoryPath,
    const std::vector<std::string>& specs) {
  for (const std::string& spec : specs) {
    if (spec == "-A" || spec == "--all") {
      return true;
    }
    if (PathMatchesSpec(
            relative,
            basePath,
            repositoryPath,
            spec)) {
      return true;
    }
  }
  return false;
}

bool PathWithinBase(
    const std::string& relative,
    const fs::path& basePath,
    const fs::path& repositoryPath) {
  const std::string baseRelative =
      RelativePathOrEmpty(repositoryPath, basePath);
  if (baseRelative.empty()) {
    return false;
  }
  return baseRelative == "." ||
      relative == baseRelative ||
      relative.rfind(baseRelative + "/", 0) == 0;
}

std::string CommandRelativePath(
    const std::string& relative,
    const fs::path& basePath,
    const fs::path& repositoryPath) {
  return (repositoryPath / fs::path(relative))
      .lexically_normal()
      .lexically_relative(basePath.lexically_normal())
      .generic_string();
}

bool PathMatchesReadSpec(
    const std::string& relative,
    const fs::path& basePath,
    const fs::path& repositoryPath,
    const std::vector<std::string>& specs) {
  if (specs.empty()) {
    return PathWithinBase(relative, basePath, repositoryPath);
  }
  const std::string baseRelative =
      RelativePathOrEmpty(repositoryPath, basePath);
  for (std::string spec : specs) {
    if (spec.empty()) {
      continue;
    }
    bool top = false;
    if (spec.rfind(":(top)", 0) == 0) {
      top = true;
      spec = spec.substr(6);
    }
    while (spec.rfind("./", 0) == 0) {
      spec = spec.substr(2);
    }
    if (spec.empty() || spec == ".") {
      if (top ||
          PathWithinBase(relative, basePath, repositoryPath)) {
        return true;
      }
      continue;
    }

    const bool absolute = fs::path(spec).is_absolute();
    const bool hasGlob = HasGlobCharacters(spec);
    if (absolute && !hasGlob) {
      const std::string requestedRelative =
          RelativePathOrEmpty(
              repositoryPath,
              fs::path(spec).lexically_normal());
      if (!requestedRelative.empty() &&
          (relative == requestedRelative ||
           relative.rfind(requestedRelative + "/", 0) == 0)) {
        return true;
      }
      continue;
    }

    if (!hasGlob) {
      const fs::path requestedPath =
          (top ? repositoryPath : basePath) /
          fs::path(spec);
      const std::string requested =
          RelativePathOrEmpty(
              repositoryPath,
              requestedPath.lexically_normal());
      if (!requested.empty() &&
          (relative == requested ||
           relative.rfind(requested + "/", 0) == 0)) {
        return true;
      }
      continue;
    }

    if (spec.find('/') == std::string::npos) {
      if (!top &&
          !PathWithinBase(relative, basePath, repositoryPath)) {
        continue;
      }
      const std::string commandRelative =
          top
              ? relative
              : CommandRelativePath(
                  relative,
                  basePath,
                  repositoryPath);
      if (commandRelative.empty()) {
        continue;
      }
      const std::string filename =
          fs::path(commandRelative).filename().generic_string();
      if (GlobMatchPathspec(spec, filename)) {
        return true;
      }
      continue;
    }
    const std::string pattern = fs::path(
        top || baseRelative == "."
            ? spec
            : baseRelative + "/" + spec)
        .lexically_normal()
        .generic_string();
    if (GlobMatchPathspec(pattern, relative)) {
      return true;
    }
  }
  return false;
}

bool PathExplicitlyMatchesReadSpec(
    const std::string& relative,
    const fs::path& basePath,
    const fs::path& repositoryPath,
    const std::vector<std::string>& specs) {
  if (specs.empty()) {
    return true;
  }
  const std::string baseRelative =
      RelativePathOrEmpty(repositoryPath, basePath);
  for (std::string spec : specs) {
    if (spec.empty()) {
      continue;
    }
    bool top = false;
    if (spec.rfind(":(top)", 0) == 0) {
      top = true;
      spec = spec.substr(6);
    }
    while (spec.rfind("./", 0) == 0) {
      spec = spec.substr(2);
    }
    if (spec.empty() || spec == ".") {
      if (top ||
          PathWithinBase(relative, basePath, repositoryPath)) {
        return true;
      }
      continue;
    }
    const bool absolute = fs::path(spec).is_absolute();
    const bool hasGlob = HasGlobCharacters(spec);
    if (absolute && !hasGlob) {
      const std::string requested =
          RelativePathOrEmpty(
              repositoryPath,
              fs::path(spec).lexically_normal());
      if (!requested.empty() && relative == requested) {
        return true;
      }
      continue;
    }
    if (!hasGlob) {
      const fs::path requestedPath =
          (top ? repositoryPath : basePath) /
          fs::path(spec);
      const std::string requested =
          RelativePathOrEmpty(
              repositoryPath,
              requestedPath.lexically_normal());
      if (!requested.empty() && relative == requested) {
        return true;
      }
      continue;
    }
    if (spec.find('/') == std::string::npos) {
      if (!top &&
          !PathWithinBase(relative, basePath, repositoryPath)) {
        continue;
      }
      const std::string commandRelative =
          top
              ? relative
              : CommandRelativePath(
                  relative,
                  basePath,
                  repositoryPath);
      if (!commandRelative.empty() &&
          GlobMatchPathspec(
              spec,
              fs::path(commandRelative)
                  .filename()
                  .generic_string())) {
        return true;
      }
      continue;
    }
    const std::string pattern = fs::path(
        top || baseRelative == "."
            ? spec
            : baseRelative + "/" + spec)
        .lexically_normal()
        .generic_string();
    if (GlobMatchPathspec(pattern, relative)) {
      return true;
    }
  }
  return false;
}

struct CleanScanResult {
  std::vector<std::string> removable;
  bool protectedContent = false;
  bool hasCandidate = false;
};

bool IsNestedRepositoryDirectory(const fs::path& path) {
  std::error_code error;
  const fs::file_status status =
      fs::symlink_status(path / ".git", error);
  return !error &&
      status.type() != fs::file_type::not_found &&
      status.type() != fs::file_type::none;
}

bool CleanPathIgnored(
    const IgnoreDecision& decision,
    const CleanOptions& options) {
  if (options.ignoredOnly) {
    return decision.ignored;
  }
  return !decision.ignored;
}

void AppendCommandExcludeRules(
    const std::vector<std::string>& excludes,
    const std::string& basePath,
    std::vector<IgnoreRule>* rules) {
  for (const std::string& exclude : excludes) {
    IgnoreRule rule;
    if (!ParseIgnoreRule(exclude, basePath, &rule)) {
      continue;
    }
    rule.sourcePath = "<command-line>";
    rule.displayPattern = exclude;
    rules->push_back(rule);
  }
}

CleanScanResult ScanCleanDirectory(
    const fs::path& directory,
    const std::string& relativeDirectory,
    const std::vector<IgnoreRule>& inheritedRules,
    const std::vector<IgnoreRule>& commandRules,
    const std::set<std::string>& trackedPaths,
    const std::set<std::string>& trackedDirectories,
    const fs::path& basePath,
    const fs::path& repositoryPath,
    const CleanOptions& options,
    const IgnoreDecision& inheritedDecision,
    std::vector<std::string>* skippedRepositories) {
  CleanScanResult result;
  std::vector<IgnoreRule> rules = inheritedRules;
  if (!options.removeIgnored) {
    ReadIgnoreRulesFile(
        directory / ".gitignore",
        relativeDirectory,
        &rules);
  }
  std::error_code iteratorError;
  fs::directory_iterator iterator(
      directory,
      fs::directory_options::skip_permission_denied,
      iteratorError);
  const fs::directory_iterator end;
  if (iteratorError) {
    result.protectedContent = true;
    return result;
  }

  std::vector<IgnoreRule> effectiveRules = rules;
  effectiveRules.insert(
      effectiveRules.end(),
      commandRules.begin(),
      commandRules.end());
  while (iterator != end) {
    const fs::path path = iterator->path();
    const std::string name = path.filename().generic_string();
    const std::string relative =
        relativeDirectory.empty()
            ? name
            : relativeDirectory + "/" + name;
    if (relativeDirectory.empty() && name == ".git") {
      iterator.increment(iteratorError);
      continue;
    }

    std::error_code typeError;
    const fs::file_status status = fs::symlink_status(path, typeError);
    if (typeError) {
      result.protectedContent = true;
      iterator.increment(iteratorError);
      continue;
    }
    const bool directoryEntry = fs::is_directory(status);
    const IgnoreDecision decision = EvaluateIgnoreRules(
        relative,
        directoryEntry,
        effectiveRules,
        inheritedDecision);
    const bool selected = PathMatchesReadSpec(
        relative,
        basePath,
        repositoryPath,
        options.paths);
    const bool tracked =
        trackedPaths.find(relative) != trackedPaths.end();

    if (!directoryEntry) {
      if (tracked) {
        result.protectedContent = true;
      } else if (selected &&
                 CleanPathIgnored(decision, options)) {
        result.removable.push_back(relative);
        result.hasCandidate = true;
      } else {
        result.protectedContent = true;
      }
      iterator.increment(iteratorError);
      continue;
    }

    const bool trackedDescendant =
        trackedDirectories.find(relative) != trackedDirectories.end();
    if (IsNestedRepositoryDirectory(path)) {
      const bool directoryRequested =
          options.directories || !options.paths.empty();
      if (selected &&
          directoryRequested &&
          CleanPathIgnored(decision, options) &&
          options.force < 2) {
        skippedRepositories->push_back(relative);
        result.protectedContent = true;
      } else if (selected &&
                 directoryRequested &&
                 CleanPathIgnored(decision, options) &&
                 options.force >= 2 &&
                 !trackedDescendant) {
        result.removable.push_back(relative);
        result.hasCandidate = true;
      } else if (selected || trackedDescendant) {
        result.protectedContent = true;
      }
      iterator.increment(iteratorError);
      continue;
    }

    if (decision.ignored &&
        !options.removeIgnored &&
        !options.ignoredOnly &&
        !trackedDescendant) {
      result.protectedContent = true;
      iterator.increment(iteratorError);
      continue;
    }

    const bool canSearchChildren =
        trackedDescendant ||
        options.directories ||
        !options.paths.empty();
    if (!canSearchChildren) {
      result.protectedContent = true;
      iterator.increment(iteratorError);
      continue;
    }

    const CleanScanResult children = ScanCleanDirectory(
        path,
        relative,
        rules,
        commandRules,
        trackedPaths,
        trackedDirectories,
        basePath,
        repositoryPath,
        options,
        decision,
        skippedRepositories);

    const bool eligibleDirectory =
        selected &&
        CleanPathIgnored(decision, options) &&
        !trackedDescendant &&
        (options.directories || !options.paths.empty()) &&
        !children.protectedContent;
    if (eligibleDirectory) {
      result.removable.push_back(relative);
      result.hasCandidate = true;
    } else {
      result.removable.insert(
          result.removable.end(),
          children.removable.begin(),
          children.removable.end());
      result.hasCandidate =
          result.hasCandidate || children.hasCandidate;
      result.protectedContent =
          result.protectedContent || children.protectedContent;
    }
    iterator.increment(iteratorError);
  }
  if (iteratorError) {
    result.protectedContent = true;
  }
  return result;
}

void AddIndexEntryToTree(
    TreeNode* root,
    const IndexEntry& entry);

bool WriteTreeNode(
    const fs::path& commonGitDirectory,
    const TreeNode& node,
    std::string* objectId,
    std::string* error) {
  struct Item {
    std::string name;
    bool directory = false;
    std::string mode;
    std::array<uint8_t, 20> objectId {};
  };
  std::vector<Item> items;
  for (const auto& item : node.directories) {
    std::string childObjectId;
    if (!WriteTreeNode(
            commonGitDirectory,
            item.second,
            &childObjectId,
            error)) {
      return false;
    }
    std::array<uint8_t, 20> childId {};
    if (!HexToObjectId(childObjectId, &childId)) {
      if (error != nullptr) {
        *error = "Cannot encode generated tree object id.";
      }
      return false;
    }
    items.push_back({item.first, true, "40000", childId});
  }
  for (const auto& item : node.files) {
    items.push_back({
        item.first,
        false,
        item.second.mode,
        item.second.objectId});
  }
  std::sort(
      items.begin(),
      items.end(),
      [](const Item& left, const Item& right) {
        const std::string leftKey = left.name + (left.directory ? "/" : "");
        const std::string rightKey =
            right.name + (right.directory ? "/" : "");
        return leftKey < rightKey;
      });

  std::string payload;
  for (const Item& item : items) {
    payload += item.mode;
    payload.push_back(' ');
    payload += item.name;
    payload.push_back('\0');
    payload.append(
        reinterpret_cast<const char*>(item.objectId.data()),
        item.objectId.size());
  }
  return WriteLooseObject(
      commonGitDirectory,
      "tree",
      payload,
      objectId,
      error);
}

bool BuildTreeFromIndex(
    const fs::path& commonGitDirectory,
    const std::vector<IndexEntry>& entries,
    std::string* objectId,
    std::string* error) {
  TreeNode root;
  for (const IndexEntry& entry : entries) {
    if (entry.stage != 0) {
      if (error != nullptr) {
        *error = "Cannot commit while the index contains unmerged paths.";
      }
      return false;
    }
    AddIndexEntryToTree(&root, entry);
  }
  return WriteTreeNode(commonGitDirectory, root, objectId, error);
}

std::string CommitAuthor(
    const fs::path& commonGitDirectory,
    std::string* error) {
  const std::string name = ReadConfigValue(commonGitDirectory, "user", "name");
  const std::string email = ReadConfigValue(commonGitDirectory, "user", "email");
  if (name.empty() || email.empty()) {
    if (error != nullptr) {
      *error =
          "Author identity unknown. Configure user.name and user.email before committing.";
    }
    return "";
  }
  return name + " <" + email + ">";
}

std::string CurrentGitTimestamp() {
  const std::time_t now = std::time(nullptr);
  struct tm localTime {};
  if (localtime_r(&now, &localTime) == nullptr) {
    return std::to_string(static_cast<long long>(now)) + " +0000";
  }
  char offset[16] = {};
  if (std::strftime(offset, sizeof(offset), "%z", &localTime) == 0) {
    return std::to_string(static_cast<long long>(now)) + " +0000";
  }
  return std::to_string(static_cast<long long>(now)) + " " + offset;
}

bool ShouldLogReferenceUpdates(const RepositoryContext& context) {
  const std::string configured = LowercaseAscii(
      Trim(ReadConfigValue(
          context.commonGitDirectory,
          "core",
          "logallrefupdates")));
  return configured != "false" &&
      configured != "no" &&
      configured != "off" &&
      configured != "0";
}

bool ReferenceFilesystemIgnoresCase(const RepositoryContext& context) {
  const std::string configured = LowercaseAscii(
      Trim(ReadConfigValue(
          context.commonGitDirectory,
          "core",
          "ignorecase")));
  if (!configured.empty()) {
    return configured == "true" ||
        configured == "yes" ||
        configured == "on" ||
        configured == "1";
  }

  const fs::path headsDirectory =
      context.commonGitDirectory / "refs" / "heads";
  std::error_code error;
  if (!fs::is_directory(headsDirectory, error) || error) {
    return false;
  }
  const fs::path alternateDirectory =
      headsDirectory.parent_path() / "HEADS";
  error.clear();
  return fs::equivalent(headsDirectory, alternateDirectory, error) &&
      !error;
}

std::string ReflogActor(const RepositoryContext& context) {
  std::string name = ReadConfigValue(
      context.commonGitDirectory,
      "user",
      "name");
  std::string email = ReadConfigValue(
      context.commonGitDirectory,
      "user",
      "email");
  const char* environmentName = std::getenv("GIT_COMMITTER_NAME");
  const char* environmentEmail = std::getenv("GIT_COMMITTER_EMAIL");
  if (name.empty() && environmentName != nullptr) {
    name = environmentName;
  }
  if (email.empty() && environmentEmail != nullptr) {
    email = environmentEmail;
  }
  if (name.empty()) {
    name = "Harmony Developer";
  }
  if (email.empty()) {
    email = "harmony@pc.local";
  }
  return name + " <" + email + ">";
}

fs::path ReflogPath(
    const RepositoryContext& context,
    const std::string& ref) {
  if (ref.empty() || ref == "HEAD") {
    return context.gitDirectory / "logs" / "HEAD";
  }
  const std::string normalized =
      ref.rfind("refs/", 0) == 0
          ? ref
          : "refs/heads/" + ref;
  return context.commonGitDirectory / "logs" / normalized;
}

bool AppendReflog(
    const RepositoryContext& context,
    const std::string& ref,
    const std::string& oldObjectId,
    const std::string& newObjectId,
    const std::string& message,
    std::string* error,
    bool forceCreate = false) {
  if (!forceCreate && !ShouldLogReferenceUpdates(context)) {
    return true;
  }
  const fs::path path = ReflogPath(context, ref);
  if (!EnsureDirectory(path.parent_path(), error)) {
    return false;
  }
  std::ofstream output(path, std::ios::binary | std::ios::app);
  if (!output) {
    if (error != nullptr) {
      *error = "Cannot append " + path.string();
    }
    return false;
  }
  const std::string zeroId(40, '0');
  const std::string oldValue =
      oldObjectId.empty() ? zeroId : oldObjectId;
  const std::string newValue =
      newObjectId.empty() ? zeroId : newObjectId;
  std::string safeMessage = message;
  for (char& character : safeMessage) {
    if (character == '\n' || character == '\r' || character == '\t') {
      character = ' ';
    }
  }
  output << oldValue << " " << newValue << " " <<
      ReflogActor(context) << " " << CurrentGitTimestamp() << "\t" <<
      safeMessage << "\n";
  if (!output.good()) {
    if (error != nullptr) {
      *error = "Failed while appending " + path.string();
    }
    return false;
  }
  return true;
}

bool RemoveReflog(
    const RepositoryContext& context,
    const std::string& ref,
    std::string* error) {
  const fs::path path = ReflogPath(context, ref);
  std::error_code removeError;
  if (!fs::remove(path, removeError) &&
      removeError &&
      removeError != std::errc::no_such_file_or_directory) {
    if (error != nullptr) {
      *error = "Cannot delete " + path.string() + ": " +
          removeError.message();
    }
    return false;
  }
  fs::path parent = path.parent_path();
  const fs::path logsDirectory = context.commonGitDirectory / "logs";
  while (parent != logsDirectory &&
         IsPathInside(logsDirectory, parent)) {
    std::error_code emptyError;
    if (!fs::is_empty(parent, emptyError) || emptyError) {
      break;
    }
    std::error_code directoryError;
    fs::remove(parent, directoryError);
    if (directoryError) {
      break;
    }
    parent = parent.parent_path();
  }
  return true;
}

std::string FormatCommitTimestamp(const std::string& value) {
  const size_t separator = value.find(' ');
  if (separator == std::string::npos) {
    return value;
  }
  try {
    const std::time_t timestamp =
        static_cast<std::time_t>(std::stoll(value.substr(0, separator)));
    struct tm localTime {};
    localtime_r(&timestamp, &localTime);
    char buffer[64] = {};
    if (std::strftime(
            buffer,
            sizeof(buffer),
            "%a %b %d %H:%M:%S %Y %z",
            &localTime) != 0) {
      return buffer;
    }
  } catch (...) {
  }
  return value;
}

std::string CommitAuthorName(const std::string& authorLine) {
  const size_t timezoneSeparator = authorLine.rfind(' ');
  if (timezoneSeparator == std::string::npos) {
    return authorLine;
  }
  const size_t timestampSeparator =
      authorLine.rfind(' ', timezoneSeparator - 1);
  return timestampSeparator == std::string::npos
      ? authorLine
      : authorLine.substr(0, timestampSeparator);
}

std::string CommitAuthorTimestamp(const std::string& authorLine) {
  const size_t timezoneSeparator = authorLine.rfind(' ');
  if (timezoneSeparator == std::string::npos) {
    return "";
  }
  const size_t timestampSeparator =
      authorLine.rfind(' ', timezoneSeparator - 1);
  return timestampSeparator == std::string::npos
      ? ""
      : authorLine.substr(timestampSeparator + 1);
}

void AddIndexEntryToTree(
    TreeNode* root,
    const IndexEntry& entry) {
  TreeNode* node = root;
  size_t start = 0;
  while (start < entry.path.size()) {
    const size_t separator = entry.path.find('/', start);
    if (separator == std::string::npos) {
      node->files[entry.path.substr(start)] = {
          entry.path,
          ModeString(entry.mode),
          entry.objectId};
      return;
    }
    node = &node->directories[entry.path.substr(start, separator - start)];
    start = separator + 1;
  }
}

std::vector<std::string> SplitLines(const std::string& value) {
  std::vector<std::string> lines;
  size_t start = 0;
  while (start < value.size()) {
    const size_t newline = value.find('\n', start);
    if (newline == std::string::npos) {
      lines.push_back(value.substr(start));
      break;
    }
    lines.push_back(value.substr(start, newline - start));
    start = newline + 1;
  }
  return lines;
}

std::string UnifiedDiffBody(
    const std::string& oldContent,
    const std::string& newContent) {
  const std::vector<std::string> oldLines = SplitLines(oldContent);
  const std::vector<std::string> newLines = SplitLines(newContent);
  const size_t oldCount = oldLines.size();
  const size_t newCount = newLines.size();
  constexpr size_t kMaximumLcsCells = 4U * 1024U * 1024U;
  if (oldCount != 0 &&
      newCount > kMaximumLcsCells / oldCount) {
    std::ostringstream fallback;
    fallback << "@@ -1," << oldCount << " +1," << newCount << " @@\n";
    for (const std::string& line : oldLines) {
      fallback << '-' << line << '\n';
    }
    for (const std::string& line : newLines) {
      fallback << '+' << line << '\n';
    }
    return fallback.str();
  }
  std::vector<std::vector<uint32_t>> common(
      oldCount + 1,
      std::vector<uint32_t>(newCount + 1, 0));
  for (size_t oldIndex = oldCount; oldIndex > 0; --oldIndex) {
    for (size_t newIndex = newCount; newIndex > 0; --newIndex) {
      common[oldIndex - 1][newIndex - 1] =
          oldLines[oldIndex - 1] == newLines[newIndex - 1]
              ? common[oldIndex][newIndex] + 1
              : std::max(
                  common[oldIndex][newIndex - 1],
                  common[oldIndex - 1][newIndex]);
    }
  }

  std::vector<std::string> operations;
  size_t oldIndex = 0;
  size_t newIndex = 0;
  while (oldIndex < oldCount || newIndex < newCount) {
    if (oldIndex < oldCount && newIndex < newCount &&
        oldLines[oldIndex] == newLines[newIndex]) {
      operations.push_back(" " + oldLines[oldIndex]);
      ++oldIndex;
      ++newIndex;
    } else if (newIndex < newCount &&
               (oldIndex == oldCount ||
                common[oldIndex][newIndex + 1] >= common[oldIndex + 1][newIndex])) {
      operations.push_back("+" + newLines[newIndex]);
      ++newIndex;
    } else {
      operations.push_back("-" + oldLines[oldIndex]);
      ++oldIndex;
    }
  }

  std::ostringstream output;
  output << "@@ -1," << oldCount << " +1," << newCount << " @@\n";
  for (const std::string& operation : operations) {
    output << operation << '\n';
  }
  return output.str();
}

std::string FormatDiffFile(const DiffFile& file) {
  const bool added = file.oldObjectId.empty();
  const bool deleted = file.newObjectId.empty();
  const bool modeChanged =
      !added && !deleted && file.oldMode != file.newMode;
  const bool contentChanged =
      added || deleted || file.oldObjectId != file.newObjectId;
  const std::string oldId =
      added ? "0000000" : file.oldObjectId.substr(0, 7);
  const std::string newId =
      deleted ? "0000000" : file.newObjectId.substr(0, 7);
  std::ostringstream output;
  output << "diff --git a/" << file.path << " b/" << file.path << '\n';
  if (added) {
    output << "new file mode " << file.newMode << '\n';
  } else if (deleted) {
    output << "deleted file mode " << file.oldMode << '\n';
  } else {
    if (modeChanged) {
      output << "old mode " << file.oldMode << '\n';
      output << "new mode " << file.newMode << '\n';
    }
    if (contentChanged) {
      output << "index " << oldId << ".." << newId;
      if (!modeChanged) {
        output << " " << file.oldMode;
      }
      output << '\n';
    }
  }
  if (contentChanged) {
    output << "--- " << (added ? "/dev/null" : "a/" + file.path) << '\n';
    output << "+++ " << (deleted ? "/dev/null" : "b/" + file.path) << '\n';
    if (file.oldContent.find('\0') != std::string::npos ||
        file.newContent.find('\0') != std::string::npos) {
      output << "Binary files "
             << (added ? "/dev/null" : "a/" + file.path)
             << " and "
             << (deleted ? "/dev/null" : "b/" + file.path)
             << " differ\n";
    } else {
      output << UnifiedDiffBody(file.oldContent, file.newContent);
    }
  }
  return output.str();
}

std::string FormatDiffStat(const std::vector<DiffFile>& files) {
  if (files.empty()) {
    return "";
  }
  struct FileStat {
    std::string path;
    size_t insertions = 0;
    size_t deletions = 0;
    bool binary = false;
  };
  std::vector<FileStat> stats;
  size_t maximumPathLength = 0;
  size_t totalInsertions = 0;
  size_t totalDeletions = 0;
  for (const DiffFile& file : files) {
    FileStat stat;
    stat.path = file.path;
    stat.binary =
        file.oldContent.find('\0') != std::string::npos ||
        file.newContent.find('\0') != std::string::npos;
    if (!stat.binary) {
      std::istringstream diff(
          UnifiedDiffBody(file.oldContent, file.newContent));
      std::string line;
      bool header = true;
      while (std::getline(diff, line)) {
        if (header) {
          header = false;
        } else if (!line.empty() && line.front() == '+') {
          ++stat.insertions;
        } else if (!line.empty() && line.front() == '-') {
          ++stat.deletions;
        }
      }
      totalInsertions += stat.insertions;
      totalDeletions += stat.deletions;
    }
    maximumPathLength = std::max(maximumPathLength, stat.path.size());
    stats.push_back(stat);
  }

  std::ostringstream output;
  for (const FileStat& stat : stats) {
    output << ' ' << stat.path;
    output << std::string(
        maximumPathLength - stat.path.size(),
        ' ');
    if (stat.binary) {
      output << " | Bin\n";
      continue;
    }
    const size_t changed = stat.insertions + stat.deletions;
    output << " | " << changed << ' ';
    constexpr size_t kMaximumGraphWidth = 50;
    if (changed <= kMaximumGraphWidth) {
      output << std::string(stat.insertions, '+')
             << std::string(stat.deletions, '-');
    } else {
      const size_t insertionWidth =
          changed == 0
              ? 0
              : stat.insertions * kMaximumGraphWidth / changed;
      output << std::string(insertionWidth, '+')
             << std::string(kMaximumGraphWidth - insertionWidth, '-');
    }
    output << '\n';
  }
  output << ' ' << files.size() << " file"
         << (files.size() == 1 ? "" : "s") << " changed";
  if (totalInsertions > 0) {
    output << ", " << totalInsertions << " insertion"
           << (totalInsertions == 1 ? "" : "s") << "(+)";
  }
  if (totalDeletions > 0) {
    output << ", " << totalDeletions << " deletion"
           << (totalDeletions == 1 ? "" : "s") << "(-)";
  }
  output << '\n';
  return output.str();
}

std::string FormatCommitMessageBlock(const std::string& message) {
  std::ostringstream output;
  std::istringstream lines(message);
  std::string line;
  bool wroteLine = false;
  while (std::getline(lines, line)) {
    output << "    " << line << '\n';
    wroteLine = true;
  }
  if (!wroteLine) {
    output << "    \n";
  }
  return output.str();
}

std::string FileObjectId(const std::string& content) {
  return HashObjectId("blob", content);
}

bool BuildStagedDiffFiles(
    const RepositoryContext& context,
    const std::vector<IndexEntry>& indexEntries,
    std::vector<DiffFile>* files,
    std::string* error) {
  std::map<std::string, TreeEntry> headEntries;
  if (!ReadHeadTree(
          context.commonGitDirectory,
          context.headObjectId,
          &headEntries,
          error)) {
    return false;
  }
  std::map<std::string, IndexEntry> index;
  for (const IndexEntry& entry : indexEntries) {
    if (entry.stage == 0) {
      index[entry.path] = entry;
    }
  }
  std::set<std::string> paths;
  for (const auto& item : headEntries) {
    paths.insert(item.first);
  }
  for (const auto& item : index) {
    paths.insert(item.first);
  }
  for (const std::string& path : paths) {
    const auto head = headEntries.find(path);
    const auto current = index.find(path);
    if (head != headEntries.end() && current != index.end() &&
        head->second.objectId == current->second.objectId &&
        head->second.mode == ModeString(current->second.mode)) {
      continue;
    }
    DiffFile file;
    file.path = path;
    if (head != headEntries.end()) {
      file.oldObjectId = ObjectIdToHex(head->second.objectId);
      file.oldMode = head->second.mode;
      file.oldContent = ReadBlob(
          context.commonGitDirectory,
          head->second.objectId,
          error);
      if (error != nullptr && !error->empty()) {
        return false;
      }
    }
    if (current != index.end()) {
      file.newObjectId = ObjectIdToHex(current->second.objectId);
      file.newMode = ModeString(current->second.mode);
      file.newContent = ReadBlob(
          context.commonGitDirectory,
          current->second.objectId,
          error);
      if (error != nullptr && !error->empty()) {
        return false;
      }
    }
    files->push_back(file);
  }
  return true;
}

bool BuildTreeDiffFiles(
    const fs::path& commonGitDirectory,
    const std::map<std::string, TreeEntry>& oldEntries,
    const std::map<std::string, TreeEntry>& newEntries,
    std::vector<DiffFile>* files,
    std::string* error) {
  std::set<std::string> paths;
  for (const auto& item : oldEntries) {
    paths.insert(item.first);
  }
  for (const auto& item : newEntries) {
    paths.insert(item.first);
  }
  for (const std::string& path : paths) {
    const auto oldEntry = oldEntries.find(path);
    const auto newEntry = newEntries.find(path);
    if (oldEntry != oldEntries.end() &&
        newEntry != newEntries.end() &&
        oldEntry->second.objectId == newEntry->second.objectId &&
        oldEntry->second.mode == newEntry->second.mode) {
      continue;
    }
    DiffFile file;
    file.path = path;
    if (oldEntry != oldEntries.end()) {
      file.oldObjectId =
          ObjectIdToHex(oldEntry->second.objectId);
      file.oldMode = oldEntry->second.mode;
      file.oldContent = ReadBlob(
          commonGitDirectory,
          oldEntry->second.objectId,
          error);
      if (error != nullptr && !error->empty()) {
        return false;
      }
    }
    if (newEntry != newEntries.end()) {
      file.newObjectId =
          ObjectIdToHex(newEntry->second.objectId);
      file.newMode = newEntry->second.mode;
      file.newContent = ReadBlob(
          commonGitDirectory,
          newEntry->second.objectId,
          error);
      if (error != nullptr && !error->empty()) {
        return false;
      }
    }
    files->push_back(file);
  }
  return true;
}

bool TreeDiffTouchesPaths(
    const std::map<std::string, TreeEntry>& parentTree,
    const std::map<std::string, TreeEntry>& currentTree,
    const fs::path& basePath,
    const fs::path& repositoryPath,
    const std::vector<std::string>& paths) {
  std::set<std::string> changedPaths;
  for (const auto& item : parentTree) {
    changedPaths.insert(item.first);
  }
  for (const auto& item : currentTree) {
    changedPaths.insert(item.first);
  }
  for (const std::string& path : changedPaths) {
    const auto oldEntry = parentTree.find(path);
    const auto newEntry = currentTree.find(path);
    if (oldEntry != parentTree.end() &&
        newEntry != currentTree.end() &&
        oldEntry->second.objectId == newEntry->second.objectId &&
        oldEntry->second.mode == newEntry->second.mode) {
      continue;
    }
    if (PathMatchesReadSpec(
            path,
            basePath,
            repositoryPath,
            paths)) {
      return true;
    }
  }
  return false;
}

struct PathHistoryDecision {
  bool includeCommit = true;
  std::vector<std::string> followedParents;
};

bool AnalyzeCommitPathHistory(
    const RepositoryContext& context,
    const std::string& startPath,
    const std::string& objectId,
    const std::vector<std::string>& parents,
    const std::vector<std::string>& paths,
    PathHistoryDecision* decision,
    std::string* error) {
  decision->includeCommit = true;
  decision->followedParents = parents;
  if (paths.empty()) {
    return true;
  }
  std::map<std::string, TreeEntry> currentTree;
  if (!ReadHeadTree(
          context.commonGitDirectory,
          objectId,
          &currentTree,
          error)) {
    return false;
  }

  const fs::path basePath = CommandBasePath(startPath);
  if (parents.empty()) {
    const std::map<std::string, TreeEntry> emptyTree;
    decision->includeCommit = TreeDiffTouchesPaths(
        emptyTree,
        currentTree,
        basePath,
        context.repositoryPath,
        paths);
    return true;
  }

  for (const std::string& parent : parents) {
    std::map<std::string, TreeEntry> parentTree;
    if (!ReadHeadTree(
            context.commonGitDirectory,
            parent,
            &parentTree,
            error)) {
      return false;
    }
    if (!TreeDiffTouchesPaths(
            parentTree,
            currentTree,
            basePath,
            context.repositoryPath,
            paths)) {
      decision->includeCommit = false;
      decision->followedParents = {parent};
      return true;
    }
  }
  return true;
}

bool BuildWorkTreeDiffFiles(
    const RepositoryContext& context,
    const std::vector<IndexEntry>& indexEntries,
    std::vector<DiffFile>* files,
    std::string* error) {
  for (const IndexEntry& entry : indexEntries) {
    if (entry.stage != 0) {
      continue;
    }
    const fs::path filePath = context.repositoryPath / fs::path(entry.path);
    struct stat fileStat {};
    const bool exists = lstat(filePath.c_str(), &fileStat) == 0;
    std::string content;
    uint32_t mode = entry.mode;
    if (exists &&
        !ReadWorkingTreeFile(
            filePath,
            &content,
            &mode,
            nullptr,
            error)) {
      return false;
    }
    if (exists &&
        FileObjectId(content) == ObjectIdToHex(entry.objectId) &&
        mode == entry.mode) {
      continue;
    }
    DiffFile file;
    file.path = entry.path;
    file.oldObjectId = ObjectIdToHex(entry.objectId);
    file.oldMode = ModeString(entry.mode);
    file.oldContent = ReadBlob(
        context.commonGitDirectory,
        entry.objectId,
        error);
    if (error != nullptr && !error->empty()) {
      return false;
    }
    if (exists) {
      file.newObjectId = FileObjectId(content);
      file.newMode = ModeString(mode);
      file.newContent = content;
    }
    files->push_back(file);
  }
  return true;
}

std::string ReadAuthorLine(const std::string& payload) {
  return CommitHeaderValue(payload, "author");
}

std::string ReadParent(const std::string& payload) {
  return CommitHeaderValue(payload, "parent");
}

std::vector<std::string> ReadParents(const std::string& payload) {
  std::vector<std::string> parents;
  std::istringstream lines(payload);
  std::string line;
  while (std::getline(lines, line)) {
    if (line.empty()) {
      break;
    }
    if (line.rfind("parent ", 0) == 0) {
      parents.push_back(line.substr(7));
    }
  }
  return parents;
}

bool ValidBranchName(const std::string& name) {
  if (name.empty() ||
      name.front() == '/' ||
      name.back() == '/' ||
      name.back() == '.' ||
      name == "@" ||
      name.find("..") != std::string::npos ||
      name.find("@{") != std::string::npos ||
      name.find("//") != std::string::npos) {
    return false;
  }
  size_t componentStart = 0;
  while (componentStart < name.size()) {
    const size_t componentEnd = name.find('/', componentStart);
    const std::string component = name.substr(
        componentStart,
        componentEnd == std::string::npos
            ? std::string::npos
            : componentEnd - componentStart);
    if (component.empty() ||
        component.front() == '.' ||
        (component.size() >= 5 &&
         component.substr(component.size() - 5) == ".lock")) {
      return false;
    }
    if (componentEnd == std::string::npos) {
      break;
    }
    componentStart = componentEnd + 1;
  }
  for (unsigned char character : name) {
    if (character <= 0x20 ||
        character == 0x7f ||
        character == '~' ||
        character == '^' ||
        character == ':' ||
        character == '?' ||
        character == '*' ||
        character == '[' ||
        character == '\\') {
      return false;
    }
  }
  return true;
}

bool ValidRemoteName(const std::string& name) {
  if (name.empty() ||
      name == "." ||
      name == ".." ||
      name.front() == '-' ||
      name.find("..") != std::string::npos) {
    return false;
  }
  for (unsigned char character : name) {
    if (character <= 0x20 ||
        character == 0x7f ||
        character == '~' ||
        character == '^' ||
        character == ':' ||
        character == '?' ||
        character == '*' ||
        character == '[' ||
        character == '\\') {
      return false;
    }
  }
  return true;
}

fs::path CommandBasePath(const std::string& startPath) {
  fs::path basePath = AbsolutePath(startPath);
  std::error_code error;
  if (!fs::is_directory(basePath, error)) {
    basePath = basePath.parent_path();
  }
  return basePath.lexically_normal();
}

std::map<std::string, IndexEntry> IndexEntryMap(
    const std::vector<IndexEntry>& entries) {
  std::map<std::string, IndexEntry> result;
  for (const IndexEntry& entry : entries) {
    if (entry.stage == 0) {
      result[entry.path] = entry;
    }
  }
  return result;
}

std::vector<IndexEntry> IndexEntryVector(
    const std::map<std::string, IndexEntry>& entries) {
  std::vector<IndexEntry> result;
  result.reserve(entries.size());
  for (const auto& item : entries) {
    result.push_back(item.second);
  }
  return result;
}

bool SameIndexContent(
    const IndexEntry& left,
    const IndexEntry& right) {
  return left.path == right.path &&
      left.objectId == right.objectId &&
      left.mode == right.mode &&
      left.stage == right.stage;
}

bool WriteReference(
    const fs::path& path,
    const std::string& objectId,
    std::string* error) {
  return WriteAtomicFile(path, objectId + "\n", error);
}

bool RemovePackedReference(
    const fs::path& packedRefsPath,
    const std::string& refName,
    bool* removed,
    std::string* error) {
  *removed = false;
  std::ifstream input(packedRefsPath, std::ios::binary);
  if (!input) {
    return true;
  }
  std::vector<std::string> lines;
  std::string line;
  bool omitPeeledLine = false;
  while (std::getline(input, line)) {
    if (omitPeeledLine && !line.empty() && line.front() == '^') {
      omitPeeledLine = false;
      continue;
    }
    omitPeeledLine = false;
    const size_t separator = line.find(' ');
    if (separator != std::string::npos &&
        Trim(line.substr(separator + 1)) == refName) {
      *removed = true;
      omitPeeledLine = true;
      continue;
    }
    lines.push_back(line);
  }
  if (!*removed) {
    return true;
  }
  std::string content;
  for (const std::string& retainedLine : lines) {
    content += retainedLine;
    content.push_back('\n');
  }
  return WriteAtomicFile(packedRefsPath, content, error);
}

bool DeleteReference(
    const RepositoryContext& context,
    const std::string& refName,
    bool* removed,
    std::string* error) {
  *removed = false;
  const fs::path loosePath = context.commonGitDirectory / refName;
  std::error_code existsError;
  if (fs::exists(loosePath, existsError)) {
    std::error_code removeError;
    if (!fs::remove(loosePath, removeError) || removeError) {
      if (error != nullptr) {
        *error =
            "Cannot delete " + refName + ": " + removeError.message();
      }
      return false;
    }
    *removed = true;
  }
  bool packedRemoved = false;
  if (!RemovePackedReference(
          context.commonGitDirectory / "packed-refs",
          refName,
          &packedRemoved,
          error)) {
    return false;
  }
  *removed = *removed || packedRemoved;
  return true;
}

struct LooseReference {
  std::string refName;
  fs::path path;
  std::string content;
};

std::vector<LooseReference> ReadLooseReferencesWithPrefix(
    const fs::path& commonGitDirectory,
    const std::string& refPrefix) {
  std::vector<LooseReference> references;
  const fs::path base = commonGitDirectory / refPrefix;
  std::error_code error;
  if (!fs::is_directory(base, error)) {
    return references;
  }
  fs::recursive_directory_iterator iterator(
      base,
      fs::directory_options::skip_permission_denied,
      error);
  const fs::recursive_directory_iterator end;
  while (!error && iterator != end) {
    std::error_code statusError;
    if (fs::is_regular_file(iterator->path(), statusError) &&
        !statusError) {
      references.push_back({
          refPrefix + RelativeGitPath(base, iterator->path()),
          iterator->path(),
          ReadTextFile(iterator->path())});
    }
    error.clear();
    iterator.increment(error);
  }
  return references;
}

std::map<std::string, std::string> ReadReferenceValuesWithPrefix(
    const fs::path& commonGitDirectory,
    const std::string& refPrefix) {
  std::map<std::string, std::string> references;
  for (const LooseReference& reference :
       ReadLooseReferencesWithPrefix(commonGitDirectory, refPrefix)) {
    references[reference.refName] = Trim(reference.content);
  }

  std::istringstream packedRefs(
      ReadTextFile(commonGitDirectory / "packed-refs"));
  std::string line;
  while (std::getline(packedRefs, line)) {
    if (line.empty() || line.front() == '#' || line.front() == '^') {
      continue;
    }
    const size_t separator = line.find(' ');
    if (separator == std::string::npos) {
      continue;
    }
    const std::string refName = Trim(line.substr(separator + 1));
    if (refName.rfind(refPrefix, 0) == 0 &&
        references.find(refName) == references.end()) {
      references[refName] = Trim(line.substr(0, separator));
    }
  }
  return references;
}

bool ValidReferenceName(const std::string& name) {
  return name == "HEAD" ||
      (name.rfind("refs/", 0) == 0 &&
       ValidBranchName(name.substr(5)));
}

fs::path ReferencePath(
    const RepositoryContext& context,
    const std::string& name) {
  return name == "HEAD"
      ? context.gitDirectory / "HEAD"
      : context.commonGitDirectory / name;
}

bool ReadReferenceValue(
    const RepositoryContext& context,
    const std::string& name,
    std::string* value) {
  value->clear();
  const fs::path primaryPath = ReferencePath(context, name);
  std::error_code existsError;
  if (fs::is_regular_file(primaryPath, existsError) && !existsError) {
    *value = Trim(ReadTextFile(primaryPath));
    return true;
  }
  if (name != "HEAD" &&
      context.gitDirectory != context.commonGitDirectory) {
    const fs::path worktreePath = context.gitDirectory / name;
    existsError.clear();
    if (fs::is_regular_file(worktreePath, existsError) && !existsError) {
      *value = Trim(ReadTextFile(worktreePath));
      return true;
    }
  }
  if (name == "HEAD") {
    return false;
  }
  std::istringstream packedRefs(
      ReadTextFile(context.commonGitDirectory / "packed-refs"));
  std::string line;
  while (std::getline(packedRefs, line)) {
    if (line.empty() || line.front() == '#' || line.front() == '^') {
      continue;
    }
    const size_t separator = line.find(' ');
    if (separator != std::string::npos &&
        Trim(line.substr(separator + 1)) == name) {
      *value = Trim(line.substr(0, separator));
      return true;
    }
  }
  return false;
}

bool ParseDirectReferenceValue(
    const std::string& value,
    std::string* objectId) {
  std::array<uint8_t, 20> parsed {};
  if (!HexToObjectId(value, &parsed)) {
    return false;
  }
  *objectId = LowercaseAscii(value);
  return true;
}

bool ResolveReferenceObjectId(
    const RepositoryContext& context,
    const std::string& name,
    std::string* objectId,
    std::string* error) {
  objectId->clear();
  std::string current = name;
  std::set<std::string> visited;
  while (visited.insert(current).second) {
    std::string value;
    if (!ReadReferenceValue(context, current, &value)) {
      return true;
    }
    if (value.rfind("ref:", 0) == 0) {
      const std::string target = Trim(value.substr(4));
      if (!ValidReferenceName(target) || target == "HEAD") {
        if (error != nullptr) {
          *error = "Invalid symbolic reference target: " + target;
        }
        return false;
      }
      current = target;
      continue;
    }
    if (!ParseDirectReferenceValue(value, objectId)) {
      if (error != nullptr) {
        *error = "Invalid object ID stored in reference " + current + ".";
      }
      return false;
    }
    return true;
  }
  if (error != nullptr) {
    *error = "Symbolic reference chain contains a cycle.";
  }
  return false;
}

bool ResolveReferenceTargetName(
    const RepositoryContext& context,
    const std::string& name,
    bool recurse,
    bool requireSymbolic,
    std::string* target,
    std::string* error) {
  target->clear();
  std::string current = name;
  std::set<std::string> visited;
  bool symbolic = false;
  while (visited.insert(current).second) {
    std::string value;
    if (!ReadReferenceValue(context, current, &value) ||
        value.rfind("ref:", 0) != 0) {
      if (requireSymbolic && !symbolic) {
        if (error != nullptr) {
          *error = "ref " + name + " is not a symbolic ref.";
        }
        return false;
      }
      *target = current;
      return true;
    }
    const std::string next = Trim(value.substr(4));
    if (!ValidReferenceName(next) || next == "HEAD") {
      if (error != nullptr) {
        *error = "Invalid symbolic reference target: " + next;
      }
      return false;
    }
    symbolic = true;
    current = next;
    if (!recurse) {
      *target = current;
      return true;
    }
  }
  if (error != nullptr) {
    *error = "Symbolic reference chain contains a cycle.";
  }
  return false;
}

enum class ReferenceBatchActionKind {
  Update,
  Create,
  Delete,
  Verify,
  SymbolicUpdate,
  SymbolicCreate,
  SymbolicDelete,
  SymbolicVerify
};

struct ReferenceBatchAction {
  ReferenceBatchActionKind kind = ReferenceBatchActionKind::Verify;
  std::string name;
  std::string newValue;
  std::string oldValue;
  bool oldValueProvided = false;
  std::string newTarget;
  std::string oldTarget;
  bool oldTargetProvided = false;
  bool noDeref = false;
  std::string targetName;
  std::string resolvedNewValue;
  std::string resolvedOldValue;
  std::string currentObjectId;
  std::string currentValue;
  std::string resolvedNewTargetObjectId;
  bool existed = false;
};

struct ReferenceBatchRejection {
  std::string name;
  std::string newValue;
  std::string oldValue;
  std::string reason;
};

struct ReferenceBatchCommand {
  std::string name;
  std::vector<std::string> arguments;
  std::string source;
};

struct ReferenceFileBackup {
  fs::path path;
  bool existed = false;
  std::string content;
};

bool IsBatchArgumentSpace(char character) {
  return character == ' ' || character == '\t';
}

bool ParseReferenceBatchArguments(
    const std::string& line,
    size_t offset,
    std::vector<std::string>* arguments,
    std::string* error) {
  arguments->clear();
  size_t index = offset;
  while (index < line.size()) {
    while (index < line.size() &&
           IsBatchArgumentSpace(line[index])) {
      ++index;
    }
    if (index >= line.size()) {
      break;
    }
    std::string argument;
    if (line[index] != '"') {
      const size_t start = index;
      while (index < line.size() &&
             !IsBatchArgumentSpace(line[index])) {
        ++index;
      }
      argument = line.substr(start, index - start);
    } else {
      const size_t quotedStart = index++;
      bool closed = false;
      while (index < line.size()) {
        const char character = line[index++];
        if (character == '"') {
          closed = true;
          break;
        }
        if (character != '\\') {
          argument.push_back(character);
          continue;
        }
        if (index >= line.size()) {
          break;
        }
        const char escaped = line[index++];
        switch (escaped) {
          case '"':
          case '\\':
            argument.push_back(escaped);
            break;
          case 'a':
            argument.push_back('\a');
            break;
          case 'b':
            argument.push_back('\b');
            break;
          case 'f':
            argument.push_back('\f');
            break;
          case 'n':
            argument.push_back('\n');
            break;
          case 'r':
            argument.push_back('\r');
            break;
          case 't':
            argument.push_back('\t');
            break;
          case 'v':
            argument.push_back('\v');
            break;
          default:
            if (escaped < '0' || escaped > '7') {
              if (error != nullptr) {
                *error =
                    "badly quoted argument: " +
                    line.substr(quotedStart);
              }
              return false;
            }
            unsigned int octal =
                static_cast<unsigned int>(escaped - '0');
            size_t digits = 1;
            while (digits < 3 &&
                   index < line.size() &&
                   line[index] >= '0' &&
                   line[index] <= '7') {
              octal = (octal << 3U) +
                  static_cast<unsigned int>(line[index] - '0');
              ++digits;
              ++index;
            }
            argument.push_back(static_cast<char>(octal & 0xffU));
            break;
        }
      }
      if (!closed) {
        if (error != nullptr) {
          *error =
              "badly quoted argument: " +
              line.substr(quotedStart);
        }
        return false;
      }
      if (index < line.size() &&
          !IsBatchArgumentSpace(line[index])) {
        if (error != nullptr) {
          *error =
              "unexpected character after quoted argument: " +
              line.substr(quotedStart);
        }
        return false;
      }
    }
    arguments->push_back(argument);
  }
  return true;
}

size_t ReferenceBatchAdditionalArgumentCount(
    const std::string& command) {
  if (command == "update") {
    return 2;
  }
  if (command == "create" ||
      command == "delete" ||
      command == "verify" ||
      command == "symref-create" ||
      command == "symref-delete" ||
      command == "symref-verify") {
    return 1;
  }
  if (command == "symref-update") {
    return 3;
  }
  return 0;
}

bool ReferenceBatchCommandHasHeaderArgument(
    const std::string& command) {
  return command == "update" ||
      command == "create" ||
      command == "delete" ||
      command == "verify" ||
      command == "symref-update" ||
      command == "symref-create" ||
      command == "symref-delete" ||
      command == "symref-verify" ||
      command == "option";
}

bool IsReferenceBatchCommand(const std::string& command) {
  return ReferenceBatchCommandHasHeaderArgument(command) ||
      command == "start" ||
      command == "prepare" ||
      command == "commit" ||
      command == "abort";
}

bool ParseReferenceBatchCommands(
    const std::string& input,
    bool nullTerminated,
    std::vector<ReferenceBatchCommand>* commands,
    std::string* error) {
  commands->clear();
  if (!nullTerminated) {
    std::istringstream lines(input);
    std::string line;
    while (std::getline(lines, line)) {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      if (line.empty()) {
        if (error != nullptr) {
          *error = "empty command in input";
        }
        return false;
      }
      if (IsBatchArgumentSpace(line.front())) {
        if (error != nullptr) {
          *error = "whitespace before command: " + line;
        }
        return false;
      }
      size_t commandEnd = 0;
      while (commandEnd < line.size() &&
             !IsBatchArgumentSpace(line[commandEnd])) {
        ++commandEnd;
      }
      ReferenceBatchCommand parsed;
      parsed.name = line.substr(0, commandEnd);
      parsed.source = line;
      if (!ParseReferenceBatchArguments(
              line,
              commandEnd,
              &parsed.arguments,
              error)) {
        return false;
      }
      commands->push_back(std::move(parsed));
    }
    return true;
  }

  size_t cursor = 0;
  while (cursor < input.size()) {
    const size_t terminator = input.find('\0', cursor);
    if (terminator == std::string::npos) {
      if (error != nullptr) {
        *error = "unterminated NUL command in input";
      }
      return false;
    }
    const std::string header =
        input.substr(cursor, terminator - cursor);
    cursor = terminator + 1;
    if (header.empty()) {
      if (error != nullptr) {
        *error = "empty command in input";
      }
      return false;
    }
    if (IsBatchArgumentSpace(header.front())) {
      if (error != nullptr) {
        *error = "whitespace before command: " + header;
      }
      return false;
    }

    const size_t separator = header.find(' ');
    ReferenceBatchCommand parsed;
    parsed.name =
        separator == std::string::npos
            ? header
            : header.substr(0, separator);
    parsed.source = header;
    if (!IsReferenceBatchCommand(parsed.name)) {
      if (error != nullptr) {
        *error = "unknown command: " + header;
      }
      return false;
    }
    const bool hasHeaderArgument =
        ReferenceBatchCommandHasHeaderArgument(parsed.name);
    if (hasHeaderArgument) {
      if (separator == std::string::npos) {
        parsed.arguments.push_back("");
      } else {
        parsed.arguments.push_back(header.substr(separator + 1));
      }
    } else if (separator != std::string::npos) {
      if (error != nullptr) {
        *error = parsed.name + ": extra input";
      }
      return false;
    }

    const size_t additionalArguments =
        ReferenceBatchAdditionalArgumentCount(parsed.name);
    for (size_t index = 0;
         index < additionalArguments && cursor < input.size();
         ++index) {
      const size_t argumentTerminator = input.find('\0', cursor);
      if (argumentTerminator == std::string::npos) {
        if (error != nullptr) {
          *error = "unterminated NUL argument in input";
        }
        return false;
      }
      parsed.arguments.push_back(
          input.substr(cursor, argumentTerminator - cursor));
      cursor = argumentTerminator + 1;
    }
    commands->push_back(std::move(parsed));
  }
  return true;
}

bool ResolveReferenceBatchObject(
    const RepositoryContext& context,
    const std::string& value,
    bool allowZero,
    bool requireExistingObject,
    const std::string& field,
    const std::string& command,
    std::string* resolved,
    bool* invalidValue,
    std::string* error) {
  *invalidValue = false;
  const std::string zeroId(40, '0');
  if (value.empty() || value == zeroId) {
    if (!allowZero) {
      if (error != nullptr) {
        *error = command + ": zero <" + field + ">";
      }
      return false;
    }
    *resolved = zeroId;
    return true;
  }
  std::string resolveError;
  *resolved = ResolveObjectName(context, value, &resolveError);
  if (resolved->empty()) {
    if (error != nullptr) {
      *error =
          command + ": invalid <" + field + ">: " + value;
    }
    return false;
  }
  if (requireExistingObject) {
    ObjectData object;
    if (!ReadObject(
            context.commonGitDirectory,
            *resolved,
            &object,
            &resolveError)) {
      *invalidValue = true;
      if (error != nullptr) {
        *error =
            command + ": invalid <" + field + ">: " + value;
      }
      return false;
    }
  }
  return true;
}

bool IsSymbolicReferenceBatchAction(
    ReferenceBatchActionKind kind) {
  return kind == ReferenceBatchActionKind::SymbolicUpdate ||
      kind == ReferenceBatchActionKind::SymbolicCreate ||
      kind == ReferenceBatchActionKind::SymbolicDelete ||
      kind == ReferenceBatchActionKind::SymbolicVerify;
}

bool IsReferenceBatchVerifyAction(
    ReferenceBatchActionKind kind) {
  return kind == ReferenceBatchActionKind::Verify ||
      kind == ReferenceBatchActionKind::SymbolicVerify;
}

std::string ReferenceBatchActionCommand(
    ReferenceBatchActionKind kind) {
  switch (kind) {
    case ReferenceBatchActionKind::Update:
      return "update";
    case ReferenceBatchActionKind::Create:
      return "create";
    case ReferenceBatchActionKind::Delete:
      return "delete";
    case ReferenceBatchActionKind::Verify:
      return "verify";
    case ReferenceBatchActionKind::SymbolicUpdate:
      return "symref-update";
    case ReferenceBatchActionKind::SymbolicCreate:
      return "symref-create";
    case ReferenceBatchActionKind::SymbolicDelete:
      return "symref-delete";
    case ReferenceBatchActionKind::SymbolicVerify:
      return "symref-verify";
  }
  return "update";
}

bool ValidateReferenceBatch(
    const RepositoryContext& context,
    std::vector<ReferenceBatchAction>* actions,
    bool batchUpdates,
    std::vector<ReferenceBatchRejection>* rejections,
    std::string* error) {
  const std::string zeroId(40, '0');
  std::set<std::string> transactionReferences;
  for (ReferenceBatchAction& action : *actions) {
    if (!ValidReferenceName(action.name)) {
      if (error != nullptr) {
        *error = "ref " + action.name + " is not a valid ref.";
      }
      return false;
    }
    if (!action.noDeref &&
        !ResolveReferenceTargetName(
            context,
            action.name,
            true,
            false,
            &action.targetName,
            error)) {
      return false;
    }
    if (action.noDeref) {
      action.targetName = action.name;
    }

    std::set<std::string> actionReferences = {
        action.name,
        action.targetName};
    if (action.noDeref && action.name == "HEAD") {
      std::string headTarget;
      std::string ignoredError;
      if (ResolveReferenceTargetName(
              context,
              "HEAD",
              true,
              true,
              &headTarget,
              &ignoredError)) {
        actionReferences.insert(headTarget);
      }
    }
    for (const std::string& reference : actionReferences) {
      if (!transactionReferences.insert(reference).second) {
        if (error != nullptr) {
          *error =
              "multiple updates for ref '" +
              reference + "' not allowed";
        }
        return false;
      }
    }
  }

  std::map<std::string, std::string> occupiedReferences =
      ReadReferenceValuesWithPrefix(
          context.commonGitDirectory,
          "refs/");
  const bool caseInsensitiveFilesystem =
      ReferenceFilesystemIgnoresCase(context);
  std::map<std::string, std::string> transactionCaseReferences;
  std::vector<ReferenceBatchAction> acceptedActions;
  acceptedActions.reserve(actions->size());
  for (ReferenceBatchAction& action : *actions) {
    std::string storedValue;
    const bool exists =
        ReadReferenceValue(context, action.targetName, &storedValue);
    std::string currentObjectId;
    if (!ResolveReferenceObjectId(
            context,
            action.targetName,
            &currentObjectId,
            error)) {
      return false;
    }
    action.existed = exists;
    action.currentValue = storedValue;
    action.currentObjectId = currentObjectId;
    const std::string actual =
        currentObjectId.empty() ? zeroId : currentObjectId;
    const std::string command =
        ReferenceBatchActionCommand(action.kind) + " " +
        action.name;
    std::string rejectionReason;
    std::string rejectionError;

    if (action.kind == ReferenceBatchActionKind::Update ||
        action.kind == ReferenceBatchActionKind::Create) {
      bool invalidNewValue = false;
      if (!ResolveReferenceBatchObject(
              context,
              action.newValue,
              action.kind == ReferenceBatchActionKind::Update,
              true,
              "new-oid",
              command,
              &action.resolvedNewValue,
              &invalidNewValue,
              error)) {
        if (!batchUpdates || !invalidNewValue) {
          return false;
        }
        rejectionReason = "invalid new value provided";
        rejectionError = *error;
      } else if (
          action.resolvedNewValue != zeroId &&
          action.targetName.rfind("refs/heads/", 0) == 0) {
        ObjectData object;
        std::string objectError;
        if (!ReadObject(
                context.commonGitDirectory,
                action.resolvedNewValue,
                &object,
                &objectError) ||
            object.type != "commit") {
          rejectionReason = "invalid new value provided";
          rejectionError =
              "Cannot write non-commit object " +
              action.resolvedNewValue + " to branch '" +
              action.name + "'.";
        }
      }
    }
    if (rejectionReason.empty() &&
        (action.kind == ReferenceBatchActionKind::SymbolicUpdate ||
         action.kind == ReferenceBatchActionKind::SymbolicCreate)) {
      if (!ValidReferenceName(action.newTarget) ||
          action.newTarget == "HEAD") {
        if (error != nullptr) {
          *error =
              command + ": invalid <new-target>: " +
              action.newTarget;
        }
        return false;
      }
      if (!ResolveReferenceObjectId(
              context,
              action.newTarget,
              &action.resolvedNewTargetObjectId,
              error)) {
        return false;
      }
    }
    if (action.oldTargetProvided &&
        (!ValidReferenceName(action.oldTarget) ||
         action.oldTarget == "HEAD")) {
      if (error != nullptr) {
        *error =
            command + ": invalid <old-target>: " +
            action.oldTarget;
      }
      return false;
    }
    if (action.oldValueProvided) {
      bool invalidOldValue = false;
      if (!ResolveReferenceBatchObject(
              context,
              action.oldValue,
              action.kind != ReferenceBatchActionKind::Delete,
              false,
              "old-oid",
              command,
              &action.resolvedOldValue,
              &invalidOldValue,
              error)) {
        return false;
      }
    }

    if (rejectionReason.empty() &&
        (action.kind == ReferenceBatchActionKind::Create ||
         action.kind == ReferenceBatchActionKind::SymbolicCreate)) {
      if (exists) {
        rejectionReason = "reference already exists";
        rejectionError =
            "Cannot lock ref '" + action.name +
            "': reference already exists.";
      }
    }
    if (rejectionReason.empty() &&
        action.kind == ReferenceBatchActionKind::SymbolicVerify) {
      if (!action.noDeref) {
        if (error != nullptr) {
          *error =
              "symref-verify: cannot operate with deref mode";
        }
        return false;
      }
      if (!action.oldTargetProvided) {
        if (exists) {
          rejectionReason = "reference already exists";
          rejectionError =
              "Cannot lock ref '" + action.name +
              "': reference already exists.";
        }
      } else {
        const bool symbolic =
            exists && storedValue.rfind("ref:", 0) == 0;
        const std::string actualTarget =
            symbolic ? Trim(storedValue.substr(4)) : "";
        if (!exists) {
          rejectionReason = "reference does not exist";
          rejectionError =
              "Cannot lock ref '" + action.name +
              "': reference does not exist.";
        } else if (!symbolic) {
          rejectionReason = "expected symref but found regular ref";
          rejectionError =
              "Cannot lock ref '" + action.name +
              "': expected a symbolic reference.";
        } else if (actualTarget != action.oldTarget) {
          rejectionReason = "incorrect old value provided";
          rejectionError =
              "Cannot lock ref '" + action.name +
              "': expected symref with target '" +
              action.oldTarget + "'.";
        }
      }
    }
    if (rejectionReason.empty() &&
        action.kind == ReferenceBatchActionKind::SymbolicDelete) {
      if (!action.noDeref) {
        if (error != nullptr) {
          *error =
              "symref-delete: cannot operate with deref mode";
        }
        return false;
      }
      if (action.name == "HEAD") {
        if (error != nullptr) {
          *error = "Deleting 'HEAD' is not allowed.";
        }
        return false;
      }
      if (action.oldTargetProvided) {
        const bool symbolic =
            exists && storedValue.rfind("ref:", 0) == 0;
        const std::string actualTarget =
            symbolic ? Trim(storedValue.substr(4)) : "";
        if (!exists) {
          rejectionReason = "reference does not exist";
          rejectionError =
              "Cannot lock ref '" + action.name +
              "': reference does not exist.";
        } else if (!symbolic) {
          rejectionReason = "expected symref but found regular ref";
          rejectionError =
              "Cannot lock ref '" + action.name +
              "': expected a symbolic reference.";
        } else if (actualTarget != action.oldTarget) {
          rejectionReason = "incorrect old value provided";
          rejectionError =
              "Cannot lock ref '" + action.name +
              "': expected symref with target '" +
              action.oldTarget + "'.";
        }
      }
    }
    if (rejectionReason.empty() &&
        action.kind == ReferenceBatchActionKind::SymbolicUpdate) {
      if (action.oldTargetProvided) {
        const bool symbolic =
            exists && storedValue.rfind("ref:", 0) == 0;
        const std::string actualTarget =
            symbolic ? Trim(storedValue.substr(4)) : "";
        if (!exists) {
          rejectionReason = "reference does not exist";
          rejectionError =
              "Cannot lock ref '" + action.name +
              "': reference does not exist.";
        } else if (!symbolic) {
          rejectionReason = "expected symref but found regular ref";
          rejectionError =
              "Cannot lock ref '" + action.name +
              "': expected a symbolic reference.";
        } else if (actualTarget != action.oldTarget) {
          rejectionReason = "incorrect old value provided";
          rejectionError =
              "Cannot lock ref '" + action.name +
              "': expected symref with target '" +
              action.oldTarget + "'.";
        }
      }
      if (rejectionReason.empty() && action.oldValueProvided) {
        if (action.resolvedOldValue == zeroId && exists) {
          rejectionReason = "reference already exists";
          rejectionError =
              "Cannot lock ref '" + action.name +
              "': reference already exists.";
        } else if (!exists) {
          rejectionReason = "reference does not exist";
          rejectionError =
              "Cannot lock ref '" + action.name +
              "': reference does not exist.";
        } else if (actual != action.resolvedOldValue) {
          rejectionReason = "incorrect old value provided";
          rejectionError =
              "Cannot lock ref '" + action.name + "': is at " +
              actual + " but expected " +
              action.resolvedOldValue + ".";
        }
      }
    }
    if (rejectionReason.empty() &&
        action.kind == ReferenceBatchActionKind::Verify) {
      const std::string expected =
          action.oldValueProvided
              ? action.resolvedOldValue
              : zeroId;
      if (expected == zeroId && exists) {
        rejectionReason = "reference already exists";
      } else if (expected != zeroId && !exists) {
        rejectionReason = "reference does not exist";
      } else if (expected != zeroId && actual != expected) {
        rejectionReason = "incorrect old value provided";
      }
      if (!rejectionReason.empty()) {
        rejectionError =
            "Cannot lock ref '" + action.name + "': is at " +
            actual + " but expected " + expected + ".";
      }
    }
    if (rejectionReason.empty() &&
        action.oldValueProvided &&
        ((action.resolvedOldValue == zeroId && exists) ||
         actual != action.resolvedOldValue)) {
      if (action.resolvedOldValue == zeroId && exists) {
        rejectionReason = "reference already exists";
      } else if (!exists) {
        rejectionReason = "reference does not exist";
      } else {
        rejectionReason = "incorrect old value provided";
      }
      rejectionError =
          "Cannot lock ref '" + action.name + "': is at " +
          actual + " but expected " +
          action.resolvedOldValue + ".";
    }

    if (rejectionReason.empty() && caseInsensitiveFilesystem) {
      const std::string foldedTarget = LowercaseAscii(action.targetName);
      const auto previous = transactionCaseReferences.find(foldedTarget);
      if (previous != transactionCaseReferences.end() &&
          previous->second != action.targetName) {
        rejectionReason =
            "reference conflict due to case-insensitive filesystem";
        rejectionError =
            "Cannot lock ref '" + action.name +
            "': reference conflicts with '" +
            previous->second +
            "' on a case-insensitive filesystem.";
      }
    }

    const bool writesReference =
        action.kind == ReferenceBatchActionKind::Create ||
        action.kind == ReferenceBatchActionKind::SymbolicCreate ||
        action.kind == ReferenceBatchActionKind::SymbolicUpdate ||
        (action.kind == ReferenceBatchActionKind::Update &&
         action.resolvedNewValue != zeroId);
    if (rejectionReason.empty() && writesReference) {
      for (const auto& occupied : occupiedReferences) {
        if (occupied.first == action.targetName) {
          continue;
        }
        if (occupied.first.rfind(action.targetName + "/", 0) == 0 ||
            action.targetName.rfind(occupied.first + "/", 0) == 0) {
          rejectionReason = "refname conflict";
          rejectionError =
              "'" + occupied.first +
              "' exists; cannot create '" +
              action.targetName + "'.";
          break;
        }
      }
    }

    if (!rejectionReason.empty()) {
      if (!batchUpdates) {
        if (error != nullptr) {
          *error = rejectionError;
        }
        return false;
      }
      const bool symbolic =
          IsSymbolicReferenceBatchAction(action.kind);
      const bool deleteOrVerify =
          action.kind == ReferenceBatchActionKind::Delete ||
          action.kind == ReferenceBatchActionKind::Verify ||
          action.kind == ReferenceBatchActionKind::SymbolicDelete ||
          action.kind == ReferenceBatchActionKind::SymbolicVerify;
      std::string rejectedNew = zeroId;
      if (!deleteOrVerify && !symbolic) {
        rejectedNew = action.resolvedNewValue.empty()
            ? LowercaseAscii(action.newValue)
            : action.resolvedNewValue;
      }
      std::string rejectedOld = zeroId;
      if (action.oldValueProvided) {
        rejectedOld = action.resolvedOldValue.empty()
            ? LowercaseAscii(action.oldValue)
            : action.resolvedOldValue;
      }
      rejections->push_back({
          action.name,
          rejectedNew,
          rejectedOld,
          rejectionReason});
      continue;
    }

    const bool deletesReference =
        action.kind == ReferenceBatchActionKind::Delete ||
        action.kind == ReferenceBatchActionKind::SymbolicDelete ||
        (action.kind == ReferenceBatchActionKind::Update &&
         action.resolvedNewValue == zeroId);
    if (deletesReference) {
      occupiedReferences.erase(action.targetName);
    } else if (writesReference) {
      occupiedReferences[action.targetName] =
          IsSymbolicReferenceBatchAction(action.kind)
              ? "ref: " + action.newTarget
              : action.resolvedNewValue;
    }
    if (caseInsensitiveFilesystem) {
      transactionCaseReferences.emplace(
          LowercaseAscii(action.targetName),
          action.targetName);
    }
    acceptedActions.push_back(std::move(action));
  }
  *actions = std::move(acceptedActions);
  return true;
}

bool CaptureReferenceFileBackup(
    const fs::path& path,
    std::set<std::string>* capturedPaths,
    std::vector<ReferenceFileBackup>* backups,
    std::string* error) {
  const std::string key = path.lexically_normal().generic_string();
  if (!capturedPaths->insert(key).second) {
    return true;
  }
  ReferenceFileBackup backup;
  backup.path = path;
  std::error_code existsError;
  backup.existed = fs::exists(path, existsError) && !existsError;
  if (existsError) {
    if (error != nullptr) {
      *error =
          "Cannot inspect " + path.string() + ": " +
          existsError.message();
    }
    return false;
  }
  if (backup.existed &&
      !ReadBinaryFile(path, &backup.content, error)) {
    return false;
  }
  backups->push_back(std::move(backup));
  return true;
}

bool CaptureReferenceBatchBackups(
    const RepositoryContext& context,
    const std::vector<ReferenceBatchAction>& actions,
    std::vector<ReferenceFileBackup>* backups,
    std::string* error) {
  backups->clear();
  std::set<std::string> capturedPaths;
  if (!CaptureReferenceFileBackup(
          context.commonGitDirectory / "packed-refs",
          &capturedPaths,
          backups,
          error)) {
    return false;
  }
  for (const ReferenceBatchAction& action : actions) {
    if (IsReferenceBatchVerifyAction(action.kind)) {
      continue;
    }
    if (!CaptureReferenceFileBackup(
            ReferencePath(context, action.targetName),
            &capturedPaths,
            backups,
            error) ||
        !CaptureReferenceFileBackup(
            ReflogPath(context, action.targetName),
            &capturedPaths,
            backups,
            error) ||
        (action.name != action.targetName &&
         !CaptureReferenceFileBackup(
             ReflogPath(context, action.name),
             &capturedPaths,
             backups,
             error))) {
      return false;
    }
  }
  return true;
}

bool ApplySymbolicReferenceBatchAction(
    const RepositoryContext& context,
    const ReferenceBatchAction& action,
    const std::string& message,
    bool createReflog,
    uint32_t* changedCount,
    std::string* error) {
  *changedCount = 0;
  if (action.kind == ReferenceBatchActionKind::SymbolicDelete) {
    bool removed = false;
    if (!DeleteReference(
            context,
            action.targetName,
            &removed,
            error) ||
        !RemoveReflog(
            context,
            action.targetName,
            error)) {
      return false;
    }
    *changedCount = removed ? 1 : 0;
    return true;
  }

  const std::string content =
      "ref: " + action.newTarget + "\n";
  if (!WriteAtomicFile(
          ReferencePath(context, action.targetName),
          content,
          error)) {
    return false;
  }
  if (action.targetName != "HEAD") {
    bool packedRemoved = false;
    if (!RemovePackedReference(
            context.commonGitDirectory / "packed-refs",
            action.targetName,
            &packedRemoved,
            error)) {
      return false;
    }
  }
  if (!AppendReflog(
          context,
          action.targetName,
          action.currentObjectId,
          action.resolvedNewTargetObjectId,
          message,
          error,
          createReflog) ||
      (action.name != action.targetName &&
       !AppendReflog(
           context,
           action.name,
           action.currentObjectId,
           action.resolvedNewTargetObjectId,
           message,
           error,
           createReflog))) {
    return false;
  }
  *changedCount =
      !action.existed ||
          action.currentValue != Trim(content)
      ? 1
      : 0;
  return true;
}

bool RestoreReferenceBatchBackups(
    const std::vector<ReferenceFileBackup>& backups,
    std::string* error) {
  for (auto iterator = backups.rbegin();
       iterator != backups.rend();
       ++iterator) {
    if (iterator->existed) {
      if (!WriteAtomicFile(
              iterator->path,
              iterator->content,
              error)) {
        return false;
      }
      continue;
    }
    std::error_code removeError;
    fs::remove(iterator->path, removeError);
    if (removeError &&
        removeError != std::errc::no_such_file_or_directory &&
        removeError != std::errc::not_a_directory) {
      if (error != nullptr) {
        *error =
            "Cannot restore " + iterator->path.string() + ": " +
            removeError.message();
      }
      return false;
    }
  }
  return true;
}

bool ReferencePatternMatches(
    const std::string& reference,
    const std::string& pattern) {
  if (reference == pattern) {
    return true;
  }
  return reference.size() > pattern.size() &&
      reference.compare(
          reference.size() - pattern.size(),
          pattern.size(),
          pattern) == 0 &&
      reference[reference.size() - pattern.size() - 1] == '/';
}

bool RewritePackedReferencePrefix(
    const fs::path& packedRefsPath,
    const std::string& oldPrefix,
    const std::string& newPrefix,
    bool remove,
    bool* changed,
    std::string* error) {
  *changed = false;
  std::ifstream input(packedRefsPath, std::ios::binary);
  if (!input) {
    return true;
  }
  std::vector<std::string> lines;
  std::string line;
  bool omitPeeledLine = false;
  while (std::getline(input, line)) {
    if (omitPeeledLine && !line.empty() && line.front() == '^') {
      omitPeeledLine = false;
      continue;
    }
    omitPeeledLine = false;
    const size_t separator = line.find(' ');
    if (separator == std::string::npos ||
        line.empty() ||
        line.front() == '#' ||
        line.front() == '^') {
      lines.push_back(line);
      continue;
    }
    const std::string refName = Trim(line.substr(separator + 1));
    if (refName.rfind(oldPrefix, 0) != 0) {
      lines.push_back(line);
      continue;
    }
    *changed = true;
    omitPeeledLine = true;
    if (!remove) {
      lines.push_back(
          line.substr(0, separator) + " " +
          newPrefix + refName.substr(oldPrefix.size()));
    }
  }
  if (!*changed) {
    return true;
  }
  return WriteAtomicFile(
      packedRefsPath,
      ConfigLinesContent(lines),
      error);
}

bool RemoveLooseReferencePrefix(
    const fs::path& commonGitDirectory,
    const std::string& refPrefix,
    std::string* error) {
  const std::vector<LooseReference> references =
      ReadLooseReferencesWithPrefix(commonGitDirectory, refPrefix);
  for (const LooseReference& reference : references) {
    std::error_code removeError;
    if (!fs::remove(reference.path, removeError) || removeError) {
      if (error != nullptr) {
        *error = "Cannot delete " + reference.refName + ": " +
            removeError.message();
      }
      return false;
    }
  }
  const fs::path root = commonGitDirectory / refPrefix;
  fs::path directory = root;
  while (directory != commonGitDirectory &&
         IsPathInside(commonGitDirectory, directory)) {
    std::error_code emptyError;
    if (!fs::is_empty(directory, emptyError) || emptyError) {
      break;
    }
    std::error_code removeError;
    fs::remove(directory, removeError);
    if (removeError) {
      break;
    }
    directory = directory.parent_path();
  }
  return true;
}

bool MoveLooseReferencePrefix(
    const fs::path& commonGitDirectory,
    const std::string& oldPrefix,
    const std::string& newPrefix,
    std::string* error) {
  const std::vector<LooseReference> references =
      ReadLooseReferencesWithPrefix(commonGitDirectory, oldPrefix);
  for (const LooseReference& reference : references) {
    const fs::path destination =
        commonGitDirectory /
        (newPrefix + reference.refName.substr(oldPrefix.size()));
    std::error_code existsError;
    if (fs::exists(destination, existsError) && !existsError) {
      if (error != nullptr) {
        *error = "Reference already exists: " +
            (newPrefix + reference.refName.substr(oldPrefix.size()));
      }
      return false;
    }
  }
  for (const LooseReference& reference : references) {
    const std::string suffix = reference.refName.substr(oldPrefix.size());
    const fs::path destination = commonGitDirectory / (newPrefix + suffix);
    if (!EnsureDirectory(destination.parent_path(), error)) {
      return false;
    }
    std::error_code renameError;
    fs::rename(reference.path, destination, renameError);
    if (renameError) {
      if (error != nullptr) {
        *error = "Cannot move " + reference.refName + ": " +
            renameError.message();
      }
      return false;
    }
    std::string content = Trim(reference.content);
    const size_t targetPrefix = content.find(oldPrefix);
    if (content.rfind("ref:", 0) == 0 &&
        targetPrefix != std::string::npos) {
      content.replace(
          targetPrefix,
          oldPrefix.size(),
          newPrefix);
      if (!WriteAtomicFile(destination, content + "\n", error)) {
        return false;
      }
    }
  }
  const fs::path root = commonGitDirectory / oldPrefix;
  fs::path directory = root;
  while (directory != commonGitDirectory &&
         IsPathInside(commonGitDirectory, directory)) {
    std::error_code emptyError;
    if (!fs::is_empty(directory, emptyError) || emptyError) {
      break;
    }
    std::error_code removeError;
    fs::remove(directory, removeError);
    if (removeError) {
      break;
    }
    directory = directory.parent_path();
  }
  return true;
}

bool MoveLogPrefix(
    const fs::path& commonGitDirectory,
    const std::string& oldPrefix,
    const std::string& newPrefix,
    bool remove,
    std::string* error) {
  const fs::path oldRoot = commonGitDirectory / "logs" / oldPrefix;
  std::error_code directoryError;
  if (!fs::is_directory(oldRoot, directoryError)) {
    return true;
  }
  std::vector<fs::path> files;
  fs::recursive_directory_iterator iterator(
      oldRoot,
      fs::directory_options::skip_permission_denied,
      directoryError);
  const fs::recursive_directory_iterator end;
  while (!directoryError && iterator != end) {
    std::error_code statusError;
    if (fs::is_regular_file(iterator->path(), statusError) &&
        !statusError) {
      files.push_back(iterator->path());
    }
    directoryError.clear();
    iterator.increment(directoryError);
  }
  if (remove) {
    for (const fs::path& path : files) {
      std::error_code removeError;
      if (!fs::remove(path, removeError) || removeError) {
        if (error != nullptr) {
          *error = "Cannot delete " + path.string() + ": " +
              removeError.message();
        }
        return false;
      }
    }
    return true;
  }
  for (const fs::path& path : files) {
    const std::string relative =
        RelativeGitPath(oldRoot, path);
    const fs::path destination =
        commonGitDirectory / "logs" / (newPrefix + relative);
    std::error_code existsError;
    if (fs::exists(destination, existsError) && !existsError) {
      if (error != nullptr) {
        *error = "Reflog already exists: " + destination.string();
      }
      return false;
    }
  }
  for (const fs::path& path : files) {
    const std::string relative =
        RelativeGitPath(oldRoot, path);
    const fs::path destination =
        commonGitDirectory / "logs" / (newPrefix + relative);
    if (!EnsureDirectory(destination.parent_path(), error)) {
      return false;
    }
    std::error_code renameError;
    fs::rename(path, destination, renameError);
    if (renameError) {
      if (error != nullptr) {
        *error = "Cannot move reflog " + path.string() + ": " +
            renameError.message();
      }
      return false;
    }
  }
  fs::path directory = oldRoot;
  const fs::path logsDirectory = commonGitDirectory / "logs";
  while (directory != logsDirectory &&
         IsPathInside(logsDirectory, directory)) {
    std::error_code emptyError;
    if (!fs::is_empty(directory, emptyError) || emptyError) {
      break;
    }
    std::error_code removeError;
    fs::remove(directory, removeError);
    if (removeError) {
      break;
    }
    directory = directory.parent_path();
  }
  return true;
}

bool UpdateWorktreeHeads(
    const RepositoryContext& context,
    const std::string& oldRef,
    const std::string& newRef,
    std::string* error) {
  const std::string oldHead = "ref: " + oldRef;
  const std::string newHead = "ref: " + newRef;
  std::vector<fs::path> headFiles;
  headFiles.push_back(context.gitDirectory / "HEAD");
  const fs::path worktrees = context.commonGitDirectory / "worktrees";
  std::error_code directoryError;
  if (fs::is_directory(worktrees, directoryError)) {
    for (fs::directory_iterator iterator(worktrees, directoryError);
         !directoryError && iterator != fs::directory_iterator();
         iterator.increment(directoryError)) {
      std::error_code statusError;
      if (!fs::is_directory(iterator->path(), statusError) ||
          statusError) {
        continue;
      }
      headFiles.push_back(iterator->path() / "HEAD");
    }
  }
  for (const fs::path& headFile : headFiles) {
    if (Trim(ReadTextFile(headFile)) != oldHead) {
      continue;
    }
    if (!WriteAtomicFile(headFile, newHead + "\n", error)) {
      return false;
    }
  }
  return true;
}

bool IsBranchCheckedOut(
    const RepositoryContext& context,
    const std::string& branchName) {
  const std::string expected = "ref: refs/heads/" + branchName;
  if (Trim(ReadTextFile(context.gitDirectory / "HEAD")) == expected) {
    return true;
  }
  const fs::path worktrees = context.commonGitDirectory / "worktrees";
  std::error_code directoryError;
  if (!fs::is_directory(worktrees, directoryError)) {
    return false;
  }
  for (fs::directory_iterator iterator(worktrees, directoryError);
       !directoryError && iterator != fs::directory_iterator();
       iterator.increment(directoryError)) {
    std::error_code statusError;
    if (!fs::is_directory(iterator->path(), statusError) ||
        statusError) {
      continue;
    }
    if (Trim(ReadTextFile(iterator->path() / "HEAD")) == expected) {
      return true;
    }
  }
  return false;
}

bool IsAncestorCommit(
    const fs::path& commonGitDirectory,
    const std::string& ancestor,
    const std::string& descendant,
    std::string* error) {
  if (ancestor == descendant) {
    return true;
  }
  std::vector<std::string> pending = {descendant};
  std::set<std::string> visited;
  while (!pending.empty()) {
    const std::string current = pending.back();
    pending.pop_back();
    if (!visited.insert(current).second) {
      continue;
    }
    ObjectData commit;
    if (!ReadCommitObject(
            commonGitDirectory,
            current,
            &commit,
            error)) {
      return false;
    }
    for (const std::string& parent : ReadParents(commit.payload)) {
      if (parent == ancestor) {
        return true;
      }
      pending.push_back(parent);
    }
  }
  return false;
}

struct CommitGraphNode {
  std::string objectId;
  std::vector<std::string> parents;
  int64_t timestamp = 0;
};

int64_t GitIdentityTimestamp(const std::string& identity) {
  const size_t timezoneSeparator = identity.rfind(' ');
  if (timezoneSeparator == std::string::npos) {
    return 0;
  }
  const size_t timestampSeparator =
      identity.rfind(' ', timezoneSeparator - 1);
  if (timestampSeparator == std::string::npos) {
    return 0;
  }
  try {
    return std::stoll(
        identity.substr(
            timestampSeparator + 1,
            timezoneSeparator - timestampSeparator - 1));
  } catch (...) {
    return 0;
  }
}

bool ReadCommitGraphNode(
    const fs::path& commonGitDirectory,
    const std::string& objectId,
    CommitGraphNode* node,
    std::string* error) {
  ObjectData commit;
  if (!ReadCommitObject(
          commonGitDirectory,
          objectId,
          &commit,
          error)) {
    return false;
  }
  node->objectId = objectId;
  node->parents = ReadParents(commit.payload);
  node->timestamp =
      GitIdentityTimestamp(
          CommitHeaderValue(commit.payload, "committer"));
  return true;
}

bool CollectCommitAncestors(
    const fs::path& commonGitDirectory,
    const std::vector<std::string>& tips,
    bool firstParent,
    std::set<std::string>* ancestors,
    std::map<std::string, CommitGraphNode>* cache,
    std::string* error) {
  std::vector<std::string> pending = tips;
  while (!pending.empty()) {
    const std::string current = pending.back();
    pending.pop_back();
    if (!ancestors->insert(current).second) {
      continue;
    }
    auto found = cache->find(current);
    if (found == cache->end()) {
      CommitGraphNode node;
      if (!ReadCommitGraphNode(
              commonGitDirectory,
              current,
              &node,
              error)) {
        return false;
      }
      found = cache->emplace(current, std::move(node)).first;
    }
    const std::vector<std::string>& parents = found->second.parents;
    const size_t parentCount =
        firstParent ? std::min<size_t>(1, parents.size()) : parents.size();
    for (size_t index = 0; index < parentCount; ++index) {
      pending.push_back(parents[index]);
    }
  }
  return true;
}

std::vector<std::string> BestCommonAncestors(
    const fs::path& commonGitDirectory,
    const std::set<std::string>& candidates,
    std::string* error) {
  std::vector<std::string> best;
  for (const std::string& candidate : candidates) {
    bool dominated = false;
    for (const std::string& other : candidates) {
      if (candidate == other) {
        continue;
      }
      std::string ancestorError;
      if (IsAncestorCommit(
              commonGitDirectory,
              candidate,
              other,
              &ancestorError)) {
        dominated = true;
        break;
      }
      if (!ancestorError.empty()) {
        if (error != nullptr) {
          *error = ancestorError;
        }
        return {};
      }
    }
    if (!dominated) {
      best.push_back(candidate);
    }
  }
  std::sort(best.begin(), best.end());
  return best;
}

std::vector<std::string> MergeBaseCandidates(
    const fs::path& commonGitDirectory,
    const std::vector<std::string>& commits,
    bool octopus,
    std::string* error) {
  if (commits.size() < 2) {
    if (error != nullptr) {
      *error = "git merge-base requires at least two commits.";
    }
    return {};
  }
  std::map<std::string, CommitGraphNode> cache;
  std::vector<std::set<std::string>> ancestorSets(commits.size());
  for (size_t index = 0; index < commits.size(); ++index) {
    if (!CollectCommitAncestors(
            commonGitDirectory,
            {commits[index]},
            false,
            &ancestorSets[index],
            &cache,
            error)) {
      return {};
    }
  }

  std::set<std::string> common = ancestorSets.front();
  if (octopus) {
    for (size_t index = 1; index < ancestorSets.size(); ++index) {
      std::set<std::string> intersection;
      std::set_intersection(
          common.begin(),
          common.end(),
          ancestorSets[index].begin(),
          ancestorSets[index].end(),
          std::inserter(intersection, intersection.begin()));
      common = std::move(intersection);
    }
  } else {
    std::set<std::string> otherAncestors;
    for (size_t index = 1; index < ancestorSets.size(); ++index) {
      otherAncestors.insert(
          ancestorSets[index].begin(),
          ancestorSets[index].end());
    }
    std::set<std::string> intersection;
    std::set_intersection(
        common.begin(),
        common.end(),
        otherAncestors.begin(),
        otherAncestors.end(),
        std::inserter(intersection, intersection.begin()));
    common = std::move(intersection);
  }
  return BestCommonAncestors(commonGitDirectory, common, error);
}

bool ResolveObjectExpression(
    const RepositoryContext& context,
    const std::string& expression,
    std::string* objectId,
    std::string* error) {
  objectId->clear();
  const std::string value = Trim(expression);
  if (value.empty()) {
    if (error != nullptr) {
      *error = "An object name is required.";
    }
    return false;
  }
  std::array<uint8_t, 20> parsed {};
  if (value.size() == 40 && HexToObjectId(value, &parsed)) {
    *objectId = LowercaseAscii(value);
    return true;
  }
  std::vector<std::string> candidates;
  if (value == "HEAD" || value.rfind("refs/", 0) == 0) {
    candidates.push_back(value);
  } else {
    candidates = {
        "refs/heads/" + value,
        "refs/remotes/" + value,
        "refs/tags/" + value};
  }
  for (const std::string& candidate : candidates) {
    if (!ResolveReferenceObjectId(
            context,
            candidate,
            objectId,
            error)) {
      return false;
    }
    if (!objectId->empty()) {
      return true;
    }
  }
  if (error != nullptr) {
    *error = "Invalid object name: " + expression;
  }
  return false;
}

bool ReferenceFilterMatches(
    const std::string& reference,
    const std::string& pattern,
    bool ignoreCase) {
  const std::string candidate =
      ignoreCase ? LowercaseAscii(reference) : reference;
  const std::string expected =
      ignoreCase ? LowercaseAscii(pattern) : pattern;
  if (HasGlobCharacters(expected)) {
    return GlobMatch(expected, candidate);
  }
  return candidate == expected ||
      (candidate.size() > expected.size() &&
       candidate.rfind(expected + "/", 0) == 0);
}

std::string ShortReferenceName(const std::string& reference) {
  for (const std::string& prefix :
       {"refs/heads/", "refs/tags/", "refs/remotes/"}) {
    if (reference.rfind(prefix, 0) == 0) {
      return reference.substr(prefix.size());
    }
  }
  return reference;
}

std::string IdentityName(const std::string& identity) {
  const size_t email = identity.rfind(" <");
  return email == std::string::npos
      ? CommitAuthorName(identity)
      : identity.substr(0, email);
}

std::string IdentityEmail(const std::string& identity) {
  const size_t begin = identity.rfind('<');
  const size_t end = identity.find('>', begin);
  return begin == std::string::npos || end == std::string::npos
      ? std::string()
      : identity.substr(begin, end - begin + 1);
}

std::string IdentityDateValue(
    const std::string& identity,
    const std::string& modifier) {
  const int64_t timestamp = GitIdentityTimestamp(identity);
  if (modifier == "unix") {
    return std::to_string(timestamp);
  }
  return FormatCommitTimestamp(CommitAuthorTimestamp(identity));
}

struct ReferenceFormatData {
  std::string refName;
  std::string objectId;
  std::string objectType;
  std::string symref;
  std::string subject;
  std::string author;
  std::string committer;
  bool head = false;
};

std::string ReferenceAtomValue(
    const ReferenceFormatData& data,
    const std::string& atom,
    std::string* error) {
  const size_t modifierSeparator = atom.find(':');
  const std::string name =
      modifierSeparator == std::string::npos
          ? atom
          : atom.substr(0, modifierSeparator);
  const std::string modifier =
      modifierSeparator == std::string::npos
          ? std::string()
          : atom.substr(modifierSeparator + 1);
  if (name == "refname") {
    return modifier == "short"
        ? ShortReferenceName(data.refName)
        : data.refName;
  }
  if (name == "objectname") {
    if (modifier == "short") {
      return data.objectId.substr(0, std::min<size_t>(7, data.objectId.size()));
    }
    if (modifier.rfind("short=", 0) == 0) {
      try {
        const size_t length =
            static_cast<size_t>(std::stoul(modifier.substr(6)));
        return data.objectId.substr(
            0,
            std::min(length, data.objectId.size()));
      } catch (...) {
        if (error != nullptr) {
          *error = "Invalid objectname abbreviation: " + modifier;
        }
        return "";
      }
    }
    return data.objectId;
  }
  if (name == "objecttype") {
    return data.objectType;
  }
  if (name == "symref") {
    return modifier == "short"
        ? ShortReferenceName(data.symref)
        : data.symref;
  }
  if (name == "HEAD") {
    return data.head ? "*" : " ";
  }
  if (name == "subject") {
    return data.subject;
  }
  if (name == "authorname") {
    return IdentityName(data.author);
  }
  if (name == "authoremail") {
    return IdentityEmail(data.author);
  }
  if (name == "authordate") {
    return IdentityDateValue(data.author, modifier);
  }
  if (name == "committername") {
    return IdentityName(data.committer);
  }
  if (name == "committeremail") {
    return IdentityEmail(data.committer);
  }
  if (name == "committerdate") {
    return IdentityDateValue(data.committer, modifier);
  }
  if (error != nullptr) {
    *error = "Unknown for-each-ref field name: " + atom;
  }
  return "";
}

std::string ExpandReferenceFormat(
    const ReferenceFormatData& data,
    const std::string& format,
    std::string* error) {
  std::string output;
  for (size_t index = 0; index < format.size();) {
    if (format[index] != '%') {
      output.push_back(format[index++]);
      continue;
    }
    if (index + 1 < format.size() && format[index + 1] == '%') {
      output.push_back('%');
      index += 2;
      continue;
    }
    if (index + 2 < format.size() &&
        IsHexCharacter(format[index + 1]) &&
        IsHexCharacter(format[index + 2])) {
      output.push_back(
          static_cast<char>(
              HexValue(format[index + 1]) * 16 +
              HexValue(format[index + 2])));
      index += 3;
      continue;
    }
    if (index + 1 >= format.size() || format[index + 1] != '(') {
      output.push_back(format[index++]);
      continue;
    }
    const size_t end = format.find(')', index + 2);
    if (end == std::string::npos) {
      if (error != nullptr) {
        *error = "Unterminated for-each-ref format field.";
      }
      return "";
    }
    output += ReferenceAtomValue(
        data,
        format.substr(index + 2, end - index - 2),
        error);
    if (error != nullptr && !error->empty()) {
      return "";
    }
    index = end + 1;
  }
  return output;
}

bool RemoveWorkingTreePath(
    const fs::path& repositoryPath,
    const std::string& relativePath,
    std::string* error) {
  const fs::path path = repositoryPath / fs::path(relativePath);
  std::error_code statusError;
  const fs::file_status status = fs::symlink_status(path, statusError);
  if (statusError || status.type() == fs::file_type::not_found) {
    return true;
  }
  if (fs::is_directory(status)) {
    if (error != nullptr) {
      *error = "Cannot replace directory " + path.string() + ".";
    }
    return false;
  }
  std::error_code removeError;
  if (!fs::remove(path, removeError) || removeError) {
    if (error != nullptr) {
      *error = "Cannot remove " + path.string() + ": " + removeError.message();
    }
    return false;
  }
  fs::path parent = path.parent_path();
  while (parent != repositoryPath && IsPathInside(repositoryPath, parent)) {
    std::error_code emptyError;
    if (!fs::is_empty(parent, emptyError) || emptyError) {
      break;
    }
    std::error_code directoryError;
    fs::remove(parent, directoryError);
    if (directoryError) {
      break;
    }
    parent = parent.parent_path();
  }
  return true;
}

bool WriteWorkingTreeEntry(
    const RepositoryContext& context,
    const TreeEntry& entry,
    std::string* error) {
  uint32_t mode = 0;
  try {
    mode = static_cast<uint32_t>(std::stoul(entry.mode, nullptr, 8));
  } catch (...) {
    if (error != nullptr) {
      *error = "Unsupported mode for " + entry.path + ".";
    }
    return false;
  }
  const uint32_t type = mode & 0170000U;
  if (type == 0160000U) {
    if (error != nullptr) {
      *error = "Git submodules are not supported by the native checkout yet.";
    }
    return false;
  }
  std::string content = ReadBlob(
      context.commonGitDirectory,
      entry.objectId,
      error);
  if (error != nullptr && !error->empty()) {
    return false;
  }
  const fs::path path = context.repositoryPath / fs::path(entry.path);
  if (!EnsureDirectory(path.parent_path(), error)) {
    return false;
  }
  std::error_code statusError;
  const fs::file_status status = fs::symlink_status(path, statusError);
  if (!statusError && fs::is_directory(status)) {
    if (error != nullptr) {
      *error = "Untracked directory would be overwritten by " + entry.path + ".";
    }
    return false;
  }
  if (type == 0120000U) {
    std::error_code removeError;
    fs::remove(path, removeError);
    std::error_code symlinkError;
    fs::create_symlink(content, path, symlinkError);
    if (symlinkError) {
      if (error != nullptr) {
        *error =
            "Cannot create symbolic link " + path.string() + ": " +
            symlinkError.message();
      }
      return false;
    }
    return true;
  }
  if (type != 0100000U) {
    if (error != nullptr) {
      *error = "Unsupported Git file mode for " + entry.path + ".";
    }
    return false;
  }
  if (!WriteAtomicFile(path, content, error)) {
    return false;
  }
  const mode_t permissions =
      (mode & 0111U) != 0 ? static_cast<mode_t>(0755) : static_cast<mode_t>(0644);
  if (chmod(path.c_str(), permissions) != 0) {
    if (error != nullptr) {
      *error = "Cannot set file mode for " + path.string() + ".";
    }
    return false;
  }
  return true;
}

bool MaterializeTree(
    const RepositoryContext& context,
    const std::map<std::string, TreeEntry>& targetEntries,
    const std::vector<IndexEntry>& currentEntries,
    bool protectUntracked,
    uint32_t* changedCount,
    std::string* error) {
  const std::map<std::string, IndexEntry> current =
      IndexEntryMap(currentEntries);
  if (protectUntracked) {
    for (const auto& item : targetEntries) {
      if (current.find(item.first) != current.end()) {
        continue;
      }
      std::error_code existsError;
      if (fs::exists(
              context.repositoryPath / fs::path(item.first),
              existsError) &&
          !existsError) {
        if (error != nullptr) {
          *error =
              "Untracked working tree file would be overwritten: " +
              item.first;
        }
        return false;
      }
    }
  }

  uint32_t changed = 0;
  for (const auto& item : current) {
    if (targetEntries.find(item.first) == targetEntries.end()) {
      if (!RemoveWorkingTreePath(
              context.repositoryPath,
              item.first,
              error)) {
        return false;
      }
      ++changed;
    }
  }

  std::vector<IndexEntry> newIndex;
  newIndex.reserve(targetEntries.size());
  for (const auto& item : targetEntries) {
    const auto existing = current.find(item.first);
    const uint32_t targetMode = static_cast<uint32_t>(
        std::stoul(item.second.mode, nullptr, 8));
    if (existing == current.end() ||
        existing->second.objectId != item.second.objectId ||
        existing->second.mode != targetMode) {
      ++changed;
    }
    if (!WriteWorkingTreeEntry(context, item.second, error)) {
      return false;
    }
    newIndex.push_back(
        IndexEntryFromTree(item.second, context.repositoryPath));
  }
  if (!WriteIndex(context.gitDirectory / "index", newIndex, error)) {
    return false;
  }
  *changedCount = changed;
  return true;
}

bool HasTrackedChanges(
    const RepositorySnapshot& snapshot) {
  for (const FileStatus& file : snapshot.files) {
    if (file.tracked &&
        (file.staged || file.workTreeState != " ")) {
      return true;
    }
  }
  return false;
}

RepositoryOperation FailedOperation(const std::string& error) {
  RepositoryOperation result;
  result.error = error;
  result.snapshot.error = error;
  return result;
}

RepositoryOperation SuccessfulOperation(
    const RepositorySnapshot& snapshot,
    uint32_t changedCount) {
  RepositoryOperation result;
  result.success = true;
  result.changedCount = changedCount;
  result.snapshot = snapshot;
  return result;
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
  for (const ConfigEntry& entry :
       ReadConfigEntriesFromFile(gitDirectory / "config")) {
    const std::string prefix = "remote.";
    if (entry.key.rfind(prefix, 0) != 0) {
      continue;
    }
    const size_t separator = entry.key.rfind('.');
    if (separator <= prefix.size() ||
        separator + 1 >= entry.key.size()) {
      continue;
    }
    const std::string name = entry.key.substr(
        prefix.size(),
        separator - prefix.size());
    const std::string key = entry.key.substr(separator + 1);
    Remote& remote = remotes[name];
    remote.name = name;
    if (key == "url") {
      if (remote.fetchUrl.empty()) {
        remote.fetchUrl = entry.value;
      }
      if (remote.pushUrl.empty()) {
        remote.pushUrl = entry.value;
      }
    } else if (key == "pushurl") {
      if (!entry.value.empty()) {
        remote.pushUrl = entry.value;
      }
    }
  }

  std::vector<Remote> result;
  for (const auto& item : remotes) {
    result.push_back(item.second);
  }
  return result;
}

struct RemoteConfigValues {
  bool exists = false;
  std::vector<std::string> urls;
  std::vector<std::string> pushUrls;
};

RemoteConfigValues ReadRemoteConfigValues(
    const fs::path& configPath,
    const std::string& name) {
  RemoteConfigValues result;
  for (const ConfigEntry& entry :
       ReadConfigEntriesFromFile(configPath)) {
    const std::string prefix = "remote.";
    if (entry.key.rfind(prefix, 0) != 0) {
      continue;
    }
    const size_t separator = entry.key.rfind('.');
    if (separator <= prefix.size() ||
        separator + 1 >= entry.key.size()) {
      continue;
    }
    if (entry.key.substr(
            prefix.size(),
            separator - prefix.size()) != name) {
      continue;
    }
    result.exists = true;
    const std::string key = entry.key.substr(separator + 1);
    if (key == "url") {
      result.urls.push_back(entry.value);
    } else if (key == "pushurl") {
      result.pushUrls.push_back(entry.value);
    }
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

  std::map<std::string, TreeEntry> headEntries;
  std::string headTreeError;
  const bool headTreeAvailable =
      snapshot.head.empty() ||
      ReadHeadTree(
          commonGitDirectory,
          snapshot.head,
          &headEntries,
          &headTreeError);

  std::set<std::string> trackedPaths;
  std::set<std::string> stageZeroPaths;
  std::map<std::string, FileStatus> changedFiles;
  for (const IndexEntry& entry : indexEntries) {
    trackedPaths.insert(entry.path);
    FileStatus& status = changedFiles[entry.path];
    status.path = entry.path;
    status.tracked = true;
    if (entry.stage != 0) {
      status.indexState = "U";
      status.workTreeState = "U";
      status.staged = true;
      continue;
    }
    stageZeroPaths.insert(entry.path);
    if (snapshot.head.empty()) {
      status.indexState = "A";
    } else if (headTreeAvailable) {
      const auto headEntry = headEntries.find(entry.path);
      if (headEntry == headEntries.end()) {
        status.indexState = "A";
      } else if (
          headEntry->second.objectId != entry.objectId ||
          headEntry->second.mode != ModeString(entry.mode)) {
        status.indexState = "M";
      } else {
        status.indexState = " ";
      }
    } else {
      status.indexState = " ";
    }
    status.staged = status.indexState != " ";
    status.workTreeState = " ";
    IsWorkingTreeModified(repositoryPath, entry, &status.workTreeState);
  }
  if (headTreeAvailable) {
    for (const auto& item : headEntries) {
      if (stageZeroPaths.find(item.first) != stageZeroPaths.end()) {
        continue;
      }
      FileStatus& status = changedFiles[item.first];
      status.path = item.first;
      status.indexState = "D";
      status.workTreeState = " ";
      status.tracked = true;
      status.staged = true;
    }
  }
  for (const auto& item : changedFiles) {
    if (item.second.indexState != " " ||
        item.second.workTreeState != " ") {
      snapshot.files.push_back(item.second);
    }
  }
  AppendUntrackedFiles(
      repositoryPath,
      commonGitDirectory,
      trackedPaths,
      &snapshot.files);
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
        "\tlogallrefupdates = true\n"
        "[user]\n"
        "\tname = Harmony Developer\n"
        "\temail = harmony@pc.local\n";
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

RepositoryOperation StageRepository(
    const std::string& startPath,
    const std::vector<std::string>& paths) {
  if (paths.empty()) {
    return FailedOperation(
        "Nothing specified, nothing added. Maybe you wanted to say 'git add .'?");
  }
  RepositoryContext context;
  std::string error;
  if (!LoadRepositoryContext(startPath, &context, &error)) {
    return FailedOperation(error);
  }
  std::vector<IndexEntry> indexEntries;
  uint32_t indexVersion = 0;
  if (!ReadIndexEntries(
          context,
          &indexEntries,
          &indexVersion,
          &error)) {
    return FailedOperation(error);
  }
  std::map<std::string, IndexEntry> index = IndexEntryMap(indexEntries);
  std::set<std::string> workingTreePaths;
  std::set<std::string> trackedPaths;
  for (const auto& item : index) {
    trackedPaths.insert(item.first);
  }
  std::vector<FileStatus> untrackedFiles;
  AppendUntrackedFiles(
      context.repositoryPath,
      context.commonGitDirectory,
      trackedPaths,
      &untrackedFiles);
  for (const FileStatus& file : untrackedFiles) {
    workingTreePaths.insert(file.path);
  }
  std::set<std::string> candidates = workingTreePaths;
  for (const auto& item : index) {
    candidates.insert(item.first);
  }

  const fs::path basePath = CommandBasePath(startPath);
  bool matched = false;
  uint32_t changedCount = 0;
  for (const std::string& relativePath : candidates) {
    if (!PathRequested(
            relativePath,
            basePath,
            context.repositoryPath,
            paths)) {
      continue;
    }
    matched = true;
    const fs::path filePath =
        context.repositoryPath / fs::path(relativePath);
    struct stat fileStat {};
    if (lstat(filePath.c_str(), &fileStat) != 0) {
      const auto existing = index.find(relativePath);
      if (existing != index.end()) {
        index.erase(existing);
        ++changedCount;
      }
      continue;
    }

    std::string content;
    uint32_t mode = 0;
    if (!ReadWorkingTreeFile(
            filePath,
            &content,
            &mode,
            &fileStat,
            &error)) {
      return FailedOperation(error);
    }
    std::string objectIdText;
    if (!WriteLooseObject(
            context.commonGitDirectory,
            "blob",
            content,
            &objectIdText,
            &error)) {
      return FailedOperation(error);
    }
    std::array<uint8_t, 20> objectId {};
    if (!HexToObjectId(objectIdText, &objectId)) {
      return FailedOperation("Cannot encode staged Git object id.");
    }
    IndexEntry staged =
        IndexEntryFromStat(relativePath, fileStat, objectId);
    staged.mode = mode;
    const auto existing = index.find(relativePath);
    if (existing == index.end() ||
        !SameIndexContent(existing->second, staged)) {
      index[relativePath] = staged;
      ++changedCount;
    }
  }

  const bool stageAll =
      std::find(paths.begin(), paths.end(), "-A") != paths.end() ||
      std::find(paths.begin(), paths.end(), "--all") != paths.end();
  if (!matched && !stageAll) {
    std::string pathspec;
    for (const std::string& path : paths) {
      if (!path.empty() && path.front() != '-') {
        pathspec = path;
        break;
      }
    }
    return FailedOperation(
        "Pathspec '" + pathspec + "' did not match any files.");
  }
  if (changedCount > 0 &&
      !WriteIndex(
          context.gitDirectory / "index",
          IndexEntryVector(index),
          &error)) {
    return FailedOperation(error);
  }
  const RepositorySnapshot snapshot =
      InspectRepository(context.repositoryPath.generic_string());
  if (!snapshot.valid) {
    return FailedOperation(snapshot.error);
  }
  return SuccessfulOperation(snapshot, changedCount);
}

RepositoryOperation RemoveRepositoryPaths(
    const std::string& startPath,
    const std::vector<std::string>& paths,
    bool cached,
    bool force,
    bool recursive) {
  if (paths.empty()) {
    return FailedOperation("git rm requires a pathspec.");
  }
  RepositoryContext context;
  std::string error;
  if (!LoadRepositoryContext(startPath, &context, &error)) {
    return FailedOperation(error);
  }
  std::vector<IndexEntry> indexEntries;
  uint32_t indexVersion = 0;
  if (!ReadIndexEntries(
          context,
          &indexEntries,
          &indexVersion,
          &error)) {
    return FailedOperation(error);
  }
  std::map<std::string, IndexEntry> index =
      IndexEntryMap(indexEntries);
  const fs::path basePath = CommandBasePath(startPath);
  std::set<std::string> selected;
  for (const auto& item : index) {
    if (PathRequested(
            item.first,
            basePath,
            context.repositoryPath,
            paths)) {
      selected.insert(item.first);
    }
  }
  if (selected.empty()) {
    return FailedOperation("Pathspec did not match any tracked files.");
  }

  if (!recursive) {
    for (const std::string& spec : paths) {
      if (spec.empty() || spec.front() == '-') {
        continue;
      }
      fs::path requested(spec);
      if (!requested.is_absolute()) {
        requested = (basePath / requested).lexically_normal();
      }
      const std::string relative =
          RelativePathOrEmpty(context.repositoryPath, requested);
      if (relative.empty()) {
        continue;
      }
      bool exact = false;
      bool child = false;
      for (const std::string& path : selected) {
        exact = exact || path == relative;
        child = child ||
            relative == "." ||
            path.rfind(relative + "/", 0) == 0;
      }
      if (child && !exact) {
        return FailedOperation(
            "Not removing '" + spec + "' recursively without -r.");
      }
    }
  }

  const RepositorySnapshot before =
      InspectRepository(context.repositoryPath.generic_string());
  if (!before.valid) {
    return FailedOperation(before.error);
  }
  std::map<std::string, FileStatus> statusByPath;
  for (const FileStatus& status : before.files) {
    statusByPath[status.path] = status;
  }
  for (const std::string& path : selected) {
    const auto status = statusByPath.find(path);
    if (force || status == statusByPath.end()) {
      continue;
    }
    const bool indexChanged = status->second.indexState != " ";
    const bool workTreeChanged =
        status->second.workTreeState != " ";
    if ((!cached && (indexChanged || workTreeChanged)) ||
        (cached && indexChanged && workTreeChanged)) {
      return FailedOperation(
          "The following path has staged or unstaged changes: " + path +
          ". Use -f to force removal.");
    }
  }

  for (const std::string& path : selected) {
    if (!cached &&
        !RemoveWorkingTreePath(
            context.repositoryPath,
            path,
            &error)) {
      return FailedOperation(error);
    }
    index.erase(path);
  }
  if (!WriteIndex(
          context.gitDirectory / "index",
          IndexEntryVector(index),
          &error)) {
    return FailedOperation(error);
  }
  const RepositorySnapshot snapshot =
      InspectRepository(context.repositoryPath.generic_string());
  if (!snapshot.valid) {
    return FailedOperation(snapshot.error);
  }
  return SuccessfulOperation(
      snapshot,
      static_cast<uint32_t>(selected.size()));
}

RepositoryOperation MoveRepositoryPath(
    const std::string& startPath,
    const std::string& source,
    const std::string& destination,
    bool force) {
  if (source.empty() || destination.empty()) {
    return FailedOperation("git mv requires a source and destination.");
  }
  RepositoryContext context;
  std::string error;
  if (!LoadRepositoryContext(startPath, &context, &error)) {
    return FailedOperation(error);
  }
  const fs::path basePath = CommandBasePath(startPath);
  fs::path sourcePath(source);
  if (!sourcePath.is_absolute()) {
    sourcePath = basePath / sourcePath;
  }
  sourcePath = sourcePath.lexically_normal();
  std::string sourceRelative =
      RelativePathOrEmpty(context.repositoryPath, sourcePath);
  if (sourceRelative.empty() || sourceRelative == ".") {
    return FailedOperation("Source path is outside the repository.");
  }
  if (sourceRelative == ".git" ||
      sourceRelative.rfind(".git/", 0) == 0) {
    return FailedOperation("Cannot move repository metadata.");
  }

  std::error_code sourceStatusError;
  const fs::file_status sourceStatus =
      fs::symlink_status(sourcePath, sourceStatusError);
  if (sourceStatusError ||
      sourceStatus.type() == fs::file_type::not_found) {
    return FailedOperation("Source path does not exist: " + source);
  }

  fs::path destinationPath(destination);
  if (!destinationPath.is_absolute()) {
    destinationPath = basePath / destinationPath;
  }
  destinationPath = destinationPath.lexically_normal();
  std::error_code destinationStatusError;
  const fs::file_status destinationStatus =
      fs::symlink_status(destinationPath, destinationStatusError);
  if (!destinationStatusError &&
      fs::is_directory(destinationStatus)) {
    destinationPath /= sourcePath.filename();
  }
  destinationPath = destinationPath.lexically_normal();
  std::string destinationRelative =
      RelativePathOrEmpty(context.repositoryPath, destinationPath);
  if (destinationRelative.empty() || destinationRelative == ".") {
    return FailedOperation("Destination path is outside the repository.");
  }
  if (destinationRelative == ".git" ||
      destinationRelative.rfind(".git/", 0) == 0) {
    return FailedOperation("Cannot move a path into repository metadata.");
  }
  if (destinationRelative == sourceRelative) {
    return FailedOperation("Source and destination are the same path.");
  }
  if (fs::is_directory(sourceStatus) &&
      destinationRelative.rfind(sourceRelative + "/", 0) == 0) {
    return FailedOperation("Cannot move a directory into itself.");
  }

  std::vector<IndexEntry> indexEntries;
  uint32_t indexVersion = 0;
  if (!ReadIndexEntries(
          context,
          &indexEntries,
          &indexVersion,
          &error)) {
    return FailedOperation(error);
  }
  std::map<std::string, IndexEntry> index =
      IndexEntryMap(indexEntries);
  std::vector<std::string> selected;
  for (const auto& item : index) {
    if (item.first == sourceRelative ||
        item.first.rfind(sourceRelative + "/", 0) == 0) {
      selected.push_back(item.first);
    }
  }
  if (selected.empty()) {
    return FailedOperation("Source path is not tracked: " + source);
  }

  std::set<std::string> selectedSet(
      selected.begin(),
      selected.end());
  std::map<std::string, std::string> destinations;
  for (const std::string& path : selected) {
    const std::string suffix =
        path == sourceRelative
            ? std::string()
            : path.substr(sourceRelative.size());
    const std::string target = destinationRelative + suffix;
    const auto collision = index.find(target);
    if (collision != index.end() &&
        selectedSet.find(target) == selectedSet.end() &&
        !force) {
      return FailedOperation(
          "Destination path is already tracked: " + target);
    }
    destinations[path] = target;
  }

  std::error_code finalDestinationError;
  const fs::file_status finalDestinationStatus =
      fs::symlink_status(destinationPath, finalDestinationError);
  if (!finalDestinationError &&
      finalDestinationStatus.type() != fs::file_type::not_found) {
    if (!force) {
      return FailedOperation(
          "Destination path already exists: " +
          destinationPath.string());
    }
    if (fs::is_directory(finalDestinationStatus)) {
      return FailedOperation(
          "Cannot force-overwrite a destination directory.");
    }
    std::error_code removeError;
    if (!fs::remove(destinationPath, removeError) || removeError) {
      return FailedOperation(
          "Cannot replace destination path: " +
          removeError.message());
    }
  }
  if (!EnsureDirectory(destinationPath.parent_path(), &error)) {
    return FailedOperation(error);
  }
  std::error_code renameError;
  fs::rename(sourcePath, destinationPath, renameError);
  if (renameError) {
    return FailedOperation(
        "Cannot move " + source + " to " + destination + ": " +
        renameError.message());
  }

  std::map<std::string, IndexEntry> nextIndex = index;
  for (const std::string& path : selected) {
    nextIndex.erase(path);
  }
  for (const auto& item : destinations) {
    IndexEntry moved = index.at(item.first);
    moved.path = item.second;
    nextIndex.erase(item.second);
    nextIndex[item.second] = moved;
  }
  if (!WriteIndex(
          context.gitDirectory / "index",
          IndexEntryVector(nextIndex),
          &error)) {
    std::error_code rollbackError;
    fs::rename(destinationPath, sourcePath, rollbackError);
    if (rollbackError) {
      error += " The working-tree move could not be rolled back: " +
          rollbackError.message();
    }
    return FailedOperation(error);
  }
  const RepositorySnapshot snapshot =
      InspectRepository(context.repositoryPath.generic_string());
  if (!snapshot.valid) {
    return FailedOperation(snapshot.error);
  }
  return SuccessfulOperation(
      snapshot,
      static_cast<uint32_t>(selected.size()));
}

RepositoryOperation RestoreStaged(
    const std::string& startPath,
    const std::vector<std::string>& paths) {
  if (paths.empty()) {
    return FailedOperation("git restore --staged requires a pathspec.");
  }
  RepositoryContext context;
  std::string error;
  if (!LoadRepositoryContext(startPath, &context, &error)) {
    return FailedOperation(error);
  }
  std::vector<IndexEntry> indexEntries;
  uint32_t indexVersion = 0;
  if (!ReadIndexEntries(
          context,
          &indexEntries,
          &indexVersion,
          &error)) {
    return FailedOperation(error);
  }
  std::map<std::string, IndexEntry> index = IndexEntryMap(indexEntries);
  std::map<std::string, TreeEntry> headEntries;
  if (!ReadHeadTree(
          context.commonGitDirectory,
          context.headObjectId,
          &headEntries,
          &error)) {
    return FailedOperation(error);
  }
  std::set<std::string> candidates;
  for (const auto& item : index) {
    candidates.insert(item.first);
  }
  for (const auto& item : headEntries) {
    candidates.insert(item.first);
  }

  const fs::path basePath = CommandBasePath(startPath);
  bool matched = false;
  uint32_t changedCount = 0;
  for (const std::string& relativePath : candidates) {
    if (!PathRequested(
            relativePath,
            basePath,
            context.repositoryPath,
            paths)) {
      continue;
    }
    matched = true;
    const auto head = headEntries.find(relativePath);
    const auto existing = index.find(relativePath);
    if (head == headEntries.end()) {
      if (existing != index.end()) {
        index.erase(existing);
        ++changedCount;
      }
      continue;
    }
    const IndexEntry restored =
        IndexEntryFromTree(head->second, context.repositoryPath);
    if (existing == index.end() ||
        !SameIndexContent(existing->second, restored)) {
      index[relativePath] = restored;
      ++changedCount;
    }
  }
  if (!matched) {
    return FailedOperation("Pathspec did not match any tracked files.");
  }
  if (changedCount > 0 &&
      !WriteIndex(
          context.gitDirectory / "index",
          IndexEntryVector(index),
          &error)) {
    return FailedOperation(error);
  }
  const RepositorySnapshot snapshot =
      InspectRepository(context.repositoryPath.generic_string());
  if (!snapshot.valid) {
    return FailedOperation(snapshot.error);
  }
  return SuccessfulOperation(snapshot, changedCount);
}

RepositoryOperation RestoreWorkingTree(
    const std::string& startPath,
    const std::vector<std::string>& paths) {
  if (paths.empty()) {
    return FailedOperation("git restore requires a pathspec.");
  }
  RepositoryContext context;
  std::string error;
  if (!LoadRepositoryContext(startPath, &context, &error)) {
    return FailedOperation(error);
  }
  std::vector<IndexEntry> indexEntries;
  uint32_t indexVersion = 0;
  if (!ReadIndexEntries(
          context,
          &indexEntries,
          &indexVersion,
          &error)) {
    return FailedOperation(error);
  }
  const fs::path basePath = CommandBasePath(startPath);
  bool matched = false;
  uint32_t changedCount = 0;
  for (const IndexEntry& indexEntry : indexEntries) {
    if (indexEntry.stage != 0 ||
        !PathRequested(
            indexEntry.path,
            basePath,
            context.repositoryPath,
            paths)) {
      continue;
    }
    matched = true;
    std::string state;
    if (!IsWorkingTreeModified(
            context.repositoryPath,
            indexEntry,
            &state)) {
      continue;
    }
    const TreeEntry treeEntry = {
        indexEntry.path,
        ModeString(indexEntry.mode),
        indexEntry.objectId};
    if (!WriteWorkingTreeEntry(context, treeEntry, &error)) {
      return FailedOperation(error);
    }
    ++changedCount;
  }
  if (!matched) {
    return FailedOperation("Pathspec did not match any indexed files.");
  }
  const RepositorySnapshot snapshot =
      InspectRepository(context.repositoryPath.generic_string());
  if (!snapshot.valid) {
    return FailedOperation(snapshot.error);
  }
  return SuccessfulOperation(snapshot, changedCount);
}

RepositoryOperation RestoreFromSource(
    const std::string& startPath,
    const std::string& source,
    const std::vector<std::string>& paths,
    bool staged,
    bool worktree) {
  if (paths.empty()) {
    return FailedOperation("git restore requires a pathspec.");
  }
  if (!staged && !worktree) {
    worktree = true;
  }

  RepositoryContext context;
  std::string error;
  if (!LoadRepositoryContext(startPath, &context, &error)) {
    return FailedOperation(error);
  }
  const std::string sourceObjectId =
      ResolveRevision(context, source, &error);
  if (sourceObjectId.empty()) {
    return FailedOperation(error);
  }
  std::map<std::string, TreeEntry> sourceEntries;
  if (!ReadHeadTree(
          context.commonGitDirectory,
          sourceObjectId,
          &sourceEntries,
          &error)) {
    return FailedOperation(error);
  }

  std::vector<IndexEntry> indexEntries;
  uint32_t indexVersion = 0;
  if (!ReadIndexEntries(
          context,
          &indexEntries,
          &indexVersion,
          &error)) {
    return FailedOperation(error);
  }
  const std::map<std::string, IndexEntry> current =
      IndexEntryMap(indexEntries);
  std::map<std::string, IndexEntry> nextIndex = current;
  bool indexChanged = false;
  std::set<std::string> candidates;
  for (const auto& item : current) {
    candidates.insert(item.first);
  }
  for (const auto& item : sourceEntries) {
    candidates.insert(item.first);
  }

  const fs::path basePath = CommandBasePath(startPath);
  std::set<std::string> selected;
  for (const std::string& relativePath : candidates) {
    if (PathRequested(
            relativePath,
            basePath,
            context.repositoryPath,
            paths)) {
      selected.insert(relativePath);
    }
  }
  if (selected.empty()) {
    return FailedOperation("Pathspec did not match any files.");
  }

  std::set<std::string> changedPaths;
  for (const std::string& relativePath : selected) {
    const auto target = sourceEntries.find(relativePath);
    const auto existing = current.find(relativePath);
    if (worktree) {
      if (target == sourceEntries.end()) {
        struct stat fileStat {};
        if (lstat(
                (context.repositoryPath / fs::path(relativePath)).c_str(),
                &fileStat) == 0) {
          if (!RemoveWorkingTreePath(
                  context.repositoryPath,
                  relativePath,
                  &error)) {
            return FailedOperation(error);
          }
          changedPaths.insert(relativePath);
        }
      } else {
        const fs::path filePath =
            context.repositoryPath / fs::path(relativePath);
        bool matchesTarget = false;
        struct stat fileStat {};
        const bool pathExists =
            lstat(filePath.c_str(), &fileStat) == 0;
        if (pathExists) {
          std::string content;
          uint32_t mode = 0;
          std::string readError;
          if (ReadWorkingTreeFile(
                  filePath,
                  &content,
                  &mode,
                  nullptr,
                  &readError)) {
            uint32_t targetMode = 0;
            try {
              targetMode = static_cast<uint32_t>(
                  std::stoul(target->second.mode, nullptr, 8));
            } catch (...) {
              return FailedOperation(
                  "Unsupported mode for " + relativePath + ".");
            }
            matchesTarget =
                FileObjectId(content) ==
                    ObjectIdToHex(target->second.objectId) &&
                mode == targetMode;
          }
        }
        if (!matchesTarget) {
          changedPaths.insert(relativePath);
        }
        if (existing == current.end() &&
            pathExists &&
            !matchesTarget) {
          return FailedOperation(
              "Cannot restore " + relativePath +
              " because an untracked path would be overwritten.");
        }
        if (!WriteWorkingTreeEntry(context, target->second, &error)) {
          return FailedOperation(error);
        }
      }
    }

    if (staged) {
      if (target == sourceEntries.end()) {
        if (existing != current.end()) {
          nextIndex.erase(relativePath);
          indexChanged = true;
          changedPaths.insert(relativePath);
        }
      } else {
        const IndexEntry replacement =
            IndexEntryFromTree(target->second, context.repositoryPath);
        if (existing == current.end() ||
            !SameIndexContent(existing->second, replacement)) {
          indexChanged = true;
          changedPaths.insert(relativePath);
        }
        nextIndex[relativePath] = replacement;
      }
    }
  }

  if (staged && indexChanged) {
    if (!WriteIndex(
            context.gitDirectory / "index",
            IndexEntryVector(nextIndex),
            &error)) {
      return FailedOperation(error);
    }
  }
  const RepositorySnapshot snapshot =
      InspectRepository(context.repositoryPath.generic_string());
  if (!snapshot.valid) {
    return FailedOperation(snapshot.error);
  }
  return SuccessfulOperation(
      snapshot,
      static_cast<uint32_t>(changedPaths.size()));
}

RepositoryOperation ResetHard(const std::string& startPath) {
  RepositoryContext context;
  std::string error;
  if (!LoadRepositoryContext(startPath, &context, &error)) {
    return FailedOperation(error);
  }
  if (context.headObjectId.empty()) {
    return FailedOperation("Cannot reset --hard before the first commit.");
  }
  std::vector<IndexEntry> indexEntries;
  uint32_t indexVersion = 0;
  if (!ReadIndexEntries(
          context,
          &indexEntries,
          &indexVersion,
          &error)) {
    return FailedOperation(error);
  }
  std::map<std::string, TreeEntry> headEntries;
  if (!ReadHeadTree(
          context.commonGitDirectory,
          context.headObjectId,
          &headEntries,
          &error)) {
    return FailedOperation(error);
  }
  uint32_t changedCount = 0;
  if (!MaterializeTree(
          context,
          headEntries,
          indexEntries,
          false,
          &changedCount,
          &error)) {
    return FailedOperation(error);
  }
  if (!AppendReflog(
          context,
          "HEAD",
          context.headObjectId,
          context.headObjectId,
          "reset: moving to HEAD",
          &error)) {
    return FailedOperation(error);
  }
  const RepositorySnapshot snapshot =
      InspectRepository(context.repositoryPath.generic_string());
  if (!snapshot.valid) {
    return FailedOperation(snapshot.error);
  }
  return SuccessfulOperation(snapshot, changedCount);
}

RepositoryOperation CommitRepository(
    const std::string& startPath,
    const std::string& message) {
  if (Trim(message).empty()) {
    return FailedOperation("Aborting commit due to empty commit message.");
  }
  RepositoryContext context;
  std::string error;
  if (!LoadRepositoryContext(startPath, &context, &error)) {
    return FailedOperation(error);
  }
  std::vector<IndexEntry> indexEntries;
  uint32_t indexVersion = 0;
  if (!ReadIndexEntries(
          context,
          &indexEntries,
          &indexVersion,
          &error)) {
    return FailedOperation(error);
  }
  std::vector<DiffFile> stagedFiles;
  if (!BuildStagedDiffFiles(
          context,
          indexEntries,
          &stagedFiles,
          &error)) {
    return FailedOperation(error);
  }
  if (stagedFiles.empty()) {
    return FailedOperation("Nothing to commit, working tree clean.");
  }

  std::string treeObjectId;
  if (!BuildTreeFromIndex(
          context.commonGitDirectory,
          indexEntries,
          &treeObjectId,
          &error)) {
    return FailedOperation(error);
  }
  const std::string author =
      CommitAuthor(context.commonGitDirectory, &error);
  if (author.empty()) {
    return FailedOperation(error);
  }
  const std::string timestamp = CurrentGitTimestamp();
  std::string payload = "tree " + treeObjectId + "\n";
  if (!context.headObjectId.empty()) {
    payload += "parent " + context.headObjectId + "\n";
  }
  payload += "author " + author + " " + timestamp + "\n";
  payload += "committer " + author + " " + timestamp + "\n\n";
  payload += message;
  if (payload.back() != '\n') {
    payload.push_back('\n');
  }

  std::string commitObjectId;
  if (!WriteLooseObject(
          context.commonGitDirectory,
          "commit",
          payload,
          &commitObjectId,
          &error)) {
    return FailedOperation(error);
  }
  const std::string headPrefix = "ref:";
  if (context.headText.rfind(headPrefix, 0) == 0) {
    const std::string refName =
        Trim(context.headText.substr(headPrefix.size()));
    if (refName.rfind("refs/heads/", 0) != 0) {
      return FailedOperation("HEAD does not point to a local branch.");
    }
    if (!WriteReference(
            context.commonGitDirectory / refName,
            commitObjectId,
            &error)) {
      return FailedOperation(error);
    }
    if (!AppendReflog(
            context,
            refName,
            context.headObjectId,
            commitObjectId,
            "commit: " + message,
            &error) ||
        !AppendReflog(
            context,
            "HEAD",
            context.headObjectId,
            commitObjectId,
            "commit: " + message,
            &error)) {
      return FailedOperation(error);
    }
  } else if (!WriteReference(
                 context.gitDirectory / "HEAD",
                 commitObjectId,
                 &error)) {
    return FailedOperation(error);
  } else if (!AppendReflog(
                 context,
                 "HEAD",
                 context.headObjectId,
                 commitObjectId,
                 "commit: " + message,
                 &error)) {
    return FailedOperation(error);
  }

  const RepositorySnapshot snapshot =
      InspectRepository(context.repositoryPath.generic_string());
  if (!snapshot.valid) {
    return FailedOperation(snapshot.error);
  }
  return SuccessfulOperation(
      snapshot,
      static_cast<uint32_t>(stagedFiles.size()));
}

RepositoryOperation CreateBranch(
    const std::string& startPath,
    const std::string& name,
    bool checkout) {
  if (!ValidBranchName(name)) {
    return FailedOperation("'" + name + "' is not a valid branch name.");
  }
  RepositoryContext context;
  std::string error;
  if (!LoadRepositoryContext(startPath, &context, &error)) {
    return FailedOperation(error);
  }
  if (context.headObjectId.empty()) {
    return FailedOperation("Cannot create a branch before the first commit.");
  }
  const std::string refName = "refs/heads/" + name;
  const std::string existing = ResolveHeadObject(
      context.gitDirectory,
      context.commonGitDirectory,
      "ref: " + refName);
  if (!existing.empty()) {
    return FailedOperation("A branch named '" + name + "' already exists.");
  }
  if (!WriteReference(
          context.commonGitDirectory / refName,
          context.headObjectId,
          &error)) {
    return FailedOperation(error);
  }
  if (checkout &&
      !WriteAtomicFile(
          context.gitDirectory / "HEAD",
          "ref: " + refName + "\n",
          &error)) {
    bool removed = false;
    std::string cleanupError;
    DeleteReference(context, refName, &removed, &cleanupError);
    return FailedOperation(error);
  }
  bool detached = false;
  const std::string oldBranch =
      BranchFromHead(context.headText, &detached);
  if (!AppendReflog(
          context,
          refName,
          "",
          context.headObjectId,
          "branch: Created from HEAD",
          &error) ||
      (checkout &&
       !AppendReflog(
           context,
           "HEAD",
           context.headObjectId,
           context.headObjectId,
           "checkout: moving from " + oldBranch + " to " + name,
           &error))) {
    return FailedOperation(error);
  }
  const RepositorySnapshot snapshot =
      InspectRepository(context.repositoryPath.generic_string());
  if (!snapshot.valid) {
    return FailedOperation(snapshot.error);
  }
  return SuccessfulOperation(snapshot, 1);
}

RepositoryOperation MoveBranch(
    const std::string& startPath,
    const std::string& oldName,
    const std::string& newName,
    bool force) {
  if (!ValidBranchName(oldName) || !ValidBranchName(newName)) {
    return FailedOperation("Invalid branch name.");
  }
  if (oldName == newName) {
    return FailedOperation(
        "A branch cannot be renamed to the same name.");
  }
  RepositoryContext context;
  std::string error;
  if (!LoadRepositoryContext(startPath, &context, &error)) {
    return FailedOperation(error);
  }
  const std::string oldRef = "refs/heads/" + oldName;
  const std::string newRef = "refs/heads/" + newName;
  const std::string oldObjectId = ResolveHeadObject(
      context.gitDirectory,
      context.commonGitDirectory,
      "ref: " + oldRef);
  if (oldObjectId.empty()) {
    return FailedOperation("Branch '" + oldName + "' not found.");
  }
  const std::string existingObjectId = ResolveHeadObject(
      context.gitDirectory,
      context.commonGitDirectory,
      "ref: " + newRef);
  if (!existingObjectId.empty()) {
    if (!force) {
      return FailedOperation(
          "A branch named '" + newName + "' already exists.");
    }
    if (IsBranchCheckedOut(context, newName)) {
      return FailedOperation(
          "Cannot force rename branch '" + newName +
          "' because it is checked out.");
    }
    bool removed = false;
    if (!DeleteReference(context, newRef, &removed, &error)) {
      return FailedOperation(error);
    }
    if (!RemoveReflog(context, newRef, &error)) {
      return FailedOperation(error);
    }
  }
  bool removed = false;
  if (!DeleteReference(context, oldRef, &removed, &error) || !removed) {
    return FailedOperation(
        error.empty() ? "Branch '" + oldName + "' not found." : error);
  }
  if (!WriteReference(
          context.commonGitDirectory / newRef,
          oldObjectId,
          &error)) {
    return FailedOperation(error);
  }

  const fs::path oldLog = ReflogPath(context, oldRef);
  const fs::path newLog = ReflogPath(context, newRef);
  std::error_code logExistsError;
  if (fs::is_regular_file(oldLog, logExistsError)) {
    if (!EnsureDirectory(newLog.parent_path(), &error)) {
      return FailedOperation(error);
    }
    std::error_code renameError;
    fs::rename(oldLog, newLog, renameError);
    if (renameError) {
      return FailedOperation(
          "Cannot move branch reflog: " + renameError.message());
    }
  }
  if (!AppendReflog(
          context,
          newRef,
          oldObjectId,
          oldObjectId,
          "Branch: renamed " + oldRef + " to " + newRef,
          &error)) {
    return FailedOperation(error);
  }
  bool configChanged = false;
  if (!RewriteBranchConfigSections(
          context.commonGitDirectory / "config",
          oldName,
          newName,
          false,
          &configChanged,
          &error)) {
    return FailedOperation(error);
  }
  if (!UpdateWorktreeHeads(context, oldRef, newRef, &error)) {
    return FailedOperation(error);
  }
  const RepositorySnapshot snapshot =
      InspectRepository(context.repositoryPath.generic_string());
  if (!snapshot.valid) {
    return FailedOperation(snapshot.error);
  }
  return SuccessfulOperation(snapshot, 1);
}

RepositoryOperation CopyBranch(
    const std::string& startPath,
    const std::string& oldName,
    const std::string& newName,
    bool force) {
  if (!ValidBranchName(oldName) || !ValidBranchName(newName)) {
    return FailedOperation("Invalid branch name.");
  }
  if (oldName == newName) {
    return FailedOperation(
        "A branch cannot be copied to the same name.");
  }
  RepositoryContext context;
  std::string error;
  if (!LoadRepositoryContext(startPath, &context, &error)) {
    return FailedOperation(error);
  }
  const std::string oldRef = "refs/heads/" + oldName;
  const std::string newRef = "refs/heads/" + newName;
  const std::string oldObjectId = ResolveHeadObject(
      context.gitDirectory,
      context.commonGitDirectory,
      "ref: " + oldRef);
  if (oldObjectId.empty()) {
    return FailedOperation("Branch '" + oldName + "' not found.");
  }
  const std::string existingObjectId = ResolveHeadObject(
      context.gitDirectory,
      context.commonGitDirectory,
      "ref: " + newRef);
  if (!existingObjectId.empty()) {
    if (!force) {
      return FailedOperation(
          "A branch named '" + newName + "' already exists.");
    }
    if (IsBranchCheckedOut(context, newName)) {
      return FailedOperation(
          "Cannot force copy branch '" + newName +
          "' because it is checked out.");
    }
    bool removed = false;
    if (!DeleteReference(context, newRef, &removed, &error)) {
      return FailedOperation(error);
    }
    if (!RemoveReflog(context, newRef, &error)) {
      return FailedOperation(error);
    }
  }
  if (!WriteReference(
          context.commonGitDirectory / newRef,
          oldObjectId,
          &error)) {
    return FailedOperation(error);
  }

  const fs::path oldLog = ReflogPath(context, oldRef);
  const fs::path newLog = ReflogPath(context, newRef);
  std::error_code logExistsError;
  if (fs::is_regular_file(oldLog, logExistsError)) {
    if (!EnsureDirectory(newLog.parent_path(), &error)) {
      return FailedOperation(error);
    }
    std::error_code copyError;
    if (!fs::copy_file(
            oldLog,
            newLog,
            fs::copy_options::overwrite_existing,
            copyError) ||
        copyError) {
      return FailedOperation(
          "Cannot copy branch reflog: " + copyError.message());
    }
  }
  if (!AppendReflog(
          context,
          newRef,
          oldObjectId,
          oldObjectId,
          "Branch: copied " + oldRef + " to " + newRef,
          &error)) {
    return FailedOperation(error);
  }
  bool configChanged = false;
  if (!RewriteBranchConfigSections(
          context.commonGitDirectory / "config",
          oldName,
          newName,
          true,
          &configChanged,
          &error)) {
    return FailedOperation(error);
  }
  const RepositorySnapshot snapshot =
      InspectRepository(context.repositoryPath.generic_string());
  if (!snapshot.valid) {
    return FailedOperation(snapshot.error);
  }
  return SuccessfulOperation(snapshot, 1);
}

RepositoryOperation SwitchBranch(
    const std::string& startPath,
    const std::string& name) {
  if (!ValidBranchName(name)) {
    return FailedOperation("Invalid reference: " + name);
  }
  RepositoryContext context;
  std::string error;
  if (!LoadRepositoryContext(startPath, &context, &error)) {
    return FailedOperation(error);
  }
  const std::string refName = "refs/heads/" + name;
  const std::string targetObjectId = ResolveHeadObject(
      context.gitDirectory,
      context.commonGitDirectory,
      "ref: " + refName);
  if (targetObjectId.empty()) {
    return FailedOperation("Invalid reference: " + name);
  }
  if (context.headText == "ref: " + refName) {
    const RepositorySnapshot snapshot =
        InspectRepository(context.repositoryPath.generic_string());
    return snapshot.valid
        ? SuccessfulOperation(snapshot, 0)
        : FailedOperation(snapshot.error);
  }
  const RepositorySnapshot before =
      InspectRepository(context.repositoryPath.generic_string());
  if (!before.valid) {
    return FailedOperation(before.error);
  }
  if (HasTrackedChanges(before)) {
    return FailedOperation(
        "Local changes would be overwritten by switch. Commit or restore them first.");
  }

  std::vector<IndexEntry> indexEntries;
  uint32_t indexVersion = 0;
  if (!ReadIndexEntries(
          context,
          &indexEntries,
          &indexVersion,
          &error)) {
    return FailedOperation(error);
  }
  std::map<std::string, TreeEntry> targetEntries;
  if (!ReadHeadTree(
          context.commonGitDirectory,
          targetObjectId,
          &targetEntries,
          &error)) {
    return FailedOperation(error);
  }
  uint32_t changedCount = 0;
  if (!MaterializeTree(
          context,
          targetEntries,
          indexEntries,
          true,
          &changedCount,
          &error)) {
    return FailedOperation(error);
  }
  if (!WriteAtomicFile(
          context.gitDirectory / "HEAD",
          "ref: " + refName + "\n",
          &error)) {
    return FailedOperation(error);
  }
  bool detached = false;
  const std::string oldBranch =
      BranchFromHead(context.headText, &detached);
  if (!AppendReflog(
          context,
          "HEAD",
          context.headObjectId,
          targetObjectId,
          "checkout: moving from " + oldBranch + " to " + name,
          &error)) {
    return FailedOperation(error);
  }
  const RepositorySnapshot snapshot =
      InspectRepository(context.repositoryPath.generic_string());
  if (!snapshot.valid) {
    return FailedOperation(snapshot.error);
  }
  return SuccessfulOperation(snapshot, changedCount);
}

RepositoryOperation CheckoutBranch(
    const std::string& startPath,
    const std::string& name,
    const std::string& startPoint) {
  if (!ValidBranchName(name)) {
    return FailedOperation("'" + name + "' is not a valid branch name.");
  }
  RepositoryContext context;
  std::string error;
  if (!LoadRepositoryContext(startPath, &context, &error)) {
    return FailedOperation(error);
  }
  const std::string source =
      Trim(startPoint).empty() ? "HEAD" : startPoint;
  const std::string targetObjectId =
      ResolveRevision(context, source, &error);
  if (targetObjectId.empty()) {
    return FailedOperation(error);
  }
  const RepositorySnapshot before =
      InspectRepository(context.repositoryPath.generic_string());
  if (!before.valid) {
    return FailedOperation(before.error);
  }
  if (HasTrackedChanges(before)) {
    return FailedOperation(
        "Local changes would be overwritten by checkout. Commit or restore them first.");
  }

  std::vector<IndexEntry> indexEntries;
  uint32_t indexVersion = 0;
  if (!ReadIndexEntries(
          context,
          &indexEntries,
          &indexVersion,
          &error)) {
    return FailedOperation(error);
  }
  std::map<std::string, TreeEntry> targetEntries;
  if (!ReadHeadTree(
          context.commonGitDirectory,
          targetObjectId,
          &targetEntries,
          &error)) {
    return FailedOperation(error);
  }
  uint32_t changedCount = 0;
  if (!MaterializeTree(
          context,
          targetEntries,
          indexEntries,
          true,
          &changedCount,
          &error)) {
    return FailedOperation(error);
  }

  const std::string refName = "refs/heads/" + name;
  const std::string existingObjectId = ResolveHeadObject(
      context.gitDirectory,
      context.commonGitDirectory,
      "ref: " + refName);
  if (!WriteReference(
          context.commonGitDirectory / refName,
          targetObjectId,
          &error) ||
      !WriteAtomicFile(
          context.gitDirectory / "HEAD",
          "ref: " + refName + "\n",
          &error)) {
    return FailedOperation(error);
  }
  bool detached = false;
  const std::string oldBranch =
      BranchFromHead(context.headText, &detached);
  const std::string branchMessage = existingObjectId.empty()
      ? "branch: Created from " + source
      : "branch: Reset to " + source;
  if (!AppendReflog(
          context,
          refName,
          existingObjectId,
          targetObjectId,
          branchMessage,
          &error) ||
      !AppendReflog(
          context,
          "HEAD",
          context.headObjectId,
          targetObjectId,
          "checkout: moving from " + oldBranch + " to " + name,
          &error)) {
    return FailedOperation(error);
  }
  const RepositorySnapshot snapshot =
      InspectRepository(context.repositoryPath.generic_string());
  if (!snapshot.valid) {
    return FailedOperation(snapshot.error);
  }
  return SuccessfulOperation(snapshot, changedCount);
}

RepositoryOperation DeleteBranch(
    const std::string& startPath,
    const std::string& name,
    bool force) {
  if (!ValidBranchName(name)) {
    return FailedOperation("'" + name + "' is not a valid branch name.");
  }
  RepositoryContext context;
  std::string error;
  if (!LoadRepositoryContext(startPath, &context, &error)) {
    return FailedOperation(error);
  }
  const std::string refName = "refs/heads/" + name;
  if (context.headText == "ref: " + refName) {
    return FailedOperation(
        "Cannot delete branch '" + name + "' because it is checked out.");
  }
  const std::string branchObjectId = ResolveHeadObject(
      context.gitDirectory,
      context.commonGitDirectory,
      "ref: " + refName);
  if (branchObjectId.empty()) {
    return FailedOperation("Branch '" + name + "' not found.");
  }
  if (!force) {
    if (context.headObjectId.empty()) {
      return FailedOperation(
          "Cannot verify whether branch '" + name + "' is merged.");
    }
    error.clear();
    const bool merged = IsAncestorCommit(
        context.commonGitDirectory,
        branchObjectId,
        context.headObjectId,
        &error);
    if (!error.empty()) {
      return FailedOperation(error);
    }
    if (!merged) {
      return FailedOperation(
          "Branch '" + name + "' is not fully merged. Use -D to force deletion.");
    }
  }
  bool removed = false;
  if (!DeleteReference(context, refName, &removed, &error)) {
    return FailedOperation(error);
  }
  if (!removed) {
    return FailedOperation("Branch '" + name + "' not found.");
  }
  if (!RemoveReflog(context, refName, &error)) {
    return FailedOperation(error);
  }
  const RepositorySnapshot snapshot =
      InspectRepository(context.repositoryPath.generic_string());
  if (!snapshot.valid) {
    return FailedOperation(snapshot.error);
  }
  return SuccessfulOperation(snapshot, 1);
}

std::string DiffRepository(
    const std::string& startPath,
    bool staged,
    std::string* error) {
  if (error != nullptr) {
    error->clear();
  }
  RepositoryContext context;
  if (!LoadRepositoryContext(startPath, &context, error)) {
    return "";
  }
  std::vector<IndexEntry> indexEntries;
  uint32_t indexVersion = 0;
  if (!ReadIndexEntries(
          context,
          &indexEntries,
          &indexVersion,
          error)) {
    return "";
  }
  std::vector<DiffFile> files;
  const bool built = staged
      ? BuildStagedDiffFiles(
          context,
          indexEntries,
          &files,
          error)
      : BuildWorkTreeDiffFiles(
          context,
          indexEntries,
          &files,
          error);
  if (!built) {
    return "";
  }
  std::ostringstream output;
  for (size_t index = 0; index < files.size(); ++index) {
    if (index > 0) {
      output << '\n';
    }
    output << FormatDiffFile(files[index]);
  }
  return output.str();
}

std::vector<Commit> ReadLog(
    const std::string& startPath,
    uint32_t maxCount,
    std::string* error) {
  std::vector<Commit> commits;
  if (error != nullptr) {
    error->clear();
  }
  if (maxCount == 0) {
    return commits;
  }
  RepositoryContext context;
  if (!LoadRepositoryContext(startPath, &context, error)) {
    return commits;
  }
  std::string current = context.headObjectId;
  std::set<std::string> visited;
  while (!current.empty() &&
         commits.size() < static_cast<size_t>(maxCount) &&
         visited.insert(current).second) {
    ObjectData commitObject;
    if (!ReadCommitObject(
            context.commonGitDirectory,
            current,
            &commitObject,
            error)) {
      commits.clear();
      return commits;
    }
    const std::string authorLine = ReadAuthorLine(commitObject.payload);
    std::string subject = CommitMessage(commitObject.payload);
    const size_t newline = subject.find('\n');
    if (newline != std::string::npos) {
      subject = subject.substr(0, newline);
    }
    commits.push_back({
        current,
        subject,
        CommitAuthorName(authorLine),
        FormatCommitTimestamp(CommitAuthorTimestamp(authorLine))});
    current = ReadParent(commitObject.payload);
  }
  return commits;
}

std::string ShowRevision(
    const std::string& startPath,
    const std::string& revision,
    bool statOnly,
    bool oneLine,
    const std::vector<std::string>& paths,
    std::string* error) {
  if (error != nullptr) {
    error->clear();
  }
  RepositoryContext context;
  if (!LoadRepositoryContext(startPath, &context, error)) {
    return "";
  }
  const std::string source =
      Trim(revision).empty() ? "HEAD" : revision;
  const std::string objectId =
      ResolveRevision(context, source, error);
  if (objectId.empty()) {
    return "";
  }
  ObjectData commit;
  if (!ReadCommitObject(
          context.commonGitDirectory,
          objectId,
          &commit,
          error)) {
    return "";
  }
  std::map<std::string, TreeEntry> currentTree;
  if (!ReadHeadTree(
          context.commonGitDirectory,
          objectId,
          &currentTree,
          error)) {
    return "";
  }
  std::map<std::string, TreeEntry> parentTree;
  const std::string parent = ReadParent(commit.payload);
  if (!parent.empty() &&
      !ReadHeadTree(
          context.commonGitDirectory,
          parent,
          &parentTree,
          error)) {
    return "";
  }
  std::vector<DiffFile> files;
  if (!BuildTreeDiffFiles(
          context.commonGitDirectory,
          parentTree,
          currentTree,
          &files,
          error)) {
    return "";
  }
  if (!paths.empty()) {
    const fs::path basePath = CommandBasePath(startPath);
    files.erase(
        std::remove_if(
            files.begin(),
            files.end(),
            [&basePath, &context, &paths](const DiffFile& file) {
              return !PathMatchesReadSpec(
                  file.path,
                  basePath,
                  context.repositoryPath,
                  paths);
            }),
        files.end());
  }

  const std::string authorLine = ReadAuthorLine(commit.payload);
  const std::string message = CommitMessage(commit.payload);
  std::string subject = message;
  const size_t newline = subject.find('\n');
  if (newline != std::string::npos) {
    subject = subject.substr(0, newline);
  }
  std::ostringstream output;
  if (oneLine) {
    output << objectId.substr(0, 7) << ' ' << subject << '\n';
  } else {
    output << "commit " << objectId << '\n';
    output << "Author: " << CommitAuthorName(authorLine) << '\n';
    output << "Date:   "
           << FormatCommitTimestamp(CommitAuthorTimestamp(authorLine))
           << "\n\n";
    output << FormatCommitMessageBlock(message);
    output << '\n';
  }
  if (statOnly) {
    output << FormatDiffStat(files);
  } else {
    for (size_t index = 0; index < files.size(); ++index) {
      if (index > 0) {
        output << '\n';
      }
      output << FormatDiffFile(files[index]);
    }
  }
  return output.str();
}

std::vector<std::string> ReadTags(
    const std::string& startPath,
    const std::vector<std::string>& patterns,
    std::string* error) {
  std::vector<std::string> tags;
  if (error != nullptr) {
    error->clear();
  }
  RepositoryContext context;
  if (!LoadRepositoryContext(startPath, &context, error)) {
    return tags;
  }
  const std::string prefix = "refs/tags/";
  const std::map<std::string, std::string> references =
      ReadReferenceValuesWithPrefix(
          context.commonGitDirectory,
          prefix);
  tags.reserve(references.size());
  for (const auto& reference : references) {
    const std::string tag =
        reference.first.substr(prefix.size());
    if (patterns.empty() ||
        std::any_of(
            patterns.begin(),
            patterns.end(),
            [&tag](const std::string& pattern) {
              return GlobMatchPathspec(pattern, tag);
            })) {
      tags.push_back(tag);
    }
  }
  return tags;
}

std::vector<std::string> ReadFiles(
    const std::string& startPath,
    const ListFilesOptions& options,
    std::string* error) {
  std::vector<std::string> lines;
  if (error != nullptr) {
    error->clear();
  }
  if (options.ignored && !options.others) {
    if (error != nullptr) {
      *error = "git ls-files --ignored requires --others.";
    }
    return lines;
  }

  RepositoryContext context;
  if (!LoadRepositoryContext(startPath, &context, error)) {
    return lines;
  }
  std::vector<IndexEntry> indexEntries;
  uint32_t indexVersion = 0;
  if (!ReadIndexEntries(
          context,
          &indexEntries,
          &indexVersion,
          error)) {
    return lines;
  }

  const fs::path basePath = CommandBasePath(startPath);
  const bool hasSelector =
      options.cached ||
      options.modified ||
      options.deleted ||
      options.others ||
      options.ignored;
  const bool includeCached =
      options.cached || !hasSelector;
  std::set<std::string> trackedPaths;
  for (const IndexEntry& entry : indexEntries) {
    trackedPaths.insert(entry.path);
  }

  const auto appendIndexEntry =
      [&lines, &options, &basePath, &context](
          const IndexEntry& entry) {
        if (!PathMatchesReadSpec(
                entry.path,
                basePath,
                context.repositoryPath,
                options.paths)) {
          return;
        }
        const std::string displayPath =
            options.fullName
                ? entry.path
                : CommandRelativePath(
                    entry.path,
                    basePath,
                    context.repositoryPath);
        if (displayPath.empty()) {
          return;
        }
        if (options.stage) {
          lines.push_back(
              ModeString(entry.mode) + " " +
              ObjectIdToHex(entry.objectId) + " " +
              std::to_string(entry.stage) + "\t" +
              displayPath);
        } else {
          lines.push_back(displayPath);
        }
      };
  const auto appendPath =
      [&lines, &options, &basePath, &context](
          const std::string& path) {
        if (!PathMatchesReadSpec(
                path,
                basePath,
                context.repositoryPath,
                options.paths)) {
          return;
        }
        const std::string displayPath =
            options.fullName
                ? path
                : CommandRelativePath(
                    path,
                    basePath,
                    context.repositoryPath);
        if (!displayPath.empty()) {
          lines.push_back(displayPath);
        }
      };

  if (includeCached) {
    for (const IndexEntry& entry : indexEntries) {
      appendIndexEntry(entry);
    }
  }
  if (options.modified || options.deleted) {
    for (const IndexEntry& entry : indexEntries) {
      std::string state;
      if (!IsWorkingTreeModified(
              context.repositoryPath,
              entry,
              &state)) {
        continue;
      }
      if (options.modified) {
        appendIndexEntry(entry);
      }
      if (options.deleted && state == "D") {
        appendIndexEntry(entry);
      }
    }
  }
  if (options.others) {
    std::vector<FileStatus> untracked;
    if (options.ignored) {
      AppendIgnoredFiles(
          context.repositoryPath,
          context.commonGitDirectory,
          trackedPaths,
          &untracked);
    } else if (options.excludeStandard) {
      AppendUntrackedFiles(
          context.repositoryPath,
          context.commonGitDirectory,
          trackedPaths,
          &untracked);
    } else {
      AppendAllUntrackedFiles(
          context.repositoryPath,
          context.commonGitDirectory,
          trackedPaths,
          &untracked);
    }
    std::sort(
        untracked.begin(),
        untracked.end(),
        [](const FileStatus& left, const FileStatus& right) {
          return left.path < right.path;
        });
    for (const FileStatus& file : untracked) {
      appendPath(file.path);
    }
  }
  std::stable_sort(
      lines.begin(),
      lines.end(),
      [](const std::string& left, const std::string& right) {
        const size_t leftTab = left.find('\t');
        const size_t rightTab = right.find('\t');
        const std::string leftPath =
            leftTab == std::string::npos
                ? left
                : left.substr(leftTab + 1);
        const std::string rightPath =
            rightTab == std::string::npos
                ? right
                : right.substr(rightTab + 1);
        return leftPath < rightPath;
      });
  return lines;
}

std::vector<std::string> HashFiles(
    const std::string& startPath,
    const std::vector<std::string>& paths,
    const std::string& type,
    bool write,
    std::string* error) {
  std::vector<std::string> objectIds;
  if (error != nullptr) {
    error->clear();
  }
  if (type != "blob" &&
      type != "tree" &&
      type != "commit" &&
      type != "tag") {
    if (error != nullptr) {
      *error = "Unsupported Git object type: " + type;
    }
    return objectIds;
  }
  RepositoryContext context;
  if (!LoadRepositoryContext(startPath, &context, error)) {
    return objectIds;
  }
  const fs::path basePath = CommandBasePath(startPath);
  objectIds.reserve(paths.size());
  for (const std::string& input : paths) {
    fs::path path(NormalizeInputPath(input));
    if (path.is_relative()) {
      path = basePath / path;
    }
    path = path.lexically_normal();
    std::string payload;
    if (!ReadBinaryFile(path, &payload, error)) {
      if (error != nullptr && !error->empty()) {
        *error = input + ": " + *error;
      }
      objectIds.clear();
      return objectIds;
    }
    if (write) {
      std::string objectId;
      if (!WriteLooseObject(
              context.commonGitDirectory,
              type,
              payload,
              &objectId,
              error)) {
        objectIds.clear();
        return objectIds;
      }
      objectIds.push_back(objectId);
    } else {
      objectIds.push_back(HashObjectId(type, payload));
    }
  }
  return objectIds;
}

std::string HashInput(
    const std::string& startPath,
    const std::string& payload,
    const std::string& type,
    bool write,
    std::string* error) {
  if (error != nullptr) {
    error->clear();
  }
  if (type != "blob" &&
      type != "tree" &&
      type != "commit" &&
      type != "tag") {
    if (error != nullptr) {
      *error = "Unsupported Git object type: " + type;
    }
    return "";
  }
  if (!write) {
    return HashObjectId(type, payload);
  }
  RepositoryContext context;
  if (!LoadRepositoryContext(startPath, &context, error)) {
    return "";
  }
  std::string objectId;
  if (!WriteLooseObject(
          context.commonGitDirectory,
          type,
          payload,
          &objectId,
          error)) {
    return "";
  }
  return objectId;
}

std::vector<std::string> CheckIgnored(
    const std::string& startPath,
    const std::vector<std::string>& paths,
    bool noIndex,
    bool verbose,
    std::string* error) {
  std::vector<std::string> lines;
  if (error != nullptr) {
    error->clear();
  }
  RepositoryContext context;
  if (!LoadRepositoryContext(startPath, &context, error)) {
    return lines;
  }

  std::set<std::string> trackedPaths;
  if (!noIndex) {
    std::vector<IndexEntry> indexEntries;
    uint32_t indexVersion = 0;
    if (!ReadIndexEntries(
            context,
            &indexEntries,
            &indexVersion,
            error)) {
      return lines;
    }
    for (const IndexEntry& entry : indexEntries) {
      trackedPaths.insert(entry.path);
    }
  }

  const fs::path basePath = CommandBasePath(startPath);
  for (const std::string& input : paths) {
    fs::path requested(NormalizeInputPath(input));
    if (requested.is_relative()) {
      requested = basePath / requested;
    }
    requested = requested.lexically_normal();
    const std::string relative =
        RelativePathOrEmpty(context.repositoryPath, requested);
    if (relative.empty() || relative == ".") {
      if (error != nullptr) {
        *error = input + " is outside the repository work tree.";
      }
      lines.clear();
      return lines;
    }
    if (!noIndex &&
        trackedPaths.find(relative) != trackedPaths.end()) {
      continue;
    }

    std::vector<IgnoreRule> rules;
    LoadGlobalIgnoreRules(context.commonGitDirectory, &rules);
    ReadIgnoreRulesFile(
        context.commonGitDirectory / "info" / "exclude",
        "",
        &rules);
    ReadIgnoreRulesFile(
        context.repositoryPath / ".gitignore",
        "",
        &rules);

    IgnoreDecision inherited;
    IgnoreDecision decision;
    fs::path relativePath(relative);
    fs::path prefix;
    size_t componentIndex = 0;
    const size_t componentCount =
        static_cast<size_t>(
            std::distance(
                relativePath.begin(),
                relativePath.end()));
    for (const fs::path& component : relativePath) {
      prefix /= component;
      ++componentIndex;
      const bool final = componentIndex == componentCount;
      std::error_code typeError;
      const bool directory =
          !final ||
          fs::is_directory(requested, typeError) ||
          (!input.empty() && input.back() == '/');
      decision = EvaluateIgnoreRules(
          prefix.generic_string(),
          directory,
          rules,
          inherited);
      if (final) {
        break;
      }
      if (decision.ignored) {
        inherited = decision;
        continue;
      }
      inherited = {};
      ReadIgnoreRulesFile(
          context.repositoryPath / prefix / ".gitignore",
          prefix.generic_string(),
          &rules);
    }

    if (!decision.matched ||
        (!decision.ignored && !verbose)) {
      continue;
    }
    if (verbose) {
      lines.push_back(
          DisplayIgnoreSource(
              context.repositoryPath,
              decision.rule) +
          ":" + std::to_string(decision.rule.lineNumber) +
          ":" + decision.rule.displayPattern + "\t" + input);
    } else {
      lines.push_back(input);
    }
  }
  return lines;
}

std::string ReadObjectContent(
    const std::string& startPath,
    const std::string& objectName,
    const std::string& mode,
    std::string* error) {
  if (error != nullptr) {
    error->clear();
  }
  RepositoryContext context;
  if (!LoadRepositoryContext(startPath, &context, error)) {
    return "";
  }
  const std::string objectId =
      ResolveObjectName(context, objectName, error);
  if (objectId.empty()) {
    return "";
  }
  ObjectData object;
  if (!ReadObject(
          context.commonGitDirectory,
          objectId,
          &object,
          error)) {
    return "";
  }

  if (mode == "exists") {
    return "";
  }
  if (mode == "type") {
    return object.type;
  }
  if (mode == "size") {
    return std::to_string(object.payload.size());
  }
  if (mode == "pretty") {
    if (object.type != "tree") {
      return object.payload;
    }
    std::vector<TreeEntry> entries;
    if (!ReadTreeEntries(
            context.commonGitDirectory,
            objectId,
            &entries,
            error)) {
      return "";
    }
    std::ostringstream output;
    for (size_t index = 0; index < entries.size(); ++index) {
      if (index > 0) {
        output << '\n';
      }
      const TreeEntry& entry = entries[index];
      output
          << NormalizedTreeMode(entry.mode) << ' '
          << TreeEntryType(entry.mode) << ' '
          << ObjectIdToHex(entry.objectId) << '\t'
          << entry.path;
    }
    return output.str();
  }
  if (mode == "blob" ||
      mode == "tree" ||
      mode == "commit" ||
      mode == "tag") {
    if (object.type != mode) {
      if (error != nullptr) {
        *error =
            "Git object " + objectId +
            " is a " + object.type +
            ", not a " + mode + ".";
      }
      return "";
    }
    return object.payload;
  }
  if (error != nullptr) {
    *error = "Unsupported cat-file mode: " + mode;
  }
  return "";
}

std::vector<std::string> ReadTree(
    const std::string& startPath,
    const std::string& treeish,
    const ListTreeOptions& options,
    std::string* error) {
  std::vector<std::string> lines;
  if (error != nullptr) {
    error->clear();
  }
  if (options.nameOnly && options.objectOnly) {
    if (error != nullptr) {
      *error =
          "git ls-tree cannot combine --name-only and --object-only.";
    }
    return lines;
  }
  RepositoryContext context;
  if (!LoadRepositoryContext(startPath, &context, error)) {
    return lines;
  }

  std::string rootTree =
      ResolveObjectName(context, treeish, error);
  if (rootTree.empty() ||
      !ResolveTreeObject(
          context.commonGitDirectory,
          &rootTree,
          error)) {
    return lines;
  }

  const fs::path commandBasePath = CommandBasePath(startPath);
  const std::string commandBaseRelative =
      RelativePathOrEmpty(
          context.repositoryPath,
          commandBasePath);
  std::string listingTree = rootTree;
  std::string listingPrefix;
  fs::path matchBasePath = commandBasePath;
  if (options.fullTree) {
    matchBasePath = context.repositoryPath;
  } else if (!commandBaseRelative.empty() &&
             commandBaseRelative != ".") {
    listingTree = ResolveTreePath(
        context.commonGitDirectory,
        rootTree,
        commandBaseRelative,
        error);
    if (listingTree.empty()) {
      return lines;
    }
    ObjectData baseObject;
    if (!ReadObject(
            context.commonGitDirectory,
            listingTree,
            &baseObject,
            error)) {
      return lines;
    }
    if (baseObject.type != "tree") {
      if (error != nullptr) {
        *error =
            "Current directory does not exist as a tree in " +
            treeish + ".";
      }
      return lines;
    }
    listingPrefix = commandBaseRelative;
  }

  const bool fullName =
      options.fullName || options.fullTree;
  const auto appendEntry =
      [&lines,
       &options,
       &context,
       &commandBasePath,
       fullName,
       error](
          const TreeEntry& entry,
          const std::string& fullPath) -> bool {
        const std::string objectId =
            ObjectIdToHex(entry.objectId);
        const std::string displayPath =
            fullName
                ? fullPath
                : CommandRelativePath(
                    fullPath,
                    commandBasePath,
                    context.repositoryPath);
        if (displayPath.empty()) {
          return true;
        }
        if (options.objectOnly) {
          lines.push_back(objectId);
          return true;
        }
        if (options.nameOnly) {
          lines.push_back(displayPath);
          return true;
        }

        std::ostringstream output;
        const std::string type =
            TreeEntryType(entry.mode);
        output
            << NormalizedTreeMode(entry.mode) << ' '
            << type << ' ' << objectId;
        if (options.longFormat) {
          std::string size = "-";
          if (type == "blob") {
            ObjectData object;
            if (!ReadObject(
                    context.commonGitDirectory,
                    objectId,
                    &object,
                    error)) {
              return false;
            }
            size = std::to_string(object.payload.size());
          }
          output << ' ' << std::setw(7) << size;
        }
        output << '\t' << displayPath;
        lines.push_back(output.str());
        return true;
      };

  const auto visit =
      [&options,
       &context,
       &matchBasePath,
       &appendEntry,
       error](
          const auto& self,
          const std::string& treeObjectId,
          const std::string& prefix) -> bool {
        std::vector<TreeEntry> entries;
        if (!ReadTreeEntries(
                context.commonGitDirectory,
                treeObjectId,
                &entries,
                error)) {
          return false;
        }
        for (const TreeEntry& entry : entries) {
          const std::string fullPath =
              prefix.empty()
                  ? entry.path
                  : prefix + "/" + entry.path;
          const bool tree =
              entry.mode == "40000" ||
              entry.mode == "040000";
          const bool selected = options.paths.empty()
              ? true
              : options.recursive
                  ? PathMatchesReadSpec(
                      fullPath,
                      matchBasePath,
                      context.repositoryPath,
                      options.paths)
                  : PathExplicitlyMatchesReadSpec(
                      fullPath,
                      matchBasePath,
                      context.repositoryPath,
                      options.paths);
          bool output = selected;
          if (options.directoriesOnly) {
            output = output && tree;
          } else if (options.recursive && tree &&
                     !options.includeTrees) {
            output = false;
          }
          if (output &&
              !appendEntry(entry, fullPath)) {
            return false;
          }
          if (tree &&
              (options.recursive ||
               (!options.paths.empty() && !selected)) &&
              !self(
                  self,
                  ObjectIdToHex(entry.objectId),
                  fullPath)) {
            return false;
          }
        }
        return true;
      };
  if (!visit(visit, listingTree, listingPrefix)) {
    lines.clear();
  }
  return lines;
}

std::vector<std::string> ReadReferences(
    const std::string& startPath,
    const ShowRefOptions& options,
    std::string* error) {
  std::vector<std::string> lines;
  if (error != nullptr) {
    error->clear();
  }
  RepositoryContext context;
  if (!LoadRepositoryContext(startPath, &context, error)) {
    return lines;
  }
  if (options.verify && options.patterns.empty()) {
    if (error != nullptr) {
      *error = "git show-ref --verify requires a reference.";
    }
    return lines;
  }

  const size_t abbreviation = std::max<size_t>(
      1,
      std::min<size_t>(40, options.abbreviation));
  const auto appendReference =
      [&context, &options, abbreviation, error, &lines](
          const std::string& refName) {
        std::string objectId;
        if (!ResolveReferenceObjectId(
                context,
                refName,
                &objectId,
                error)) {
          return false;
        }
        if (objectId.empty()) {
          return true;
        }
        if (!options.quiet) {
          const std::string abbreviated =
              objectId.substr(0, abbreviation);
          lines.push_back(
              options.hashOnly
                  ? abbreviated
                  : abbreviated + " " + refName);
        }
        if (!options.dereference ||
            refName.rfind("refs/tags/", 0) != 0) {
          return true;
        }
        std::string peeled = objectId;
        ObjectData object;
        if (!PeelAnnotatedTags(
                context.commonGitDirectory,
                &peeled,
                &object,
                error)) {
          return false;
        }
        if (peeled != objectId && !options.quiet) {
          const std::string abbreviated =
              peeled.substr(0, abbreviation);
          lines.push_back(
              options.hashOnly
                  ? abbreviated
                  : abbreviated + " " + refName + "^{}");
        }
        return true;
      };

  if (options.verify) {
    for (const std::string& pattern : options.patterns) {
      if (!ValidReferenceName(pattern)) {
        if (!options.quiet && error != nullptr) {
          *error = "'" + pattern + "' - not a valid ref.";
        }
        lines.clear();
        return lines;
      }
      std::string objectId;
      if (!ResolveReferenceObjectId(
              context,
              pattern,
              &objectId,
              error)) {
        lines.clear();
        return lines;
      }
      if (objectId.empty()) {
        if (!options.quiet && error != nullptr) {
          *error = "'" + pattern + "' - not a valid ref.";
        }
        lines.clear();
        return lines;
      }
      if (!appendReference(pattern)) {
        lines.clear();
        return lines;
      }
    }
    return lines;
  }

  if (options.includeHead) {
    if (!appendReference("HEAD")) {
      lines.clear();
      return lines;
    }
  }
  const std::map<std::string, std::string> references =
      ReadReferenceValuesWithPrefix(
          context.commonGitDirectory,
          "refs/");
  for (const auto& reference : references) {
    const std::string& refName = reference.first;
    const bool selectedNamespace =
        (!options.heads && !options.tags) ||
        (options.heads &&
         refName.rfind("refs/heads/", 0) == 0) ||
        (options.tags &&
         refName.rfind("refs/tags/", 0) == 0);
    if (!selectedNamespace) {
      continue;
    }
    const bool selectedPattern =
        options.patterns.empty() ||
        std::any_of(
            options.patterns.begin(),
            options.patterns.end(),
            [&refName](const std::string& pattern) {
              return ReferencePatternMatches(refName, pattern);
            });
    if (selectedPattern && !appendReference(refName)) {
      lines.clear();
      return lines;
    }
  }
  return lines;
}

std::vector<std::string> ExcludeExistingReferences(
    const std::string& startPath,
    const std::string& input,
    const std::string& pattern,
    std::string* error) {
  std::vector<std::string> lines;
  if (error != nullptr) {
    error->clear();
  }
  RepositoryContext context;
  if (!LoadRepositoryContext(startPath, &context, error)) {
    return lines;
  }
  const std::map<std::string, std::string> references =
      ReadReferenceValuesWithPrefix(
          context.commonGitDirectory,
          "refs/");
  std::istringstream inputLines(input);
  std::string line;
  while (std::getline(inputLines, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.size() >= 3 &&
        line.compare(line.size() - 3, 3, "^{}") == 0) {
      line.resize(line.size() - 3);
    }
    size_t refStart = line.size();
    while (refStart > 0 &&
           std::isspace(
               static_cast<unsigned char>(line[refStart - 1])) == 0) {
      --refStart;
    }
    const std::string refName = line.substr(refStart);
    if (!pattern.empty() &&
        refName.rfind(pattern, 0) != 0) {
      continue;
    }
    if (refName.rfind("refs/", 0) != 0 ||
        !ValidReferenceName(refName)) {
      lines.push_back(
          "warning: ref '" + refName + "' ignored");
      continue;
    }
    if (references.find(refName) == references.end()) {
      lines.push_back(line);
    }
  }
  return lines;
}

std::vector<std::string> ReadRevisionList(
    const std::string& startPath,
    const RevListOptions& options,
    std::string* error) {
  std::vector<std::string> lines;
  if (error != nullptr) {
    error->clear();
  }
  if (options.noMerges && options.merges) {
    if (error != nullptr) {
      *error = "git rev-list cannot combine --merges and --no-merges.";
    }
    return lines;
  }
  RepositoryContext context;
  if (!LoadRepositoryContext(startPath, &context, error)) {
    return lines;
  }

  std::vector<std::string> positiveTips;
  std::vector<std::string> negativeTips;
  const auto resolveCommit =
      [&context, error](const std::string& revision) {
        return ResolveRevision(context, revision, error);
      };
  const auto appendReferenceTips =
      [&context, &positiveTips, error](const std::string& prefix) {
        const std::map<std::string, std::string> references =
            ReadReferenceValuesWithPrefix(
                context.commonGitDirectory,
                prefix);
        for (const auto& reference : references) {
          std::string revisionError;
          const std::string objectId =
              ResolveRevision(
                  context,
                  reference.first,
                  &revisionError);
          if (objectId.empty()) {
            if (!revisionError.empty() && error != nullptr) {
              *error = revisionError;
              return false;
            }
            continue;
          }
          positiveTips.push_back(objectId);
        }
        return true;
      };

  if (options.all) {
    if (!context.headObjectId.empty()) {
      positiveTips.push_back(context.headObjectId);
    }
    if (!appendReferenceTips("refs/")) {
      return lines;
    }
  } else {
    if (options.branches && !appendReferenceTips("refs/heads/")) {
      return lines;
    }
    if (options.tags && !appendReferenceTips("refs/tags/")) {
      return lines;
    }
    if (options.remotes && !appendReferenceTips("refs/remotes/")) {
      return lines;
    }
  }

  for (const std::string& rawRevision : options.revisions) {
    const size_t symmetric = rawRevision.find("...");
    if (symmetric != std::string::npos) {
      const std::string leftName =
          symmetric == 0
              ? "HEAD"
              : rawRevision.substr(0, symmetric);
      const std::string rightName =
          symmetric + 3 == rawRevision.size()
              ? "HEAD"
              : rawRevision.substr(symmetric + 3);
      const std::string left = resolveCommit(leftName);
      if (left.empty()) {
        return {};
      }
      const std::string right = resolveCommit(rightName);
      if (right.empty()) {
        return {};
      }
      positiveTips.push_back(left);
      positiveTips.push_back(right);
      const std::vector<std::string> bases =
          MergeBaseCandidates(
              context.commonGitDirectory,
              {left, right},
              false,
              error);
      if (error != nullptr && !error->empty()) {
        return {};
      }
      negativeTips.insert(
          negativeTips.end(),
          bases.begin(),
          bases.end());
      continue;
    }
    const size_t range = rawRevision.find("..");
    if (range != std::string::npos) {
      const std::string leftName =
          range == 0 ? "HEAD" : rawRevision.substr(0, range);
      const std::string rightName =
          range + 2 == rawRevision.size()
              ? "HEAD"
              : rawRevision.substr(range + 2);
      const std::string left = resolveCommit(leftName);
      if (left.empty()) {
        return {};
      }
      const std::string right = resolveCommit(rightName);
      if (right.empty()) {
        return {};
      }
      negativeTips.push_back(left);
      positiveTips.push_back(right);
      continue;
    }
    const bool negative =
        rawRevision.size() > 1 && rawRevision.front() == '^';
    const std::string objectId =
        resolveCommit(
            negative ? rawRevision.substr(1) : rawRevision);
    if (objectId.empty()) {
      return {};
    }
    (negative ? negativeTips : positiveTips).push_back(objectId);
  }

  if (positiveTips.empty()) {
    if (error != nullptr) {
      *error = "git rev-list requires a commit or a reference selector.";
    }
    return lines;
  }

  std::map<std::string, CommitGraphNode> cache;
  std::set<std::string> excluded;
  if (!CollectCommitAncestors(
          context.commonGitDirectory,
          negativeTips,
          options.firstParent,
          &excluded,
          &cache,
          error)) {
    return {};
  }

  std::vector<std::string> pending = positiveTips;
  std::set<std::string> visited;
  while (!pending.empty() &&
         lines.size() < static_cast<size_t>(options.maxCount)) {
    size_t nextIndex = 0;
    CommitGraphNode nextNode;
    bool haveNode = false;
    for (size_t index = 0; index < pending.size(); ++index) {
      if (visited.find(pending[index]) != visited.end() ||
          excluded.find(pending[index]) != excluded.end()) {
        continue;
      }
      auto found = cache.find(pending[index]);
      if (found == cache.end()) {
        CommitGraphNode node;
        if (!ReadCommitGraphNode(
                context.commonGitDirectory,
                pending[index],
                &node,
                error)) {
          return {};
        }
        found = cache.emplace(pending[index], std::move(node)).first;
      }
      if (!haveNode ||
          found->second.timestamp > nextNode.timestamp ||
          (found->second.timestamp == nextNode.timestamp &&
           found->second.objectId > nextNode.objectId)) {
        nextIndex = index;
        nextNode = found->second;
        haveNode = true;
      }
    }
    if (!haveNode) {
      break;
    }
    pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(nextIndex));
    if (!visited.insert(nextNode.objectId).second) {
      continue;
    }
    const size_t parentCount =
        options.firstParent
            ? std::min<size_t>(1, nextNode.parents.size())
            : nextNode.parents.size();
    std::vector<std::string> followedParents(
        nextNode.parents.begin(),
        nextNode.parents.begin() +
            static_cast<std::ptrdiff_t>(parentCount));
    bool includeCommit = true;
    if (!options.paths.empty()) {
      PathHistoryDecision decision;
      if (!AnalyzeCommitPathHistory(
              context,
              startPath,
              nextNode.objectId,
              followedParents,
              options.paths,
              &decision,
              error)) {
        return {};
      }
      includeCommit = decision.includeCommit;
      followedParents = std::move(decision.followedParents);
    }
    for (const std::string& parent : followedParents) {
      pending.push_back(parent);
    }
    const bool merge = nextNode.parents.size() > 1;
    if ((options.noMerges && merge) ||
        (options.merges && !merge) ||
        !includeCommit) {
      continue;
    }
    const auto displayId =
        [&options](const std::string& objectId) {
          return options.abbreviate
              ? objectId.substr(
                    0,
                    std::min<size_t>(
                        options.abbreviation,
                        objectId.size()))
              : objectId;
        };
    std::string line = displayId(nextNode.objectId);
    if (options.parents) {
      for (const std::string& parent : followedParents) {
        line += " " + parent;
      }
    }
    lines.push_back(line);
  }
  if (options.count) {
    return {std::to_string(lines.size())};
  }
  if (options.reverse) {
    std::reverse(lines.begin(), lines.end());
  }
  return lines;
}

std::vector<std::string> ReadMergeBases(
    const std::string& startPath,
    const MergeBaseOptions& options,
    std::string* error) {
  std::vector<std::string> lines;
  if (error != nullptr) {
    error->clear();
  }
  RepositoryContext context;
  if (!LoadRepositoryContext(startPath, &context, error)) {
    return lines;
  }
  std::vector<std::string> commits;
  for (const std::string& revision : options.revisions) {
    const std::string objectId =
        ResolveRevision(context, revision, error);
    if (objectId.empty()) {
      return {};
    }
    commits.push_back(objectId);
  }
  if (options.independent) {
    if (commits.empty()) {
      if (error != nullptr) {
        *error = "git merge-base --independent requires a commit.";
      }
      return {};
    }
    for (size_t index = 0; index < commits.size(); ++index) {
      bool reachable = false;
      for (size_t other = 0; other < commits.size(); ++other) {
        if (index == other || commits[index] == commits[other]) {
          continue;
        }
        std::string ancestorError;
        if (IsAncestorCommit(
                context.commonGitDirectory,
                commits[index],
                commits[other],
                &ancestorError)) {
          reachable = true;
          break;
        }
        if (!ancestorError.empty()) {
          if (error != nullptr) {
            *error = ancestorError;
          }
          return {};
        }
      }
      if (!reachable &&
          std::find(lines.begin(), lines.end(), commits[index]) ==
              lines.end()) {
        lines.push_back(commits[index]);
      }
    }
    return lines;
  }

  lines = MergeBaseCandidates(
      context.commonGitDirectory,
      commits,
      options.octopus,
      error);
  if (error != nullptr && !error->empty()) {
    return {};
  }
  if (!options.all && lines.size() > 1) {
    lines.resize(1);
  }
  return lines;
}

bool IsAncestorRevision(
    const std::string& startPath,
    const std::string& ancestor,
    const std::string& descendant,
    std::string* error) {
  if (error != nullptr) {
    error->clear();
  }
  RepositoryContext context;
  if (!LoadRepositoryContext(startPath, &context, error)) {
    return false;
  }
  const std::string ancestorId =
      ResolveRevision(context, ancestor, error);
  if (ancestorId.empty()) {
    return false;
  }
  const std::string descendantId =
      ResolveRevision(context, descendant, error);
  if (descendantId.empty()) {
    return false;
  }
  return IsAncestorCommit(
      context.commonGitDirectory,
      ancestorId,
      descendantId,
      error);
}

bool ResolveForkPointReference(
    const RepositoryContext& context,
    const std::string& reference,
    std::string* fullName,
    std::string* objectId,
    std::string* error) {
  fullName->clear();
  objectId->clear();
  std::vector<std::string> candidates;
  if (reference == "HEAD" ||
      reference.rfind("refs/", 0) == 0) {
    candidates.push_back(reference);
  } else {
    candidates = {
        "refs/" + reference,
        "refs/tags/" + reference,
        "refs/heads/" + reference,
        "refs/remotes/" + reference,
        "refs/remotes/" + reference + "/HEAD"};
  }

  std::vector<std::pair<std::string, std::string>> matches;
  for (const std::string& candidate : candidates) {
    std::string resolved;
    if (!ResolveReferenceObjectId(
            context,
            candidate,
            &resolved,
            error)) {
      return false;
    }
    if (!resolved.empty()) {
      matches.push_back({candidate, resolved});
    }
  }
  if (matches.empty()) {
    if (error != nullptr) {
      *error = "No such ref: '" + reference + "'";
    }
    return false;
  }
  if (matches.size() > 1) {
    if (error != nullptr) {
      *error = "Ambiguous refname: '" + reference + "'";
    }
    return false;
  }
  *fullName = matches.front().first;
  *objectId = matches.front().second;
  return true;
}

std::string FindForkPointRevision(
    const std::string& startPath,
    const std::string& reference,
    const std::string& derived,
    std::string* error) {
  if (error != nullptr) {
    error->clear();
  }
  RepositoryContext context;
  if (!LoadRepositoryContext(startPath, &context, error)) {
    return "";
  }

  std::string fullReference;
  std::string referenceId;
  if (!ResolveForkPointReference(
          context,
          reference,
          &fullReference,
          &referenceId,
          error)) {
    return "";
  }
  const std::string derivedId = ResolveRevision(
      context,
      derived.empty() ? "HEAD" : derived,
      error);
  if (derivedId.empty()) {
    return "";
  }

  const std::vector<ReflogEntry> entries = ReadReflog(
      startPath,
      fullReference,
      std::numeric_limits<uint32_t>::max(),
      error);
  if (error != nullptr && !error->empty()) {
    return "";
  }

  std::vector<std::string> reflogTips;
  std::set<std::string> seen;
  const std::string zeroObjectId(40, '0');
  const auto appendCommit =
      [&context, &reflogTips, &seen, &zeroObjectId](
          const std::string& rawObjectId) {
        const std::string objectId = LowercaseAscii(rawObjectId);
        std::array<uint8_t, 20> parsed {};
        if (objectId == zeroObjectId ||
            !HexToObjectId(objectId, &parsed) ||
            !seen.insert(objectId).second) {
          return;
        }
        ObjectData commit;
        std::string ignoredError;
        if (ReadCommitObject(
                context.commonGitDirectory,
                objectId,
                &commit,
                &ignoredError)) {
          reflogTips.push_back(objectId);
        }
      };

  if (!entries.empty()) {
    appendCommit(entries.back().oldId);
    for (auto iterator = entries.rbegin();
         iterator != entries.rend();
         ++iterator) {
      appendCommit(iterator->newId);
    }
  }
  if (reflogTips.empty()) {
    appendCommit(referenceId);
  }
  if (reflogTips.empty()) {
    return "";
  }

  std::vector<std::string> commits = {derivedId};
  commits.insert(
      commits.end(),
      reflogTips.begin(),
      reflogTips.end());
  const std::vector<std::string> bases = MergeBaseCandidates(
      context.commonGitDirectory,
      commits,
      false,
      error);
  if ((error != nullptr && !error->empty()) ||
      bases.size() != 1 ||
      seen.find(bases.front()) == seen.end()) {
    return "";
  }
  return bases.front();
}

std::vector<std::string> FormatReferences(
    const std::string& startPath,
    const ForEachRefOptions& options,
    std::string* error) {
  std::vector<std::string> lines;
  if (error != nullptr) {
    error->clear();
  }
  RepositoryContext context;
  if (!LoadRepositoryContext(startPath, &context, error)) {
    return lines;
  }
  std::map<std::string, std::string> references =
      ReadReferenceValuesWithPrefix(
          context.commonGitDirectory,
          "refs/");
  if (options.includeRootRefs && !context.headObjectId.empty()) {
    references["HEAD"] = Trim(ReadTextFile(context.gitDirectory / "HEAD"));
  }

  std::string pointsAtId;
  if (!options.pointsAt.empty() &&
      !ResolveObjectExpression(
          context,
          options.pointsAt,
          &pointsAtId,
          error)) {
    return {};
  }
  std::string mergedId;
  std::string noMergedId;
  std::string containsId;
  std::string noContainsId;
  const auto resolveFilter =
      [&context, error](const std::string& value, std::string* objectId) {
        if (value.empty()) {
          return true;
        }
        *objectId = ResolveRevision(context, value, error);
        return !objectId->empty();
      };
  if (!resolveFilter(options.merged, &mergedId) ||
      !resolveFilter(options.noMerged, &noMergedId) ||
      !resolveFilter(options.contains, &containsId) ||
      !resolveFilter(options.noContains, &noContainsId)) {
    return {};
  }

  std::string headTarget;
  std::string headError;
  ResolveReferenceTargetName(
      context,
      "HEAD",
      true,
      false,
      &headTarget,
      &headError);
  std::vector<ReferenceFormatData> selected;
  for (const auto& reference : references) {
    const std::string& refName = reference.first;
    const bool included =
        options.patterns.empty() ||
        std::any_of(
            options.patterns.begin(),
            options.patterns.end(),
            [&refName, &options](const std::string& pattern) {
              return ReferenceFilterMatches(
                  refName,
                  pattern,
                  options.ignoreCase);
            });
    const bool excluded =
        std::any_of(
            options.excludes.begin(),
            options.excludes.end(),
            [&refName, &options](const std::string& pattern) {
              return ReferenceFilterMatches(
                  refName,
                  pattern,
                  options.ignoreCase);
            });
    if (!included || excluded) {
      continue;
    }

    ReferenceFormatData data;
    data.refName = refName;
    if (!ResolveReferenceObjectId(
            context,
            refName,
            &data.objectId,
            error)) {
      return {};
    }
    if (data.objectId.empty() ||
        (!pointsAtId.empty() && data.objectId != pointsAtId)) {
      continue;
    }
    std::string rawValue;
    if (ReadReferenceValue(context, refName, &rawValue) &&
        rawValue.rfind("ref:", 0) == 0) {
      data.symref = Trim(rawValue.substr(4));
    }
    data.head = refName == headTarget;

    ObjectData object;
    if (!ReadObject(
            context.commonGitDirectory,
            data.objectId,
            &object,
            error)) {
      return {};
    }
    data.objectType = object.type;
    if (object.type == "commit") {
      data.author = CommitHeaderValue(object.payload, "author");
      data.committer = CommitHeaderValue(object.payload, "committer");
      data.subject = CommitMessage(object.payload);
    } else if (object.type == "tag") {
      data.subject = CommitMessage(object.payload);
    }
    const size_t subjectEnd = data.subject.find('\n');
    if (subjectEnd != std::string::npos) {
      data.subject = data.subject.substr(0, subjectEnd);
    }

    if (!mergedId.empty() || !noMergedId.empty() ||
        !containsId.empty() || !noContainsId.empty()) {
      std::string commitId = data.objectId;
      std::string peelError;
      const bool commit =
          PeelToCommit(
              context.commonGitDirectory,
              &commitId,
              &peelError);
      if (!commit) {
        continue;
      }
      if (!mergedId.empty()) {
        std::string ancestorError;
        if (!IsAncestorCommit(
                context.commonGitDirectory,
                commitId,
                mergedId,
                &ancestorError)) {
          if (!ancestorError.empty()) {
            if (error != nullptr) {
              *error = ancestorError;
            }
            return {};
          }
          continue;
        }
      }
      if (!noMergedId.empty()) {
        std::string ancestorError;
        if (IsAncestorCommit(
                context.commonGitDirectory,
                commitId,
                noMergedId,
                &ancestorError)) {
          continue;
        }
        if (!ancestorError.empty()) {
          if (error != nullptr) {
            *error = ancestorError;
          }
          return {};
        }
      }
      if (!containsId.empty()) {
        std::string ancestorError;
        if (!IsAncestorCommit(
                context.commonGitDirectory,
                containsId,
                commitId,
                &ancestorError)) {
          if (!ancestorError.empty()) {
            if (error != nullptr) {
              *error = ancestorError;
            }
            return {};
          }
          continue;
        }
      }
      if (!noContainsId.empty()) {
        std::string ancestorError;
        if (IsAncestorCommit(
                context.commonGitDirectory,
                noContainsId,
                commitId,
                &ancestorError)) {
          continue;
        }
        if (!ancestorError.empty()) {
          if (error != nullptr) {
            *error = ancestorError;
          }
          return {};
        }
      }
    }
    selected.push_back(std::move(data));
  }

  std::vector<std::string> sortKeys = options.sortKeys;
  if (sortKeys.empty()) {
    sortKeys.push_back("refname");
  }
  std::stable_sort(
      selected.begin(),
      selected.end(),
      [&sortKeys, &options, error](
          const ReferenceFormatData& left,
          const ReferenceFormatData& right) {
        for (auto iterator = sortKeys.rbegin();
             iterator != sortKeys.rend();
             ++iterator) {
          const bool descending =
              !iterator->empty() && iterator->front() == '-';
          const std::string atom =
              descending ? iterator->substr(1) : *iterator;
          std::string leftError;
          std::string rightError;
          std::string leftValue =
              ReferenceAtomValue(left, atom, &leftError);
          std::string rightValue =
              ReferenceAtomValue(right, atom, &rightError);
          if (!leftError.empty() || !rightError.empty()) {
            if (error != nullptr && error->empty()) {
              *error = !leftError.empty() ? leftError : rightError;
            }
            return false;
          }
          if (options.ignoreCase) {
            leftValue = LowercaseAscii(leftValue);
            rightValue = LowercaseAscii(rightValue);
          }
          if (leftValue == rightValue) {
            continue;
          }
          return descending
              ? leftValue > rightValue
              : leftValue < rightValue;
        }
        return left.refName < right.refName;
      });
  if (error != nullptr && !error->empty()) {
    return {};
  }

  const std::string format =
      options.format.empty()
          ? "%(objectname) %(objecttype)\t%(refname)"
          : options.format;
  const size_t count =
      std::min<size_t>(selected.size(), options.count);
  for (size_t index = 0; index < count; ++index) {
    lines.push_back(
        ExpandReferenceFormat(
            selected[index],
            format,
            error));
    if (error != nullptr && !error->empty()) {
      return {};
    }
  }
  return lines;
}

CleanResult CleanRepository(
    const std::string& startPath,
    const CleanOptions& options) {
  CleanResult result;
  RepositoryContext context;
  std::string error;
  if (!LoadRepositoryContext(startPath, &context, &error)) {
    result.error = error;
    return result;
  }

  const std::string requireForce = LowercaseAscii(
      Trim(ReadConfigValue(
          context.commonGitDirectory,
          "clean",
          "requireForce")));
  const bool requireForceDisabled =
      requireForce == "false" ||
      requireForce == "no" ||
      requireForce == "off" ||
      requireForce == "0";
  if (!options.dryRun &&
      options.force == 0 &&
      !requireForceDisabled) {
    result.error =
        "clean.requireForce is enabled; use -f or set "
        "clean.requireForce=false.";
    return result;
  }

  std::vector<IndexEntry> indexEntries;
  uint32_t indexVersion = 0;
  if (!ReadIndexEntries(
          context,
          &indexEntries,
          &indexVersion,
          &error)) {
    result.error = error;
    return result;
  }

  std::set<std::string> trackedPaths;
  std::set<std::string> trackedDirectories;
  for (const IndexEntry& entry : indexEntries) {
    if (entry.stage != 0) {
      continue;
    }
    trackedPaths.insert(entry.path);
    fs::path path(entry.path);
    fs::path parent = path.parent_path();
    while (!parent.empty() && parent != fs::path(".")) {
      trackedDirectories.insert(parent.generic_string());
      parent = parent.parent_path();
    }
  }

  const fs::path basePath = CommandBasePath(startPath);
  std::vector<IgnoreRule> initialRules;
  if (!options.removeIgnored) {
    LoadGlobalIgnoreRules(context.commonGitDirectory, &initialRules);
    ReadIgnoreRulesFile(
        context.commonGitDirectory / "info" / "exclude",
        "",
        &initialRules);
  }
  std::vector<IgnoreRule> commandRules;
  const std::string baseRelative =
      RelativePathOrEmpty(context.repositoryPath, basePath);
  AppendCommandExcludeRules(
      options.excludes,
      baseRelative == "." ? "" : baseRelative,
      &commandRules);

  const CleanScanResult scan = ScanCleanDirectory(
      context.repositoryPath,
      "",
      initialRules,
      commandRules,
      trackedPaths,
      trackedDirectories,
      basePath,
      context.repositoryPath,
      options,
      IgnoreDecision {},
      &result.skippedRepositories);

  std::vector<std::string> displayedSkippedRepositories;
  displayedSkippedRepositories.reserve(
      result.skippedRepositories.size());
  for (const std::string& relative : result.skippedRepositories) {
    std::string display =
        CommandRelativePath(
            relative,
            basePath,
            context.repositoryPath);
    if (!display.empty()) {
      display += "/";
      displayedSkippedRepositories.push_back(display);
    }
  }
  result.skippedRepositories =
      std::move(displayedSkippedRepositories);
  std::sort(
      result.skippedRepositories.begin(),
      result.skippedRepositories.end());
  result.skippedRepositories.erase(
      std::unique(
          result.skippedRepositories.begin(),
          result.skippedRepositories.end()),
      result.skippedRepositories.end());

  std::vector<std::string> candidates = scan.removable;
  std::sort(candidates.begin(), candidates.end());
  candidates.erase(
      std::unique(candidates.begin(), candidates.end()),
      candidates.end());
  result.changedCount = static_cast<uint32_t>(candidates.size());
  for (const std::string& relative : candidates) {
    const fs::path path =
        (context.repositoryPath / fs::path(relative)).lexically_normal();
    std::string display =
        CommandRelativePath(
            relative,
            basePath,
            context.repositoryPath);
    if (display.empty()) {
      continue;
    }
    std::error_code typeError;
    const fs::file_status status = fs::symlink_status(path, typeError);
    if (!typeError && fs::is_directory(status)) {
      display += "/";
    }
    result.cleanedPaths.push_back(display);
    if (options.dryRun) {
      continue;
    }
    std::error_code removeError;
    if (fs::is_directory(status)) {
      fs::remove_all(path, removeError);
    } else {
      fs::remove(path, removeError);
    }
    if (removeError) {
      result.success = false;
      result.error =
          "Cannot remove " + path.string() + ": " +
          removeError.message();
      return result;
    }
  }

  result.success = true;
  return result;
}

std::string ReadSymbolicReference(
    const std::string& startPath,
    const std::string& name,
    bool shortName,
    bool recurse,
    std::string* error) {
  if (error != nullptr) {
    error->clear();
  }
  if (!ValidReferenceName(name)) {
    if (error != nullptr) {
      *error = "ref " + name + " is not a valid ref.";
    }
    return "";
  }
  RepositoryContext context;
  if (!LoadRepositoryContext(startPath, &context, error)) {
    return "";
  }
  std::string target;
  if (!ResolveReferenceTargetName(
          context,
          name,
          recurse,
          true,
          &target,
          error)) {
    return "";
  }
  if (!shortName) {
    return target;
  }
  static const std::vector<std::string> prefixes = {
      "refs/heads/", "refs/tags/", "refs/remotes/"};
  for (const std::string& prefix : prefixes) {
    if (target.rfind(prefix, 0) == 0) {
      return target.substr(prefix.size());
    }
  }
  return target.rfind("refs/", 0) == 0
      ? target.substr(5)
      : target;
}

RepositoryOperation UpdateSymbolicReference(
    const std::string& startPath,
    const std::string& name,
    const std::string& target,
    bool deleteReference,
    const std::string& message) {
  if (!ValidReferenceName(name)) {
    return FailedOperation("ref " + name + " is not a valid ref.");
  }
  if (deleteReference && name == "HEAD") {
    return FailedOperation("Deleting 'HEAD' is not allowed.");
  }
  if (!deleteReference &&
      (!ValidReferenceName(target) ||
       target == "HEAD" ||
       target.rfind("refs/", 0) != 0)) {
    return FailedOperation(
        "Refusing to point " + name +
        " outside of refs/: " + target);
  }
  RepositoryContext context;
  std::string error;
  if (!LoadRepositoryContext(startPath, &context, &error)) {
    return FailedOperation(error);
  }

  std::string oldObjectId;
  if (!ResolveReferenceObjectId(
          context,
          name,
          &oldObjectId,
          &error)) {
    return FailedOperation(error);
  }
  if (deleteReference) {
    std::string ignoredTarget;
    if (!ResolveReferenceTargetName(
            context,
            name,
            false,
            true,
            &ignoredTarget,
            &error)) {
      return FailedOperation(error);
    }
    bool removed = false;
    if (!DeleteReference(
            context,
            name,
            &removed,
            &error)) {
      return FailedOperation(error);
    }
    if (!removed) {
      return FailedOperation("ref " + name + " does not exist.");
    }
    if (!RemoveReflog(context, name, &error)) {
      return FailedOperation(error);
    }
  } else {
    std::string newObjectId;
    if (!ResolveReferenceObjectId(
            context,
            target,
            &newObjectId,
            &error)) {
      return FailedOperation(error);
    }
    if (!WriteAtomicFile(
            ReferencePath(context, name),
            "ref: " + target + "\n",
            &error)) {
      return FailedOperation(error);
    }
    if (name != "HEAD") {
      bool packedRemoved = false;
      if (!RemovePackedReference(
              context.commonGitDirectory / "packed-refs",
              name,
              &packedRemoved,
              &error)) {
        return FailedOperation(error);
      }
    }
    if (!AppendReflog(
            context,
            name,
            oldObjectId,
            newObjectId,
            message,
            &error)) {
      return FailedOperation(error);
    }
  }

  const RepositorySnapshot snapshot =
      InspectRepository(context.repositoryPath.generic_string());
  if (!snapshot.valid) {
    return FailedOperation(snapshot.error);
  }
  return SuccessfulOperation(snapshot, 1);
}

RepositoryOperation UpdateReference(
    const std::string& startPath,
    const std::string& name,
    const std::string& newValue,
    const std::string& oldValue,
    bool deleteReference,
    bool noDeref,
    const std::string& message,
    bool createReflog) {
  if (!ValidReferenceName(name)) {
    return FailedOperation("ref " + name + " is not a valid ref.");
  }
  RepositoryContext context;
  std::string error;
  if (!LoadRepositoryContext(startPath, &context, &error)) {
    return FailedOperation(error);
  }

  std::string targetName = name;
  if (!noDeref &&
      !ResolveReferenceTargetName(
          context,
          name,
          true,
          false,
          &targetName,
          &error)) {
    return FailedOperation(error);
  }
  std::string currentObjectId;
  if (!ResolveReferenceObjectId(
          context,
          targetName,
          &currentObjectId,
          &error)) {
    return FailedOperation(error);
  }

  const std::string zeroId(40, '0');
  if (!oldValue.empty() &&
      !(deleteReference && oldValue == zeroId)) {
    std::string expectedObjectId;
    if (oldValue == zeroId) {
      expectedObjectId = zeroId;
    } else {
      expectedObjectId =
          ResolveObjectName(context, oldValue, &error);
      if (expectedObjectId.empty()) {
        return FailedOperation(error);
      }
    }
    const std::string actual =
        currentObjectId.empty() ? zeroId : currentObjectId;
    if (actual != expectedObjectId) {
      return FailedOperation(
          "Cannot lock ref '" + name + "': is at " + actual +
          " but expected " + expectedObjectId + ".");
    }
  }

  const bool deleteResolved =
      deleteReference || newValue == zeroId;
  if (deleteResolved) {
    bool removed = false;
    if (!DeleteReference(
            context,
            targetName,
            &removed,
            &error)) {
      return FailedOperation(error);
    }
    if (!RemoveReflog(context, targetName, &error)) {
      return FailedOperation(error);
    }
    if (name != targetName &&
        !AppendReflog(
            context,
            name,
            currentObjectId,
            "",
            message,
            &error,
            createReflog)) {
      return FailedOperation(error);
    }
    const RepositorySnapshot snapshot =
        InspectRepository(context.repositoryPath.generic_string());
    if (!snapshot.valid) {
      return FailedOperation(snapshot.error);
    }
    return SuccessfulOperation(snapshot, removed ? 1 : 0);
  }

  if (Trim(newValue).empty()) {
    return FailedOperation("A new object ID is required.");
  }
  const std::string newObjectId =
      ResolveObjectName(context, newValue, &error);
  if (newObjectId.empty()) {
    return FailedOperation(error);
  }
  ObjectData newObject;
  if (!ReadObject(
          context.commonGitDirectory,
          newObjectId,
          &newObject,
          &error)) {
    return FailedOperation(error);
  }
  if (!WriteReference(
          ReferencePath(context, targetName),
          newObjectId,
          &error)) {
    return FailedOperation(error);
  }
  if (targetName != "HEAD") {
    bool packedRemoved = false;
    if (!RemovePackedReference(
            context.commonGitDirectory / "packed-refs",
            targetName,
            &packedRemoved,
            &error)) {
      return FailedOperation(error);
    }
  }
  if (!AppendReflog(
          context,
          targetName,
          currentObjectId,
          newObjectId,
          message,
          &error,
          createReflog) ||
      (name != targetName &&
       !AppendReflog(
           context,
           name,
           currentObjectId,
           newObjectId,
           message,
           &error,
           createReflog))) {
    return FailedOperation(error);
  }
  const RepositorySnapshot snapshot =
      InspectRepository(context.repositoryPath.generic_string());
  if (!snapshot.valid) {
    return FailedOperation(snapshot.error);
  }
  return SuccessfulOperation(
      snapshot,
      currentObjectId == newObjectId ? 0 : 1);
}

RepositoryOperation UpdateReferences(
    const std::string& startPath,
    const std::string& input,
    bool noDeref,
    bool createReflog,
    const std::string& message,
    bool nullTerminated,
    bool batchUpdates) {
  enum class TransactionState {
    Open,
    Prepared,
    Closed
  };

  RepositoryContext context;
  std::string error;
  if (!LoadRepositoryContext(startPath, &context, &error)) {
    return FailedOperation(error);
  }

  std::vector<ReferenceBatchAction> actions;
  std::vector<std::string> output;
  TransactionState state = TransactionState::Open;
  bool explicitlyStarted = false;
  bool nextNoDeref = false;
  uint32_t changedCount = 0;

  const auto fail =
      [&output](const std::string& failure) {
        RepositoryOperation result = FailedOperation(failure);
        result.output = output;
        return result;
      };
  const auto prepareTransaction =
      [&]() {
        if (!LoadRepositoryContext(startPath, &context, &error)) {
          return false;
        }
        std::vector<ReferenceBatchRejection> rejections;
        if (!ValidateReferenceBatch(
                context,
                &actions,
                batchUpdates,
                &rejections,
                &error)) {
          return false;
        }
        for (const ReferenceBatchRejection& rejection : rejections) {
          output.push_back(
              "rejected " + rejection.name + " " +
              rejection.newValue + " " + rejection.oldValue + " " +
              rejection.reason);
        }
        return true;
      };
  const auto commitTransaction =
      [&]() {
        if (state != TransactionState::Prepared &&
            !prepareTransaction()) {
          return false;
        }
        std::vector<ReferenceFileBackup> backups;
        if (!CaptureReferenceBatchBackups(
                context,
                actions,
                &backups,
                &error)) {
          return false;
        }
        uint32_t transactionChanged = 0;
        for (const ReferenceBatchAction& action : actions) {
          if (IsReferenceBatchVerifyAction(action.kind)) {
            continue;
          }
          if (IsSymbolicReferenceBatchAction(action.kind)) {
            uint32_t actionChanged = 0;
            if (!ApplySymbolicReferenceBatchAction(
                    context,
                    action,
                    message,
                    createReflog,
                    &actionChanged,
                    &error)) {
              std::string restoreError;
              if (!RestoreReferenceBatchBackups(
                      backups,
                      &restoreError)) {
                error += " Rollback failed: " + restoreError;
              }
              return false;
            }
            transactionChanged += actionChanged;
            continue;
          }
          const bool deleteReference =
              action.kind == ReferenceBatchActionKind::Delete ||
              action.resolvedNewValue == std::string(40, '0');
          const RepositoryOperation operation =
              UpdateReference(
                  startPath,
                  action.name,
                  deleteReference ? "" : action.resolvedNewValue,
                  "",
                  deleteReference,
                  action.noDeref,
                  message,
                  createReflog);
          if (!operation.success) {
            std::string restoreError;
            if (!RestoreReferenceBatchBackups(
                    backups,
                    &restoreError)) {
              error =
                  operation.error + " Rollback failed: " +
                  restoreError;
            } else {
              error = operation.error;
            }
            return false;
          }
          transactionChanged += operation.changedCount;
        }
        changedCount += transactionChanged;
        actions.clear();
        nextNoDeref = false;
        state = TransactionState::Closed;
        explicitlyStarted = false;
        return true;
      };

  std::vector<ReferenceBatchCommand> commands;
  if (!ParseReferenceBatchCommands(
          input,
          nullTerminated,
          &commands,
          &error)) {
    return fail(error);
  }
  for (const ReferenceBatchCommand& parsed : commands) {
    const std::string& command = parsed.name;
    const std::vector<std::string>& arguments = parsed.arguments;

    if (command == "start") {
      if (!arguments.empty()) {
        return fail("start: extra input");
      }
      if (state == TransactionState::Prepared) {
        return fail("prepared transactions can only be closed");
      }
      if (state == TransactionState::Open &&
          (explicitlyStarted || !actions.empty())) {
        return fail("transaction is already active");
      }
      state = TransactionState::Open;
      explicitlyStarted = true;
      nextNoDeref = false;
      output.push_back("start: ok");
      continue;
    }
    if (state == TransactionState::Closed) {
      return fail("transaction is closed");
    }
    if (command == "prepare") {
      if (!arguments.empty()) {
        return fail("prepare: extra input");
      }
      if (state == TransactionState::Prepared) {
        return fail("transaction is already prepared");
      }
      if (!prepareTransaction()) {
        return fail(error);
      }
      state = TransactionState::Prepared;
      output.push_back("prepare: ok");
      continue;
    }
    if (command == "commit") {
      if (!arguments.empty()) {
        return fail("commit: extra input");
      }
      if (!commitTransaction()) {
        return fail(error);
      }
      output.push_back("commit: ok");
      continue;
    }
    if (command == "abort") {
      if (!arguments.empty()) {
        return fail("abort: extra input");
      }
      actions.clear();
      nextNoDeref = false;
      state = TransactionState::Closed;
      explicitlyStarted = false;
      output.push_back("abort: ok");
      continue;
    }
    if (state == TransactionState::Prepared) {
      return fail("prepared transactions can only be closed");
    }
    if (command == "option") {
      if (arguments.size() != 1 ||
          arguments[0] != "no-deref") {
        return fail(
            "option " +
            (arguments.empty() ? "" : arguments[0]) +
            ": unknown");
      }
      nextNoDeref = true;
      continue;
    }

    ReferenceBatchAction action;
    if (command == "update") {
      if (arguments.empty() || arguments[0].empty()) {
        return fail("update: missing <ref>");
      }
      if (arguments.size() < 2) {
        return fail(
            "update " + arguments[0] +
            ": missing <new-oid>");
      }
      if (arguments.size() > 3) {
        return fail(
            "update " + arguments[0] +
            ": extra input");
      }
      action.kind = ReferenceBatchActionKind::Update;
      action.name = arguments[0];
      action.newValue = arguments[1];
      action.oldValueProvided =
          arguments.size() == 3 &&
          (!nullTerminated || !arguments[2].empty());
      if (action.oldValueProvided) {
        action.oldValue = arguments[2];
      }
    } else if (command == "create") {
      if (arguments.empty() || arguments[0].empty()) {
        return fail("create: missing <ref>");
      }
      if (arguments.size() < 2) {
        return fail(
            "create " + arguments[0] +
            ": missing <new-oid>");
      }
      if (arguments.size() > 2) {
        return fail(
            "create " + arguments[0] +
            ": extra input");
      }
      action.kind = ReferenceBatchActionKind::Create;
      action.name = arguments[0];
      action.newValue = arguments[1];
    } else if (command == "delete") {
      if (arguments.empty() || arguments[0].empty()) {
        return fail("delete: missing <ref>");
      }
      if (arguments.size() > 2) {
        return fail(
            "delete " + arguments[0] +
            ": extra input");
      }
      action.kind = ReferenceBatchActionKind::Delete;
      action.name = arguments[0];
      action.oldValueProvided =
          arguments.size() == 2 &&
          (!nullTerminated || !arguments[1].empty());
      if (action.oldValueProvided) {
        action.oldValue = arguments[1];
      }
    } else if (command == "verify") {
      if (arguments.empty() || arguments[0].empty()) {
        return fail("verify: missing <ref>");
      }
      if (arguments.size() > 2) {
        return fail(
            "verify " + arguments[0] +
            ": extra input");
      }
      action.kind = ReferenceBatchActionKind::Verify;
      action.name = arguments[0];
      action.oldValueProvided =
          arguments.size() == 2 &&
          (!nullTerminated || !arguments[1].empty());
      if (action.oldValueProvided) {
        action.oldValue = arguments[1];
      }
    } else if (command == "symref-update") {
      if (arguments.empty() || arguments[0].empty()) {
        return fail("symref-update: missing <ref>");
      }
      if (arguments.size() < 2 || arguments[1].empty()) {
        return fail(
            "symref-update " + arguments[0] +
            ": missing <new-target>");
      }
      if (arguments.size() > 4) {
        return fail(
            "symref-update " + arguments[0] +
            ": extra input");
      }
      action.kind = ReferenceBatchActionKind::SymbolicUpdate;
      action.name = arguments[0];
      action.newTarget = arguments[1];
      if (arguments.size() >= 3 && !arguments[2].empty()) {
        if (arguments.size() < 4 || arguments[3].empty()) {
          return fail(
              "symref-update " + arguments[0] +
              ": expected old value");
        }
        if (arguments[2] == "ref") {
          action.oldTargetProvided = true;
          action.oldTarget = arguments[3];
        } else if (arguments[2] == "oid") {
          action.oldValueProvided = true;
          action.oldValue = arguments[3];
        } else {
          return fail(
              "symref-update " + arguments[0] +
              ": invalid arg '" + arguments[2] +
              "' for old value");
        }
      } else if (arguments.size() >= 4 &&
                 !arguments[3].empty()) {
        return fail(
            "symref-update " + arguments[0] +
            ": extra input");
      }
    } else if (command == "symref-create") {
      if (arguments.empty() || arguments[0].empty()) {
        return fail("symref-create: missing <ref>");
      }
      if (arguments.size() < 2 || arguments[1].empty()) {
        return fail(
            "symref-create " + arguments[0] +
            ": missing <new-target>");
      }
      if (arguments.size() > 2) {
        return fail(
            "symref-create " + arguments[0] +
            ": extra input");
      }
      action.kind = ReferenceBatchActionKind::SymbolicCreate;
      action.name = arguments[0];
      action.newTarget = arguments[1];
    } else if (command == "symref-delete") {
      if (arguments.empty() || arguments[0].empty()) {
        return fail("symref-delete: missing <ref>");
      }
      if (arguments.size() > 2) {
        return fail(
            "symref-delete " + arguments[0] +
            ": extra input");
      }
      action.kind = ReferenceBatchActionKind::SymbolicDelete;
      action.name = arguments[0];
      action.oldTargetProvided =
          arguments.size() == 2 && !arguments[1].empty();
      if (action.oldTargetProvided) {
        action.oldTarget = arguments[1];
      }
    } else if (command == "symref-verify") {
      if (arguments.empty() || arguments[0].empty()) {
        return fail("symref-verify: missing <ref>");
      }
      if (arguments.size() > 2) {
        return fail(
            "symref-verify " + arguments[0] +
            ": extra input");
      }
      action.kind = ReferenceBatchActionKind::SymbolicVerify;
      action.name = arguments[0];
      action.oldTargetProvided =
          arguments.size() == 2 && !arguments[1].empty();
      if (action.oldTargetProvided) {
        action.oldTarget = arguments[1];
      }
    } else {
      return fail("unknown command: " + parsed.source);
    }
    action.noDeref = noDeref || nextNoDeref;
    nextNoDeref = false;
    actions.push_back(std::move(action));
  }

  if (state == TransactionState::Open &&
      !explicitlyStarted &&
      !commitTransaction()) {
    return fail(error);
  }
  const RepositorySnapshot snapshot =
      InspectRepository(context.repositoryPath.generic_string());
  if (!snapshot.valid) {
    return fail(snapshot.error);
  }
  RepositoryOperation result =
      SuccessfulOperation(snapshot, changedCount);
  result.output = std::move(output);
  return result;
}

RepositoryOperation CreateTag(
    const std::string& startPath,
    const std::string& name,
    const std::string& target,
    bool force,
    bool annotated,
    const std::string& message) {
  if (!ValidBranchName(name)) {
    return FailedOperation("'" + name + "' is not a valid tag name.");
  }
  if (annotated && Trim(message).empty()) {
    return FailedOperation(
        "Annotated tags require a non-empty message.");
  }
  RepositoryContext context;
  std::string error;
  if (!LoadRepositoryContext(startPath, &context, &error)) {
    return FailedOperation(error);
  }
  const std::string source =
      Trim(target).empty() ? "HEAD" : target;
  const std::string targetObjectId =
      ResolveRevision(context, source, &error);
  if (targetObjectId.empty()) {
    return FailedOperation(error);
  }
  const std::string refName = "refs/tags/" + name;
  const std::string existing = ResolveHeadObject(
      context.gitDirectory,
      context.commonGitDirectory,
      "ref: " + refName);
  if (!existing.empty() && !force) {
    return FailedOperation("Tag '" + name + "' already exists.");
  }

  std::string referenceObjectId = targetObjectId;
  if (annotated) {
    const std::string tagger =
        CommitAuthor(context.commonGitDirectory, &error);
    if (tagger.empty()) {
      return FailedOperation(error);
    }
    std::string payload =
        "object " + targetObjectId + "\n"
        "type commit\n"
        "tag " + name + "\n"
        "tagger " + tagger + " " + CurrentGitTimestamp() + "\n\n" +
        message;
    if (payload.back() != '\n') {
      payload.push_back('\n');
    }
    if (!WriteLooseObject(
            context.commonGitDirectory,
            "tag",
            payload,
            &referenceObjectId,
            &error)) {
      return FailedOperation(error);
    }
  }
  if (!WriteReference(
          context.commonGitDirectory / refName,
          referenceObjectId,
          &error)) {
    return FailedOperation(error);
  }
  bool packedRemoved = false;
  if (!RemovePackedReference(
          context.commonGitDirectory / "packed-refs",
          refName,
          &packedRemoved,
          &error)) {
    return FailedOperation(error);
  }
  const RepositorySnapshot snapshot =
      InspectRepository(context.repositoryPath.generic_string());
  if (!snapshot.valid) {
    return FailedOperation(snapshot.error);
  }
  return SuccessfulOperation(snapshot, 1);
}

RepositoryOperation DeleteTags(
    const std::string& startPath,
    const std::vector<std::string>& names) {
  if (names.empty()) {
    return FailedOperation("git tag -d requires at least one tag name.");
  }
  RepositoryContext context;
  std::string error;
  if (!LoadRepositoryContext(startPath, &context, &error)) {
    return FailedOperation(error);
  }
  const std::string prefix = "refs/tags/";
  const std::map<std::string, std::string> references =
      ReadReferenceValuesWithPrefix(
          context.commonGitDirectory,
          prefix);
  std::set<std::string> uniqueNames;
  for (const std::string& name : names) {
    if (!ValidBranchName(name)) {
      return FailedOperation("'" + name + "' is not a valid tag name.");
    }
    if (references.find(prefix + name) == references.end()) {
      return FailedOperation("Tag '" + name + "' not found.");
    }
    uniqueNames.insert(name);
  }
  for (const std::string& name : uniqueNames) {
    bool removed = false;
    if (!DeleteReference(
            context,
            prefix + name,
            &removed,
            &error)) {
      return FailedOperation(error);
    }
    if (!removed) {
      return FailedOperation("Tag '" + name + "' not found.");
    }
  }
  const RepositorySnapshot snapshot =
      InspectRepository(context.repositoryPath.generic_string());
  if (!snapshot.valid) {
    return FailedOperation(snapshot.error);
  }
  return SuccessfulOperation(
      snapshot,
      static_cast<uint32_t>(uniqueNames.size()));
}

std::vector<ConfigEntry> ReadConfig(
    const std::string& startPath,
    std::string* error) {
  std::vector<ConfigEntry> entries;
  if (error != nullptr) {
    error->clear();
  }
  RepositoryContext context;
  if (!LoadRepositoryContext(startPath, &context, error)) {
    return entries;
  }
  return ReadConfigEntriesFromFile(context.commonGitDirectory / "config");
}

RepositoryOperation SetConfigValue(
    const std::string& startPath,
    const std::string& key,
    const std::string& value) {
  RepositoryContext context;
  std::string error;
  if (!LoadRepositoryContext(startPath, &context, &error)) {
    return FailedOperation(error);
  }
  bool changed = false;
  if (!RewriteLocalConfig(
          context.commonGitDirectory / "config",
          key,
          value,
          false,
          &changed,
          &error)) {
    return FailedOperation(error);
  }
  const RepositorySnapshot snapshot =
      InspectRepository(context.repositoryPath.generic_string());
  if (!snapshot.valid) {
    return FailedOperation(snapshot.error);
  }
  return SuccessfulOperation(snapshot, changed ? 1 : 0);
}

RepositoryOperation UnsetConfigValue(
    const std::string& startPath,
    const std::string& key) {
  RepositoryContext context;
  std::string error;
  if (!LoadRepositoryContext(startPath, &context, &error)) {
    return FailedOperation(error);
  }
  bool changed = false;
  if (!RewriteLocalConfig(
          context.commonGitDirectory / "config",
          key,
          "",
          true,
          &changed,
          &error)) {
    return FailedOperation(error);
  }
  const RepositorySnapshot snapshot =
      InspectRepository(context.repositoryPath.generic_string());
  if (!snapshot.valid) {
    return FailedOperation(snapshot.error);
  }
  return SuccessfulOperation(snapshot, changed ? 1 : 0);
}

RepositoryOperation AddRemote(
    const std::string& startPath,
    const std::string& name,
    const std::string& url) {
  if (!ValidRemoteName(name)) {
    return FailedOperation("'" + name + "' is not a valid remote name.");
  }
  if (url.empty()) {
    return FailedOperation("A remote URL is required.");
  }
  RepositoryContext context;
  std::string error;
  if (!LoadRepositoryContext(startPath, &context, &error)) {
    return FailedOperation(error);
  }
  const RemoteConfigValues existing = ReadRemoteConfigValues(
      context.commonGitDirectory / "config",
      name);
  if (existing.exists) {
    return FailedOperation("remote '" + name + "' already exists.");
  }
  if (!AddRemoteConfig(
          context.commonGitDirectory / "config",
          name,
          url,
          &error)) {
    return FailedOperation(error);
  }
  const RepositorySnapshot snapshot =
      InspectRepository(context.repositoryPath.generic_string());
  if (!snapshot.valid) {
    return FailedOperation(snapshot.error);
  }
  return SuccessfulOperation(snapshot, 1);
}

RepositoryOperation RemoveRemote(
    const std::string& startPath,
    const std::string& name) {
  if (!ValidRemoteName(name)) {
    return FailedOperation("'" + name + "' is not a valid remote name.");
  }
  RepositoryContext context;
  std::string error;
  if (!LoadRepositoryContext(startPath, &context, &error)) {
    return FailedOperation(error);
  }
  const RemoteConfigValues existing = ReadRemoteConfigValues(
      context.commonGitDirectory / "config",
      name);
  if (!existing.exists) {
    return FailedOperation("No such remote: '" + name + "'.");
  }
  bool configChanged = false;
  if (!RewriteRemoteConfigSections(
          context.commonGitDirectory / "config",
          name,
          "",
          true,
          &configChanged,
          &error)) {
    return FailedOperation(error);
  }
  const std::string prefix = "refs/remotes/" + name + "/";
  bool packedChanged = false;
  if (!RemoveLooseReferencePrefix(
          context.commonGitDirectory,
          prefix,
          &error) ||
      !RewritePackedReferencePrefix(
          context.commonGitDirectory / "packed-refs",
          prefix,
          "",
          true,
          &packedChanged,
          &error) ||
      !MoveLogPrefix(
          context.commonGitDirectory,
          prefix,
          "",
          true,
          &error)) {
    return FailedOperation(error);
  }
  const RepositorySnapshot snapshot =
      InspectRepository(context.repositoryPath.generic_string());
  if (!snapshot.valid) {
    return FailedOperation(snapshot.error);
  }
  return SuccessfulOperation(snapshot, 1);
}

RepositoryOperation RenameRemote(
    const std::string& startPath,
    const std::string& oldName,
    const std::string& newName) {
  if (!ValidRemoteName(oldName) || !ValidRemoteName(newName)) {
    return FailedOperation("Invalid remote name.");
  }
  if (oldName == newName) {
    return FailedOperation(
        "A remote cannot be renamed to the same name.");
  }
  RepositoryContext context;
  std::string error;
  if (!LoadRepositoryContext(startPath, &context, &error)) {
    return FailedOperation(error);
  }
  const fs::path configPath = context.commonGitDirectory / "config";
  const RemoteConfigValues oldRemote =
      ReadRemoteConfigValues(configPath, oldName);
  if (!oldRemote.exists) {
    return FailedOperation("No such remote: '" + oldName + "'.");
  }
  if (ReadRemoteConfigValues(configPath, newName).exists) {
    return FailedOperation("remote '" + newName + "' already exists.");
  }
  const std::string oldPrefix = "refs/remotes/" + oldName + "/";
  const std::string newPrefix = "refs/remotes/" + newName + "/";
  const std::map<std::string, std::string> oldReferences =
      ReadReferenceValuesWithPrefix(
          context.commonGitDirectory,
          oldPrefix);
  const std::map<std::string, std::string> newReferences =
      ReadReferenceValuesWithPrefix(
          context.commonGitDirectory,
          newPrefix);
  for (const auto& item : oldReferences) {
    const std::string newRef =
        newPrefix + item.first.substr(oldPrefix.size());
    if (newReferences.find(newRef) != newReferences.end()) {
      return FailedOperation("Reference already exists: " + newRef);
    }
  }

  bool configChanged = false;
  if (!RewriteRemoteConfigSections(
          configPath,
          oldName,
          newName,
          false,
          &configChanged,
          &error) ||
      !MoveLooseReferencePrefix(
          context.commonGitDirectory,
          oldPrefix,
          newPrefix,
          &error)) {
    return FailedOperation(error);
  }
  bool packedChanged = false;
  if (!RewritePackedReferencePrefix(
          context.commonGitDirectory / "packed-refs",
          oldPrefix,
          newPrefix,
          false,
          &packedChanged,
          &error) ||
      !MoveLogPrefix(
          context.commonGitDirectory,
          oldPrefix,
          newPrefix,
          false,
          &error)) {
    return FailedOperation(error);
  }
  for (const auto& item : oldReferences) {
    std::array<uint8_t, 20> objectId {};
    if (!HexToObjectId(item.second, &objectId)) {
      continue;
    }
    const std::string newRef =
        newPrefix + item.first.substr(oldPrefix.size());
    if (!AppendReflog(
            context,
            newRef,
            item.second,
            item.second,
            "remote: renamed " + item.first + " to " + newRef,
            &error)) {
      return FailedOperation(error);
    }
  }
  const RepositorySnapshot snapshot =
      InspectRepository(context.repositoryPath.generic_string());
  if (!snapshot.valid) {
    return FailedOperation(snapshot.error);
  }
  return SuccessfulOperation(snapshot, 1);
}

std::string GetRemoteUrl(
    const std::string& startPath,
    const std::string& name,
    bool push,
    std::string* error) {
  if (error != nullptr) {
    error->clear();
  }
  if (!ValidRemoteName(name)) {
    if (error != nullptr) {
      *error = "'" + name + "' is not a valid remote name.";
    }
    return "";
  }
  RepositoryContext context;
  if (!LoadRepositoryContext(startPath, &context, error)) {
    return "";
  }
  const RemoteConfigValues remote = ReadRemoteConfigValues(
      context.commonGitDirectory / "config",
      name);
  if (!remote.exists) {
    if (error != nullptr) {
      *error = "No such remote: '" + name + "'.";
    }
    return "";
  }
  const std::vector<std::string>& values =
      push && !remote.pushUrls.empty()
          ? remote.pushUrls
          : remote.urls;
  if (values.empty()) {
    if (error != nullptr) {
      *error = "Remote '" + name + "' has no URL configured.";
    }
    return "";
  }
  return values.front();
}

RepositoryOperation SetRemoteUrl(
    const std::string& startPath,
    const std::string& name,
    const std::string& url,
    bool push) {
  if (!ValidRemoteName(name)) {
    return FailedOperation("'" + name + "' is not a valid remote name.");
  }
  if (url.empty()) {
    return FailedOperation("A remote URL is required.");
  }
  RepositoryContext context;
  std::string error;
  if (!LoadRepositoryContext(startPath, &context, &error)) {
    return FailedOperation(error);
  }
  bool changed = false;
  if (!UpdateRemoteUrlConfig(
          context.commonGitDirectory / "config",
          name,
          url,
          push,
          &changed,
          &error)) {
    return FailedOperation(error);
  }
  const RepositorySnapshot snapshot =
      InspectRepository(context.repositoryPath.generic_string());
  if (!snapshot.valid) {
    return FailedOperation(snapshot.error);
  }
  return SuccessfulOperation(snapshot, changed ? 1 : 0);
}

std::vector<ReflogEntry> ReadReflog(
    const std::string& startPath,
    const std::string& ref,
    uint32_t maxCount,
    std::string* error) {
  std::vector<ReflogEntry> entries;
  if (error != nullptr) {
    error->clear();
  }
  if (maxCount == 0) {
    return entries;
  }
  RepositoryContext context;
  if (!LoadRepositoryContext(startPath, &context, error)) {
    return entries;
  }
  std::ifstream input(ReflogPath(context, ref), std::ios::binary);
  if (!input) {
    return entries;
  }
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(input, line)) {
    lines.push_back(line);
  }
  for (auto iterator = lines.rbegin();
       iterator != lines.rend() &&
       entries.size() < static_cast<size_t>(maxCount);
       ++iterator) {
    const size_t tab = iterator->find('\t');
    const std::string metadata =
        tab == std::string::npos
            ? *iterator
            : iterator->substr(0, tab);
    if (metadata.size() < 82 ||
        metadata[40] != ' ' ||
        metadata[81] != ' ') {
      continue;
    }
    ReflogEntry entry;
    entry.oldId = metadata.substr(0, 40);
    entry.newId = metadata.substr(41, 40);
    const std::string actorAndTimestamp = metadata.substr(82);
    const size_t timezoneSeparator = actorAndTimestamp.rfind(' ');
    if (timezoneSeparator == std::string::npos ||
        timezoneSeparator == 0) {
      continue;
    }
    const size_t timestampSeparator =
        actorAndTimestamp.rfind(' ', timezoneSeparator - 1);
    if (timestampSeparator == std::string::npos) {
      continue;
    }
    entry.actor = actorAndTimestamp.substr(0, timestampSeparator);
    entry.timestamp = actorAndTimestamp.substr(timestampSeparator + 1);
    entry.message = tab == std::string::npos
        ? ""
        : iterator->substr(tab + 1);
    entries.push_back(entry);
  }
  return entries;
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

std::string ReadWorkspaceFile(
    const std::string& startPath,
    const std::string& filePath,
    std::string* error) {
  if (error != nullptr) {
    error->clear();
  }
  fs::path candidate(NormalizeInputPath(filePath));
  if (candidate.is_relative()) {
    candidate = CommandBasePath(startPath) / candidate;
  }
  candidate = candidate.lexically_normal();
  std::string content;
  if (!ReadBinaryFile(candidate, &content, error)) {
    return "";
  }
  return content;
}

RepositoryOperation WriteWorkspaceFile(
    const std::string& startPath,
    const std::string& filePath,
    const std::string& content,
    bool append) {
  RepositoryOperation operation;
  fs::path candidate(NormalizeInputPath(filePath));
  if (candidate.is_relative()) {
    candidate = CommandBasePath(startPath) / candidate;
  }
  candidate = candidate.lexically_normal();
  std::string existing;
  std::string error;
  if (append && fs::exists(candidate)) {
    if (!ReadBinaryFile(candidate, &existing, &error)) {
      operation.error = error;
      return operation;
    }
  }
  if (!WriteBinaryFile(
          candidate,
          append ? existing + content : content,
          &error)) {
    operation.error = error;
    return operation;
  }
  operation.success = true;
  operation.changedCount = 1;
  operation.snapshot = InspectRepository(startPath);
  operation.output.push_back(candidate.generic_string());
  return operation;
}

}  // namespace harmony_git
