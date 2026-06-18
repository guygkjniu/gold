#pragma once

#include "engine/BspMap.h"
#include "engine/SoundSystem.h"
#include "engine/StudioModel.h"
#include "engine/WadFile.h"

#include <chrono>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace goldsrc
{
class Renderer
{
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool initialize(const std::string& title, int width, int height);
    bool renderFrame();
    bool isOpen() const;
    void shutdown();
    void setWorldFaces(std::vector<BspRenderFace> faces);
    void setMapEntities(std::vector<BspEntity> entities, std::vector<BspBrushModel> brushModels);
    void setCollisionMap(const BspMap* map);
    void setSoundSystem(SoundSystem* soundSystem);
    void setEntityMarkers(std::vector<BspEntityMarker> markers);
    void setTextureImages(std::vector<TextureImage> textures);
    void setStudioModels(std::vector<StudioModelSceneEntry> models);

private:
    struct PlatformState;

    void updateCamera(float deltaSeconds);
    void configureProjection();
    void uploadPendingTextures();
    void uploadLightmapTextures();
    void uploadStudioModelTextures();
    void destroyTextures();
    Vec3 toRenderSpace(const Vec3& point) const;
    Vec3 toMapSpace(const Vec3& point) const;
    void syncCameraToPlayer();
    bool unstickPlayerHull();
    Vec3 buildWishVelocity(float maxSpeed);
    void categorizePlayerPosition(int hull);
    void applyPlayerFriction(float deltaSeconds);
    void acceleratePlayer(const Vec3& wishDirection, float wishSpeed, float acceleration, float deltaSeconds);
    BspTraceResult tracePlayerHull(const Vec3& start, const Vec3& end, int hull) const;
    std::int32_t pointContentsWithBrushes(const Vec3& point, int hull) const;
    BspTraceResult flyMovePlayer(float deltaSeconds, int hull, bool recordTrace);
    BspTraceResult stepMovePlayer(float deltaSeconds, int hull);
    void updatePlayerPhysics(float deltaSeconds);
    void updateMapLogic(float deltaSeconds);
    void fireEntityTarget(const std::string& targetName);
    void useEntity(std::size_t entityIndex);
    void updateDebugTitle();
    void renderCube(float angleDegrees);
    void renderWorld();
    void renderEntityMarkers();
    void renderStudioModels();
    Vec3 brushModelOffset(std::int32_t modelIndex) const;
    Vec3 faceVertexPosition(const BspRenderFace& face, const BspRenderVertex& vertex) const;
    void emitFaceVertex(const BspRenderFace& face, const BspRenderVertex& vertex) const;
    void emitFaceLightmap(const BspRenderFace& face, std::size_t faceIndex) const;

    PlatformState* platform_ = nullptr;
    std::vector<BspRenderFace> worldFaces_;
    std::vector<BspEntity> mapEntities_;
    std::vector<BspBrushModel> brushModels_;
    std::unordered_map<std::string, std::vector<std::size_t>> targetNameIndex_;
    std::unordered_set<std::size_t> removedLogicEntities_;
    std::vector<BspEntityMarker> entityMarkers_;
    std::vector<StudioModelSceneEntry> studioModels_;
    std::vector<TextureImage> pendingTextures_;
    std::unordered_map<std::string, unsigned int> textureIds_;
    std::vector<unsigned int> lightmapTextureIds_;
    std::vector<std::vector<unsigned int>> studioTextureIds_;
    const BspMap* collisionMap_ = nullptr;
    SoundSystem* soundSystem_ = nullptr;
    Vec3 worldCenter_;
    Vec3 cameraPosition_ = {0.0f, 0.4f, 4.8f};
    Vec3 playerOriginMap_;
    Vec3 playerVelocity_;
    Vec3 worldMins_;
    Vec3 worldMaxs_;
    float worldScale_ = 1.0f;
    float playerEyeHeight_ = 28.0f;
    float lastMoveX_ = 0.0f;
    float lastMoveY_ = 0.0f;
    float cameraYawDegrees_ = 0.0f;
    float cameraPitchDegrees_ = 0.0f;
    bool grounded_ = false;
    bool noClip_ = false;
    bool nKeyWasDown_ = false;
    bool eKeyWasDown_ = false;
    bool showEntityMarkers_ = true;
    bool bKeyWasDown_ = false;
    bool lastMoveTraceHit_ = false;
    bool lastMoveTraceStartSolid_ = false;
    bool lastMoveTraceAllSolid_ = false;
    float lastMoveTraceFraction_ = 1.0f;
    std::int32_t lastHullContents_ = 0;
    std::size_t lastVisibleFaceCount_ = 0;
    int lastMovementHull_ = 1;
    bool mouseLookActive_ = false;
    bool mouseReleasedByEscape_ = false;
    bool hasLastMousePosition_ = false;
    int lastMouseX_ = 0;
    int lastMouseY_ = 0;
    int width_ = 0;
    int height_ = 0;
    float angleDegrees_ = 0.0f;
    float studioAnimationTime_ = 0.0f;
    bool open_ = false;
    std::chrono::steady_clock::time_point lastFrameTime_;

    enum class DoorState
    {
        Closed,
        Opening,
        Open,
        Closing,
    };

    struct RuntimeDoor
    {
        std::int32_t entityIndex = -1;
        std::int32_t modelIndex = -1;
        Vec3 direction;
        Vec3 mins;
        Vec3 maxs;
        float distance = 0.0f;
        float speed = 100.0f;
        float wait = 4.0f;
        float progress = 0.0f;
        float waitTimer = 0.0f;
        int moveSound = 0;
        int stopSound = 0;
        DoorState state = DoorState::Closed;
    };

    void useDoor(RuntimeDoor& door);

    std::vector<RuntimeDoor> doors_;

    struct RuntimeTrigger
    {
        std::int32_t entityIndex = -1;
        std::int32_t modelIndex = -1;
        Vec3 mins;
        Vec3 maxs;
        std::string target;
        bool touching = false;
    };

    std::vector<RuntimeTrigger> triggers_;

    struct RuntimeAutoTrigger
    {
        std::int32_t entityIndex = -1;
        std::string target;
        float timer = 0.1f;
        bool fireOnce = false;
        bool fired = false;
    };

    std::vector<RuntimeAutoTrigger> autoTriggers_;

    struct RuntimeMultiManager
    {
        struct TargetEvent
        {
            std::string target;
            float delay = 0.0f;
            bool fired = false;
        };

        std::int32_t entityIndex = -1;
        std::vector<TargetEvent> events;
        bool active = false;
        float elapsed = 0.0f;
    };

    std::vector<RuntimeMultiManager> multiManagers_;
};
}
