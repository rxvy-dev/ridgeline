#include "gps.h"
#include <queue>
#include <algorithm>
#include <cmath>

int Gps::cellOf(vec3 p) const {
    int x = std::clamp(int((p.x + World::HALF) / CELL), 0, gN - 1);
    int z = std::clamp(int((p.z + World::HALF) / CELL), 0, gN - 1);
    return z * gN + x;
}

void Gps::build(const World& w) {
    nodes.clear();
    adj.clear();
    gN = int(2 * World::HALF / CELL) + 1;
    grid.assign(size_t(gN) * gN, {});
    const int STEP = 3;  // ~12 m between nodes
    for (int r = 0; r < int(w.roads.size()); r++) {
        const Road& rd = w.roads[r];
        int first = int(nodes.size());
        int n = int(rd.pts.size());
        for (int i = 0; i < n; i += STEP) nodes.push_back({rd.pts[i], r, i});
        if (!rd.closed && (n - 1) % STEP != 0) nodes.push_back({rd.pts[n - 1], r, n - 1});
        int last = int(nodes.size());
        adj.resize(nodes.size());
        for (int i = first; i + 1 < last; i++) {
            float d = length(nodes[i + 1].p - nodes[i].p);
            adj[i].push_back({i + 1, d});
            adj[i + 1].push_back({i, d});
        }
        if (rd.closed && last - first > 2) {
            float d = length(nodes[first].p - nodes[last - 1].p);
            adj[first].push_back({last - 1, d});
            adj[last - 1].push_back({first, d});
        }
    }
    for (int i = 0; i < int(nodes.size()); i++) grid[cellOf(nodes[i].p)].push_back(i);
    // junctions: link nodes of different roads that come close
    for (int i = 0; i < int(nodes.size()); i++) {
        const vec3& p = nodes[i].p;
        int cx = std::clamp(int((p.x + World::HALF) / CELL), 0, gN - 1), cz = std::clamp(int((p.z + World::HALF) / CELL), 0, gN - 1);
        for (int dz = -1; dz <= 1; dz++)
            for (int dx = -1; dx <= 1; dx++) {
                int x = cx + dx, z = cz + dz;
                if (x < 0 || z < 0 || x >= gN || z >= gN) continue;
                for (int j : grid[z * gN + x]) {
                    if (j <= i || nodes[j].road == nodes[i].road) continue;
                    float d = length(nodes[j].p - p);
                    if (d < 16.f) {
                        adj[i].push_back({j, d});
                        adj[j].push_back({i, d});
                    }
                }
            }
    }
}

int Gps::nearestNode(const World&, vec3 p, float maxDist) const {
    int best = -1;
    float bd = maxDist * maxDist;
    int cx = std::clamp(int((p.x + World::HALF) / CELL), 0, gN - 1), cz = std::clamp(int((p.z + World::HALF) / CELL), 0, gN - 1);
    int rad = int(maxDist / CELL) + 1;
    for (int ring = 0; ring <= rad; ring++) {
        for (int dz = -ring; dz <= ring; dz++)
            for (int dx = -ring; dx <= ring; dx++) {
                if (std::max(std::abs(dx), std::abs(dz)) != ring) continue;
                int x = cx + dx, z = cz + dz;
                if (x < 0 || z < 0 || x >= gN || z >= gN) continue;
                for (int j : grid[z * gN + x]) {
                    vec3 d = nodes[j].p - p;
                    d.y = 0;
                    float dd = dot(d, d);
                    if (dd < bd) { bd = dd; best = j; }
                }
            }
        if (best >= 0 && ring * CELL > std::sqrt(bd) + CELL) break;
    }
    return best;
}

bool Gps::route(const World& w, vec3 from, vec3 to) {
    path.clear();
    pathLen = 0;
    int s = nearestNode(w, from), t = nearestNode(w, to);
    if (s < 0 || t < 0) {
        path = {from, to};
        pathLen = length(to - from);
        return false;
    }
    std::vector<float> dist(nodes.size(), 1e30f);
    std::vector<int> prev(nodes.size(), -1);
    using QE = std::pair<float, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> q;
    dist[s] = 0;
    q.push({0, s});
    while (!q.empty()) {
        auto [d, u] = q.top();
        q.pop();
        if (d > dist[u]) continue;
        if (u == t) break;
        for (auto [v, c] : adj[u])
            if (d + c < dist[v]) { dist[v] = d + c; prev[v] = u; q.push({dist[v], v}); }
    }
    if (dist[t] >= 1e29f) {
        path = {from, to};
        pathLen = length(to - from);
        return false;
    }
    std::vector<vec3> rev;
    rev.push_back(to);
    for (int v = t; v >= 0; v = prev[v]) rev.push_back(nodes[v].p);
    rev.push_back(from);
    path.assign(rev.rbegin(), rev.rend());
    for (size_t i = 1; i < path.size(); i++) pathLen += length(path[i] - path[i - 1]);
    return true;
}
