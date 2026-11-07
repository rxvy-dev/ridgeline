#include "carimport.h"
#include "inflate.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>

#define GL_TEXTURE_MAX_ANISOTROPY 0x84FE

namespace {

struct Reader {
    std::vector<uint8_t> d;
    size_t p = 0;
    bool ok = true;
    template <class T> T get() {
        T v{};
        if (p + sizeof(T) > d.size()) { ok = false; return v; }
        std::memcpy(&v, &d[p], sizeof(T));
        p += sizeof(T);
        return v;
    }
    const uint8_t* take(size_t n) {
        if (p + n > d.size()) { ok = false; return nullptr; }
        const uint8_t* r = &d[p];
        p += n;
        return r;
    }
};

std::map<std::string, std::string> readIni(const std::string& path) {
    std::map<std::string, std::string> kv;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
        while (!k.empty() && k.back() == ' ') k.pop_back();
        kv[k] = v;
    }
    return kv;
}

}  // namespace

bool ImportedModel::load(const std::string& d, std::string& err) {
    dir = d;
    Reader r;
    {
        std::ifstream f(d + "/model.rcar", std::ios::binary);
        if (!f) { err = "missing model.rcar"; return false; }
        r.d.assign(std::istreambuf_iterator<char>(f), {});
    }
    if (r.d.size() < 8 || std::memcmp(r.d.data(), "RCAR", 4) != 0) { err = "not an RCAR file"; return false; }
    r.p = 4;
    uint32_t ver = r.get<uint32_t>();
    if (ver != 1) { err = "unsupported RCAR version"; return false; }
    L = r.get<float>(); W = r.get<float>(); H = r.get<float>();
    for (int k = 0; k < 4; k++) {
        wc[k].x = r.get<float>(); wc[k].y = r.get<float>(); wc[k].z = r.get<float>();
        wr[k] = r.get<float>(); ww[k] = r.get<float>();
    }
    // textures: decode in parallel, upload on this thread
    uint32_t nt = r.get<uint32_t>();
    struct TexJob { int w, h; const uint8_t* src; uint32_t len; std::vector<uint8_t> px; bool ok; };
    std::vector<TexJob> jobs(nt);
    for (auto& j : jobs) {
        j.w = r.get<uint16_t>();
        j.h = r.get<uint16_t>();
        j.len = r.get<uint32_t>();
        j.src = r.take(j.len);
        if (!r.ok) { err = "truncated textures"; return false; }
    }
    {
        unsigned nth = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
        std::vector<std::thread> pool;
        size_t next = 0;
        std::mutex m;
        for (unsigned t = 0; t < nth; t++)
            pool.emplace_back([&]() {
                for (;;) {
                    size_t i;
                    { std::lock_guard<std::mutex> lk(m); i = next++; }
                    if (i >= jobs.size()) return;
                    jobs[i].ok = zlibInflate(jobs[i].src, jobs[i].len, jobs[i].px, size_t(jobs[i].w) * jobs[i].h * 4);
                }
            });
        for (auto& t : pool) t.join();
    }
    tex.resize(nt, 0);
    for (uint32_t i = 0; i < nt; i++) {
        if (!jobs[i].ok) continue;
        glGenTextures(1, &tex[i]);
        glBindTexture(GL_TEXTURE_2D, tex[i]);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, jobs[i].w, jobs[i].h, 0, GL_RGBA, GL_UNSIGNED_BYTE, jobs[i].px.data());
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, 16.f);
        glGetError();
        std::vector<uint8_t>().swap(jobs[i].px);
    }
    // materials
    uint32_t nm = r.get<uint32_t>();
    mats.resize(nm);
    for (auto& m : mats) {
        uint16_t nl = r.get<uint16_t>();
        r.take(nl);
        m.color.x = r.get<float>(); m.color.y = r.get<float>(); m.color.z = r.get<float>(); m.color.w = r.get<float>();
        m.base = r.get<int32_t>(); m.normal = r.get<int32_t>(); m.emissive = r.get<int32_t>();
        m.emis.x = r.get<float>(); m.emis.y = r.get<float>(); m.emis.z = r.get<float>();
        m.flags = r.get<uint32_t>();
        if (m.flags & IF_PAINT) paintable = true;
        for (int* t : {&m.base, &m.normal, &m.emissive})
            if (*t >= int(nt) || (*t >= 0 && !tex[*t])) *t = -1;
    }
    // parts
    uint32_t np = r.get<uint32_t>();
    float frontZ = std::max(wc[0].z, wc[1].z), rearZ = std::min(wc[2].z, wc[3].z);
    float hoodY = 0.f;
    for (uint32_t i = 0; i < np && r.ok; i++) {
        ImpPart part;
        part.role = int(r.get<uint32_t>());
        part.mat = r.get<int32_t>();
        uint32_t nv = r.get<uint32_t>(), ni = r.get<uint32_t>();
        const float* vd = reinterpret_cast<const float*>(r.take(size_t(nv) * 32));
        const uint32_t* id = reinterpret_cast<const uint32_t*>(r.take(size_t(ni) * 4));
        if (!r.ok) break;
        std::vector<Vertex> v(nv);
        for (uint32_t k = 0; k < nv; k++) {
            const float* q = vd + k * 8;
            v[k] = {vec3(q[0], q[1], q[2]), vec3(q[3], q[4], q[5]), vec4(q[6], q[7], 0, 0)};
            if (part.role == 0 && std::fabs(q[0]) < 0.3f && q[2] < frontZ - 0.15f && q[2] > frontZ - 0.75f) hoodY = std::max(hoodY, q[1]);
        }
        std::vector<uint32_t> idx(id, id + ni);
        part.mesh.upload(v, idx);
        parts.push_back(std::move(part));
    }
    if (!r.ok) { err = "truncated geometry"; return false; }
    // draw opaque first, glass last (sorted once)
    std::stable_sort(parts.begin(), parts.end(), [&](const ImpPart& a, const ImpPart& b) {
        auto g = [&](const ImpPart& p) { return p.mat >= 0 && (mats[p.mat].flags & (IF_GLASS | IF_BLEND)) ? 1 : 0; };
        return g(a) < g(b);
    });
    float wbMid = (frontZ + rearZ) * 0.5f;
    eye = vec3(W * 0.19f, H * 0.80f, wbMid - 0.08f * (frontZ - rearZ));
    hood = vec3(0, (hoodY > 0.3f ? hoodY : H * 0.7f) + 0.14f, frontZ - 0.35f);
    bumper = vec3(0, std::max(0.45f, wr[0] * 1.4f), L * 0.5f + 0.12f);
    return true;
}

std::vector<ImportedModel*> loadImportedCars(const std::vector<std::string>& dirs) {
    std::vector<ImportedModel*> out;
    std::vector<std::string> seen;
    for (const auto& base : dirs) {
        char** entries = SDL_GlobDirectory(base.c_str(), nullptr, 0, nullptr);
        if (!entries) continue;
        std::vector<std::string> names;
        for (char** e = entries; *e; e++) names.push_back(*e);
        SDL_free(entries);
        std::sort(names.begin(), names.end());
        for (const auto& n : names) {
            std::string dir = base + "/" + n;
            auto kv = readIni(dir + "/car.ini");
            if (kv.empty()) continue;
            std::string id = kv.count("id") ? kv["id"] : n;
            if (std::find(seen.begin(), seen.end(), id) != seen.end()) continue;
            Uint64 t0 = SDL_GetTicks();
            auto* m = new ImportedModel();
            std::string err;
            if (!m->load(dir, err)) {
                SDL_Log("Car '%s' skipped: %s", dir.c_str(), err.c_str());
                delete m;
                continue;
            }
            seen.push_back(id);
            auto num = [&](const char* k, float d) { return kv.count(k) ? float(std::atof(kv[k].c_str())) : d; };
            CarSpec s{};
            s.make = kv.count("make") ? kv["make"] : "Imported";
            s.model = kv.count("name") ? kv["name"] : n;
            s.cls = kv.count("class") && !kv["class"].empty() ? char(std::toupper((unsigned char)kv["class"][0])) : 'B';
            s.price = int(num("price", 50000));
            s.powerKW = num("power_kw", 300);
            s.massKg = num("mass", 1400);
            s.topKmh = num("top_kmh", 280);
            std::string dr = kv.count("drive") ? kv["drive"] : "RWD";
            s.drive = (dr == "FWD" || dr == "fwd") ? FWD : (dr == "AWD" || dr == "awd") ? AWD : RWD;
            s.grip = num("grip", 1.1f);
            s.offroad = num("offroad", 0.4f);
            s.redline = num("redline", 7500);
            std::string body = kv.count("body") ? kv["body"] : "coupe";
            s.body = body == "muscle" ? BODY_MUSCLE : body == "hatch" ? BODY_HATCH : body == "midengine" ? BODY_MIDENGINE
                   : body == "truck" ? BODY_TRUCK : body == "gt" ? BODY_GT : body == "hyper" ? BODY_HYPER : body == "rally" ? BODY_RALLY : BODY_COUPE;
            s.color = vec3(0.75f, 0.1f, 0.08f);
            if (kv.count("color")) std::sscanf(kv["color"].c_str(), "%f,%f,%f", &s.color.x, &s.color.y, &s.color.z);
            s.stripes = false;
            s.spoiler = false;
            s.length = m->L;
            s.width = m->W;
            s.height = m->H;
            s.wheelbase = std::max(1.8f, (m->wc[0].z + m->wc[1].z - m->wc[2].z - m->wc[3].z) * 0.5f);
            s.wheelR = (m->wr[0] + m->wr[1] + m->wr[2] + m->wr[3]) * 0.25f;
            s.id = id;
            s.cylinders = int(num("cylinders", 6));
            s.credit = kv.count("credit") ? kv["credit"] : "";
            s.starter = num("starter", 0) != 0;
            s.importIdx = int(out.size());
            kCars.push_back(s);
            out.push_back(m);
            SDL_Log("Loaded car '%s %s' (%zu parts) in %llu ms", s.make.c_str(), s.model.c_str(), m->parts.size(),
                    (unsigned long long)(SDL_GetTicks() - t0));
        }
    }
    return out;
}
