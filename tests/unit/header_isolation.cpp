// Deliberately include error.hpp first: SDK headers must own their includes.
#include <cordis/error.hpp>
#include <cordis/state.hpp>

bool sdkHeaderIsolation()
{
    cordis::ErrorInfo error;
    error.instance = cordis::InstanceId{1};
    error.causes.push_back(cordis::ErrorInfo{});
    return error.instance->valid() && error.causes.size() == 1;
}
