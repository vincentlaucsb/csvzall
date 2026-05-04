#include "gzip_stream.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace csvzall::pipeline::common {

namespace {

bool HasSuffixCi(std::string_view path, std::string_view suffix) {
  if (path.size() < suffix.size()) {
    return false;
  }

  const auto start = path.size() - suffix.size();
  for (std::size_t i = 0; i < suffix.size(); ++i) {
    const auto lhs = static_cast<unsigned char>(path[start + i]);
    const auto rhs = static_cast<unsigned char>(suffix[i]);
    if (std::tolower(lhs) != std::tolower(rhs)) {
      return false;
    }
  }
  return true;
}

std::uint16_t ReadLe16(const std::vector<unsigned char>& data, std::size_t offset) {
  if (offset + 2 > data.size()) {
    throw std::runtime_error("Invalid ZIP archive: truncated field");
  }
  return static_cast<std::uint16_t>(data[offset] | (data[offset + 1] << 8));
}

std::uint32_t ReadLe32(const std::vector<unsigned char>& data, std::size_t offset) {
  if (offset + 4 > data.size()) {
    throw std::runtime_error("Invalid ZIP archive: truncated field");
  }
  return static_cast<std::uint32_t>(data[offset]) |
         (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
         (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
         (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

std::string ReadBytesAsString(const std::vector<unsigned char>& data,
                              std::size_t offset,
                              std::size_t size) {
  if (offset + size > data.size()) {
    throw std::runtime_error("Invalid ZIP archive: truncated entry data");
  }
  return std::string(reinterpret_cast<const char*>(data.data() + offset), size);
}

struct ZipEntry {
  std::string name;
  std::uint16_t flags = 0;
  std::uint16_t method = 0;
  std::uint32_t crc = 0;
  std::uint32_t compressed_size = 0;
  std::uint32_t uncompressed_size = 0;
  std::uint32_t local_header_offset = 0;
};

std::vector<unsigned char> ReadWholeFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Unable to open ZIP file: " + path);
  }

  file.seekg(0, std::ios::end);
  const auto end = file.tellg();
  if (end < 0) {
    throw std::runtime_error("Unable to read ZIP file size: " + path);
  }
  file.seekg(0, std::ios::beg);

  std::vector<unsigned char> data(static_cast<std::size_t>(end));
  if (!data.empty()) {
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!file) {
      throw std::runtime_error("Unable to read ZIP file: " + path);
    }
  }
  return data;
}

std::size_t FindEndOfCentralDirectory(const std::vector<unsigned char>& data) {
  constexpr std::uint32_t eocd_signature = 0x06054b50;
  constexpr std::size_t eocd_min_size = 22;
  constexpr std::size_t max_comment_size = 0xffff;

  if (data.size() < eocd_min_size) {
    throw std::runtime_error("Invalid ZIP archive: missing end of central directory");
  }

  const auto search_start =
      data.size() > eocd_min_size + max_comment_size
          ? data.size() - (eocd_min_size + max_comment_size)
          : 0;

  for (std::size_t offset = data.size() - eocd_min_size + 1; offset-- > search_start;) {
    if (ReadLe32(data, offset) == eocd_signature) {
      return offset;
    }
  }

  throw std::runtime_error("Invalid ZIP archive: missing end of central directory");
}

std::vector<ZipEntry> ReadCentralDirectory(const std::vector<unsigned char>& data) {
  constexpr std::uint32_t central_signature = 0x02014b50;
  constexpr std::uint16_t max_16 = std::numeric_limits<std::uint16_t>::max();
  constexpr std::uint32_t max_32 = std::numeric_limits<std::uint32_t>::max();

  const auto eocd = FindEndOfCentralDirectory(data);
  const auto disk = ReadLe16(data, eocd + 4);
  const auto central_disk = ReadLe16(data, eocd + 6);
  const auto entries_this_disk = ReadLe16(data, eocd + 8);
  const auto entries_total = ReadLe16(data, eocd + 10);
  const auto central_size = ReadLe32(data, eocd + 12);
  const auto central_offset = ReadLe32(data, eocd + 16);

  if (disk != 0 || central_disk != 0 || entries_this_disk != entries_total) {
    throw std::runtime_error("Unsupported ZIP archive: split archives are not supported");
  }
  if (entries_total == max_16 || central_size == max_32 || central_offset == max_32) {
    throw std::runtime_error("Unsupported ZIP archive: ZIP64 is not supported");
  }
  if (static_cast<std::size_t>(central_offset) + central_size > data.size()) {
    throw std::runtime_error("Invalid ZIP archive: central directory is truncated");
  }

  std::vector<ZipEntry> entries;
  entries.reserve(entries_total);
  std::size_t offset = central_offset;
  for (std::uint16_t i = 0; i < entries_total; ++i) {
    if (ReadLe32(data, offset) != central_signature) {
      throw std::runtime_error("Invalid ZIP archive: bad central directory entry");
    }

    ZipEntry entry;
    entry.flags = ReadLe16(data, offset + 8);
    entry.method = ReadLe16(data, offset + 10);
    entry.crc = ReadLe32(data, offset + 16);
    entry.compressed_size = ReadLe32(data, offset + 20);
    entry.uncompressed_size = ReadLe32(data, offset + 24);
    const auto name_len = ReadLe16(data, offset + 28);
    const auto extra_len = ReadLe16(data, offset + 30);
    const auto comment_len = ReadLe16(data, offset + 32);
    entry.local_header_offset = ReadLe32(data, offset + 42);
    entry.name = ReadBytesAsString(data, offset + 46, name_len);

    if (entry.compressed_size == max_32 ||
        entry.uncompressed_size == max_32 ||
        entry.local_header_offset == max_32) {
      throw std::runtime_error("Unsupported ZIP archive: ZIP64 is not supported");
    }

    offset += 46 + name_len + extra_len + comment_len;
    if (entry.name.empty() || entry.name.back() == '/') {
      continue;
    }
    entries.push_back(std::move(entry));
  }

  return entries;
}

const ZipEntry& SelectEntry(const std::vector<ZipEntry>& entries,
                            const std::string& requested_entry) {
  if (!requested_entry.empty()) {
    const auto it = std::find_if(entries.begin(), entries.end(), [&](const ZipEntry& entry) {
      return entry.name == requested_entry;
    });
    if (it == entries.end()) {
      throw std::runtime_error("ZIP entry not found: " + requested_entry);
    }
    return *it;
  }

  if (entries.empty()) {
    throw std::runtime_error("ZIP archive contains no file entries");
  }
  if (entries.size() == 1) {
    return entries.front();
  }

  std::ostringstream message;
  message << "ZIP archive contains multiple files; pass --zip-entry <name>. Entries:";
  for (const auto& entry : entries) {
    message << ' ' << entry.name;
  }
  throw std::runtime_error(message.str());
}

std::size_t ZipDataOffset(const std::vector<unsigned char>& data, const ZipEntry& entry) {
  constexpr std::uint32_t local_signature = 0x04034b50;
  const std::size_t offset = entry.local_header_offset;
  if (ReadLe32(data, offset) != local_signature) {
    throw std::runtime_error("Invalid ZIP archive: bad local file header");
  }

  const auto name_len = ReadLe16(data, offset + 26);
  const auto extra_len = ReadLe16(data, offset + 28);
  return offset + 30 + name_len + extra_len;
}

std::string InflateRawDeflate(const std::vector<unsigned char>& data,
                              std::size_t offset,
                              std::size_t compressed_size,
                              std::size_t uncompressed_size) {
  if (offset + compressed_size > data.size()) {
    throw std::runtime_error("Invalid ZIP archive: truncated deflate data");
  }
  if (compressed_size > std::numeric_limits<uInt>::max() ||
      uncompressed_size > std::numeric_limits<uInt>::max()) {
    throw std::runtime_error("Unsupported ZIP archive: entry is too large");
  }

  std::string output(uncompressed_size, '\0');
  z_stream stream{};
  stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data.data() + offset));
  stream.avail_in = static_cast<uInt>(compressed_size);
  stream.next_out = reinterpret_cast<Bytef*>(output.data());
  stream.avail_out = static_cast<uInt>(output.size());

  if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
    throw std::runtime_error("Unable to initialize ZIP decompressor");
  }

  const int rc = inflate(&stream, Z_FINISH);
  inflateEnd(&stream);
  if (rc != Z_STREAM_END || stream.total_out != uncompressed_size) {
    throw std::runtime_error("ZIP decompression failed");
  }

  return output;
}

}  // namespace

bool IsGzipPath(std::string_view path) {
  return HasSuffixCi(path, ".gz");
}

bool IsZipPath(std::string_view path) {
  return HasSuffixCi(path, ".zip");
}

std::string ReadZipEntry(const std::string& path, const std::string& requested_entry) {
  const auto data = ReadWholeFile(path);
  const auto entries = ReadCentralDirectory(data);
  const auto& entry = SelectEntry(entries, requested_entry);

  if ((entry.flags & 0x1) != 0) {
    throw std::runtime_error("Unsupported ZIP archive: encrypted entries are not supported");
  }

  const auto data_offset = ZipDataOffset(data, entry);
  std::string output;
  switch (entry.method) {
    case 0:
      output = ReadBytesAsString(data, data_offset, entry.uncompressed_size);
      break;
    case 8:
      output = InflateRawDeflate(data, data_offset, entry.compressed_size,
                                 entry.uncompressed_size);
      break;
    default:
      throw std::runtime_error("Unsupported ZIP compression method for entry: " + entry.name);
  }

  const auto actual_crc = crc32(0, reinterpret_cast<const Bytef*>(output.data()),
                               static_cast<uInt>(output.size()));
  if (actual_crc != entry.crc) {
    throw std::runtime_error("Invalid ZIP archive: CRC mismatch for entry: " + entry.name);
  }

  return output;
}

GzipStreambuf::GzipStreambuf(const std::string& path) {
  file_ = gzopen(path.c_str(), "rb");
  if (!file_) {
    throw std::runtime_error("Unable to open gzip file: " + path);
  }
  setg(buffer_.data(), buffer_.data(), buffer_.data());
}

GzipStreambuf::~GzipStreambuf() {
  if (file_) {
    gzclose(file_);
  }
}

GzipStreambuf::int_type GzipStreambuf::underflow() {
  if (!file_) {
    return traits_type::eof();
  }

  const int bytes = gzread(file_, buffer_.data(), static_cast<unsigned int>(buffer_.size()));
  if (bytes < 0) {
    int error_code = Z_OK;
    const char* message = gzerror(file_, &error_code);
    throw std::runtime_error(message ? message : "gzip decompression failed");
  }
  if (bytes == 0) {
    return traits_type::eof();
  }

  setg(buffer_.data(), buffer_.data(), buffer_.data() + bytes);
  return traits_type::to_int_type(*gptr());
}

GzipInputStream::GzipInputStream(const std::string& path)
    : std::istream(nullptr), buffer_(path) {
  rdbuf(&buffer_);
}

}  // namespace csvzall::pipeline::common
