// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, and Java:
// https://pvs-studio.com

#pragma once
// ---------------------------------------------------------------------------
// HeaderMap.h -- ordered map for HTTP headers / query params
//
// std::map with std::less<> (transparent comparator) so find() accepts
// std::string_view directly without allocating a temporary std::string.
//
// To swap the storage implementation, edit the one 'using' line below.
// ---------------------------------------------------------------------------
#include <map>
#include <string>

namespace aiSocks {

using HeaderMap = std::map<std::string, std::string, std::less<>>;

} // namespace aiSocks
