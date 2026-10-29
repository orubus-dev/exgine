#pragma once

#include "exgine/runtime.hpp"

#include <cstdint>
#include <unordered_map>

namespace exgine {

// Converts streamed procedural WorldChunks into ordinary renderable runtime
// entities and matching static physics. The bridge is deliberately generic:
// it does not know about a game, mission, city, or asset catalog.
class WorldRenderBridge final {
public:
    bool sync(Runtime& runtime, Vec3 focus_position);
    void clear(Runtime& runtime) noexcept;
    [[nodiscard]] std::size_t terrain_entities() const noexcept { return terrain_entities_.size(); }
    [[nodiscard]] std::size_t water_entities() const noexcept { return water_entities_.size(); }
    [[nodiscard]] std::size_t vegetation_entities() const noexcept { return vegetation_entities_.size(); }
    [[nodiscard]] std::size_t terrain_physics_bodies() const noexcept { return terrain_physics_.size(); }

private:
    std::unordered_map<WorldChunkCoord, EntityId, WorldChunkCoordHash> terrain_entities_;
    std::unordered_map<WorldChunkCoord, EntityId, WorldChunkCoordHash> water_entities_;
    std::unordered_map<WorldChunkCoord, EntityId, WorldChunkCoordHash> vegetation_entities_;
    std::unordered_map<WorldChunkCoord, std::uint64_t, WorldChunkCoordHash> terrain_generations_;
    std::unordered_map<WorldChunkCoord, std::uint64_t, WorldChunkCoordHash> water_generations_;
    std::unordered_map<WorldChunkCoord, std::uint64_t, WorldChunkCoordHash> vegetation_generations_;
    std::unordered_map<WorldChunkCoord, PhysicsBodyId, WorldChunkCoordHash> terrain_physics_;

    static EntityId create_render_entity(Runtime& runtime, NodeKind kind, const char* name);
    static bool attach_mesh(Runtime& runtime, EntityId entity, const Mesh& mesh, const char* material);
    static bool attach_vegetation(Runtime& runtime, EntityId entity, const WorldChunk& chunk);
    static void ensure_material(Runtime& runtime, const char* name, std::uint64_t seed);
    static PhysicsBodyId attach_heightfield(Runtime& runtime, WorldChunkCoord coord, const Mesh& terrain, float chunk_size);
    void destroy_physics(Runtime& runtime, WorldChunkCoord coord) noexcept;
};

} // namespace exgine
