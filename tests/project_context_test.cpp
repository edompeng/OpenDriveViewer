#include "src/core/project_context.h"
#include <gtest/gtest.h>
#include <string>

using namespace geoviewer::core;

TEST(ProjectContextTest, MapPathUpdates) {
    ProjectContext& context = ProjectContext::Instance();
    bool called = false;
    std::string new_path = "/path/to/map.xodr";
    
    context.OnMapChanged([&](const std::string& p) {
        called = true;
        EXPECT_EQ(p, new_path);
    });
    
    context.SetMapPath(new_path);
    EXPECT_EQ(context.MapPath(), new_path);
    EXPECT_TRUE(called);
}

TEST(ProjectContextTest, LayerVisibility) {
    ProjectContext& context = ProjectContext::Instance();
    context.SetLayerVisible(LayerType::kLanes, false);
    EXPECT_FALSE(context.IsLayerVisible(LayerType::kLanes));
    
    context.SetLayerVisible(LayerType::kLanes, true);
    EXPECT_TRUE(context.IsLayerVisible(LayerType::kLanes));
}
