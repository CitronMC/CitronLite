#pragma once

namespace citron::telemetry {

// The anonymous online count on citron.lol: a random id made each launch, kept in memory only, sent
// every 45 seconds while the switch in Settings is on.
void Start();
void Stop();
bool Enabled();
void SetEnabled(bool enabled);

} // namespace citron::telemetry
