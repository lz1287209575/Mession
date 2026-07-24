#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/IO/Socket/Socket.h"

#include <mutex>

// MClientTargetResolver — framework-level client-target resolution for
// server→client (CallClient) pushes.
//
// Scope:
//   - Single global registry of connected client INetConnection instances.
//   - A single "current target" bind context (RAII-driven via
//     MClientTargetContextGuard) used by non-Broadcast CallClient invocations
//     to find which connection should receive the push.
//   - Broadcast resolution (returns all registered connections) for
//     MFUNCTION(Async, CallClient, Broadcast).
//
// Design notes:
//   - Pure framework layer. No business-entity types (Faction/Team/...) are
//     exposed here — those are concerns of higher layers.
//   - Process-local singleton. Multi-process deployments each maintain their
//     own resolver (each Service process owns its client connections).
//   - Registration/unregistration is driven by the business SDK when a player
//     goes online/offline; bind/clear is driven by request-handling code that
//     knows the current player's connection.
//   - Resolution is thread-safe via an internal std::mutex.
class MClientTargetResolver
{
public:
    static MClientTargetResolver& Get();

    // Registers a connected client. Safe to call with nullptr (no-op).
    // Re-registering the same connection is idempotent.
    void RegisterConn(TSharedPtr<INetConnection> Conn);

    // Unregisters a connected client. Safe to call with nullptr (no-op).
    // If the unregistered connection is the current bind context, the bind
    // context is cleared.
    void UnregisterConn(TSharedPtr<INetConnection> Conn);

    // Sets the "current target" bind context used by ResolveCurrentContext.
    // Only one binding is active at a time per process. Prefer the RAII guard
    // (MClientTargetContextGuard) over calling these directly.
    void BindContext(TSharedPtr<INetConnection> Conn);

    // Clears the "current target" bind context. No-op if already empty.
    void ClearContext();

    // Resolves the current bind context to exactly one connection (or empty
    // when no context is bound or the bound connection is no longer connected).
    TVector<TSharedPtr<INetConnection>> ResolveCurrentContext();

    // Resolves to every currently-registered, still-connected client
    // connection. Used by Broadcast CallClient invocations.
    TVector<TSharedPtr<INetConnection>> ResolveBroadcast();

private:
    MClientTargetResolver() = default;

    mutable std::mutex Mutex;
    TMap<uint64, TSharedPtr<INetConnection>> Connections;
    TSharedPtr<INetConnection> CurrentContext;
};
