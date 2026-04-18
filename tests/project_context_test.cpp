#include "src/core/project_context.h"
#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace geoviewer::core;

TEST_CASE("ProjectContext Singleton and Map State", "[core]") {
    ProjectContext& context = ProjectContext::Instance();
    
    SECTION("Map Path Updates") {
        bool called = false;
        std::string new_path = "/path/to/map.xodr";
        
        context.OnMapChanged([&](const std::string& p) {
            called = true;
            CHECK(p == new_path);
        });
        
        context.SetMapPath(new_path);
        CHECK(context.MapPath() == new_path);
        CHECK(called == true);
    }
    
    SECTION("Layer Visibility") {
        context.SetLayerVisible(LayerType::kLanes, false);
        CHECK(context.IsLayerVisible(LayerType::kLanes) == false);
        
        context.SetLayerVisible(LayerType::kLanes, true);
        CHECK(context.IsLayerVisible(LayerType::kLanes) == true);
    }
}
