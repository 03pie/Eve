#pragma once

namespace cordis {
// Diagnostic states, not a persisted format or a public binary ABI.
enum class InstanceState {
    Pending,
    Loading,
    Active,
    Unloading,
    Failed,
    Disposed
};

enum class BindingState { Staged, Published, Retiring, Released };
enum class EffectState { SettingUp, Live, Closing, Closed };
enum class StableOutcome {
    Active,
    PendingMissingDependency,
    Disabled,
    Failed,
    Disposed,
    Superseded
};
} // namespace cordis
