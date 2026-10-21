#include "Endpoint.h"

#include <cctype>
#include <cstring>

namespace core {

bool isValidHostName(const char* host) {
    if (!host) return false;
    const size_t n = strlen(host);
    if (n == 0 || n > 253) return false;
    if (host[0] == '.' || host[0] == '-' || host[n - 1] == '.') return false;
    for (size_t i = 0; i < n; ++i) {
        const unsigned char c = static_cast<unsigned char>(host[i]);
        if (!isalnum(c) && c != '-' && c != '.') return false;
    }
    return true;
}

}  // namespace core
