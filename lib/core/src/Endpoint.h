#pragma once

// Validation of the broker endpoint taken from secrets.ini.
namespace core {

// A bare DNS host name: letters, digits, '-' and '.', 1-253 chars. Rejects the usual copy-paste
// slips: a port suffix (":8883"), a scheme ("mqtts://"), a path, or whitespace.
bool isValidHostName(const char* host);

}  // namespace core
