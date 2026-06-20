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
    struct MapChangeRequest
    {
        std::string mapName;
        std::string landmarkName;
        std::string changeTarget;
        Vec3 playerLandmarkOffset;
        Vec3 playerVelocity;
        float yaw = 0.0f;
        float pitch = 0.0f;
        float changeTargetDelay = 0.0f;
    };

    struct MapRuntimeState
    {
        struct MoverState
        {
            std::int32_t entityIndex = -1;
            float progress = 0.0f;
            float waitTimer = 0.0f;
            int state = 0;
        };
        struct TriggerState
        {
            std::int32_t entityIndex = -1;
            float cooldown = 0.0f;
            bool enabled = true;
        };
        struct CounterState
        {
            std::int32_t entityIndex = -1;
            int remaining = 0;
            bool fired = false;
        };
        struct ManagerState
        {
            std::int32_t entityIndex = -1;
            float elapsed = 0.0f;
            bool active = false;
            std::vector<bool> firedEvents;
        };
        struct DelayedOutputState
        {
            std::int32_t sourceEntityIndex = -1;
            std::string target;
            std::string killTarget;
            float timer = 0.0f;
        };

        std::vector<MoverState> doors;
        std::vector<MoverState> buttons;
        std::vector<TriggerState> triggers;
        std::vector<CounterState> counters;
        std::vector<ManagerState> managers;
        std::vector<DelayedOutputState> delayedOutputs;
        std::vector<std::pair<std::int32_t, bool>> autoTriggers;
        std::vector<std::size_t> removedEntities;
    };

    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool initialize(const std::string& title, int width, int height);
    bool renderFrame();
    bool isOpen() const;
    void shutdown();
    void beginMapLoad();
    void setWorldFaces(std::vector<BspRenderFace> faces);
    void setMapEntities(std::vector<BspEntity> entities, std::vector<BspBrushModel> brushModels);
    void setCollisionMap(const BspMap* map);
    void setSoundSystem(SoundSystem* soundSystem);
    void setEntityMarkers(std::vector<BspEntityMarker> markers);
    void setTextureImages(std::vector<TextureImage> textures);
    void setStudioModels(std::vector<StudioModelSceneEntry> models);
    bool takeMapChangeRequest(MapChangeRequest& request);
    bool requestMapChangeTo(const std::string& mapName, const std::string& landmarkName);
    bool applyMapTransition(const MapChangeRequest& request);
    MapRuntimeState captureMapRuntimeState() const;
    void restoreMapRuntimeState(const MapRuntimeState& state);

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
    void fireEntityOutputs(std::int32_t sourceEntityIndex,
                           const std::string& target,
                           const std::string& killTarget,
                           float delay);
    void executeEntityOutputs(std::int32_t sourceEntityIndex,
                              const std::string& target,
                              const std::string& killTarget);
    void killEntityTarget(const std::string& targetName);
    std::string entityDebugName(std::int32_t entityIndex) const;
    bool requestMapChange(std::int32_t entityIndex);
    void useEntity(std::size_t entityIndex);
    bool useRayBlocked(const Vec3& origin,
                       const Vec3& direction,
                       float distance,
                       std::int32_t candidateEntityIndex) const;
    void updateDebugTitle();
    void renderCube(float angleDegrees);
    void renderWorld();
    void renderEntityMarkers();
    void renderStudioModels();
    Vec3 brushModelOffset(std::int32_t modelIndex) const;
    float soundVolumeForBrushModel(std::int32_t modelIndex) const;
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
        std::string master;
        bool useOnly = false;
        bool touchOpen = false;
        bool touching = false;
        DoorState state = DoorState::Closed;
    };

    void useDoor(RuntimeDoor& door);

    std::vector<RuntimeDoor> doors_;

    enum class ButtonState
    {
        Released,
        Pressing,
        Pressed,
        Returning,
    };

    struct RuntimeButton
    {
        std::int32_t entityIndex = -1;
        std::int32_t modelIndex = -1;
        Vec3 direction;
        Vec3 mins;
        Vec3 maxs;
        std::string target;
        std::string sound;
        float distance = 0.0f;
        float speed = 40.0f;
        float wait = 1.0f;
        float progress = 0.0f;
        float waitTimer = 0.0f;
        bool toggle = false;
        bool touchOnly = false;
        bool playerUsable = true;
        bool touching = false;
        std::string master;
        ButtonState state = ButtonState::Released;
    };

    void useButton(RuntimeButton& button);
    void buttonReachedPressed(RuntimeButton& button);
    void buttonReachedReleased(RuntimeButton& button);

    std::vector<RuntimeButton> buttons_;

    struct RuntimeTrigger
    {
        std::int32_t entityIndex = -1;
        std::int32_t modelIndex = -1;
        Vec3 mins;
        Vec3 maxs;
        std::string className;
        std::string target;
        std::string killTarget;
        std::string master;
        float wait = 0.2f;
        float cooldown = 0.0f;
        float delay = 0.0f;
        bool once = false;
        bool playerAllowed = true;
        bool enabled = true;
    };

    std::vector<RuntimeTrigger> triggers_;

    struct RuntimeChangeLevel
    {
        std::int32_t entityIndex = -1;
        std::int32_t modelIndex = -1;
        Vec3 mins;
        Vec3 maxs;
        std::string mapName;
        std::string landmarkName;
        std::string target;
        std::string changeTarget;
        std::string master;
        float changeTargetDelay = 0.0f;
        bool useOnly = false;
        bool enabled = true;
    };

    std::vector<RuntimeChangeLevel> changeLevels_;
    MapChangeRequest pendingMapChange_;
    bool hasPendingMapChange_ = false;

    struct RuntimeAutoTrigger
    {
        std::int32_t entityIndex = -1;
        std::string target;
        std::string killTarget;
        float timer = 0.1f;
        float delay = 0.0f;
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

    struct RuntimeCounter
    {
        std::int32_t entityIndex = -1;
        std::string target;
        std::string killTarget;
        std::string master;
        float delay = 0.0f;
        int remaining = 2;
        bool fired = false;
    };

    std::vector<RuntimeCounter> counters_;

    struct DelayedEntityOutput
    {
        std::int32_t sourceEntityIndex = -1;
        std::string target;
        std::string killTarget;
        float timer = 0.0f;
    };

    std::vector<DelayedEntityOutput> delayedEntityOutputs_;
};
}
