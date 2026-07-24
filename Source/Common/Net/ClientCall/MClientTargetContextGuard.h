#pragma once

#include "Common/Net/ClientCall/MClientTargetResolver.h"

// MClientTargetContextGuard — RAII helper for binding the "current target"
// connection used by non-Broadcast CallClient invocations.
//
// Usage:
//   {
//       MClientTargetContextGuard Guard(Conn);
//       // ... call business API; any MFUNCTION(Async, CallClient) invoked
//       // while Guard is alive resolves to Conn via ResolveCurrentContext().
//   } // <- guard destructor calls ClearContext()
//
// Notes:
//   - Non-copyable, non-movable. The guard owns the binding for its lifetime.
//   - Only one guard may be active at a time per process; constructing a new
//     guard while another is alive will overwrite the previous binding (last
//     writer wins). For nested scopes, prefer scoping the guards explicitly.
//   - The guard does not own the connection — it only holds a reference.
class MClientTargetContextGuard
{
public:
    explicit MClientTargetContextGuard(TSharedPtr<INetConnection> Conn)
    {
        MClientTargetResolver::Get().BindContext(std::move(Conn));
    }

    ~MClientTargetContextGuard()
    {
        MClientTargetResolver::Get().ClearContext();
    }

    MClientTargetContextGuard(const MClientTargetContextGuard&) = delete;
    MClientTargetContextGuard& operator=(const MClientTargetContextGuard&) = delete;
    MClientTargetContextGuard(MClientTargetContextGuard&&) = delete;
    MClientTargetContextGuard& operator=(MClientTargetContextGuard&&) = delete;
};
