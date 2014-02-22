//
//  mac_mount.hpp
//  Safe
//
//  Created by Rian Hunter on 11/7/13.
//  Copyright (c) 2013 Rian Hunter. All rights reserved.
//

#ifndef __Safe__mac_mount
#define __Safe__mac_mount

#include <safe/webdav_server.hpp>

#include <encfs/fs/FileUtils.h>
#include <encfs/fs/FsIO.h>
#include <encfs/cipher/MemoryPool.h>

#include <davfuse/util_sockets.h>

#include <pthread.h>

namespace safe { namespace mac {

struct CloseFileDescriptor {
    void
    operator()(int fd) {
        int ret = close(fd);
        if (ret) throw std::system_error(errno, std::generic_category());
    }
};

typedef ManagedResource<int, CloseFileDescriptor> RAMDiskHandle;

class MountEvent;

class MountDetails {
    port_t listen_port;
    std::string name;
    pthread_t thread_handle;
    std::string mount_point;
    bool is_mounted;
    std::shared_ptr<MountEvent> mount_event;
    encfs::Path source_path;
    safe::WebdavServerHandle ws;
    RAMDiskHandle ramdisk_handle;

public:
    MountDetails(port_t listen_port_,
                 std::string name_,
                 pthread_t thread_handle_,
                 std::string mount_point_,
                 std::shared_ptr<MountEvent> mount_event_,
                 encfs::Path source_path_,
                 safe::WebdavServerHandle ws_,
                 RAMDiskHandle ramdisk_handle_)
    : listen_port(listen_port_)
    , name(std::move(name_))
    , thread_handle(thread_handle_)
    , mount_point(std::move(mount_point_))
    , is_mounted(true)
    , mount_event(std::move(mount_event_))
    , source_path(std::move(source_path_))
    , ws(std::move(ws_))
    , ramdisk_handle(std::move(ramdisk_handle_)) {}

    // copy is not allowed
    MountDetails(const MountDetails &) = delete;
    MountDetails &operator=(const MountDetails &) = delete;
    
    // move is allowed
    MountDetails(MountDetails &&) = default;
    MountDetails &operator=(MountDetails &&) = default;
    
    const std::string &
    get_mount_name() const { return name; }
    
    const std::string &
    get_mount_point() const { return mount_point; }
    
    const encfs::Path &
    get_source_path() const { return source_path; }
    
    bool
    is_still_mounted() const;
    
    void
    wait_until_stopped() const;
    
    void
    unmount();

    void
    open_mount() const;
    
    void
    disconnect_clients() const;
};
    
MountDetails
mount_new_encfs_drive(const std::shared_ptr<encfs::FsIO> & native_fs,
                      const encfs::Path & encrypted_container_path,
                      const encfs::EncfsConfig & cfg,
                      const encfs::SecureMem & password);

}}

#endif /* defined(__Safe__File__) */
