#pragma once

#include <cstdlib>
#include <cstring>

namespace xr64::rage_wars::recomp {

// Features the owner accepted for 0.5.0 are on by default. Setting the variable
// to "0" or "off" switches that one feature off; any other value, or no value,
// keeps it on. Read once per process by the callers, like the opt-in switches
// they replace.
inline bool feature_default_on(const char* name) {
#ifdef _MSC_VER
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0) return true;
    const bool off = value && (std::strcmp(value, "0") == 0 || std::strcmp(value, "off") == 0);
    std::free(value);
    return !off;
#else
    const char* value = std::getenv(name);
    return !(value && (std::strcmp(value, "0") == 0 || std::strcmp(value, "off") == 0));
#endif
}

// Experimental native code stays inactive unless the owner explicitly enables it.
inline bool feature_explicit_on(const char* name) {
#ifdef _MSC_VER
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0) return false;
    const bool on = value && (std::strcmp(value, "1") == 0 || std::strcmp(value, "on") == 0);
    std::free(value);
    return on;
#else
    const char* value = std::getenv(name);
    return value && (std::strcmp(value, "1") == 0 || std::strcmp(value, "on") == 0);
#endif
}

} // namespace xr64::rage_wars::recomp
