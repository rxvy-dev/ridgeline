// Road-network GPS: a graph over road samples with junction links, routed with Dijkstra.
#pragma once
#include "world.h"
#include <vector>

struct Gps {
    struct Node { vec3 p; int road, sample; };
    std::vector<Node> nodes;
    std::vector<std::vector<std::pair<int, float>>> adj;
    std::vector<vec3> path;     // current route polyline (world space), player end first
    float pathLen = 0.f;
    void build(const World& w);
    int nearestNode(const World& w, vec3 p, float maxDist = 400.f) const;
    // Plans a route from 'from' to 'to'. Returns false if no route.
    bool route(const World& w, vec3 from, vec3 to);
private:
    std::vector<std::vector<int>> grid;
    int gN = 0;
    static constexpr float CELL = 40.f;
    int cellOf(vec3 p) const;
};
