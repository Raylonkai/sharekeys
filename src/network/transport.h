#pragma once
// The concrete ordered transport is Peer (peer.h). Each authenticated connection
// is a fresh session with independent directional sequence counters. InputEvent
// is serialized explicitly by protocol.h; C++ object memory is never sent.
#include "network/peer.h"
