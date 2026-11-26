/** @file module_query.h
 * @brief Windows x64 只读模块发现 ABI。宿主用发现结果核对清单和卸载策略，业务函数表由 plugin.h 单独协商。
 */
#ifndef CORDIS_ABI_MODULE_QUERY_H
#define CORDIS_ABI_MODULE_QUERY_H
#include <stdint.h>

#if defined(_WIN32)
#define CORDIS_QUERY_CALL __cdecl
#if defined(CORDIS_MODULE_EXPORTS)
#define CORDIS_QUERY_EXPORT __declspec(dllexport)
#else
#define CORDIS_QUERY_EXPORT
#endif
#else
#define CORDIS_QUERY_CALL
#define CORDIS_QUERY_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define CORDIS_MODULE_QUERY_MAJOR UINT32_C(1)
#define CORDIS_MODULE_QUERY_MINOR UINT32_C(0)
#define CORDIS_MODULE_QUERY_EXPLICIT_UNLOAD_MINOR UINT32_C(1)
#define CORDIS_MODULE_RESIDENT UINT64_C(1)
#define CORDIS_MODULE_EXPLICIT_UNLOAD UINT64_C(2)
#define CORDIS_QUERY_OK INT32_C(0)
#define CORDIS_QUERY_UNSUPPORTED (-INT32_C(1))
#define CORDIS_QUERY_INVALID_ARGUMENT (-INT32_C(2))
#define CORDIS_QUERY_FAILED (-INT32_C(3))

typedef int32_t CordisQueryStatus;
typedef struct CordisQueryHeaderV1 {
    uint32_t struct_size;
    uint32_t query_major;
    uint32_t query_minor;
    uint32_t reserved;
} CordisQueryHeaderV1;

/** @brief 按 size 读取借用字节，无需终止符。size 大于零时，data 必须指向有效数据。 */
typedef struct CordisQueryBytesV1 {
    const uint8_t* data;
    uint64_t size;
} CordisQueryBytesV1;

/** @brief 模块只在发现调用期间借用请求，flags 与保留字段均为零。
 * 表头的 query_minor 为 0 时请求 CORDIS_MODULE_RESIDENT，为 1 时请求 CORDIS_MODULE_EXPLICIT_UNLOAD。
 * 两种发现请求的 runtime_abi_major 和 runtime_abi_minor 均指定业务 ABI 1.0。
 */
typedef struct CordisModuleQueryV1 {
    CordisQueryHeaderV1 header;
    uint32_t runtime_abi_major;
    uint32_t runtime_abi_minor;
    uint64_t flags;
} CordisModuleQueryV1;

/** @brief 模块拥有返回的字节和插件数组，并在模块映射的整个存续期内保持其可读且不可变。
 * 标识符遵循清单的 ASCII 规则，每个标识最多 128 字节，版本最多 64 字节，插件标识最多 64 个且必须唯一。
 * 保留字段必须为零。表头的 query_minor 必须与已接受的请求一致。次版本 0 的 flags 必须恰好为
 * CORDIS_MODULE_RESIDENT，次版本 1 必须恰好为 CORDIS_MODULE_EXPLICIT_UNLOAD。
 * 声明显式卸载的模块仍须经过宿主批准和实际的卸载安全检查。
 */
typedef struct CordisModuleDescriptorV1 {
    CordisQueryHeaderV1 header;
    uint32_t runtime_abi_major;
    uint32_t runtime_abi_minor;
    uint64_t flags;
    CordisQueryBytesV1 module_id;
    CordisQueryBytesV1 module_version;
    uint32_t plugin_count;
    uint32_t reserved;
    const CordisQueryBytesV1* plugins;
} CordisModuleDescriptorV1;

/** @brief 同步发现入口。调用期间禁止回调、创建资源、保留请求指针或向边界外传播异常。
 * 宿主在 output.header.struct_size 中写入可写容量。模块写入的字节数以该容量和自身描述结构长度的
 * 较小值为限，并在同一字段报告自身结构长度。版本无法协商时返回 CORDIS_QUERY_UNSUPPORTED。
 * 宿主仅在 CORDIS_QUERY_OK 时读取输出。业务插件的创建、注册和激活均在发现调用之外进行。
 */
typedef CordisQueryStatus(CORDIS_QUERY_CALL* CordisModuleQueryFnV1)(const CordisModuleQueryV1* request,
                                                                    CordisModuleDescriptorV1* output);
/** @brief 模块导出的发现入口。输入与输出容量遵循 CordisModuleQueryFnV1，宿主只在成功时读取描述。 */
CORDIS_QUERY_EXPORT CordisQueryStatus CORDIS_QUERY_CALL cordis_module_query_v1(const CordisModuleQueryV1* request,
                                                                               CordisModuleDescriptorV1* output);

#ifdef __cplusplus
}
#endif
#endif
