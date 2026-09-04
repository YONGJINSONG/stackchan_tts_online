#pragma once

#include "agent/TailscaleFunnelRootCA.h"

// api.openai.com currently chains through Let's Encrypt. Reuse the official
// ISRG Root X1/X2 trust bundle used by the Funnel client so both HTTPS paths
// retain hostname and CA verification without maintaining duplicate PEM data.
static const char* root_ca_openai = TAILSCALE_FUNNEL_ROOT_CA;
