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

struct IgnoreRule {
  std::string basePath;
  std::string pattern;
  bool negated = false;
  bool directoryOnly = false;
  bool anchored = false;
  bool hasSlash = false;
};

bool IsHexCharacter(char value);
int HexValue(char value);

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
  if (*version == 4) {
    *error =
        "Git index version 4 path compression is not supported by this native reader yet.";
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
    size_t pathEnd = pathStart;
    while (pathEnd < indexEnd && data[pathEnd] != 0) {
      ++pathEnd;
    }
    if (pathEnd >= indexEnd) {
      *error = "Git index path is missing its terminator.";
      return false;
    }
    entry.path.assign(
        reinterpret_cast<const char*>(data.data() + pathStart),
        pathEnd - pathStart);
    entries->push_back(entry);

    const size_t entryLength = pathEnd - entryStart + 1;
    offset = entryStart + ((entryLength + 7) / 8) * 8;
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
  while (std::getline(input, line)) {
    IgnoreRule rule;
    if (ParseIgnoreRule(line, basePath, &rule)) {
      rules->push_back(rule);
    }
  }
}

std::string ConfigValueFromFile(
    const fs::path& configPath,
    const std::string& section,
    const std::string& key) {
  std::istringstream input(ReadTextFile(configPath));
  std::string currentSection;
  std::string line;
  while (std::getline(input, line)) {
    const std::string trimmed = Trim(line);
    if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
      continue;
    }
    if (trimmed.front() == '[' && trimmed.back() == ']') {
      currentSection = trimmed.substr(1, trimmed.size() - 2);
      continue;
    }
    if (currentSection != section) {
      continue;
    }
    const size_t separator = trimmed.find('=');
    if (separator == std::string::npos) {
      continue;
    }
    std::string actualKey = Trim(trimmed.substr(0, separator));
    std::string expectedKey = key;
    std::transform(
        actualKey.begin(),
        actualKey.end(),
        actualKey.begin(),
        [](unsigned char character) {
          return static_cast<char>(std::tolower(character));
        });
    std::transform(
        expectedKey.begin(),
        expectedKey.end(),
        expectedKey.begin(),
        [](unsigned char character) {
          return static_cast<char>(std::tolower(character));
        });
    if (actualKey == expectedKey) {
      return Trim(trimmed.substr(separator + 1));
    }
  }
  return "";
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
    std::vector<IgnoreRule> const& rules) {
  bool ignored = false;
  for (const IgnoreRule& rule : rules) {
    if (IsIgnoredByRule(rule, relativePath, directory)) {
      ignored = !rule.negated;
    }
  }
  return ignored;
}

void AppendUntrackedFilesRecursive(
    const fs::path& directory,
    const std::string& relativeDirectory,
    const std::set<std::string>& trackedPaths,
    std::vector<IgnoreRule> rules,
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
    if (directory) {
      if (!IsIgnored(relative, true, rules)) {
        AppendUntrackedFilesRecursive(
            path,
            relative,
            trackedPaths,
            rules,
            files);
      }
    } else if (trackedPaths.find(relative) == trackedPaths.end() &&
               !IsIgnored(relative, false, rules)) {
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
      files);
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
  }
  if (objectId.empty()) {
    if (error != nullptr) {
      *error = "Invalid reference: " + source;
    }
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

bool ReadTreeRecursive(
    const fs::path& commonGitDirectory,
    const std::string& treeObjectId,
    const std::string& prefix,
    std::map<std::string, TreeEntry>* entries,
    std::string* error) {
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
      return false;
    }
    const std::string mode =
        tree.payload.substr(offset, modeEnd - offset);
    const std::string name =
        tree.payload.substr(modeEnd + 1, nameEnd - modeEnd - 1);
    std::array<uint8_t, 20> childObjectId {};
    std::copy_n(
        tree.payload.begin() + static_cast<std::ptrdiff_t>(nameEnd + 1),
        childObjectId.size(),
        childObjectId.begin());
    const std::string path =
        prefix.empty() ? name : prefix + "/" + name;
    if (mode == "40000" || mode == "040000") {
      if (!ReadTreeRecursive(
              commonGitDirectory,
              ObjectIdToHex(childObjectId),
              path,
              entries,
              error)) {
        return false;
      }
    } else {
      (*entries)[path] = {path, mode, childObjectId};
    }
    offset = nameEnd + 21;
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
  std::istringstream input(ReadTextFile(gitDirectory / "config"));
  std::string currentSection;
  std::string line;
  while (std::getline(input, line)) {
    const std::string trimmed = Trim(line);
    if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
      continue;
    }
    if (trimmed.front() == '[' && trimmed.back() == ']') {
      currentSection = trimmed.substr(1, trimmed.size() - 2);
      continue;
    }
    if (currentSection != section) {
      continue;
    }
    const size_t separator = trimmed.find('=');
    if (separator == std::string::npos) {
      continue;
    }
    if (Trim(trimmed.substr(0, separator)) == key) {
      return Trim(trimmed.substr(separator + 1));
    }
  }
  return "";
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
  } else if (!WriteReference(
                 context.gitDirectory / "HEAD",
                 commitObjectId,
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
