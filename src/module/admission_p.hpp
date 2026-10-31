/** @file admission_p.hpp
 * @brief 跨后端共用的实际创建准入；入场至外部调用/登记完成前不能取得预约。
 */
#pragma once
#include <cordis/result.hpp>
#include <memory>
#include <utility>

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
    friend Result<GenerationAdmission> admitGeneration(GenerationId);
    explicit GenerationAdmission(std::shared_ptr<Generation> generation) : generation_(std::move(generation)) {}
    void reset() noexcept;
    std::shared_ptr<Generation> generation_;
};
Result<GenerationAdmission> admitGeneration(GenerationId generation);
} // namespace cordis::module::detail
