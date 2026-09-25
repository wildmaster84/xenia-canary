/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/xam_content_device.h"

#include "xenia/base/byte_order.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/kernel/xenumerator.h"
#include "xenia/vfs/devices/stfs_xbox.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {

// TODO(gibbed): real information.
//
// Until we expose real information about a HDD device, we
// claim there is 3GB free on a 4GB dummy HDD.
//
// There is a possibility that certain games are bugged in that
// they incorrectly only look at the lower 32-bits of free_bytes,
// when it is a 64-bit value. Which means any size above ~4GB
// will not be recognized properly.
#define ONE_GB (1024ull * 1024ull * 1024ull)
#define ONE_MB (1024ull * 1024ull)

static DummyDeviceInfo dummy_hdd_device_info_ = {
    DummyDeviceId::HDD, DeviceType::HDD,
    20ull * ONE_GB,  // 20GB
    10ull * ONE_GB,  // 10GB.
    u"Dummy HDD",
};
static DummyDeviceInfo dummy_odd_device_info_ = {
    DummyDeviceId::ODD, DeviceType::ODD,
    7ull * ONE_GB,  // 7GB (rough maximum)
    0ull * ONE_GB,  // read-only FS, so no free space
    u"Dummy ODD",
};
static DummyDeviceInfo dummy_cloud_device_info_ = {
    DummyDeviceId::CloudStorage,
    DeviceType::CloudStorage,
    1ull * ONE_GB,  // 1GB
    1ull * ONE_GB,  // 1GB
    u"Cloud Storage",
};
static const DummyDeviceInfo* dummy_device_infos_[] = {
    &dummy_hdd_device_info_,
    &dummy_odd_device_info_,
    &dummy_cloud_device_info_,
};
#undef ONE_GB

const DummyDeviceInfo* GetDummyDeviceInfo(uint32_t device_id) {
  const auto& begin = std::begin(dummy_device_infos_);
  const auto& end = std::end(dummy_device_infos_);
  auto it = std::find_if(begin, end, [device_id](const auto& item) {
    return static_cast<uint32_t>(item->device_id) == device_id;
  });

  if (it == end) {
    return nullptr;
  }

  // We update the device info here to keep everything clean.
  auto* device = const_cast<DummyDeviceInfo*>(*it);
  if (device->device_type == DeviceType::HDD ||
      device->device_type == DeviceType::ODD) {
    device->total_bytes = kernel_state()->content_manager()->GetContentTotalSpace();
    device->free_bytes = kernel_state()->content_manager()->GetContentFreeSpace();
  } else if (device->device_type == DeviceType::CloudStorage) {
    device->total_bytes = 2 * (1024ull * 1024ull);
    device->free_bytes = 1 * (1024ull * 1024ull * 1024ull);
  }

  return device;
}

std::vector<const DummyDeviceInfo*> ListStorageDevices(bool include_readonly) {
  // FIXME: Should probably check content flags here instead.
  std::vector<const DummyDeviceInfo*> devices;

  for (const auto& device_info : dummy_device_infos_) {
    if (!include_readonly && device_info->device_type == DeviceType::ODD) {
      continue;
    }
    devices.emplace_back(device_info);
  }

  return devices;
}

dword_result_t XamContentGetDeviceName_entry(dword_t device_id,
                                             dword_t name_buffer_ptr,
                                             dword_t name_capacity) {
  auto device_info = GetDummyDeviceInfo(device_id);
  if (device_info == nullptr) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  auto name = std::u16string(device_info->name);
  if (name_capacity < name.size() + 1) {
    return X_ERROR_INSUFFICIENT_BUFFER;
  }

  char16_t* name_buffer =
      kernel_memory()->TranslateVirtual<char16_t*>(name_buffer_ptr);

  xe::string_util::copy_and_swap_truncating(name_buffer, name, name_capacity);
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamContentGetDeviceName, kContent, kImplemented);

dword_result_t XamContentGetDeviceState_entry(dword_t device_id,
                                              lpunknown_t overlapped_ptr) {
  auto device_info = GetDummyDeviceInfo(device_id);
  if (device_info == nullptr) {
    if (overlapped_ptr) {
      kernel_state()->CompleteOverlappedImmediateEx(
          overlapped_ptr, X_ERROR_FUNCTION_FAILED, X_ERROR_DEVICE_NOT_CONNECTED,
          0);
      return X_ERROR_IO_PENDING;
    } else {
      return X_ERROR_DEVICE_NOT_CONNECTED;
    }
  }
  if (overlapped_ptr) {
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr,
                                                X_ERROR_SUCCESS);
    return X_ERROR_IO_PENDING;
  } else {
    return X_ERROR_SUCCESS;
  }
}
DECLARE_XAM_EXPORT1(XamContentGetDeviceState, kContent, kStub);

typedef struct {
  xe::be<uint32_t> device_id;
  xe::be<uint32_t> device_type;
  xe::be<uint64_t> total_bytes;
  xe::be<uint64_t> free_bytes;
  union {
    xe::be<uint16_t> name[28];
    char16_t name_chars[28];
  };
} X_CONTENT_DEVICE_DATA;
static_assert_size(X_CONTENT_DEVICE_DATA, 0x50);

dword_result_t XamContentGetDeviceData_entry(
    dword_t device_id, pointer_t<X_CONTENT_DEVICE_DATA> device_data) {
  auto device_info = GetDummyDeviceInfo(device_id);
  if (device_info == nullptr) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  device_data.Zero();
  device_data->device_id = static_cast<uint32_t>(device_info->device_id);
  device_data->device_type = static_cast<uint32_t>(device_info->device_type);
  device_data->total_bytes =
      device_info->device_type == DeviceType::HDD
          ? kernel_state()->content_manager()->GetContentTotalSpace()
          : device_info->total_bytes;
  device_data->free_bytes =
      device_info->device_type == DeviceType::HDD
          ? kernel_state()->content_manager()->GetContentFreeSpace()
          : device_info->free_bytes;
  xe::string_util::copy_and_swap_truncating(
      device_data->name_chars, device_info->name,
      xe::countof(device_data->name_chars));
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamContentGetDeviceData, kContent, kImplemented);

dword_result_t XamContentGetLocalizedDeviceData_entry(
    dword_t device_id, pointer_t<X_CONTENT_DEVICE_DATA> device_data) {
  auto device_info = GetDummyDeviceInfo(device_id);
  if (device_info == nullptr) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  device_data.Zero();
  device_data->device_id = static_cast<uint32_t>(device_info->device_id);
  device_data->device_type = static_cast<uint32_t>(device_info->device_type);
  device_data->total_bytes = static_cast<uint64_t>(device_info->total_bytes);
  device_data->free_bytes = static_cast<uint64_t>(device_info->free_bytes);
  xe::string_util::copy_and_swap_truncating(
      device_data->name_chars, device_info->name,
      xe::countof(device_data->name_chars));
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamContentGetLocalizedDeviceData, kContent, kImplemented);

dword_result_t XamContentCreateDeviceEnumerator_entry(dword_t content_type,
                                                      dword_t content_flags,
                                                      dword_t max_count,
                                                      lpdword_t buffer_size_ptr,
                                                      lpdword_t handle_out) {
  assert_not_null(handle_out);

  if (buffer_size_ptr) {
    *buffer_size_ptr = sizeof(X_CONTENT_DEVICE_DATA) * max_count;
  }

  auto e = make_object<XStaticEnumerator<X_CONTENT_DEVICE_DATA>>(kernel_state(),
                                                                 max_count);
  auto result = e->Initialize(XUserIndexNone, 0xFE, 0x2000A, 0x20009, 0);
  if (XFAILED(result)) {
    return result;
  }

  for (const auto& device_info : dummy_device_infos_) {
    if (device_info->device_type == DeviceType::ODD &&
        (content_flags & XContentFlag::kExcludeReadOnlyDevices)) {
      continue;
    }

    // Copy our dummy device into the enumerator
    auto device_data = e->AppendItem();
    assert_not_null(device_data);
    if (device_data) {
      device_data->device_id = static_cast<uint32_t>(device_info->device_id);
      device_data->device_type = static_cast<uint32_t>(device_info->device_type);
      device_data->total_bytes = static_cast<uint64_t>(device_info->total_bytes);
      device_data->free_bytes = static_cast<uint64_t>(device_info->free_bytes);
      xe::string_util::copy_and_swap_truncating(
          device_data->name_chars, device_info->name,
          xe::countof(device_data->name_chars));
    }
  }

  *handle_out = e->handle();
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamContentCreateDeviceEnumerator, kNone, kImplemented);

// XamNetworkStorageHasUserEnabledStorage (0x638)
// Returns BOOL — TRUE if user has enabled cloud storage.
dword_result_t XamNetworkStorageHasUserEnabledStorage_entry(
    dword_t user_index) {
  return 1;  // TRUE — cloud storage is enabled
}
DECLARE_XAM_EXPORT1(XamNetworkStorageHasUserEnabledStorage, kContent, kStub);

// XamNetworkStorageGetNetworkDevice (0x614)
// Takes 2 args: user_index (r3) and device_data pointer (r4).
dword_result_t XamNetworkStorageGetNetworkDevice_entry(
    dword_t user_index, pointer_t<X_CONTENT_DEVICE_DATA> device_data) {
  if (device_data) {
    device_data.Zero();
    device_data->device_id =
        static_cast<uint32_t>(DummyDeviceId::CloudStorage);
    device_data->device_type =
        static_cast<uint32_t>(DeviceType::CloudStorage);
    device_data->total_bytes = dummy_cloud_device_info_.total_bytes;
    device_data->free_bytes = dummy_cloud_device_info_.free_bytes;
    xe::string_util::copy_and_swap_truncating(
        device_data->name_chars, dummy_cloud_device_info_.name,
        xe::countof(device_data->name_chars));
  }
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamNetworkStorageGetNetworkDevice, kContent, kStub);

// XamNetworkStorageDeviceHasCacheFile (0x615)
// Returns whether a cache file exists on the device.
// Dashboard shows "No items found" if this returns 0.
dword_result_t XamNetworkStorageDeviceHasCacheFile_entry(
    dword_t user_index, dword_t device_id, lpvoid_t unused) {
  return 1;  // TRUE — cache file exists
}
DECLARE_XAM_EXPORT1(XamNetworkStorageDeviceHasCacheFile, kContent, kStub);

// XamNetworkStorageGetStatus (0x618)
// Gets sync status. Dashboard checks if status == 2 (synced/ready).
dword_result_t XamNetworkStorageGetStatus_entry(
    dword_t user_index, lpdword_t status_ptr, lpvoid_t unused) {
  if (status_ptr) {
    *status_ptr = 2;  // 2 = synced/ready
  }
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamNetworkStorageGetStatus, kContent, kStub);

// XamPackageManagerGetExperienceMode (0x646)
// Gets the experience mode. Return 0 (standard mode).
dword_result_t XamPackageManagerGetExperienceMode_entry(
    lpdword_t mode_ptr) {
  if (mode_ptr) {
    *mode_ptr = 0;  // standard mode
  }
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamPackageManagerGetExperienceMode, kNone, kStub);

// XamNetworkStorageIsSupportedContentType (0x619)
// Returns whether the cloud supports a content type.
// Return TRUE for all types — dashboard may check this before enumerating.
dword_result_t XamNetworkStorageIsSupportedContentType_entry(
    dword_t content_type) {
  return 1;  // TRUE — all content types supported
}
DECLARE_XAM_EXPORT1(XamNetworkStorageIsSupportedContentType, kContent, kStub);

// XamLogLocalizationEtx (0x59F)
// Logging function — no-op.
dword_result_t XamLogLocalizationEtx_entry() {
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamLogLocalizationEtx, kNone, kStub);

// XamNetworkStorageGetUserProperties (0x631)
// Fills in user cloud storage properties (total/used bytes).
// Dashboard uses these to show capacity on the cloud device.
// Args: user_index, total_bytes_ptr, used_bytes_ptr, handle
dword_result_t XamNetworkStorageGetUserProperties_entry(
    dword_t user_index, lpvoid_t total_bytes_ptr,
    lpvoid_t used_bytes_ptr, lpvoid_t handle) {
  auto mem = kernel_memory();
  if (total_bytes_ptr) {
    auto ptr = mem->TranslateVirtual<xe::be<uint64_t>*>(total_bytes_ptr);
    *ptr = dummy_cloud_device_info_.total_bytes;
  }
  if (used_bytes_ptr) {
    auto ptr = mem->TranslateVirtual<xe::be<uint64_t>*>(used_bytes_ptr);
    *ptr = dummy_cloud_device_info_.total_bytes -
           dummy_cloud_device_info_.free_bytes;
  }
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamNetworkStorageGetUserProperties, kContent, kStub);

// XamNetworkStorageGetTitleProperties (0x627)
// Gets title-specific cloud storage properties.
dword_result_t XamNetworkStorageGetTitleProperties_entry(
    dword_t user_index, dword_t title_id, lpvoid_t buffer,
    dword_t buffer_size, lpvoid_t out_ptr) {
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamNetworkStorageGetTitleProperties, kContent, kStub);

// XamNetworkStorageUserHasPrivilege (0x629)
// Returns whether user has cloud storage privilege.
dword_result_t XamNetworkStorageUserHasPrivilege_entry(
    dword_t user_index) {
  return 1;  // TRUE — user has privilege
}
DECLARE_XAM_EXPORT1(XamNetworkStorageUserHasPrivilege, kContent, kStub);

// XamNetworkStorageDeleteCacheOnDevice (0x617)
// Deletes cache on device. No-op for our implementation.
dword_result_t XamNetworkStorageDeleteCacheOnDevice_entry(
    dword_t user_index, dword_t device_id) {
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamNetworkStorageDeleteCacheOnDevice, kContent, kStub);

// XamBackgroundDownloadNetworkStorageEnable (0x4D1)
// Enables/disables background download for cloud storage.
dword_result_t XamBackgroundDownloadNetworkStorageEnable_entry(
    dword_t enable, lpvoid_t unused) {
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamBackgroundDownloadNetworkStorageEnable, kContent, kStub);

// XamBackgroundDownloadNetworkStorageRegisterChangeCallback (0x4E4)
// Registers a callback for content changes. No-op.
dword_result_t XamBackgroundDownloadNetworkStorageRegisterChangeCallback_entry(
    lpvoid_t callback, lpvoid_t context) {
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamBackgroundDownloadNetworkStorageRegisterChangeCallback,
                    kContent, kStub);

// XamBackgroundDownloadNetworkStorageOnContentChange (0x4DF)
// Notifies of content change. No-op.
dword_result_t XamBackgroundDownloadNetworkStorageOnContentChange_entry(
    dword_t user_index, lpvoid_t content_info) {
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamBackgroundDownloadNetworkStorageOnContentChange,
                    kContent, kStub);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

DECLARE_XAM_EMPTY_REGISTER_EXPORTS(ContentDevice);
