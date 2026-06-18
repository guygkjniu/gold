#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <vector>
#include <string>

namespace goldsrc
{
struct BspMapSummary
{
    std::int32_t version = 0;
    std::uint32_t entitiesBytes = 0;
    std::uint32_t planes = 0;
    std::uint32_t texturesBytes = 0;
    std::uint32_t vertices = 0;
    std::uint32_t visibilityBytes = 0;
    std::uint32_t nodes = 0;
    std::uint32_t textureInfo = 0;
    std::uint32_t faces = 0;
    std::uint32_t lightingBytes = 0;
    std::uint32_t clipNodes = 0;
    std::uint32_t leaves = 0;
    std::uint32_t markSurfaces = 0;
    std::uint32_t edges = 0;
    std::uint32_t surfEdges = 0;
    std::uint32_t models = 0;
};

struct Vec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct BspRenderVertex
{
    Vec3 position;
    float u = 0.0f;
    float v = 0.0f;
    float lightU = 0.0f;
    float lightV = 0.0f;
};

struct BspRenderFace
{
    std::vector<BspRenderVertex> vertices;
    std::string textureName;
    std::uint32_t textureWidth = 1;
    std::uint32_t textureHeight = 1;
    float color[3] = {1.0f, 1.0f, 1.0f};
    std::uint32_t lightmapWidth = 0;
    std::uint32_t lightmapHeight = 0;
    std::vector<std::uint8_t> lightmapRgba;
    std::int32_t modelIndex = 0;
    bool pvsControlled = true;
    bool brushEntity = false;
    bool translucent = false;
    bool sky = false;
    bool visible = true;
};

struct BspEntityMarker
{
    Vec3 origin;
    Vec3 angles;
    std::int32_t body = 0;
    std::int32_t skin = 0;
    std::int32_t sequence = -1;
    std::string className;
    std::string model;
};

struct BspEntity
{
    std::size_t index = 0;
    std::map<std::string, std::string> keyValues;
    Vec3 origin;
    Vec3 angles;
    std::int32_t body = 0;
    std::int32_t skin = 0;
    std::int32_t sequence = -1;
    std::int32_t modelIndex = -1;
    std::string className;
    std::string targetName;
    std::string target;
    std::string model;
    bool hasOrigin = false;
    bool brushEntity = false;

    const std::string* value(const std::string& key) const;
};

struct BspCollisionPlane
{
    Vec3 normal;
    float distance = 0.0f;
};

struct BspCollisionNode
{
    std::int32_t plane = 0;
    std::int32_t children[2] = {0, 0};
};

struct BspCollisionModel
{
    Vec3 mins;
    Vec3 maxs;
    Vec3 origin;
    std::int32_t headNodes[4] = {0, 0, 0, 0};
};

struct BspBrushModel
{
    std::int32_t modelIndex = 0;
    Vec3 mins;
    Vec3 maxs;
    Vec3 origin;
};

struct BspVisibilityNode
{
    std::int32_t plane = 0;
    std::int32_t children[2] = {0, 0};
    std::int32_t parent = -1;
    std::uint16_t firstFace = 0;
    std::uint16_t faceCount = 0;
};

struct BspVisibilityLeaf
{
    std::int32_t contents = 0;
    std::int32_t visibilityOffset = -1;
    std::uint16_t firstMarkSurface = 0;
    std::uint16_t markSurfaceCount = 0;
    std::int32_t parent = -1;
};

struct BspTraceResult
{
    bool allSolid = false;
    bool startSolid = false;
    bool hit = false;
    float fraction = 1.0f;
    Vec3 endPosition;
    Vec3 planeNormal;
};

class BspMap
{
public:
    bool load(const std::filesystem::path& path, std::string& error);

    const std::filesystem::path& path() const;
    const BspMapSummary& summary() const;
    const std::vector<BspRenderFace>& renderFaces() const;
    const std::vector<BspEntity>& entities() const;
    const std::vector<BspEntityMarker>& entityMarkers() const;
    const std::vector<BspBrushModel>& brushModels() const;
    const std::vector<std::filesystem::path>& wadPaths() const;
    std::vector<std::uint8_t> visibleRenderFacesFromPoint(const Vec3& point) const;
    BspTraceResult traceHull(const Vec3& start, const Vec3& end, int hull) const;
    BspTraceResult traceHullForModel(std::int32_t modelIndex, const Vec3& start, const Vec3& end, int hull, const Vec3& modelOffset) const;
    std::int32_t pointContentsForHull(const Vec3& point, int hull) const;
    std::int32_t pointContentsForModel(std::int32_t modelIndex, const Vec3& point, int hull, const Vec3& modelOffset) const;

private:
    std::int32_t findVisibilityLeaf(const Vec3& point) const;
    std::vector<std::uint8_t> decompressPvs(std::int32_t visibilityOffset) const;
    bool hullHeadNodeForModel(std::int32_t modelIndex, int hull, std::int32_t& headNode) const;
    bool recursiveHullTrace(std::int32_t nodeIndex,
                            std::int32_t rootNode,
                            float startFraction,
                            float endFraction,
                            const Vec3& start,
                            const Vec3& end,
                            BspTraceResult& trace) const;
    std::int32_t hullPointContents(std::int32_t nodeIndex, const Vec3& point) const;

    std::filesystem::path path_;
    BspMapSummary summary_;
    std::vector<BspRenderFace> renderFaces_;
    std::vector<BspEntity> entities_;
    std::vector<BspEntityMarker> entityMarkers_;
    std::vector<BspBrushModel> brushModels_;
    std::vector<std::filesystem::path> wadPaths_;
    std::vector<BspCollisionPlane> collisionPlanes_;
    std::vector<BspCollisionNode> collisionNodes_;
    std::vector<BspCollisionModel> collisionModels_;
    std::vector<BspVisibilityNode> visibilityNodes_;
    std::vector<BspVisibilityLeaf> visibilityLeaves_;
    std::vector<std::uint16_t> markSurfaces_;
    std::vector<std::int32_t> originalFaceToRenderFace_;
    std::vector<std::uint8_t> visibilityBytes_;
};
}
