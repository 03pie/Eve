#pragma once

#include <cordis/identity.hpp>
#include <optional>
#include <string>
#include <vector>

namespace cordis {
// Source SDK only. Assign explicit stable wire codes when publishing C ABI.
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
    OperationCancelled
};

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
