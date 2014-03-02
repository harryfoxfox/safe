#ifndef __recent_path_store_hpp
#define __recent_path_store_hpp

#include <safe/logging.h>
#include <safe/parse.hpp>
#include <safe/util.hpp>

#include <encfs/fs/FsIO.h>

#include <algorithm>
#include <memory>
#include <string>

#include <climits>
#include <cstdlib>

namespace safe {

class RecentlyUsedPathsParseError : public std::runtime_error {
public:
  RecentlyUsedPathsParseError(const char *msg) : std::runtime_error(msg) {}
  RecentlyUsedPathsParseError(const std::string & msg) : std::runtime_error(msg) {}
};

namespace _rps_int {

typedef uint8_t byte;

class FileStream {
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
  FileStream(encfs::File f)
    : _f(std::move(f))
    , _off(0)
    , _idx(0)
    , _buf_size(0) {}

  opt::optional<byte>
  peek() const {
    _fill_buf_if_necessary();
    // EOF
    if (!_buf_size) return opt::nullopt;
    return _buf[_idx];
  }

  void
  skip() {
    _fill_buf_if_necessary();
    // EOF
    if (!_buf_size) return;
    _idx += 1;
  }
};

inline
encfs::fs_off_t
writestringterm(encfs::File &f, encfs::fs_off_t off, const std::string &s, encfs::byte term) {
  f.write(off, (encfs::byte *) s.data(), s.size());
  off += s.size();
  f.write(off, &term, 1);
  return off + 1;
}

// NB: we have to define our own version of to_string() since old versions
//     of mingw don't include it
inline
std::string
to_string(unsigned long t) {
  static_assert(sizeof(t) <= 8, "unsigned long is too big");
  char buf[21]; // log10(2**64-1) => 19.xxx
  auto num_chars = snprintf(buf, sizeof(buf), "%lu", t);
  return std::string(buf, num_chars);
}

}

class RecentlyUsedByteStringStoreV1 {
public:
  typedef uint8_t byte_t;
  typedef std::vector<byte_t> ByteString;
  typedef std::vector<ByteString>::size_type max_ent_t;

private:
  typedef unsigned long rev_t;

  static constexpr auto MAGIC = "RecentlyUsedByteStringStoreV1";
  static const auto MAX_LINE_SIZE = (size_t) 1024;
  static const auto MAX_NUM_SIZE = (size_t) 20;
  static const auto MAX_PATH_SIZE = (size_t) 1024;

  std::shared_ptr<encfs::FsIO> _fs;
  encfs::Path _storage_file;
  std::string _tag;
  max_ent_t _num_paths;
  rev_t _last_rev;
  std::vector<ByteString> _path_cache;

  std::pair<_rps_int::FileStream, rev_t>
  _open_file_and_read_rev() {
    auto fit = _rps_int::FileStream(_fs->openfile(_storage_file));

    auto magic = read_string(fit, '\n', strlen(MAGIC));
    if (magic != MAGIC) {
      throw RecentlyUsedPathsParseError("BAD MAGIC: " + magic);
    }
    expect(fit, '\n');

    auto tag = read_string(fit, '\n', _tag.size());
    if (tag != _tag) {
      throw RecentlyUsedPathsParseError("BAD tag: " + tag);
    }
    expect(fit, '\n');

    auto rev = parse_ascii_integer<rev_t>(fit);
    expect(fit, '\n');

    return std::make_pair(std::move(fit), rev);
  }

  std::pair<rev_t, std::vector<ByteString>>
  _load_file() {
    rev_t rev;
    opt::optional<_rps_int::FileStream> maybeFit;
    try {
      std::tie(maybeFit, rev) = _open_file_and_read_rev();
    }
    catch (const std::system_error &err) {
      if (err.code() != std::errc::no_such_file_or_directory) throw;
      return std::make_pair(0, std::vector<ByteString>());
    }

    auto & fit = *maybeFit;

    auto nentries = parse_ascii_integer<max_ent_t>(fit);
    if (nentries > _num_paths) {
        lbx_log_warning("More entries than requested in file, going to skip some: %lu vs %lu",
                        (long unsigned) nentries, (long unsigned) _num_paths);
    }
    expect(fit, '\n');

    std::vector<ByteString> path_list;
    for (decltype(_num_paths) idx = 0; idx < _num_paths; ++idx) {
      if (!fit.peek()) break;

      auto str_size = parse_ascii_integer<size_t>(fit);
      if (str_size >= MAX_PATH_SIZE) {
        throw RecentlyUsedPathsParseError("path too large!");
      }
      expect(fit, '\n');

      auto path = read_token_vector<byte_t>(fit, opt::nullopt, opt::make_optional(str_size));
      path_list.push_back(std::move(path));
      expect(fit, '\n');
    }

    return std::make_pair(rev, std::move(path_list));
  }

  rev_t
  _save_file(rev_t rev, std::vector<ByteString> paths) {
    // check magic + rev first
    if (encfs::file_exists(_fs, _storage_file)) {
      auto read_rev = std::get<1>(_open_file_and_read_rev());
      if (read_rev != rev) throw std::runtime_error("File rev has changeD!");
    }

    auto new_rev = rev + 1;

    auto f = _fs->openfile(_storage_file, true, true);

    encfs::fs_off_t off = 0;

    off = _rps_int::writestringterm(f, off, MAGIC, '\n');
    off = _rps_int::writestringterm(f, off, _tag, '\n');
    off = _rps_int::writestringterm(f, off, _rps_int::to_string(new_rev), '\n');
    off = _rps_int::writestringterm(f, off, _rps_int::to_string(paths.size()), '\n');

    for (const auto & path : paths) {
      auto pstring = std::string(path.begin(), path.end());
      off = _rps_int::writestringterm(f, off, _rps_int::to_string(pstring.size()), '\n');
      off = _rps_int::writestringterm(f, off, pstring, '\n');
    }

    f.truncate(off);

    return new_rev;
  }

  void
  _update_file() {
    _last_rev = _save_file(_last_rev, _path_cache);
  }

  const std::vector<ByteString> &
  _recently_used_paths() const {
    return _path_cache;
  }

public:
  RecentlyUsedByteStringStoreV1(std::shared_ptr<encfs::FsIO> fs,
                                encfs::Path storage_file,
                                std::string tag,
                                max_ent_t num_paths)
    : _fs(std::move(fs))
    , _storage_file(std::move(storage_file))
    , _tag(std::move(tag))
    , _num_paths(num_paths) {
    std::tie(_last_rev, _path_cache) = _load_file();
  }

  // move is okay
  RecentlyUsedByteStringStoreV1(RecentlyUsedByteStringStoreV1 &&) = default;
  RecentlyUsedByteStringStoreV1 &operator=(RecentlyUsedByteStringStoreV1 &&) = default;

  template <typename Pred>
  void
  remove_and_add(Pred pred, ByteString bs) {
    _path_cache.erase(std::remove_if(_path_cache.begin(), _path_cache.end(), pred),
                      _path_cache.end());
    _path_cache.insert(_path_cache.begin(), std::move(bs));
    _update_file();
  }

  void
  clear() {
    _path_cache.clear();
    _update_file();
  }

  bool
  empty() const {
    return _recently_used_paths().empty();
  }

  const ByteString &
  front() const {
    return _recently_used_paths().front();
  }

  const ByteString &
  operator[](max_ent_t n) {
    return _recently_used_paths()[n];
  }

  max_ent_t
  size() const {
    return _recently_used_paths().size();
  }

  std::vector<ByteString>::const_iterator
  begin() const {
    return _recently_used_paths().begin();
  }

  std::vector<ByteString>::const_iterator
  end() const {
    return _recently_used_paths().end();
  }
};

template <typename PathSerializer>
class RecentlyUsedPathStore {
  typedef decltype(std::declval<PathSerializer>().deserialize(RecentlyUsedByteStringStoreV1::ByteString())) PathResolver;

  RecentlyUsedByteStringStoreV1 _byte_string_store;
  PathSerializer _serializer;

  class FromByteString {
    PathSerializer _serializer;

  public:
    FromByteString(PathSerializer serializer)
    : _serializer(std::move(serializer)) {}

    PathResolver
    operator()(const RecentlyUsedByteStringStoreV1::ByteString & bs) const {
      return _serializer.deserialize(bs);
    }
  };

public:
  RecentlyUsedPathStore(std::shared_ptr<encfs::FsIO> fs,
                        encfs::Path storage_file,
                        std::string tag,
                        RecentlyUsedByteStringStoreV1::max_ent_t num_paths,
                        PathSerializer serializer = PathSerializer())
  : _byte_string_store(std::move(fs), std::move(storage_file),
                       std::move(tag), std::move(num_paths))
  , _serializer(std::move(serializer)) {}

  // this method is supposed to be heavyweight / do some IO
  void
  use_path(const encfs::Path & path) {
    _byte_string_store.remove_and_add([&] (const RecentlyUsedByteStringStoreV1::ByteString & bs) {
      try {
        // TODO: equating paths isn't great, it would be better to see if both paths
        //       map to same location
        return std::get<0>(_serializer.deserialize(bs).resolve_path()) == path;
      }
      catch (const std::exception & err) {
        lbx_log_error("Error while resolving/deserializing: %s", err.what());
        return _serializer.deserialize(bs).get_last_known_path() == path;;
      }
    }, _serializer.serialize(path));
  }

  void
  clear() {
    return _byte_string_store.clear();
  }

  bool
  empty() const {
    return _byte_string_store.empty();
  }

  PathResolver
  front() const {
    return _serializer.deserialize(_byte_string_store.front());
  }

  PathResolver
  operator[](RecentlyUsedByteStringStoreV1::max_ent_t n) {
    return _serializer.deserialize(_byte_string_store[n]);
  }

  RecentlyUsedByteStringStoreV1::max_ent_t
  size() const {
    return _byte_string_store.size();
  }

  decltype(range_map(FromByteString(std::declval<PathSerializer>()), std::declval<RecentlyUsedByteStringStoreV1>()).begin())
  begin() const {
    return range_map(FromByteString(_serializer), _byte_string_store).begin();
  }

  decltype(range_map(FromByteString(std::declval<PathSerializer>()), std::declval<RecentlyUsedByteStringStoreV1>()).end())
  end() const {
    return range_map(FromByteString(_serializer), _byte_string_store).end();
  }
};

class DefaultPathSerializer {
  std::shared_ptr<encfs::FsIO> _fs;

  class DefaultPathResolver {
    encfs::Path _p;

  public:
    DefaultPathResolver(encfs::Path p)
    : _p(std::move(p)) {}

    encfs::Path
    get_last_known_path() const { return _p; }

    std::pair<encfs::Path, bool>
    resolve_path() const { return std::make_pair(_p, false); }

    std::string
    get_last_known_name() const { return _p.basename(); }
  };

public:
  DefaultPathSerializer(std::shared_ptr<encfs::FsIO> fs)
  : _fs(std::move(fs)) {}

  DefaultPathResolver
  deserialize(const RecentlyUsedByteStringStoreV1::ByteString & bs) const {
    return DefaultPathResolver(_fs->pathFromString(std::string(bs.begin(), bs.end())));
  }

  RecentlyUsedByteStringStoreV1::ByteString
  serialize(const encfs::Path &path) const {
    const std::string & s = path;
    return RecentlyUsedByteStringStoreV1::ByteString(s.begin(), s.end());
  }
};

class RecentlyUsedPathStoreV1 : public RecentlyUsedPathStore<DefaultPathSerializer> {
public:
  RecentlyUsedPathStoreV1(std::shared_ptr<encfs::FsIO> fs,
                          encfs::Path storage_file,
                          RecentlyUsedByteStringStoreV1::max_ent_t num_paths)
  : RecentlyUsedPathStore(fs,
                          std::move(storage_file),
                          "RecentlyUsedPathStoreV1",
                          num_paths,
                          DefaultPathSerializer(fs))
  {}
};

}

#endif
