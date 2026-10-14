#pragma once
#include "chat/infra/reticulum/reticulum_wire.h"
#include "chat/ports/i_mesh_adapter.h"
namespace app
{
struct FakeMessagingFacade
{
    chat::IMeshAdapter* adapter = nullptr;
    chat::IMeshAdapter* getMeshAdapter() { return adapter; }
};
inline FakeMessagingFacade fake_messaging;
inline bool hasAppFacade() { return true; }
inline FakeMessagingFacade& messagingFacade() { return fake_messaging; }
} // namespace app
// These deterministic public hash stubs exercise dispatch and buffer ownership.
// Real SHA-256/Ed25519 interoperability is verified by separate vector tests.
namespace chat::reticulum
{
inline void computeIdentityHash(const uint8_t public_key[64], uint8_t output[16]) { std::memcpy(output, public_key, 16); }
inline void computeNameHash(const char*, const char*, uint8_t output[10]) { std::memset(output, 0x41, 10); }
inline void computeDestinationHash(const uint8_t*, const uint8_t identity_hash[16], uint8_t output[16]) { std::memcpy(output, identity_hash, 16); }
} // namespace chat::reticulum
