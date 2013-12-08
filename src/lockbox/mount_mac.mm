//
//  mac_mount.mm
//  Lockbox
//
//  Created by Rian Hunter on 11/7/13.
//  Copyright (c) 2013 Rian Hunter. All rights reserved.
//

#import <lockbox/mount_mac.hpp>

#import <lockbox/fs.hpp>
#import <lockbox/util.hpp>
#import <lockbox/webdav_server.hpp>

#import <encfs/fs/FsIO.h>

#import <davfuse/util_sockets.h>
#import <davfuse/webdav_server.h>

#import <Cocoa/Cocoa.h>

#import <random>
#import <sstream>

#import <ctime>

#import <libgen.h>

#import <sys/stat.h>

namespace lockbox { namespace mac {

class MountEvent;
    
struct ServerThreadParams {
    std::shared_ptr<MountEvent> event;
    // the control-block of std::shared_ptr is thread-safe
    // (i.e. multiple threads of control can have a copy of a root shared_ptr)
    // a single std::shared_ptr is not thread-safe, this use-case doesn't apply to us
    // for proper operation the encfs::FsIO implementation used
    // must be thread-safe as well, the mac native fs is thread-safe so that isn't a concern
    std::shared_ptr<encfs::FsIO> native_fs;
    encfs::Path encrypted_container_path;
    encfs::EncfsConfig encfs_config;
    encfs::SecureMem password;
    std::string mount_name;
};
    
class MountEvent {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool msg_sent;
    bool error;
    port_t listen_port;
    
public:
    MountEvent()
    : mutex(PTHREAD_MUTEX_INITIALIZER)
    , cond(PTHREAD_COND_INITIALIZER)
    , msg_sent(false) {}
    
    void
    send_mount_success(port_t listen_port_) {
        auto ret_lock = pthread_mutex_lock(&mutex);
        if (ret_lock) throw std::runtime_error("pthread_mutex_lock");
        
        auto _deferred_unlock = lockbox::create_deferred(pthread_mutex_unlock, &mutex);
        
        if (msg_sent) throw std::runtime_error("Message already sent!");
        
        error = false;
        listen_port = listen_port_;
        msg_sent = true;
        pthread_cond_signal(&cond);
    }
    
    void
    send_mount_fail() {
        auto ret_lock = pthread_mutex_lock(&mutex);
        if (ret_lock) throw std::runtime_error("pthread_mutex_lock");
        
        auto _deferred_unlock = lockbox::create_deferred(pthread_mutex_unlock, &mutex);
        
        if (msg_sent) throw std::runtime_error("Message already sent!");
        
        error = true;
        msg_sent = true;
        pthread_cond_signal(&cond);
    }
    
    void
    send_thread_done() {
    }
    
    opt::optional<port_t>
    wait_for_mount_msg() {
        auto ret_lock = pthread_mutex_lock(&mutex);
        if (ret_lock) throw std::runtime_error("pthread_mutex_lock");
        
        auto _deferred_unlock = lockbox::create_deferred(pthread_mutex_unlock, &mutex);
        
        while (!msg_sent) {
            auto ret = pthread_cond_wait(&cond, &mutex);
            if (ret) throw std::runtime_error("pthread_cond_wait");
        }
        
        return (error
                ? opt::nullopt
                : opt::make_optional(listen_port));
    }
};

static
std::string
escape_double_quotes(std::string mount_name) {
    return lockbox::escape_double_quotes(std::move(mount_name));
}

static
std::string
webdav_mount_quit_url(port_t listen_port, std::string name) {
    (void) name;
    return std::string("http://localhost:") + std::to_string(listen_port) + WEBDAV_SERVER_QUIT_URL;
}
    
static
std::string
webdav_mount_disconnect_url(port_t listen_port, std::string name) {
    (void) name;
    return std::string("http://localhost:") + std::to_string(listen_port) + WEBDAV_SERVER_DISCONNECT_URL;
}

static
std::string
webdav_mount_url(port_t listen_port, std::string name) {
    return std::string("http://localhost:") + std::to_string(listen_port) + "/" + name + "/";
}
    
void
MountDetails::signal_stop() const {
    auto quit_url = webdav_mount_quit_url(listen_port, name);
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0),
                   ^{
                       NSString *requestUrl = [NSString stringWithUTF8String:quit_url.c_str()];
                       NSMutableURLRequest *postRequest = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:requestUrl]];
                       [postRequest setHTTPMethod:@"POST"];
                       [NSURLConnection sendSynchronousRequest:postRequest returningResponse:nil error:nil];
                   });
}
    
void
MountDetails::disconnect_clients() const {
    auto disconnect_url = webdav_mount_disconnect_url(listen_port, name);
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0),
                   ^{
                       NSString *requestUrl = [NSString stringWithUTF8String:disconnect_url.c_str()];
                       NSMutableURLRequest *postRequest = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:requestUrl]];
                       [postRequest setHTTPMethod:@"POST"];
                       [NSURLConnection sendSynchronousRequest:postRequest returningResponse:nil error:nil];
                   });
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

bool
MountDetails::is_still_mounted() const {
    // we compare dev ids of the mount point and its parent
    // if they are different when we consider it unmounted
    // NB: this isn't perfect but it's good enough for now
    //     it would be better to check if the mount point
    //     is still connected to the right webdav server
    

    struct stat child_st;
    auto ret_stat_1 = stat(this->mount_point.c_str(), &child_st);
    if (ret_stat_1 < 0) {
        if (errno == ENOENT) return false;
        throw std::runtime_error("bad stat");
    }
    
    char *mp_copy = strdup(this->mount_point.c_str());
    if (!mp_copy) throw std::runtime_error("couldn't dup string");
    auto _free_resource = lockbox::create_destroyer(mp_copy, free);
    
    char *parent = dirname(mp_copy);
    struct stat parent_st;
    auto ret_stat_2 = lstat(parent, &parent_st);
    if (ret_stat_2 < 0) {
        if (errno == ENOENT) return false;
        throw std::runtime_error("bad stat");
    }
    
    return parent_st.st_dev != child_st.st_dev;
}
    
static
void *
mount_thread_fn(void *p) {
    // TODO: catch all exceptions since this is a top-level
        
    auto params = std::unique_ptr<ServerThreadParams>((ServerThreadParams *) p);
        
    std::srand(std::time(nullptr));
        
    auto enc_fs =
    lockbox::create_enc_fs(std::move(params->native_fs),
                           params->encrypted_container_path,
                           std::move(params->encfs_config),
                           std::move(params->password));
    
    // we only listen on localhost
    auto ip_addr = LOCALHOST_IP;
    auto listen_port =
    find_random_free_listen_port(ip_addr,
                                 PRIVATE_PORT_START, PRIVATE_PORT_END);

    bool sent_signal = false;
    auto our_callback = [&] (event_loop_handle_t /*loop*/) {
        params->event->send_mount_success(listen_port);
        sent_signal = true;
    };
        
    lockbox::run_webdav_server(std::move(enc_fs),
                               std::move(params->encrypted_container_path),
                               ip_addr,
                               listen_port,
                               std::move(params->mount_name),
                               our_callback);
    
    if (!sent_signal) params->event->send_mount_fail();

    params->event->send_thread_done();
        
    // server is done, possible unmount
    return NULL;
}
    
MountDetails
mount_new_encfs_drive(const std::shared_ptr<encfs::FsIO> & native_fs,
                      const encfs::Path & encrypted_container_path,
                      const encfs::EncfsConfig & cfg,
                      const encfs::SecureMem & password) {
    // TODO: catch all exceptions and clean up state if one occurs
    // (etc. clean up threads, etc.)

    auto event = std::make_shared<MountEvent>();
    
    // TODO: perhaps make this an argument
    auto mount_name = encrypted_container_path.basename();
    
    // create thread details
    auto thread_params = new ServerThreadParams {
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
    auto listen_port = event->wait_for_mount_msg();
    if (!listen_port) throw std::runtime_error("failed to mount!");
    
    // mount new drive
    
    // first attempt to use preferred name
    std::string mount_point;
    auto preferred_mount_point_name = "/Volumes/bv-mount-" + mount_name;
    int ret_mkdir = mkdir(preferred_mount_point_name.c_str(), 0700);
    if (ret_mkdir) {
        if (errno != EEXIST) throw std::runtime_error("couldn't mkdir");
        
        char *mount_point_template = strdup((preferred_mount_point_name + "-XXXXXXXXX").c_str());
        if (!mount_point_template) throw std::runtime_error("couldn't dup string");
        auto _free_resource =
        lockbox::create_dynamic_managed_resource(mount_point_template, free);
        char *mount_point_cstr = mkdtemp(mount_point_template);
        if (!mount_point_cstr) throw std::runtime_error("couldn't mkdtemp()");
        mount_point = std::string(mount_point_cstr);
    }
    else {
        mount_point = preferred_mount_point_name;
    }

    std::ostringstream os;
    os << "mount_webdav -S -v \"" << escape_double_quotes(mount_name) << "\" \"" <<
    escape_double_quotes(webdav_mount_url(*listen_port, mount_name)) << "\" \"" << mount_point << "\"";
    auto mount_command = os.str();
    
    auto ret_system = system(mount_command.c_str());
    if (ret_system) throw std::runtime_error("running mount command failed");

    // return new mount details with thread info
    return MountDetails(*listen_port, std::move(mount_name),
                        thread, mount_point, event, encrypted_container_path);
}
    
}}
