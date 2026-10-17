// dl masquerade
#pragma once

namespace masq {

// apply the version + challenge-response byte patches. idempotent, revision-specific, must run before connecting
bool Apply();

// true once Apply() has succeeded
bool Enabled();

} // namespace masq
