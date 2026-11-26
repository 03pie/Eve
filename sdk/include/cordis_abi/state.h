/** @file state.h
 * @brief Windows x64 可选同步状态扩展。宿主按独立导出符号协商，用于替换前捕获和新实例启动前恢复。
 */
#ifndef CORDIS_ABI_STATE_H
#define CORDIS_ABI_STATE_H
#include <cordis_abi/plugin.h>
#define CORDIS_STATE_ABI_MAJOR UINT32_C(1)
#define CORDIS_STATE_ABI_MINOR UINT32_C(0)
#define CORDIS_STATE_MAX_ID_BYTES UINT32_C(128)
#define CORDIS_STATE_MAX_JSON_BYTES UINT64_C(1048576)

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 宿主设置 struct_size 并清零其余字段，插件只填写 version、id_size 和 id。
 * version 必须大于零。标识由 1 至 128 个可见 ASCII 字节组成，按 id_size 读取，无需终止符。
 */
typedef struct CordisStateSchemaV1 {
    uint32_t struct_size;
    uint32_t version;
    uint32_t id_size;
    uint32_t reserved;
    uint8_t id[CORDIS_STATE_MAX_ID_BYTES];
} CordisStateSchemaV1;

/** @brief 宿主一次提供并持有 data 缓冲区，容量最多为 CORDIS_STATE_MAX_JSON_BYTES。
 * 插件只可改写 size 及容量内的数据。成功时必须写入单个非空的 UTF-8 JSON 值。
 * 返回值异于 CORDIS_OK 时 size 必须为零。插件对缓冲区的借用随调用返回而结束。
 */
typedef struct CordisStateBufferV1 {
    uint32_t struct_size;
    uint32_t reserved;
    uint8_t* data;
    uint64_t capacity;
    uint64_t size;
} CordisStateBufferV1;

/** @brief 状态扩展的所有函数槽均为必填项，在宿主所属线程同步执行。
 * 查询入口、describe 和 validate 均不得创建业务资源。所有调用都禁止抛出异常、等待、驱动事件循环或返回 CORDIS_PENDING。
 * capture 返回一致的状态快照，无法立即取得时返回 CORDIS_E_BUSY，避免阻塞工作线程。
 * validate 将符合 describe 所报模式的转换结果写入宿主的目标缓冲区。
 * restore 在新实例已创建且尚未 start 时调用，需保留的借用输入须在返回前复制。
 */
typedef struct CordisStateApiV1 {
    CordisTableHeaderV1 header;
    /* UNSUPPORTED means this actual plugin ID is stateless; zero schema output. */
    CordisStatus(CORDIS_CALL* describe)(CordisBytesV1 plugin_id, CordisStateSchemaV1* schema);
    CordisStatus(CORDIS_CALL* validate)(CordisBytesV1 plugin_id, const CordisStateSchemaV1* source_schema,
                                        CordisBytesV1 source_json, CordisStateBufferV1* target_json);
    CordisStatus(CORDIS_CALL* capture)(CordisHandle instance, CordisStateBufferV1* json);
    CordisStatus(CORDIS_CALL* restore)(CordisHandle instance, const CordisStateSchemaV1* schema, CordisBytesV1 json);
} CordisStateApiV1;

/** @brief 符号缺失表示模块没有提供状态扩展。符号存在时，协商失败或函数表格式错误均须报告错误并保留状态。
 * 模块写入返回表时必须遵守宿主提供的可写容量。
 */
typedef CordisStatus(CORDIS_CALL* CordisQueryStateFnV1)(uint32_t abi_major, CordisStateApiV1* api);
/** @brief 返回指定主版本的状态函数表。宿主只在成功并校验完表头及所有槽位后调用。 */
CORDIS_EXPORT CordisStatus CORDIS_CALL cordis_query_state_v1(uint32_t abi_major, CordisStateApiV1* api);

#ifdef __cplusplus
}
#endif
#endif
