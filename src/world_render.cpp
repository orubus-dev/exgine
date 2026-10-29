#include "exgine/world_render.hpp"

#include "exgine/geometry.hpp"
#include "exgine/material.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>

namespace exgine {

namespace {

void destroy_render_entity(Runtime& runtime, EntityId id) noexcept {
    if (!id) return;
    if (auto* entity = runtime.state().entities.get(id)) {
        if (entity->scene_node != invalid_scene_node)
            (void)runtime.scene().destroy(entity->scene_node);
    }
    (void)runtime.state().entities.destroy(id);
}

// Appends a transformed copy of `shape` into `mesh`. Rotation is around +Y only;
// vegetation primitives are radially symmetric enough that this is all we need.
void append_transformed(Mesh& mesh, const Mesh& shape, const Vec3& at, float scale, float yaw) {
    const float c = std::cos(yaw), s = std::sin(yaw);
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    for (const auto& v : shape.vertices) {
        const float x = v.position.x * scale, z = v.position.z * scale;
        const float rx = x * c + z * s, rz = -x * s + z * c;
        mesh.vertices.push_back({{at.x + rx, at.y + v.position.y * scale, at.z + rz}, v.normal, v.uv});
    }
    for (const auto i : shape.indices) mesh.indices.push_back(base + i);
}

// Builds one merged low-poly assembly (trunk + canopy parts) for every
// vegetation instance the chunk generator already produced. Forests were
// generated for years and never drawn; this is the draw.
MeshAssembly build_vegetation_assembly(const WorldChunk& chunk) {
    MeshAssembly assembly;
    Mesh trunks, canopy;
    constexpr std::size_t kMaxInstances = 96;
    const auto count = std::min(chunk.vegetation.size(), kMaxInstances);
    trunks.vertices.reserve(count * 14);
    canopy.vertices.reserve(count * 30);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& v = chunk.vegetation[i];
        const float s = std::clamp(v.scale, 0.35f, 2.2f);
        const bool palm = v.species == 0u || v.species == 2u;
        const float trunk_h = palm ? 2.6f * s : 1.9f * s;
        CylinderShape trunk_shape;
        trunk_shape.radius = 0.14f * s;
        trunk_shape.height = trunk_h;
        trunk_shape.segments = 6;
        append_transformed(trunks, make_cylinder(trunk_shape), v.position, 1.f, v.rotation);
        SphereShape canopy_shape;
        canopy_shape.radius = (palm ? 1.15f : 1.5f) * s;
        canopy_shape.segments = 7;
        canopy_shape.rings = 4;
        const Vec3 top{v.position.x, v.position.y + trunk_h + canopy_shape.radius * 0.45f, v.position.z};
        append_transformed(canopy, make_sphere(canopy_shape), top, 1.f, v.rotation * 1.7f);
        if (palm) {
            SphereShape mid;
            mid.radius = 0.8f * s;
            mid.segments = 6;
            mid.rings = 3;
            append_transformed(canopy, make_sphere(mid),
                               {top.x, top.y + 0.7f * s, top.z}, 1.f, v.rotation);
        }
    }
    if (trunks.indices.empty() || canopy.indices.empty()) return {};
    MeshPart trunk_part;
    trunk_part.name = "vegetation_trunk";
    trunk_part.mesh = std::move(trunks);
    trunk_part.material_slot = "trunk";
    MeshPart canopy_part;
    canopy_part.name = "vegetation_canopy";
    canopy_part.mesh = std::move(canopy);
    canopy_part.material_slot = "foliage";
    assembly.parts.push_back(std::move(trunk_part));
    assembly.parts.push_back(std::move(canopy_part));
    return assembly;
}

const char* terrain_material_for(const Runtime& runtime, WorldChunkCoord coord) {
    const auto* world = runtime.world();
    if (!world) return "terrain";
    const float size = world->config().chunk_size;
    const auto biome = world->sample_biome(
        (static_cast<float>(coord.x) + 0.5f) * size,
        (static_cast<float>(coord.z) + 0.5f) * size).biome;
    switch (biome) {
        case Biome::Forest: return "terrain_forest";
        case Biome::Desert: return "terrain_desert";
        case Biome::Snow: return "terrain_snow";
        case Biome::Rocky: return "terrain_rock";
        case Biome::Wetland: return "terrain_wetland";
        case Biome::Beach:
        case Biome::Ocean: return "terrain_beach";
        case Biome::Plains:
        default: return "terrain_plains";
    }
}

}

EntityId WorldRenderBridge::create_render_entity(Runtime& runtime, NodeKind kind, const char* name) {
    const auto id = runtime.state().entities.create(kind, name ? std::string{name} : std::string{});
    if (!id) return invalid_entity;
    auto* entity = runtime.state().entities.get(id);
    if (!entity) return invalid_entity;
    entity->scene_node = runtime.scene().create(runtime.scene().root());
    if (!entity->scene_node) {
        runtime.state().entities.destroy(id);
        return invalid_entity;
    }
    return id;
}

bool WorldRenderBridge::attach_mesh(Runtime& runtime, EntityId entity, const Mesh& mesh, const char* material) {
    if (!entity || !mesh.valid() || !material || !*material) return false;
    MeshAssembly assembly;
    MeshPart part;
    part.name = material;
    part.mesh = mesh;
    part.material_slot = material;
    assembly.parts.push_back(std::move(part));
    return runtime.attach_geometry(entity, std::move(assembly));
}

bool WorldRenderBridge::attach_vegetation(Runtime& runtime, EntityId entity, const WorldChunk& chunk) {
    if (!entity || chunk.vegetation.empty()) return false;
    auto assembly = build_vegetation_assembly(chunk);
    if (!assembly.valid()) return false;
    ensure_material(runtime, "trunk", chunk.vegetation.front().id);
    ensure_material(runtime, "foliage", chunk.vegetation.front().id + 1);
    return runtime.attach_geometry(entity, std::move(assembly));
}

void WorldRenderBridge::ensure_material(Runtime& runtime, const char* name, std::uint64_t seed) {
    if (runtime.material_resource(name)) return;
    (void)runtime.define_material(make_real_world_material(name, seed));
}

PhysicsBodyId WorldRenderBridge::attach_heightfield(Runtime& runtime, WorldChunkCoord coord,
                                                     const Mesh& terrain, float chunk_size) {
    auto* physics = runtime.physics();
    if (!physics || !terrain.valid() || terrain.vertices.size() < 4 || chunk_size <= 0.f)
        return invalid_physics_body;

    const auto count = terrain.vertices.size();
    const auto side = static_cast<std::size_t>(std::llround(std::sqrt(static_cast<double>(count))));
    if (side < 2 || side * side != count) return invalid_physics_body;

    auto heights = std::make_shared<std::vector<float>>();
    heights->reserve(count);
    for (const auto& vertex : terrain.vertices) {
        if (!std::isfinite(vertex.position.y)) return invalid_physics_body;
        heights->push_back(vertex.position.y);
    }

    PhysicsRigidBodyDesc body;
    body.type = PhysicsBodyType::Static;
    body.transform.position = {
        static_cast<float>(coord.x) * chunk_size,
        0.f,
        static_cast<float>(coord.z) * chunk_size
    };
    body.flags = static_cast<PhysicsBodyFlags>(PhysicsBodyFlag::AllowSleep);
    const auto body_id = physics->create_body(body);
    if (!body_id) return invalid_physics_body;

    PhysicsHeightFieldShape heightfield;
    heightfield.width = static_cast<std::uint32_t>(side);
    heightfield.depth = static_cast<std::uint32_t>(side);
    heightfield.cell_size = chunk_size / static_cast<float>(side - 1);
    heightfield.heights = std::move(heights);

    PhysicsColliderDesc collider;
    collider.shape.type = PhysicsShapeType::HeightField;
    collider.shape.data = std::move(heightfield);
    collider.material.static_friction = 0.9f;
    collider.material.dynamic_friction = 0.75f;
    collider.material.restitution = 0.02f;

    if (!physics->add_collider(body_id, collider)) {
        (void)physics->destroy_body(body_id);
        return invalid_physics_body;
    }
    return body_id;
}

void WorldRenderBridge::destroy_physics(Runtime& runtime, WorldChunkCoord coord) noexcept {
    const auto it = terrain_physics_.find(coord);
    if (it == terrain_physics_.end()) return;
    if (auto* physics = runtime.physics()) (void)physics->destroy_body(it->second);
    terrain_physics_.erase(it);
}

bool WorldRenderBridge::sync(Runtime& runtime, Vec3 focus_position) {
    if (!runtime.stream_open_world(focus_position)) return false;
    auto* streamer = runtime.open_world_streamer();
    if (!streamer) return false;

    if (!runtime.material_resource("terrain"))
        (void)runtime.define_material(make_real_world_material("terrain", 0x5445525241494Eull));
    if (!runtime.material_resource("water"))
        (void)runtime.define_material(make_real_world_material("water", 0x5741544552ull));

    for (const auto id : runtime.state().entities.ids()) {
        auto* e = runtime.state().entities.get(id);
        if (!e) continue;
        if (e->name == "GroundPlate" || e->name == "GroundGrass" || e->name == "GroundWater")
            e->active = false;
    }

    const auto& chunks = streamer->chunks();
    const float time = static_cast<float>(runtime.state().elapsed_seconds);
    for (const auto& [coord, streamed] : chunks) {
        const auto& chunk = streamed.chunk;
        const auto generation = streamed.generation;

        if (chunk.terrain.valid()) {
            auto it = terrain_entities_.find(coord);
            EntityId id = it == terrain_entities_.end() ? invalid_entity : it->second;
            if (!id || !runtime.state().entities.get(id)) {
                id = create_render_entity(runtime, NodeKind::Terrain, "WorldChunkTerrain");
                if (!id) return false;
                terrain_entities_[coord] = id;
            }
            if (terrain_generations_[coord] != generation) {
                const char* terrain_material = terrain_material_for(runtime, coord);
                if (!runtime.material_resource(terrain_material))
                    (void)runtime.define_material(make_real_world_material(terrain_material, coord.x * 7 + coord.z));
                if (!attach_mesh(runtime, id, chunk.terrain, terrain_material)) return false;
                terrain_generations_[coord] = generation;

                destroy_physics(runtime, coord);
                const auto physics_body = attach_heightfield(
                    runtime, coord, chunk.terrain,
                    runtime.world() ? runtime.world()->config().chunk_size : 0.f);
                if (physics_body) terrain_physics_[coord] = physics_body;
            }
        }

        if (chunk.water.valid() && !chunk.water.indices.empty()) {
            auto it = water_entities_.find(coord);
            EntityId id = it == water_entities_.end() ? invalid_entity : it->second;
            if (!id || !runtime.state().entities.get(id)) {
                id = create_render_entity(runtime, NodeKind::Prop, "WorldChunkWater");
                if (!id) return false;
                water_entities_[coord] = id;
            }
            if (water_generations_[coord] != generation) {
                if (!attach_mesh(runtime, id, chunk.water, "water")) return false;
                water_generations_[coord] = generation;
            }

            if (auto* e = runtime.state().entities.get(id); e && e->scene_node) {
                const float phase = static_cast<float>(coord.x) * 0.71f +
                                    static_cast<float>(coord.z) * 1.13f;
                const float wave = std::sin(time * 1.7f + phase) * 0.025f;
                (void)runtime.scene().set_local_transform(
                    e->scene_node,
                    SceneTransform{{0.f, wave, 0.f}, {0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}});
            }
        }
        if (!chunk.vegetation.empty()) {
            auto it = vegetation_entities_.find(coord);
            EntityId id = it == vegetation_entities_.end() ? invalid_entity : it->second;
            if (!id || !runtime.state().entities.get(id)) {
                id = create_render_entity(runtime, NodeKind::Prop, "WorldChunkVegetation");
                if (!id) return false;
                vegetation_entities_[coord] = id;
            }
            if (vegetation_generations_[coord] != generation) {
                // Tolerant on purpose: a degenerate vegetation assembly must never
                // abort terrain/water sync for the chunk. Marking the generation
                // anyway prevents retrying the same bad assembly every frame.
                (void)attach_vegetation(runtime, id, chunk);
                vegetation_generations_[coord] = generation;
            }
        }
    }

    for (auto it = terrain_entities_.begin(); it != terrain_entities_.end();) {
        if (chunks.find(it->first) == chunks.end()) {
            destroy_render_entity(runtime, it->second);
            destroy_physics(runtime, it->first);
            terrain_generations_.erase(it->first);
            it = terrain_entities_.erase(it);
        } else ++it;
    }
    for (auto it = water_entities_.begin(); it != water_entities_.end();) {
        if (chunks.find(it->first) == chunks.end()) {
            destroy_render_entity(runtime, it->second);
            water_generations_.erase(it->first);
            it = water_entities_.erase(it);
        } else ++it;
    }
    for (auto it = vegetation_entities_.begin(); it != vegetation_entities_.end();) {
        if (chunks.find(it->first) == chunks.end()) {
            destroy_render_entity(runtime, it->second);
            vegetation_generations_.erase(it->first);
            it = vegetation_entities_.erase(it);
        } else ++it;
    }

    runtime.scene().update_world_transforms();
    return true;
}

void WorldRenderBridge::clear(Runtime& runtime) noexcept {
    for (const auto& [coord, id] : terrain_entities_) {
        destroy_render_entity(runtime, id);
        destroy_physics(runtime, coord);
    }
    for (const auto& [coord, id] : water_entities_) {
        destroy_render_entity(runtime, id);
    }
    for (const auto& [coord, id] : vegetation_entities_) {
        destroy_render_entity(runtime, id);
    }
    terrain_entities_.clear();
    water_entities_.clear();
    vegetation_entities_.clear();
    terrain_generations_.clear();
    water_generations_.clear();
    vegetation_generations_.clear();
    terrain_physics_.clear();
}

} // namespace exgine
