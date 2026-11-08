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
struct Box { float x0, z0, x1, z1, top; };  // axis-aligned solid (buildings, containers)

// regions outside the original valley
struct Region { const char* name; float x0, z0, x1, z1; };
static const vec3 kFuji(-1700.f, 0.f, 3050.f);
static const float kFujiR = 2800.f, kFujiH = 960.f, kFujiBase = 40.f;
// radial gully pattern on Fuji (the terrain shader evaluates the same formula for the snow streaks)
inline float fujiRib(float ang, float fd) {
    return std::sin(ang * 34.f + 2.5f * std::sin(ang * 7.f + fd * 0.0021f) + 1.7f * std::sin(ang * 3.f - fd * 0.0013f));
}
static const Region kCity = {"Tokyo", 1785.f, -870.f, 2715.f, 225.f};
static const Region kDocks = {"Daikoku Wharf", 150.f, -2420.f, 2300.f, -1950.f};

// a house the player can buy: levelled plot beside a road, with a garage
struct Estate {
    std::string name, blurb;
    vec3 pos;           // plot centre (ground level)
    float yaw = 0;      // front faces the road
    float roadGap = 30; // plot centre to road centre
    int price = 0, style = 0;
    vec3 spawn;         // in front of the garage door
    float spawnYaw = 0;
};

struct World {
    static constexpr float HALF = 4000.f;   // world spans [-HALF, HALF] on x and z (8 x 8 km)
    static constexpr int N = 3201;          // heightmap vertices per side (2.5 m grid)
    static constexpr float CELL = 2 * HALF / (N - 1);
    float waterLevel = 6.f;
    struct Lake { vec3 c; float r; float seed; };
    std::vector<Lake> lakes = {{vec3(380, 0, -300), 250.f, 1.f}, {vec3(-640, 0, -960), 230.f, 2.3f}, {vec3(980, 0, -880), 190.f, 4.1f}, {vec3(520, 0, 430), 80.f, 5.7f}, {vec3(-1950, 0, 900), 260.f, 6.2f}};
    vec3 hubPos;           // festival hub centre
    vec3 festCenter;       // the festival grounds, beside the hub
    void rebuildBoxGrid();
    bool inBox(float x, float z, float pad) const;
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
    Mesh farTerrain;  // whole island at 20 m resolution, for the horizon beyond the chunks
    Mesh roadMesh, waterMesh, hubMesh, propsMesh, cityMesh, cityGlass;
    std::vector<Box> boxes;                       // solid buildings / containers
    std::vector<std::vector<int>> boxGrid;        // TGRID cells -> box indices
    MeshBuilder city, cityGl;                     // buildings, docks, street furniture
    bool urban(float x, float z, float pad = 0.f) const;
    std::vector<Estate> estates;
    void placeEstates();
    void erode(uint32_t seed);  // hydraulic erosion of the mountains (erosion.cpp)
    void buildEstates();
    bool estatePaved(float x, float z, float pad) const;
    bool nearEstate(float x, float z, float r) const;
    const char* estateBlurb(int i) const;
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
    void buildShrines();
    void buildJapan();        // towns, utility poles and wires, the pagoda
    void buildMapTexture();
    void buildUrban(uint32_t seed);
public:
    std::vector<Instance> pineInst, leafInst, rockInst;
    MeshBuilder props;  // marker posts + guardrails
};
