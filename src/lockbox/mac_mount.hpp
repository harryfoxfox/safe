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

class MountDetails {
    port_t listen_port;
    std::string name;
    pthread_t thread_handle;
    std::string mount_point;

public:
    MountDetails(port_t listen_port_,
                 std::string name_,
                 pthread_t thread_handle_,
                 std::string mount_point_)
    : listen_port(listen_port_)
    , name(std::move(name_))
    , thread_handle(thread_handle_)
    , mount_point(std::move(mount_point_)) {}
    
    void
    send_thread_termination_signal();
    
    void
    wait_for_thread_to_die();
    
    void
    unmount_drive();
};
    
MountDetails
mount_new_encfs_drive(const std::shared_ptr<encfs::FsIO> & native_fs,
                      const encfs::Path & encrypted_container_path,
                      const encfs::EncfsConfig & cfg,
                      const encfs::SecureMem & password);

}}

#endif /* defined(__Lockbox__File__) */
