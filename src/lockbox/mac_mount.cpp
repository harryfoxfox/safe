//
//  mac_mount.cpp
//  Lockbox
//
//  Created by Rian Hunter on 11/7/13.
//  Copyright (c) 2013 Rian Hunter. All rights reserved.
//

#include <lockbox/mac_mount.hpp>

#include <lockbox/util.hpp>
#include <lockbox/lockbox_server.hpp>

#include <encfs/fs/FsIO.h>

#include <davfuse/util_sockets.h>

#include <random>
#include <sstream>

#include <ctime>

namespace lockbox { namespace mac {

class MountEvent;
    
struct ServerThreadParams {
    MountEvent *event;
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

void
MountDetails::send_thread_termination_signal() {
}
    
void
MountDetails::wait_for_thread_to_die() {
}
    
static
void *
mount_thread_fn(void *p) {
    // TODO: catch all exceptions, since this is a top-level
        
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
        
    lockbox::run_lockbox_webdav_server(std::move(enc_fs),
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
    MountEvent event;
    
    // TODO: perhaps make this an argument
    auto mount_name = encrypted_container_path.basename();
    
    // create thread details
    auto thread_params = new ServerThreadParams {
        &event,
        native_fs,
        encrypted_container_path,
        cfg,
        password,
        mount_name,
    };
    
    // start thread
    pthread_t thread;
    auto ret = pthread_create(&thread, NULL, mount_thread_fn, thread_params);
    if (ret) throw std::runtime_error("pthread_create");
    
    // wait for server to run
    auto listen_port = event.wait_for_mount_msg();
    if (!listen_port) throw std::runtime_error("failed to mount!");
    
    // mount new drive
    std::vector<char> replaced;
    for (const auto & c : mount_name) {
        if (c == '"') {
            replaced.push_back('\\');
        }
        replaced.push_back(c);
    }
    mount_name = std::string(replaced.begin(), replaced.end());
    
    char *mount_point_template = strdup("/Volumes/bvmount.XXXXXXXXXXX");
    if (!mount_point_template) throw std::runtime_error("couldn't dup string");
    auto _free_resource =
        lockbox::create_dynamic_managed_resource(mount_point_template, free);
    
    char *mount_point = mkdtemp(mount_point_template);
    if (!mount_point) throw std::runtime_error("couldn't mkdtemp()");
    std::ostringstream os;
    os << "mount_webdav -S -v \"" << mount_name << "\" \"http://localhost:" <<
    *listen_port << "/" << mount_name << "/\" \"" << mount_point << "\"";
    auto mount_command = os.str();
    
    auto ret_system = system(mount_command.c_str());
    if (ret_system) throw std::runtime_error("running mount command failed");

    // return new mount details with thread info
    return MountDetails(*listen_port, std::move(mount_name),
                        thread, mount_point_template);
}
    
}}
