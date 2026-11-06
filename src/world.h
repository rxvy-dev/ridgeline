// Procedural open world: terrain heightmap, road network, forests, lake, festival hub.
#pragma once
#include "render.h"
#include <vector>
#include <string>

enum Surface { SURF_ASPHALT = 0, SURF_DIRT = 1, SURF_GRASS = 2, SURF_WATER = 3 };

struct Road {
    std::string name;
    std::vector<vec3> pts;   // dense samples (~4 m apart) including height
    std::vector<vec3> tan;   // unit tangent (horizontal)
    std::vector<float> dist; // cumulative distance
    bool closed = false;
    float width = 12.f;
    int surface = SURF_ASPHALT;
    float length = 0.f;
};

struct TreeTrunk { float x, z, r; };

struct World {
    static constexpr float HALF = 1500.f;   // world spans [-HALF, HALF] on x and z
    static constexpr int N = 1201;          // heightmap vertices per side (2.5 m grid)
    static constexpr float CELL = 2 * HALF / (N - 1);
    float waterLevel = 6.f;
    vec3 lakeCenter = vec3(380, 0, -300);
    float lakeRadius = 230.f;
    vec3 hubPos;           // festival hub centre
    float hubYaw = 0.f;    // spawn heading at hub
    vec3 hubSpawn;

    std::vector<float> height;     // N*N
    std::vector<float> roadDist;   // signed distance to nearest road edge (negative = on road)
    std::vector<uint8_t> roadId;   // nearest road index
    std::vector<Road> roads;
    std::vector<vec3> crossCountry; // checkpoint path through the fields

    // tree collision grid
    static constexpr float TGRID = 16.f;
    int tgN = 0;
    std::vector<std::vector<TreeTrunk>> trunkGrid;

    // GPU resources
    struct Chunk { Mesh mesh; vec3 center; };
    std::vector<Chunk> chunks;
    Mesh roadMesh, waterMesh, hubMesh, propsMesh;
    Mesh pineMesh, pineLod, leafMesh, leafLod, rockMesh, grassMesh;
    InstanceField pines, leafy, rocks;
    GLuint mapTex = 0, heightTex = 0, roadTex = 0;
    struct GrassRing { GLuint buf = 0; int count = 0; float spacing, inner, outer, scale; };
    std::vector<GrassRing> grassRings;
    void drawGrass(struct Shader& sh, vec3 eye, int rings) const;
    void buildGrass();
    static constexpr int MAP_RES = 1024;

    void generate(uint32_t seed);
    void buildGPU();

    float baseHeight(float x, float z) const;
    float heightAt(float x, float z) const;
    vec3 normalAt(float x, float z) const;
    float roadDistAt(float x, float z) const;
    int surfaceAt(float x, float z) const;
    // Nearest road sample to p. Returns false if nothing within maxDist.
    bool nearestRoad(vec3 p, int& road, int& sample, float maxDist = 1e9f) const;
    // Pushes a circle out of tree trunks. Returns impact speed factor (0 = no hit).
    bool collideTrees(vec3& p, float r, vec3& normal) const;

    // spatial hash over road samples
    static constexpr float RGRID = 24.f;
    int rgN = 0;
    std::vector<std::vector<std::pair<int, int>>> roadGrid;

private:
    void buildRoad(Road& r, const std::vector<vec3>& ctrl, bool closed);
    void computeRoadHeights();
    void carveTerrain();
    void scatter(uint32_t seed);
    void buildMapTexture();
    std::vector<Instance> pineInst, leafInst, rockInst;
    MeshBuilder props;  // marker posts + guardrails
};
