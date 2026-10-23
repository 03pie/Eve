/**
 * @file error.hpp
 * @brief 声明错误分类及其上下文信息。
 */
#pragma once

#include <cordis/identity.hpp>
#include <optional>
#include <string>
#include <vector>

namespace cordis {
/**
 * @brief 源代码 SDK 使用的错误分类。
 *
 * 发布 C ABI 时必须另行分配稳定且显式的线协议编号。
 */
enum class ErrorCode {
    InvalidArgument,
    WrongThread,
    StaleContext,
    ContextClosing,
    InstanceDisposed,
    DependencyMissing,
    DependencyVersion,
    DependencyCycle,
    ServiceConflict,
    ServiceRetiring,
    ProvisionIncomplete,
    ActivationFailed,
    CleanupFailed,
    CleanupTimeout,
    CleanupCycle,
    NextInvalid,
    ConfigInvalid,
    ConfigConflict,
    AbiMismatch,
    AbiProtocol,
    ModulePinned,
    RecoveryFailed,
    SchedulerClosed,
    OperationCancelled,
    CallbackFailed,
    StepLimitExceeded,
    BrokenPromise,
    ClockClosed
};

/**
 * @brief 描述错误原因、发生阶段、上下文及其因果链。
 */
struct ErrorInfo {
    ErrorCode code = ErrorCode::InvalidArgument;
    std::string message;
    OperationId operation;
    std::optional<InstanceId> instance;
    std::optional<std::string> entryPath;
    std::string phase;
    std::vector<ErrorInfo> causes;
};
} // namespace cordis
