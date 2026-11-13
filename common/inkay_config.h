//
// Created by ash on 17/12/24.
//

#ifndef INKAY_INKAY_CONFIG_H
#define INKAY_INKAY_CONFIG_H

#ifdef __has_include
#if __has_include("inkay_config.local.h")

#include "inkay_config.local.h"
#define INKAY_CUSTOM 1

#endif
#endif

#ifndef NETWORK_BASEURL
#define NETWORK_BASEURL "pretendo.cc"
#endif

// Shared by both the module (src/main.cpp) and the plugin (plugin/src/main.cpp) so their
// reported versions can't drift apart - bump this one place for a release.
#define INKAY_VERSION "v3.0.0-70"

#endif //INKAY_INKAY_CONFIG_H
