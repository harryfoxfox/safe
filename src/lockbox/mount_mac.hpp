//
//  mac_mount.hpp
//  Lockbox
//
//  Created by Rian Hunter on 11/7/13.
//  Copyright (c) 2013 Rian Hunter. All rights reserved.
//

#ifndef __Lockbox__mac_mount
#define __Lockbox__mac_mount

#include <encfs/fs/FileUtils.h>
#include <encfs/fs/FsIO.h>
#include <encfs/cipher/MemoryPool.h>

#include <davfuse/util_sockets.h>

#include <pthread.h>

namespace lockbox { namespace mac {

class MountEvent;

class MountDetails {
    port_t listen_port;
    std::string name;
    pthread_t thread_handle;
    std::string mount_point;
    bool is_mounted;
    std::shared_ptr<MountEvent> mount_event;

public:
    MountDetails(port_t listen_port_,
                 std::string name_,
                 pthread_t thread_handle_,
                 std::string mount_point_,
                 std::shared_ptr<MountEvent> mount_event_)
    : listen_port(listen_port_)
    , name(std::move(name_))
    , thread_handle(thread_handle_)
    , mount_point(std::move(mount_point_))
    , is_mounted(true)
    , mount_event(std::move(mount_event_)) {}

    // copy is not allowed
    MountDetails(const MountDetails &) = delete;
    MountDetails &operator=(const MountDetails &) = delete;
    
    // move is allowed
    MountDetails(MountDetails &&) = default;
    MountDetails &operator=(MountDetails &&) = default;
    
    const std::string &
    get_mount_name() const {
        return name;
    }
    
    bool
    is_still_mounted() const;
    
    void
    signal_stop() const;
    
    void
    wait_until_stopped() const;
    
    void
    unmount();

    void
    open_mount() const;
};
    
MountDetails
mount_new_encfs_drive(const std::shared_ptr<encfs::FsIO> & native_fs,
                      const encfs::Path & encrypted_container_path,
                      const encfs::EncfsConfig & cfg,
                      const encfs::SecureMem & password);

}}

#endif /* defined(__Lockbox__File__) */
