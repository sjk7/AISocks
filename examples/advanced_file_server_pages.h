// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#pragma once

#include <string>

namespace AdvancedFileServerPages {

std::string generateTestingInstructions();
std::string generateDemoPage();
std::string generateAccessLogTailPage(const std::string& tail);
std::string generateDirectoryListing(
    const std::string& dirPath, const std::string& documentRoot);
std::string generateErrorHtml(
    int code, const std::string& status, const std::string& message);

} // namespace AdvancedFileServerPages
