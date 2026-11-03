/** @file admission_p.hpp
 * @brief 跨后端共用的实际创建准入；入场至外部调用/登记完成前不能取得预约。
 */
#pragma once
#include <cordis/result.hpp>
#include <memory>
#include <utility>

namespace cordis {
class Context;
}
namespace cordis::module::detail {
struct Generation;
class GenerationAdmission {
public:
    GenerationAdmission(const GenerationAdmission&) = delete;
    GenerationAdmission& operator=(const GenerationAdmission&) = delete;
    GenerationAdmission(GenerationAdmission&&) noexcept = default;
    GenerationAdmission& operator=(GenerationAdmission&& other) noexcept;
    ~GenerationAdmission();

private:
    friend Result<GenerationAdmission> admitGeneration(GenerationId, const Context*);
    friend Result<GenerationAdmission> admitGenerationLifecycle(GenerationId);
    explicit GenerationAdmission(std::shared_ptr<Generation> generation) : generation_(std::move(generation)) {}
    void reset() noexcept;
    std::shared_ptr<Generation> generation_;
};
Result<GenerationAdmission> admitGeneration(GenerationId generation, const Context* context = nullptr);
/** @brief 关闭前取得短期生命周期准入；拒绝预约/创建重入，但失败代次仍须可以清理。 */
Result<GenerationAdmission> admitGenerationLifecycle(GenerationId generation);
} // namespace cordis::module::detail
