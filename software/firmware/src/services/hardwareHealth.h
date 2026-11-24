#pragma once

#include <cstdint>

// Stable serial evidence shared by release firmware and the hardware runner,
// printed in every build as RADR_HEALTH lines (hardwareHealthFormat.h).
// These hooks observe normal application work. They never advance the state
// machine, start a task, allocate memory or open a network connection.
void hardwareHealthBoot();
// Every got-IP, from the existing Wi-Fi event handler. ipv4 is in lwIP order.
void hardwareHealthWifiConnected(uint32_t ipv4);
// Called by the idle screens' draw loops on every pass.
void hardwareHealthMenuProgress();
// Called from a task that already runs; prints at most every 5 seconds.
void hardwareHealthHeartbeat();
