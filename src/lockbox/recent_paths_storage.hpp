#ifndef __recent_path_store_hpp
#define __recent_path_store_hpp

#include <lockbox/logging.h>

#include <encfs/fs/FsIO.h>

#include <algorithm>
#include <memory>
#include <string>

namespace lockbox {

static const auto RECENTLY_USED_PATHS_V1_FILE_NAME = std::string("RecentlyUsedPathsV1.db");

class RecentlyUsedPathsParseError : public std::runtime_error {
public:
  RecentlyUsedPathsParseError(const char *msg) : std::runtime_error(msg) {}
  RecentlyUsedPathsParseError(const std::string & msg) : std::runtime_error(msg) {}
};

namespace _rps_int {

// TODO: this would be better as native C++ iterator
class FileReader {
  static const size_t BUFFER_SIZE = 4096;
  encfs::File _f;
  mutable encfs::fs_off_t _off;
  mutable size_t _idx;
  mutable size_t _buf_size;
  mutable encfs::byte _buf[BUFFER_SIZE];

  void
  _fill_buf_if_necessary() const {
    if (_idx == _buf_size) {
        _buf_size = _f.read(_off, _buf, sizeof(_buf));
        _off += _buf_size;
        _idx = 0;
    }
  }

public:
  FileReader(encfs::File f)
    : _f(std::move(f))
    , _off(0)
    , _idx(0)
    , _buf_size(0) {}

  opt::optional<encfs::byte>
  peekb() const {
    _fill_buf_if_necessary();
    // EOF
    if (!_buf_size) return opt::nullopt;
    return _buf[_idx];
  }

  opt::optional<encfs::byte>
  getb() {
    _fill_buf_if_necessary();
    // EOF
    if (!_buf_size) return opt::nullopt;
    return _buf[_idx++];
  }
};

std::string
read_string(FileReader &f, opt::optional<encfs::byte> term, size_t max_amt) {
  std::vector<char> toret;

  for (size_t i = 0; i < max_amt + (term ? 1 : 0); ++i) {
    auto mB = f.getb();
    if (!mB) throw RecentlyUsedPathsParseError("premature EOF");
    if (term && *mB == *term) break;
    else if (i == max_amt) throw RecentlyUsedPathsParseError("String too large!");
    toret.push_back((char) *mB);
  }

  return std::string(toret.begin(), toret.end());
}

encfs::fs_off_t
writestringterm(encfs::File &f, encfs::fs_off_t off, const std::string &s, encfs::byte term) {
  f.write(off, (encfs::byte *) s.data(), s.size());
  off += s.size();
  f.write(off, &term, 1);
  return off + 1;
}

}

class RecentlyUsedPathStoreV1 {
public:
  typedef std::vector<encfs::Path>::size_type max_ent_t;

private:
  typedef unsigned long rev_t;

  static constexpr auto MAGIC = "RecentlyUsedPathStoreV1";
  static const auto MAX_NUM_SIZE = (size_t) 20;
  static const auto MAX_PATH_SIZE = (size_t) 1024;

  std::shared_ptr<encfs::FsIO> _fs;
  encfs::Path _storage_file;
  unsigned long _num_paths;
  rev_t _last_rev;
  std::vector<encfs::Path> _path_cache;

  static
  unsigned long
  _read_unsigned_int(_rps_int::FileReader & fit) {
      auto str = _rps_int::read_string(fit, '\n', RecentlyUsedPathStoreV1::MAX_NUM_SIZE);
      try {
          return std::stoul(str);
      }
      catch (const std::logic_error & err) {
          throw RecentlyUsedPathsParseError(err.what());
      }
      catch (const std::out_of_range & err) {
          throw RecentlyUsedPathsParseError(err.what());
      }
  }

  static
  std::pair<rev_t, std::vector<encfs::Path>>
  _load_file(std::shared_ptr<encfs::FsIO> fs, const encfs::Path & storage_path, max_ent_t maxents) {
    opt::optional<_rps_int::FileReader> maybeFit;
    try {
      maybeFit = _rps_int::FileReader(fs->openfile(storage_path));
    }
    catch (const std::system_error &err) {
      if (err.code() != std::errc::no_such_file_or_directory) throw;
      return std::make_pair(0, std::vector<encfs::Path>());
    }

    auto fit = std::move(*maybeFit);

    auto magic = _rps_int::read_string(fit, '\n', strlen(RecentlyUsedPathStoreV1::MAGIC));
    if (magic != RecentlyUsedPathStoreV1::MAGIC) {
      throw RecentlyUsedPathsParseError("BAD MAGIC: " + magic);
    }

    auto rev = (rev_t) _read_unsigned_int(fit);

    auto nentries = (max_ent_t) _read_unsigned_int(fit);
    if (nentries > maxents) {
        lbx_log_warning("More entries than requested in file, going to skip some: %lu vs %lu",
                        nentries, maxents);
    }

    std::vector<encfs::Path> path_list;
    for (decltype(maxents) idx = 0; idx < maxents; ++idx) {
      if (!fit.peekb()) break;

      auto str_size = _read_unsigned_int(fit);
      if (str_size >= RecentlyUsedPathStoreV1::MAX_PATH_SIZE) {
        throw RecentlyUsedPathsParseError("path too large!");
      }
      auto path = _rps_int::read_string(fit, opt::nullopt, str_size);
      path_list.push_back(fs->pathFromString(std::move(path)));
      auto nextB = fit.getb();
      if (!nextB || *nextB != '\n') throw RecentlyUsedPathsParseError("path didn't end with newline!");
    }

    return std::make_pair(rev, std::move(path_list));
  }

  rev_t
  _save_file(std::shared_ptr<encfs::FsIO> fs, const encfs::Path & storage_path,
             rev_t rev, std::vector<encfs::Path> paths) {
    // check magic + rev first
    if (encfs::file_exists(fs, storage_path)) {
      auto fit = _rps_int::FileReader(fs->openfile(storage_path));

      auto magic = _rps_int::read_string(fit, '\n', strlen(RecentlyUsedPathStoreV1::MAGIC));
      if (magic != RecentlyUsedPathStoreV1::MAGIC) {
        throw std::runtime_error("BAD MAGIC: " + magic);
      }

      auto read_rev = std::stoul(_rps_int::read_string(fit, '\n', RecentlyUsedPathStoreV1::MAX_NUM_SIZE));
      if (read_rev != rev) throw std::runtime_error("File rev has changeD!");
    }

    auto new_rev = rev + 1;

    auto f = _fs->openfile(_storage_file, true, true);

    encfs::fs_off_t off = 0;

    off = _rps_int::writestringterm(f, off, RecentlyUsedPathStoreV1::MAGIC, '\n');
    off = _rps_int::writestringterm(f, off, std::to_string(new_rev), '\n');
    off = _rps_int::writestringterm(f, off, std::to_string(paths.size()), '\n');

    for (const auto & path : paths) {
      auto pstring = std::string(path);
      off = _rps_int::writestringterm(f, off, std::to_string(pstring.size()), '\n');
      off = _rps_int::writestringterm(f, off, pstring, '\n');
    }

    return new_rev;
  }

  void
  _update_file() {
    _last_rev = _save_file(_fs, _storage_file, _last_rev, _path_cache);
  }

public:
  RecentlyUsedPathStoreV1(std::shared_ptr<encfs::FsIO> fs,
                          encfs::Path storage_file,
                          max_ent_t num_paths)
    : _fs(std::move(fs))
    , _storage_file(std::move(storage_file))
    , _num_paths(num_paths) {
    std::tie(_last_rev, _path_cache) = _load_file(_fs, _storage_file, _num_paths);
  }

  void
  use_path(encfs::Path p) {
    _path_cache.erase(std::remove(_path_cache.begin(), _path_cache.end(), p),
                      _path_cache.end());
    _path_cache.insert(_path_cache.begin(), std::move(p));
    _update_file();
  }

  const std::vector<encfs::Path> &
  recently_used_paths() const {
    return _path_cache;
  }

  void
  clear() {
    _path_cache.clear();
    _update_file();
  }
};

}

#endif
