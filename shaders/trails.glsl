// Trails the integrated tracer draws itself, so they are lensed like the disk: predicted orbits and object path
// trails inside the integration sphere (SplitTrailsForTracer in src/rendering/orbit_overlays.cppm). Each ray step
// tests its chord against the segments of the grid cells it touches. SampleTrails in orbit_overlays.cppm is the
// C++ reference of trailSample. See docs/notes/orbit_overlays_and_nodes.md.

struct TrailSegment {
    vec4 a;     // xyz: start, in the first hole's local frame (render units)
    vec4 b;     // xyz: end
    vec4 color; // rgb
};

layout(set = 0, binding = 9, std430) readonly buffer TrailSegmentBlock { TrailSegment u_trailSegments[]; };
// Open-addressing hash table of the occupied grid cells: x = cell key (or 0xFFFFFFFF), y = start, z = count in
// u_trailIndices; size u_trailGrid2.x, a power of two
layout(set = 0, binding = 10, std430) readonly buffer TrailCellBlock { uvec4 u_trailCells[]; };
layout(set = 0, binding = 11, std430) readonly buffer TrailIndexBlock { uint u_trailIndices[]; };
// Occupancy bits of a coarse 32³ grid: steps far from every trail stop after one or two reads
layout(set = 0, binding = 12, std430) readonly buffer TrailCoarseBlock { uint u_trailCoarse[]; };

const int TRAIL_GRID_CELLS = 256;
const uint TRAIL_EMPTY_CELL = 0xFFFFFFFFu;
const int TRAIL_COARSE_CELLS = 64;

// Whether any coarse cell overlapping [lo, hi] holds a trail (CoarseTrailHit in orbit_overlays.cppm)
bool trailCoarseHit(vec3 lo, vec3 hi) {
    float radius = u_trailGrid.x;
    ivec3 c0 = clamp(ivec3(floor((lo + radius) / (2.0f * radius) * float(TRAIL_COARSE_CELLS))), ivec3(0), ivec3(TRAIL_COARSE_CELLS - 1));
    ivec3 c1 = clamp(ivec3(floor((hi + radius) / (2.0f * radius) * float(TRAIL_COARSE_CELLS))), ivec3(0), ivec3(TRAIL_COARSE_CELLS - 1));
    for (int z = c0.z; z <= c1.z; z++) {
        for (int y = c0.y; y <= c1.y; y++) {
            for (int x = c0.x; x <= c1.x; x++) {
                uint bit = uint((z * TRAIL_COARSE_CELLS + y) * TRAIL_COARSE_CELLS + x);
                if ((u_trailCoarse[bit >> 5u] & (1u << (bit & 31u))) != 0u) return true;
            }
        }
    }
    return false;
}

bool trailsEnabled() { return u_trailGrid.z > 0.5f; }

// Path length between trail tests in the ray marcher: one fine cell (0.4 M at a 50 M integration radius), the
// longest chord a single midpoint lookup covers exactly. The ray bends little over it: a 0.4 M chord on a light ray
// near the photon orbit has a sagitta of ~0.007 M, a fraction of a line's half-width at typical distances.
float trailTestSpacing() { return 2.0f * u_trailGrid.x / float(TRAIL_GRID_CELLS); }

// Closest distance between segments p0-p1 and q0-q1; s is the parameter of the closest point on p0-p1.
float segmentDistance(vec3 p0, vec3 p1, vec3 q0, vec3 q1, out float s) {
    vec3 d1 = p1 - p0;
    vec3 d2 = q1 - q0;
    vec3 r = p0 - q0;
    float a = dot(d1, d1);
    float e = dot(d2, d2);
    float f = dot(d2, r);
    float t = 0.0f;
    s = 0.0f;
    if (a <= 1e-12f && e <= 1e-12f) return length(r);
    if (a <= 1e-12f) {
        t = clamp(f / e, 0.0f, 1.0f);
    } else {
        float c = dot(d1, r);
        if (e <= 1e-12f) {
            s = clamp(-c / a, 0.0f, 1.0f);
        } else {
            float b = dot(d1, d2);
            float denom = a * e - b * b;
            s = denom > 1e-12f ? clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
            t = (b * s + f) / e;
            if (t < 0.0f) { t = 0.0f; s = clamp(-c / a, 0.0f, 1.0f); }
            else if (t > 1.0f) { t = 1.0f; s = clamp((b - c) / a, 0.0f, 1.0f); }
        }
    }
    return length(p0 + d1 * s - (q0 + d2 * t));
}

// Slot of a cell key in the hash table, or -1 for an empty cell (TrailHash / FindTrailCell in orbit_overlays.cppm)
int trailSlot(uint key) {
    uint size = uint(u_trailGrid2.x);
    uint mask = size - 1u;
    uint slot = (key * 2654435761u) & mask;
    for (uint probe = 0u; probe < size; probe++) {
        uint stored = u_trailCells[slot].x;
        if (stored == key) return int(slot);
        if (stored == TRAIL_EMPTY_CELL) return -1;
        slot = (slot + 1u) & mask;
    }
    return -1;
}

int trailCell(float v, float radius) {
    return clamp(int(floor((v + radius) / (2.0f * radius) * float(TRAIL_GRID_CELLS))), 0, TRAIL_GRID_CELLS - 1);
}

// One piece of trailSample (at most one fine cell long): the segments registered in the fine cell of its midpoint.
// best / bestIndex carry the nearest hit across pieces (SearchTrails in orbit_overlays.cppm).
void trailSearch(vec3 p0, vec3 p1, vec3 camera, inout float best, inout int bestIndex) {
    float radius = u_trailGrid.x;
    vec3 mid = 0.5f * (p0 + p1);
    if (any(greaterThan(abs(mid), vec3(radius)))) return;
    int slot = trailSlot(uint((trailCell(mid.z, radius) * TRAIL_GRID_CELLS + trailCell(mid.y, radius)) * TRAIL_GRID_CELLS
                              + trailCell(mid.x, radius)));
    if (slot < 0) return;
    uvec4 cell = u_trailCells[slot];
    for (uint k = cell.y; k < cell.y + cell.z; k++) {
        uint index = u_trailIndices[k];
        float s;
        float d = segmentDistance(p0, p1, u_trailSegments[index].a.xyz, u_trailSegments[index].b.xyz, s);
        float width = u_trailGrid.w * max(length(mix(p0, p1, s) - camera), 1e-6f);
        if (d / width < best) {
            best = d / width;
            bestIndex = int(index);
        }
    }
}

// Coverage (0..1) and colour of the nearest trail along the chord p0-p1 (hole-local); the line's half-width is
// u_trailGrid.w (radians) times the distance to the camera, so it keeps its width on screen. Long chords are split
// into pieces of at most one fine cell, each looking only into the cell of its midpoint.
float trailSample(vec3 p0, vec3 p1, vec3 camera, out vec3 color) {
    color = vec3(0.0f);
    if (!trailCoarseHit(min(p0, p1), max(p0, p1))) return 0.0f;
    float cellSize = 2.0f * u_trailGrid.x / float(TRAIL_GRID_CELLS);
    int pieces = clamp(int(ceil(length(p1 - p0) / cellSize)), 1, 128);
    float best = 1e30f;
    int bestIndex = -1;
    for (int i = 0; i < pieces; i++) {
        trailSearch(mix(p0, p1, float(i) / float(pieces)), mix(p0, p1, float(i + 1) / float(pieces)), camera, best, bestIndex);
    }
    if (bestIndex < 0 || best >= 1.0f) return 0.0f;
    color = u_trailSegments[bestIndex].color.rgb;
    return 1.0f - smoothstep(0.6f, 1.0f, best);
}
