/* Shared C11/C++17 assertions exercise exactly the same ABI layout. */
#include <cordis_abi/plugin.h>
#include <stddef.h>
#ifdef __cplusplus
#define ABI_ASSERT static_assert
#define ABI_ALIGN alignof
#else
#define ABI_ASSERT _Static_assert
#define ABI_ALIGN _Alignof
#endif
ABI_ASSERT(sizeof(CordisStatus) == 4, "status width");
ABI_ASSERT(sizeof(CordisHandle) == 8 && sizeof(CordisRequestId) == 8, "handle width");
ABI_ASSERT(CORDIS_OK == 0 && CORDIS_PENDING == 1 && CORDIS_E_ABI_PROTOCOL == -7, "wire codes");
ABI_ASSERT(sizeof(CordisTableHeaderV1) == 16, "table header");
ABI_ASSERT(offsetof(CordisTableHeaderV1, struct_size) == 0, "header size");
ABI_ASSERT(offsetof(CordisTableHeaderV1, abi_major) == 4, "header major");
ABI_ASSERT(offsetof(CordisTableHeaderV1, abi_minor) == 8, "header minor");
ABI_ASSERT(offsetof(CordisTableHeaderV1, reserved) == 12, "header reserved");
#if UINTPTR_MAX == UINT64_MAX
ABI_ASSERT(sizeof(CordisBytesV1) == 16 && ABI_ALIGN(CordisBytesV1) == 8, "bytes");
ABI_ASSERT(offsetof(CordisBytesV1, data) == 0 && offsetof(CordisBytesV1, size) == 8, "byte span offsets");
ABI_ASSERT(sizeof(CordisServiceKeyV1) == 32, "service key");
ABI_ASSERT(offsetof(CordisServiceKeyV1, name) == 0, "service name");
ABI_ASSERT(offsetof(CordisServiceKeyV1, interface_major) == 16, "service major");
ABI_ASSERT(offsetof(CordisServiceKeyV1, reserved) == 20, "service reserved");
ABI_ASSERT(offsetof(CordisServiceKeyV1, realm) == 24, "realm offset");
ABI_ASSERT(sizeof(CordisServiceMetadataV1) == 24, "metadata");
ABI_ASSERT(offsetof(CordisServiceMetadataV1, struct_size) == 0, "metadata size");
ABI_ASSERT(offsetof(CordisServiceMetadataV1, minor) == 4, "metadata minor");
ABI_ASSERT(offsetof(CordisServiceMetadataV1, capabilities_json) == 8, "capabilities offset");
ABI_ASSERT(sizeof(CordisHostV1) == 112 && ABI_ALIGN(CordisHostV1) == 8, "host");
ABI_ASSERT(offsetof(CordisHostV1, capabilities) == 16, "host capabilities");
ABI_ASSERT(offsetof(CordisHostV1, reserved) == 24, "host reserved");
ABI_ASSERT(offsetof(CordisHostV1, complete) == 32, "complete");
ABI_ASSERT(offsetof(CordisHostV1, log) == 40, "log");
ABI_ASSERT(offsetof(CordisHostV1, request_cancel) == 48, "request cancel");
ABI_ASSERT(offsetof(CordisHostV1, dispatch) == 56, "dispatch");
ABI_ASSERT(offsetof(CordisHostV1, service_register) == 64, "service register");
ABI_ASSERT(offsetof(CordisHostV1, service_withdraw) == 72, "service withdraw");
ABI_ASSERT(offsetof(CordisHostV1, service_invoke) == 80, "service invoke");
ABI_ASSERT(offsetof(CordisHostV1, effect_create) == 88, "effect create");
ABI_ASSERT(offsetof(CordisHostV1, effect_ready) == 96, "effect ready");
ABI_ASSERT(offsetof(CordisHostV1, effect_request_cleanup) == 104, "effect cleanup");
ABI_ASSERT(sizeof(CordisPluginApiV1) == 152 && ABI_ALIGN(CordisPluginApiV1) == 8, "plugin");
ABI_ASSERT(offsetof(CordisPluginApiV1, capabilities) == 16, "plugin capabilities");
ABI_ASSERT(offsetof(CordisPluginApiV1, required_host_capabilities) == 24, "plugin required capabilities");
ABI_ASSERT(offsetof(CordisPluginApiV1, module_id) == 32, "module id");
ABI_ASSERT(offsetof(CordisPluginApiV1, module_version) == 48, "module version");
ABI_ASSERT(offsetof(CordisPluginApiV1, plugin_count) == 64, "plugin count");
ABI_ASSERT(offsetof(CordisPluginApiV1, reserved) == 68, "plugin reserved");
ABI_ASSERT(offsetof(CordisPluginApiV1, plugins) == 72, "catalog");
ABI_ASSERT(offsetof(CordisPluginApiV1, create) == 80, "create");
ABI_ASSERT(offsetof(CordisPluginApiV1, start) == 88, "start");
ABI_ASSERT(offsetof(CordisPluginApiV1, request_stop) == 96, "stop");
ABI_ASSERT(offsetof(CordisPluginApiV1, destroy) == 104, "destroy");
ABI_ASSERT(offsetof(CordisPluginApiV1, cancel_request) == 112, "cancel");
ABI_ASSERT(offsetof(CordisPluginApiV1, request_complete) == 120, "request complete");
ABI_ASSERT(offsetof(CordisPluginApiV1, dispatch) == 128, "plugin dispatch");
ABI_ASSERT(offsetof(CordisPluginApiV1, invoke_service) == 136, "plugin service");
ABI_ASSERT(offsetof(CordisPluginApiV1, request_cleanup) == 144, "plugin cleanup");
ABI_ASSERT(sizeof(CordisQueryFnV1) == 8, "query entry");
#endif
#undef ABI_ASSERT
#undef ABI_ALIGN
