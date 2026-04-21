#include "src/core/crash_handler.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <string>

namespace geoviewer::core {
namespace {

TEST(CrashHandlerTest, InitializeCreatesDirectory) {
    std::string test_dir = "test_crash_dump_dir";
    
    // Ensure directory does not exist initially
    if (std::filesystem::exists(test_dir)) {
        std::filesystem::remove_all(test_dir);
    }
    EXPECT_FALSE(std::filesystem::exists(test_dir));

    // Initialize should create the directory
    CrashHandler::Initialize(test_dir);
    
    EXPECT_TRUE(std::filesystem::exists(test_dir));
    
    // Clean up
    std::filesystem::remove_all(test_dir);
}

} // namespace
} // namespace geoviewer::core
