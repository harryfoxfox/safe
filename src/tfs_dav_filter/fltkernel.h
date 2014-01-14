/*
  Lockbox: Encrypted File System
  Copyright (C) 2014 Rian Hunter <rian@alum.mit.edu>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

/* This is our hacked version of fltkernel.h, defs were derived from msdn
   we'll soon contribute this to to the mingw32(-w64) project */

#ifndef __tfs_dav_store_fltkernel_h
#define __tfs_dav_store_fltkernel_h

#include <ntifs.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLT_MGR_LONGHORN (NTDDI_VERSION >= NTDDI_VISTA)
#define FLT_MGR_WIN8 (NTDDI_VERSION >= NTDDI_WIN8)

/* opaque structures */
typedef struct _flt_filter *PFLT_FILTER;
typedef struct _flt_volume *PFLT_VOLUME;
typedef struct _flt_instance *PFLT_INSTANCE;
typedef struct _flt_port *PFLT_PORT;
typedef struct _flt_context *PFLT_CONTEXT;

#if !FLT_MGR_LONGHORN
typedef struct _ktransaction *PKTRANSACTION;
#endif

/* flags & enum types */
typedef ULONG FLT_INSTANCE_SETUP_FLAGS;
typedef ULONG FLT_INSTANCE_TEARDOWN_FLAGS;
typedef ULONG FLT_INSTANCE_UNLOAD_FLAGS;
typedef ULONG FLT_INSTANCE_QUERY_TEARDOWN_FLAGS;
typedef ULONG FLT_INSTANCE_UNLOAD_FLAGS;
typedef ULONG FLT_FILTER_UNLOAD_FLAGS;
typedef ULONG FLT_CALLBACK_DATA_FLAGS;
typedef ULONG FLT_POST_OPERATION_FLAGS;
typedef ULONG FLT_OPERATION_REGISTRATION_FLAGS;
typedef USHORT FLT_REGISTRATION_FLAGS;
typedef USHORT FLT_CONTEXT_REGISTRATION_FLAGS;
typedef ULONG FLT_NORMALIZE_NAME_FLAGS;

typedef enum _FLT_FILESYSTEM_TYPE {
  FLT_FSTYPE_UNKNOWN,
  FLT_FSTYPE_RAW,
  FLT_FSTYPE_NTFS,
  FLT_FSTYPE_FAT,
  FLT_FSTYPE_CDFS,
  FLT_FSTYPE_UDFS,
  FLT_FSTYPE_LANMAN,
  FLT_FSTYPE_WEBDAV,
  FLT_FSTYPE_RDPDR,
  FLT_FSTYPE_NFS,
  FLT_FSTYPE_MS_NETWARE,
  FLT_FSTYPE_NETWARE,
  FLT_FSTYPE_BSUDF,
  FLT_FSTYPE_MUP,
  FLT_FSTYPE_RSFX,
  FLT_FSTYPE_ROXIO_UDF1,
  FLT_FSTYPE_ROXIO_UDF2,
  FLT_FSTYPE_ROXIO_UDF3,
  FLT_FSTYPE_TACIT,
  FLT_FSTYPE_FS_REC,
  FLT_FSTYPE_INCD,
  FLT_FSTYPE_INCD_FAT,
  FLT_FSTYPE_EXFAT,
  FLT_FSTYPE_PSFS,
  FLT_FSTYPE_GPFS,
  FLT_FSTYPE_NPFS,
  FLT_FSTYPE_MSFS,
  FLT_FSTYPE_CSVFS,
  FLT_FSTYPE_REFS,
  FLT_FSTYPE_OPENAFS
} FLT_FILESYSTEM_TYPE, *PFLT_FILESYSTEM_TYPE;

typedef USHORT FLT_CONTEXT_TYPE;
enum {
  FLT_VOLUME_CONTEXT=0x1,
  FLT_INSTANCE_CONTEXT=0x2,
  FLT_FILE_CONTEXT=0x4,
  FLT_STREAM_CONTEXT=0x8,
  FLT_STREAMHANDLE_CONTEXT=0x10,
  FLT_TRANSACTION_CONTEXT=0x20,
};

typedef enum _FLT_PREOP_CALLBACK_STATUS {
  FLT_PREOP_SUCCESS_WITH_CALLBACK,
  FLT_PREOP_SUCCESS_NO_CALLBACK,
  FLT_PREOP_PENDING,
  FLT_PREOP_DISALLOW_FASTIO,
  FLT_PREOP_COMPLETE,
  FLT_PREOP_SYNCHRONIZE
} FLT_PREOP_CALLBACK_STATUS, *PFLT_PREOP_CALLBACK_STATUS;

typedef enum _FLT_POSTOP_CALLBACK_STATUS {
  FLT_POSTOP_FINISHED_PROCESSING,
  FLT_POSTOP_MORE_PROCESSING_REQUIRED
} FLT_POSTOP_CALLBACK_STATUS, *PFLT_POSTOP_CALLBACK_STATUS;

#define IRP_MJ_ACQUIRE_FOR_SECTION_SYNCHRONIZATION   0xff
#define IRP_MJ_RELEASE_FOR_SECTION_SYNCHRONIZATION   0xfe
#define IRP_MJ_ACQUIRE_FOR_MOD_WRITE                 0xfd
#define IRP_MJ_RELEASE_FOR_MOD_WRITE                 0xfc
#define IRP_MJ_ACQUIRE_FOR_CC_FLUSH                  0xfb
#define IRP_MJ_RELEASE_FOR_CC_FLUSH                  0xfa
#define IRP_MJ_FAST_IO_CHECK_IF_POSSIBLE             0xf2
#define IRP_MJ_NETWORK_QUERY_OPEN                    0xf1
#define IRP_MJ_MDL_READ                              0xf0
#define IRP_MJ_MDL_READ_COMPLETE                     0xef
#define IRP_MJ_PREPARE_MDL_WRITE                     0xee
#define IRP_MJ_MDL_WRITE_COMPLETE                    0xed
#define IRP_MJ_VOLUME_MOUNT                          0xec
#define IRP_MJ_VOLUME_DISMOUNT                       0xeb
#define IRP_MJ_OPERATION_END                         0x80

#define FLT_REGISTRATION_VERSION_0200  0x0200
#define FLT_REGISTRATION_VERSION_0201  0x0201
#define FLT_REGISTRATION_VERSION_0202  0x0202

#if FLT_MGR_LONGHORN
    #define FLT_REGISTRATION_VERSION   0x0202
#else
    #define FLT_REGISTRATION_VERSION   0x0200
#endif

typedef ULONG FLT_FILE_NAME_OPTIONS;
#define FLT_VALID_FILE_NAME_FORMATS 0x000000ff
    #define FLT_FILE_NAME_NORMALIZED    0x01
    #define FLT_FILE_NAME_OPENED        0x02
    #define FLT_FILE_NAME_SHORT         0x03
#define FLT_VALID_FILE_NAME_QUERY_METHODS 0x0000ff00
    #define FLT_FILE_NAME_QUERY_DEFAULT     0x0100
    #define FLT_FILE_NAME_QUERY_CACHE_ONLY  0x0200
    #define FLT_FILE_NAME_QUERY_FILESYSTEM_ONLY 0x0300
    #define FLT_FILE_NAME_QUERY_ALWAYS_ALLOW_CACHE_LOOKUP 0x0400
#define FLT_VALID_FILE_NAME_FLAGS 0xff000000
    #define FLT_FILE_NAME_REQUEST_FROM_CURRENT_PROVIDER 0x01000000
    #define FLT_FILE_NAME_DO_NOT_CACHE                  0x02000000

/* structures and unions */

typedef union _FLT_PARAMETERS {
  union {
    struct {
      PVPB           Vpb;
      PDEVICE_OBJECT DeviceObject;
    } VerifyVolume;
    struct {
      ULONG                   OutputBufferLength;
      ULONG POINTER_ALIGNMENT InputBufferLength;
      ULONG POINTER_ALIGNMENT FsControlCode;
    } Common;
    struct {
      ULONG                    OutputBufferLength;
      ULONG POINTER_ALIGNMENT  InputBufferLength;
      ULONG POINTER_ALIGNMENT  FsControlCode;
      PVOID                    InputBuffer;
      PVOID                    OutputBuffer;
      PMDL                     OutputMdlAddress;
    } Neither;
    struct {
      ULONG                   OutputBufferLength;
      ULONG POINTER_ALIGNMENT InputBufferLength;
      ULONG POINTER_ALIGNMENT FsControlCode;
      PVOID                   SystemBuffer;
    } Buffered;
    struct {
      ULONG                   OutputBufferLength;
      ULONG POINTER_ALIGNMENT InputBufferLength;
      ULONG POINTER_ALIGNMENT FsControlCode;
      PVOID                   InputSystemBuffer;
      PVOID                   OutputBuffer;
      PMDL                    OutputMdlAddress;
    } Direct;
  } FileSystemControl;
} FLT_PARAMETERS, *PFLT_PARAMETERS;

typedef struct _FLT_IO_PARAMETER_BLOCK {
  ULONG          IrpFlags;
  UCHAR          MajorFunction;
  UCHAR          MinorFunction;
  UCHAR          OperationFlags;
  UCHAR          Reserved;
  PFILE_OBJECT   TargetFileObject;
  PFLT_INSTANCE  TargetInstance;
  FLT_PARAMETERS Parameters;
} FLT_IO_PARAMETER_BLOCK, *PFLT_IO_PARAMETER_BLOCK;

typedef struct _FLT_CALLBACK_DATA {
  FLT_CALLBACK_DATA_FLAGS       Flags;
  const PETHREAD                Thread;
  const PFLT_IO_PARAMETER_BLOCK Iopb;
  IO_STATUS_BLOCK               IoStatus;
  struct _FLT_TAG_DATA_BUFFER  *TagData;
  union {
    struct {
      LIST_ENTRY QueueLinks;
      PVOID      QueueContext[2];
    };
    PVOID  FilterContext[4];
  };
  KPROCESSOR_MODE               RequestorMode;
} FLT_CALLBACK_DATA, *PFLT_CALLBACK_DATA;

typedef struct _FLT_RELATED_OBJECTS {
  const USHORT        Size;
  const USHORT        TransactionContext;
  const PFLT_FILTER   Filter;
  const PFLT_VOLUME   Volume;
  const PFLT_INSTANCE Instance;
  const PFILE_OBJECT  FileObject;
  const PKTRANSACTION Transaction;
} FLT_RELATED_OBJECTS, *PFLT_RELATED_OBJECTS;

typedef CONST struct _FLT_RELATED_OBJECTS *PCFLT_RELATED_OBJECTS;

typedef struct _FLT_NAME_CONTROL {
  UNICODE_STRING Name;
} FLT_NAME_CONTROL, *PFLT_NAME_CONTROL;

/* function pointer types */

/* these help with documentation */
#define _Inout_
#define _In_opt_
#define _In_
#define _Out_

typedef FLT_PREOP_CALLBACK_STATUS (NTAPI *PFLT_PRE_OPERATION_CALLBACK)(
  _Inout_  PFLT_CALLBACK_DATA Data,
  _In_     PCFLT_RELATED_OBJECTS FltObjects,
  _Out_    PVOID *CompletionContext
);

typedef FLT_POSTOP_CALLBACK_STATUS (NTAPI *PFLT_POST_OPERATION_CALLBACK)(
  _Inout_   PFLT_CALLBACK_DATA Data,
  _In_      PCFLT_RELATED_OBJECTS FltObjects,
  _In_opt_  PVOID CompletionContext,
  _In_      FLT_POST_OPERATION_FLAGS FLAGS);

typedef NTSTATUS (NTAPI *PFLT_INSTANCE_SETUP_CALLBACK) (
  _In_  PCFLT_RELATED_OBJECTS FltObjects,
  _In_  FLT_INSTANCE_SETUP_FLAGS Flags,
  _In_  DEVICE_TYPE VolumeDeviceType,
  _In_  FLT_FILESYSTEM_TYPE VolumeFilesystemType);

typedef NTSTATUS (NTAPI *PFLT_FILTER_UNLOAD_CALLBACK) (
  FLT_FILTER_UNLOAD_FLAGS FLAGS);

typedef VOID (NTAPI *PFLT_CONTEXT_CLEANUP_CALLBACK) (
  _In_  PFLT_CONTEXT Context,
  _In_  FLT_CONTEXT_TYPE ContextType);

typedef PVOID (NTAPI *PFLT_CONTEXT_ALLOCATE_CALLBACK)(
  _In_  POOL_TYPE PoolType,
  _In_  SIZE_T Size,
  _In_  FLT_CONTEXT_TYPE ContextType);

typedef VOID (NTAPI *PFLT_CONTEXT_FREE_CALLBACK)(
  _In_  PVOID Pool,
  _In_  FLT_CONTEXT_TYPE ContextType);

typedef NTSTATUS (NTAPI *PFLT_INSTANCE_QUERY_TEARDOWN_CALLBACK)(
  _In_  PCFLT_RELATED_OBJECTS FltObjects,
  _In_  FLT_INSTANCE_QUERY_TEARDOWN_FLAGS FLAGS);

typedef VOID (NTAPI *PFLT_INSTANCE_TEARDOWN_CALLBACK)(
  _In_  PCFLT_RELATED_OBJECTS FltObjects,
  _In_  FLT_INSTANCE_TEARDOWN_FLAGS Reason);

typedef NTSTATUS (NTAPI *PFLT_GENERATE_FILE_NAME)(
  _In_      PFLT_INSTANCE Instance,
  _In_      PFILE_OBJECT FileObject,
  _In_opt_  PFLT_CALLBACK_DATA CallbackData,
  _In_      FLT_FILE_NAME_OPTIONS NameOptions,
  _Out_     PBOOLEAN CacheFileNameInformation,
  _Out_     PFLT_NAME_CONTROL FileName);

typedef NTSTATUS (NTAPI *PFLT_NORMALIZE_NAME_COMPONENT)(
  _In_     PFLT_INSTANCE Instance,
  _In_     PCUNICODE_STRING ParentDirectory,
  _In_     USHORT VolumeNameLength,
  _In_     PCUNICODE_STRING Component,
  _Out_    PFILE_NAMES_INFORMATION ExpandComponentName,
  _In_     ULONG ExpandComponentNameLength,
  _In_     FLT_NORMALIZE_NAME_FLAGS Flags,
  _Inout_  PVOID *NormalizationContext);

typedef VOID (NTAPI *PFLT_NORMALIZE_CONTEXT_CLEANUP)(
  _In_opt_  PVOID *NormalizationContext);

typedef VOID (NTAPI *PFLT_GET_OPERATION_STATUS_CALLBACK)(
  _In_      PCFLT_RELATED_OBJECTS  FltObjects,
  _In_      PFLT_IO_PARAMETER_BLOCK IopbSnapshot,
  _In_      NTSTATUS OperationStatus,
  _In_opt_  PVOID RequesterContext);

/* final structure */
typedef struct _FLT_CONTEXT_REGISTRATION {
  FLT_CONTEXT_TYPE               ContextType;
  FLT_CONTEXT_REGISTRATION_FLAGS Flags;
  PFLT_CONTEXT_CLEANUP_CALLBACK  ContextCleanupCallback;
  SIZE_T                         Size;
  ULONG                          PoolTag;
  PFLT_CONTEXT_ALLOCATE_CALLBACK ContextAllocateCallback;
  PFLT_CONTEXT_FREE_CALLBACK     ContextFreeCallback;
  PVOID                          Reserved1;
} FLT_CONTEXT_REGISTRATION, *PFLT_CONTEXT_REGISTRATION;

typedef struct _FLT_OPERATION_REGISTRATION {
  UCHAR                            MajorFunction;
  FLT_OPERATION_REGISTRATION_FLAGS Flags;
  PFLT_PRE_OPERATION_CALLBACK      PreOperation;
  PFLT_POST_OPERATION_CALLBACK     PostOperation;
  PVOID                            Reserved1;
} FLT_OPERATION_REGISTRATION, *PFLT_OPERATION_REGISTRATION;

typedef struct _FLT_REGISTRATION {
  USHORT                                      Size;
  USHORT                                      Version;
  FLT_REGISTRATION_FLAGS                      Flags;
  const FLT_CONTEXT_REGISTRATION              *ContextRegistration;
  const FLT_OPERATION_REGISTRATION            *OperationRegistration;
  PFLT_FILTER_UNLOAD_CALLBACK                 FilterUnloadCallback;
  PFLT_INSTANCE_SETUP_CALLBACK                InstanceSetupCallback;
  PFLT_INSTANCE_QUERY_TEARDOWN_CALLBACK       InstanceQueryTeardownCallback;
  PFLT_INSTANCE_TEARDOWN_CALLBACK             InstanceTeardownStartCallback;
  PFLT_INSTANCE_TEARDOWN_CALLBACK             InstanceTeardownCompleteCallback;
  PFLT_GENERATE_FILE_NAME                     GenerateFileNameCallback;
  PFLT_NORMALIZE_NAME_COMPONENT               NormalizeNameComponentCallback;
  PFLT_NORMALIZE_CONTEXT_CLEANUP              NormalizeContextCleanupCallback;
#if FLT_MGR_LONGHORN
  PFLT_TRANSACTION_NOTIFICATION_CALLBACK      TransactionNotificationCallback;
  PFLT_NORMALIZE_NAME_COMPONENT_EX            NormalizeNameComponentExCallback;
#endif
#ifdef FLT_MFG_WIN8
  PFLT_SECTION_CONFLICT_NOTIFICATION_CALLBACK SectionNotificationCallback;
#endif
} FLT_REGISTRATION, *PFLT_REGISTRATION;

/* function protoypes */

NTSTATUS
NTAPI
FltRegisterFilter(
 _In_   PDRIVER_OBJECT Driver,
 _In_   const FLT_REGISTRATION *Registration,
 _Out_  PFLT_FILTER *RetFilter
);


NTSTATUS
NTAPI
FltStartFiltering(
  _In_  PFLT_FILTER FILTER);

VOID
NTAPI
FltUnregisterFilter(
  _In_  PFLT_FILTER FILTER);

NTSTATUS
NTAPI
FltRequestOperationStatusCallback(
  _In_      PFLT_CALLBACK_DATA Data,
  _In_      PFLT_GET_OPERATION_STATUS_CALLBACK CallbackRoutine,
  _In_opt_  PVOID RequesterContext
);

PCHAR
NTAPI
FltGetIrpName(
  _In_  UCHAR IrpMajorCode
);

#define FLT_ASSERT ASSERT

#ifdef __cplusplus
}
#endif

#endif

