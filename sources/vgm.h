#pragma once

#ifdef __GNUC__
#define EXPORT __attribute__((visibility("default")))
#else
#define EXPORT
#endif

extern "C" {
#include <ip.h>

EXPORT extern const struct input_plugin_ops ip_ops;
EXPORT extern const int ip_priority;
EXPORT extern const char * const ip_extensions[];
EXPORT extern const char * const ip_mime_types[];
EXPORT extern const struct input_plugin_opt ip_options[];
EXPORT extern const unsigned ip_abi_version;

}
