//
//  mac_mount.mm
//  Safe
//
//  Created by Rian Hunter on 11/7/13.
//  Copyright (c) 2013 Rian Hunter. All rights reserved.
//

#import <safe/mac/mount.hpp>

#import <safe/mac/shared_file_list.hpp>

#import <safe/mount_common.hpp>
#import <safe/util.hpp>
#import <safe/webdav_server.hpp>

#import <mount_webdav_interpose/mount_webdav_interpose.h>

#import <encfs/base/optional.h>
#import <encfs/fs/FsIO.h>

#import <davfuse/util_sockets.h>
#import <davfuse/webdav_server.h>

#import <Cocoa/Cocoa.h>

#import <memory>
#import <random>
#import <sstream>
#import <unordered_map>

#import <ctime>

#import <fcntl.h>
#import <libgen.h>
#import <semaphore.h>

#import <sys/mman.h>
#import <sys/mount.h>
#import <sys/param.h>
#import <sys/stat.h>

namespace safe { namespace mac {

class MountEvent {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool msg_sent;
    enum {
        EVENT_TYPE_SUCCESS,
        EVENT_TYPE_FAIL,
        EVENT_TYPE_EXCEPTION,
    } event_type;
    port_t listen_port;
    opt::optional<WebdavServerHandle> ws;
    std::exception_ptr eptr;
    
    decltype(safe::create_deferred(pthread_mutex_unlock, (pthread_mutex_t *)nullptr))
    _get_mutex_guard() {
        auto ret_lock = pthread_mutex_lock(&mutex);
        if (ret_lock) throw std::runtime_error("pthread_mutex_lock");
        
        return safe::create_deferred(pthread_mutex_unlock, &mutex);
    }
    
    template <class F>
    void
    _receive_event(F f) {
        auto mutex_guard = _get_mutex_guard();
        if (msg_sent) throw std::runtime_error("Message already sent!");
        f();
        msg_sent = true;
        pthread_cond_signal(&cond);
    }

public:
    MountEvent()
    : mutex(PTHREAD_MUTEX_INITIALIZER)
    , cond(PTHREAD_COND_INITIALIZER)
    , msg_sent(false) {}
    
    void
    set_mount_success(port_t listen_port_, WebdavServerHandle ws_) {
        _receive_event([&] () {
            event_type = EVENT_TYPE_SUCCESS;
            listen_port = listen_port_;
            ws = std::move(ws_);
        });
    }
    
    void
    set_mount_fail() {
        _receive_event([&] {
            event_type = EVENT_TYPE_FAIL;
        });
    }

    void
    set_mount_exception(std::exception_ptr eptr_) {
        _receive_event([&] {
            event_type = EVENT_TYPE_EXCEPTION;
            eptr = std::move(eptr_);
        });
    }

    void
    set_thread_done() {
    }
    
    opt::optional<std::pair<port_t, WebdavServerHandle>>
    wait_for_mount_event() {
        auto mutex_guard = _get_mutex_guard();
        
        while (!msg_sent) {
            auto ret = pthread_cond_wait(&cond, &mutex);
            if (ret) throw std::runtime_error("pthread_cond_wait");
        }
        
        switch (event_type) {
            case EVENT_TYPE_SUCCESS:
                return opt::make_optional(std::make_pair(std::move(listen_port), std::move(*ws)));
            case EVENT_TYPE_FAIL:
                return opt::nullopt;
            case EVENT_TYPE_EXCEPTION:
                std::rethrow_exception(eptr);
            default:
                /* notreached */
                assert(false);
                return opt::nullopt;
        }
    }
};

static
std::string
escape_double_quotes(std::string mount_name) {
    return safe::escape_double_quotes(std::move(mount_name));
}

static
std::string
webdav_mount_url(port_t listen_port, std::string name) {
    // NB: use "127.0.0.1" here instead of "localhost"
    //     windows prefers ipv6 by default and we aren't
    //     listening on ipv6, so that will slow down connections
    // NB: we do this on mac too since the common code now
    //     uses "127.0.0.1"
    return std::string("http://127.0.0.1:") + std::to_string(listen_port) + "/" + name + "/";
}

void
MountDetails::signal_stop() const {
    ws.signal_stop();
}

void
MountDetails::disconnect_clients() const {
    ws.signal_disconnect_all_clients();
}

void
MountDetails::wait_until_stopped() const {
}

static
NSURL *
url_from_string_path(const std::string & path) {
    NSString *path2 = [NSFileManager.defaultManager
                       stringWithFileSystemRepresentation:path.data()
                       length:path.size()];
    return [NSURL fileURLWithPath:path2];
}

void
MountDetails::unmount() {
    if (!is_mounted) throw std::runtime_error("isn't mounted!");
    
    bool USE_COMMAND_LINE = true;
    if (USE_COMMAND_LINE) {
        std::ostringstream os;
        os << "umount \"" << escape_double_quotes(this->mount_point) << "\"";
        auto ret = system(os.str().c_str());
        if (ret) throw std::runtime_error("couldn't unmount");
    }
    else {
        // NB: for some reason this is much slower
        NSURL *path_url = url_from_string_path(this->mount_point);
        NSError *err;
        BOOL unmounted =
        [NSWorkspace.sharedWorkspace unmountAndEjectDeviceAtURL:path_url error:&err];
        if (!unmounted) {
            throw std::runtime_error(std::string("couldn't unmount: ") + err.localizedDescription.UTF8String);
        }
    }
    
    is_mounted = false;
}

void
MountDetails::open_mount() const {
    if (!is_mounted) throw std::runtime_error("isn't mounted!");
    NSURL *path_url = url_from_string_path(this->mount_point);
    BOOL opened =
    [NSWorkspace.sharedWorkspace openURL:path_url];
    if (!opened) throw std::runtime_error("couldn't open mount");
}

static
std::string
get_mount_device(std::string mount_point) {
    struct statfs b;
    int ret = statfs(mount_point.c_str(), &b);
    if (ret < 0) throw std::system_error(errno, std::generic_category());
    return b.f_mntfromname;
}

bool
MountDetails::is_still_mounted() const {
    try {
        return get_mount_device(mount_point) == webdav_mount_url(listen_port, name);
    }
    catch (const std::system_error & err) {
        if (err.code() == std::errc::no_such_file_or_directory) return false;
        throw;
    }
}

template <typename T>
class Guard {
    T &_ref;
public:
    Guard(const Guard &) = delete;
    Guard(Guard &&) = delete;
    
    Guard(T & f) : _ref(f) { _ref.lock(); }
    ~Guard() { _ref.unlock(); }
};

typedef std::unordered_map<std::string, std::pair<size_t, std::shared_ptr<pthread_mutex_t>>> SystemGlobalMutexMap;

class SystemGlobalMutex {
    // implemented via flock on files in /tmp/safe
    static pthread_mutex_t _g_name_map_mutex;
    static SystemGlobalMutexMap _g_name_map;
    
    std::shared_ptr<pthread_mutex_t> _mutex;
    int _fd;
    std::string _name;
    
public:
    SystemGlobalMutex(std::string name) : _fd(-1), _name(name) {
        int ret = pthread_mutex_lock(&_g_name_map_mutex);
        if (ret) throw std::runtime_error("pthread_mutex_lock");
        auto _unlock_mutex = safe::create_deferred(pthread_mutex_unlock, &_g_name_map_mutex);
        
        std::ostringstream os;
        os << "/tmp/.safe-lock-" << name;
        auto str = os.str();
        
        _fd = open(str.c_str(), O_CREAT | O_RDONLY, 0777);
        if (_fd < 0) throw std::runtime_error(std::string("failed open") + strerror(errno));
        
        if (!_g_name_map.count(name)) {
            _g_name_map[name] = std::make_pair(0, std::shared_ptr<pthread_mutex_t>(new pthread_mutex_t(PTHREAD_MUTEX_INITIALIZER)));
        }
        
        auto & p = _g_name_map[name];
        p.first += 1;
        _mutex = p.second;
    }
    
    ~SystemGlobalMutex() {
        if (_fd < 0) return;
        
        int ret = close(_fd);
        if (ret < 0) log_error("Error while closing fd: %d, leaking...", _fd);
        
        int ret2 = pthread_mutex_lock(&_g_name_map_mutex);
        if (ret2) throw std::runtime_error("pthread_mutex_lock");
        auto _unlock_mutex = safe::create_deferred(pthread_mutex_unlock, &_g_name_map_mutex);
        
        auto & p = _g_name_map[_name];
        p.first -= 1;
        if (!p.first) _g_name_map.erase(_name);
    }
    
    void
    lock() {
        int ret2 = flock(_fd, LOCK_EX);
        if (ret2 < 0) throw std::runtime_error("couldn't lock");
        
        int ret = pthread_mutex_lock(_mutex.get());
        if (ret) {
            int ret3 = flock(_fd, LOCK_UN);
            if (ret3 < 0) abort();
            throw std::runtime_error("couldn't lock");
        }
    }
    
    void
    unlock() {
        int ret1 = pthread_mutex_unlock(_mutex.get());
        if (ret1) throw std::runtime_error("couldn't unlock");
        
        auto ret2 = flock(_fd, LOCK_UN);
        if (ret2 < 0) {
            int ret3 = pthread_mutex_lock(_mutex.get());
            if (ret3) abort();
            throw std::runtime_error("couldn't unlock");
        }
    }
};

pthread_mutex_t SystemGlobalMutex::_g_name_map_mutex = PTHREAD_MUTEX_INITIALIZER;
SystemGlobalMutexMap SystemGlobalMutex::_g_name_map = SystemGlobalMutexMap();

static
SystemGlobalMutex
get_ramdisk_mutex() {
    const char *const SEM_SUFFIX = "-safe-ramdisk-sem";
    std::ostringstream os;
    os << geteuid() << SEM_SUFFIX;
    return SystemGlobalMutex(os.str());
}

const char *const SHM_SUFFIX = "-safe-ramdisk-shm";

class RamDiskHandle {
    typedef uint64_t io_registry_entry_id_t;
    
    typedef struct {
        io_registry_entry_id_t entry_id;
        bool is_set;
    } SharedStorage;
    
    SharedStorage *_mapping;
    
public:
    RamDiskHandle(RamDiskHandle && rdh) {
        _mapping = rdh._mapping;
        rdh._mapping = nullptr;
    }
    
    RamDiskHandle &operator=(RamDiskHandle && rdh) {
        this->~RamDiskHandle();
        new (this) RamDiskHandle(std::move(rdh));
        return *this;
    }
    
    RamDiskHandle() : _mapping((SharedStorage *) MAP_FAILED) {
        std::ostringstream os;
        os << "/" << geteuid() << SHM_SUFFIX;
        auto str = os.str();
        
        bool new_shm = false;
        int shm_fd = shm_open(str.c_str(), O_EXCL | O_CREAT | O_RDWR, 0777);
        if (shm_fd < 0) {
            if (EEXIST == errno) {
                shm_fd = shm_open(str.c_str(), O_CREAT | O_RDWR, 0777);
            }
        }
        else new_shm = true;
        
        if (shm_fd < 0) {
            throw std::runtime_error(std::string("failed shm_open") + strerror(errno));
        }
        
        auto _close_fd = safe::create_deferred(close, shm_fd);
        
        if (new_shm) {
            auto ret = ftruncate(shm_fd, sizeof(SharedStorage));
            if (ret < 0) throw std::runtime_error(std::string("failed truncate ") + strerror(errno));
        }
        
        _mapping = (SharedStorage *) mmap(0, sizeof(SharedStorage), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        if (MAP_FAILED == _mapping) throw std::runtime_error(std::string("failed truncate") + strerror(errno));
    }
    
    ~RamDiskHandle() {
        if (_mapping != MAP_FAILED) {
            int ret = munmap(_mapping, MAXPATHLEN);
            if (ret < 0) lbx_log_error("failed to unmoun memory: %p, leaking...", _mapping);
        }
    }
    
    opt::optional<io_registry_entry_id_t>
    io_registry_entry() {
        return _mapping->is_set ? opt::make_optional(_mapping->entry_id) : opt::nullopt;
    }
    
    void
    set_io_registry_entry(io_registry_entry_id_t entry_id) {
        _mapping->is_set = true;
        _mapping->entry_id = entry_id;
    }
    
    opt::optional<std::string>
    bsd_name() {
        auto maybe_entry_id = io_registry_entry();
        if (!maybe_entry_id) return opt::nullopt;
        
        auto entry_id = *maybe_entry_id;
        CFMutableDictionaryRef matching_dict = IORegistryEntryIDMatching(entry_id);
        if (!matching_dict) throw std::runtime_error("IORegistryEntryIDMatching");
        // IOServiceGetMatchingService will consume the matching_dict reference
        
        io_service_t disk_service = IOServiceGetMatchingService(kIOMasterPortDefault, matching_dict);
        if (disk_service == IO_OBJECT_NULL) return opt::nullopt;
        auto _free_disk_service = safe::create_deferred(IOObjectRelease, disk_service);
        
        CFTypeRef prop = IORegistryEntryCreateCFProperty(disk_service, (CFStringRef) @"BSD Name", kCFAllocatorDefault, 0);
        auto _free_prop = safe::create_deferred(CFRelease, prop);
        
        if (CFGetTypeID(prop) != CFStringGetTypeID()) throw std::runtime_error("Bad Property Type");
        
        return std::string([(__bridge NSString *) prop UTF8String]);
    }
    
    std::string
    set_bsd_name(std::string a) {
        // first get matching dict for bsd name
        CFMutableDictionaryRef matching_dict = IOBSDNameMatching(kIOMasterPortDefault, 0, a.c_str());
        if (!matching_dict) throw std::runtime_error("IOBSDNameMatching");
        // IOServiceGetMatchingService will consume the matching_dict reference
        
        // then use matching dict to get io_service_t for disk
        io_service_t disk_service = IOServiceGetMatchingService(kIOMasterPortDefault, matching_dict);
        if (disk_service == IO_OBJECT_NULL) throw std::runtime_error("IOServiceGetMatchingService");
        auto _free_disk_service = safe::create_deferred(IOObjectRelease, disk_service);
        
        // then get the registry id for the io_service_ta
        io_registry_entry_id_t io_reg_key;
        kern_return_t ret = IORegistryEntryGetRegistryEntryID(disk_service, &io_reg_key);
        if (ret) throw std::runtime_error("IORegisteryEntryGetRegistryEntryID");
        
        // save the registry id for later
        set_io_registry_entry(io_reg_key);
        
        return a;
    }
};

static
RamDiskHandle
get_ram_disk_handle() {
    return RamDiskHandle();
}

const char *const DEV_PREFIX = "/dev/";
    
static
std::string
get_ram_disk_bsd_name() {
    auto ram_disk_handle = get_ram_disk_handle();
    
    auto cur_bsd_name = ram_disk_handle.bsd_name();
    if (cur_bsd_name) return *cur_bsd_name;
    
    // create ramdisk
    // NB: we specify full path to hdiutil since we have to trust
    //     the information that is returned from it
    auto f = popen("/usr/bin/hdiutil attach -nomount ram://8388608", "r");
    if (!f) throw std::runtime_error("hdiutil fail");
    auto _close_open = safe::create_deferred(pclose, f);
    
    char device_name[MAXPATHLEN + 1];
    auto amt_read = fread(device_name,
                          sizeof(device_name[0]),
                          sizeof(device_name) / sizeof(device_name[0]),
                          f);
    if (!amt_read || !feof(f)) throw std::runtime_error("didn't get path!");
    
    int new_len;
    for (new_len = 0; device_name[new_len] && !::isspace(device_name[new_len]); ++new_len);
    
    auto toret = std::string(&device_name[0], new_len);
    
    return ram_disk_handle.set_bsd_name(toret.substr(strlen(DEV_PREFIX)));
}

static
std::pair<std::string, std::string>
ensure_ramdisk() {
    auto ramdisk_global_mutex = get_ramdisk_mutex();
    Guard<decltype(ramdisk_global_mutex)> _m_guard(ramdisk_global_mutex);
    
    // get ram disk device
    auto ram_disk_device = DEV_PREFIX + get_ram_disk_bsd_name();
    
    // iterate over all current mounts, see if any of them are
    // mounted by this device
    for (NSURL *mount_point_url in [NSFileManager.defaultManager
                                    mountedVolumeURLsIncludingResourceValuesForKeys:nil
                                    options:0]) {
        auto ram_disk_mount_point = std::string([[mount_point_url path] fileSystemRepresentation]);
        
        auto device_of_mount = get_mount_device(ram_disk_mount_point);
        if (device_of_mount != ram_disk_device) continue;
        
        return std::make_pair(ram_disk_mount_point, ram_disk_device);
    }
    
    // device isn't mounted, mount it and return
    
    // create mount point
    char buf[] = "/Volumes/.safe-ramdisk-mount-XXXXXX";
    char *ret = mkdtemp(buf);
    if (!ret) throw std::runtime_error("fail mkdtemp");
    auto ram_disk_mount_point = std::string(ret);
    
    std::ostringstream os2;
    os2 << "newfs_hfs -v SafeRamDisk " << ram_disk_device;
    auto str = os2.str();
    auto sys_ret = system(str.c_str());
    if (sys_ret) throw std::runtime_error("formatting disk failed");
    
    std::ostringstream os3;
    os3 << "diskutil mount -mountPoint " << ram_disk_mount_point << " " << ram_disk_device;
    auto str2 = os3.str();
    auto sys_ret2 = system(str2.c_str());
    if (sys_ret2) throw std::runtime_error("mounting disk failed");

    // remove ramdisk from favorites menu
    NSURL *mount_url = [NSURL fileURLWithPath:[NSFileManager.defaultManager
                                               stringWithFileSystemRepresentation:buf
                                               length:strlen(buf)]
                                  isDirectory:YES];
    auto was_removed = remove_url_from_shared_file_list(kLSSharedFileListFavoriteVolumes, mount_url);
    lbx_log_debug("RAMDisk was %sremoved from favorites", was_removed ? "" : "NOT ");
    
    // ramdisk is mounted by this point :)
    return std::make_pair(ram_disk_mount_point, ram_disk_device);
}

static
void *
mount_thread_fn(void *p) {
    // TODO: catch all exceptions since this is a top-level
    auto params = std::unique_ptr<ServerThreadParams<MountEvent>>((ServerThreadParams<MountEvent> *) p);
    mount_thread_fn(std::move(params));
    return NULL;
}

MountDetails
mount_new_encfs_drive(const std::shared_ptr<encfs::FsIO> & native_fs,
                      const encfs::Path & encrypted_container_path,
                      const encfs::EncfsConfig & cfg,
                      const encfs::SecureMem & password) {
    auto event = std::make_shared<MountEvent>();
    
    // TODO: perhaps make this an argument
    auto mount_name = encrypted_container_path.basename();
    
    // create thread details
    auto thread_params = new ServerThreadParams<MountEvent> {
        event,
        native_fs,
        encrypted_container_path,
        cfg,
        password,
        mount_name,
    };
    
    // start thread
    pthread_t thread;
    pthread_attr_t 	stackSizeAttribute;
    auto ret_attr_init = pthread_attr_init (&stackSizeAttribute);
    if (ret_attr_init) throw std::runtime_error("couldn't init attr");
    
    /* Get the default value */
    size_t stackSize = 0;
    auto ret_attr_getstacksize = pthread_attr_getstacksize(&stackSizeAttribute, &stackSize);
    if (ret_attr_getstacksize) throw std::runtime_error("couldn't get stack size");
    
    /* If the default size does not fit our needs, set the attribute with our required value */
    const size_t REQUIRED_STACK_SIZE = 8192 * 1024;
    if (stackSize < REQUIRED_STACK_SIZE) {
        auto err = pthread_attr_setstacksize (&stackSizeAttribute, REQUIRED_STACK_SIZE);
        if (err) throw std::runtime_error("couldn't get stack size");
    }
    auto ret = pthread_create(&thread, &stackSizeAttribute, mount_thread_fn, thread_params);
    if (ret) throw std::runtime_error("pthread_create");
    
    // wait for server to run
    auto maybe_listen_port_ws_handle = event->wait_for_mount_event();
    if (!maybe_listen_port_ws_handle) throw std::runtime_error("failed to mount!");
    
    auto listen_port = maybe_listen_port_ws_handle->first;
    auto webdav_server_handle = std::move(maybe_listen_port_ws_handle->second);
    
    // mount new drive
    
    // first attempt to use preferred name
    std::string mount_point;
    auto preferred_mount_point_name = "/Volumes/SafeMount-" + mount_name;
    int ret_mkdir = mkdir(preferred_mount_point_name.c_str(), 0700);
    if (ret_mkdir) {
        if (errno != EEXIST) throw std::runtime_error("couldn't mkdir");
        
        char *mount_point_template = strdup((preferred_mount_point_name + "-XXXXXXXXX").c_str());
        if (!mount_point_template) throw std::runtime_error("couldn't dup string");
        auto _free_resource =
        safe::create_dynamic_managed_resource(mount_point_template, free);
        char *mount_point_cstr = mkdtemp(mount_point_template);
        if (!mount_point_cstr) throw std::runtime_error("couldn't mkdtemp()");
        mount_point = std::string(mount_point_cstr);
    }
    else {
        mount_point = preferred_mount_point_name;
    }
    
    // check if ramdisk is mounted
    auto ramdisk_pair = ensure_ramdisk();
    std::string ramdisk_root, ramdisk_dev;
    std::tie(ramdisk_root, ramdisk_dev) = ramdisk_pair;

    // keep open handle to file inside ramdisk to prevent it from being unmounted
    auto ramdisk_mount_lock_file = ramdisk_root + "/.safe_mount_lock";
    auto fd = open(ramdisk_mount_lock_file.c_str(), O_RDWR | O_CREAT, 0777);
    if (fd < 0) throw std::system_error(errno, std::generic_category());
    unlink(ramdisk_mount_lock_file.c_str());
    auto ramdisk_handle = RAMDiskHandle(fd);
    
    // get dyld path
    NSString *dyld_file = [NSBundle.mainBundle pathForResource:@"libmount_webdav_interpose" ofType:@"dylib"];
    if (!dyld_file) throw std::runtime_error("dyld could not be found!");
    
    // finally run mount command
    std::ostringstream os;
    os << SAFE_RAMDISK_MOUNT_ROOT_ENV << "=\"" << escape_double_quotes(ramdisk_root) << "\" " <<
    SAFE_RAMDISK_MOUNT_DEV_ENV << "=\"" << escape_double_quotes(ramdisk_dev) << "\" " <<
    "DYLD_FORCE_FLAT_NAMESPACE=1 " <<
    "DYLD_INSERT_LIBRARIES=\"" << escape_double_quotes([dyld_file fileSystemRepresentation]) << "\" " <<
    "mount_webdav -S -v \"" << escape_double_quotes(mount_name) << "\" \"" <<
    escape_double_quotes(webdav_mount_url(listen_port, mount_name)) << "\" \"" << mount_point << "\"";
    auto mount_command = os.str();
    
    auto ret_system = system(mount_command.c_str());
    if (ret_system) throw std::runtime_error("running mount command failed");
    
    // return new mount details with thread info
    return MountDetails(listen_port, std::move(mount_name),
                        thread, mount_point, event, encrypted_container_path,
                        std::move(webdav_server_handle), std::move(ramdisk_handle));
}

}}
