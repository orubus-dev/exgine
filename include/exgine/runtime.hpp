#pragma once
#include "exgine/animation.hpp"
#include "exgine/building.hpp"
#include "exgine/exsound.hpp"
#include "exgine/gameplay.hpp"
#include "exgine/geometry.hpp"
#include "exgine/ir.hpp"
#include "exgine/lighting.hpp"
#include "exgine/material.hpp"
#include "exgine/open_world.hpp"
#include "exgine/physics.hpp"
#include "exgine/resources.hpp"
#include "exgine/scene.hpp"
#include "exgine/vehicle.hpp"
#include "exgine/vehicle_dynamics.hpp"
#include "exgine/world.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
namespace exgine {
class AndroidAudioBackend;
struct AudioMixResult;
using EntityId=std::uint64_t;inline constexpr EntityId invalid_entity=0;
struct Transform{float x=0,y=0,z=0;};
struct Entity{EntityId id=invalid_entity;NodeKind kind=NodeKind::Property;std::string name;Transform transform{};bool active=true;std::vector<Property> properties;std::shared_ptr<MeshAssembly> geometry;SceneNodeId scene_node=invalid_scene_node;};
class EntityRegistry{public:EntityId create(NodeKind,std::string);bool destroy(EntityId)noexcept;[[nodiscard]]Entity*get(EntityId)noexcept;[[nodiscard]]const Entity*get(EntityId)const noexcept;[[nodiscard]]std::vector<EntityId>ids()const;[[nodiscard]]std::size_t size()const noexcept{return entities_.size();}void clear()noexcept;private:EntityId next_id_=1;std::unordered_map<EntityId,Entity>entities_;};
struct WorldState{EntityId world_entity=invalid_entity;EntityRegistry entities;std::uint64_t tick=0;double elapsed_seconds=0;};
class Runtime{public:
 Runtime();~Runtime();Runtime(const Runtime&)=delete;Runtime& operator=(const Runtime&)=delete;Runtime(Runtime&&) noexcept;Runtime& operator=(Runtime&&) noexcept;
 bool load(const IR&);void update(double)noexcept;void reset()noexcept;
 bool attach_geometry(EntityId,MeshAssembly);bool generate_building(EntityId,BuildingConfig={});bool set_building_door(EntityId,std::uint64_t,bool)noexcept;const BuildingInstance*building(EntityId)const noexcept;std::vector<BuildingCollisionVolume>building_collision(EntityId)const;const BuildingRoom*find_building_room(EntityId,Vec3)const noexcept;
 bool generate_vehicle(EntityId,VehicleConfig={});bool set_vehicle_door(EntityId,std::uint64_t,bool)noexcept;const VehicleInstance*vehicle(EntityId)const noexcept;std::vector<VehicleCollisionVolume>vehicle_collision(EntityId)const;bool set_vehicle_input(EntityId,VehicleInput)noexcept;bool possess_vehicle(EntityId,bool)noexcept;
 bool define_material(Material,TextureGenerationSettings={});bool stream_world(Vec3,std::uint32_t);bool stream_open_world(Vec3)noexcept;bool set_main_camera(Camera)noexcept;std::uint64_t create_light(Light);bool destroy_light(std::uint64_t)noexcept;
 CharacterId create_character(CharacterDefinition={},Vec3={});bool destroy_character(CharacterId)noexcept;bool update_character(CharacterId,CharacterControllerInput,float)noexcept;bool damage_character(const DamageEvent&,CombatResult*result=nullptr)noexcept;bool recover_character(CharacterId,float)noexcept;
 bool define_animation(AnimationClip);bool attach_skeleton(EntityId,Skeleton);bool attach_skinned_mesh(EntityId,SkinnedMesh,std::string material_slot);bool play_animation(EntityId,AnimationClipId,float blend_seconds=.15f)noexcept;bool update_animation(EntityId,float)noexcept;const Skeleton*skeleton(EntityId)const noexcept;const SkeletonPose*animation_pose(EntityId)const noexcept;const SkinnedMesh*skinned_mesh(EntityId)const noexcept;std::string_view skinned_material(EntityId)const noexcept;
 [[nodiscard]] SoundSample generate_sound(const SoundEventParams&)const;[[nodiscard]] SoundSample generate_sound(const SoundRecipe&)const;[[nodiscard]] SoundSample generate_speech(const SpeechRequest&)const;[[nodiscard]] bool play_sound(const SoundSample&)noexcept;[[nodiscard]] bool start_audio(std::uint32_t=48000)noexcept;void stop_audio()noexcept;void set_audio_mix(AudioMixResult)noexcept;
 [[nodiscard]] ExSound& sound()noexcept{return sound_;}[[nodiscard]] const ExSound& sound()const noexcept{return sound_;}[[nodiscard]] AndroidAudioBackend& audio_backend()noexcept;
 GameplayWorld*gameplay()noexcept;const GameplayWorld*gameplay()const noexcept;PhysicsWorld*physics()noexcept{return physics_.get();}const PhysicsWorld*physics()const noexcept{return physics_.get();}const SceneGraph&scene()const noexcept{return scene_;}SceneGraph&scene()noexcept{return scene_;}const LightingWorld&lighting()const noexcept{return lighting_;}LightingWorld&lighting()noexcept{return lighting_;}const ProceduralWorld*world()const noexcept{return world_.get();}const WorldStreamer*world_streamer()const noexcept{return streamer_.get();}const OpenWorldStreamer*open_world_streamer()const noexcept{return open_streamer_.get();}OpenWorldStreamer*open_world_streamer()noexcept{return open_streamer_.get();}const Material*material(std::string_view)const noexcept;std::shared_ptr<const MaterialResource>material_resource(std::string_view)const noexcept;const MaterialLibrary&materials()const noexcept{return materials_;}const ResourceCache&resources()const noexcept{return resources_;}const AnimationLibrary&animations()const noexcept{return animations_;}const WorldState&state()const noexcept{return state_;}WorldState&state()noexcept{return state_;}const std::unordered_map<EntityId,CharacterId>&character_bindings()const noexcept{return gameplay_characters_;}const std::unordered_map<EntityId,std::unique_ptr<VehicleDynamicsController>>&vehicle_dynamics()const noexcept{return vehicle_dynamics_;}
private:
 struct SkinnedAsset{std::shared_ptr<SkinnedMesh>mesh;std::string material_slot;};
 WorldState state_;MaterialLibrary materials_;ResourceCache resources_;AnimationLibrary animations_;std::unordered_map<EntityId,Skeleton>skeletons_;std::unordered_map<EntityId,std::unique_ptr<AnimationController>>animation_controllers_;std::unordered_map<EntityId,SkinnedAsset>skinned_assets_;std::unique_ptr<ProceduralWorld>world_;std::unique_ptr<WorldStreamer>streamer_;std::unique_ptr<OpenWorldStreamer>open_streamer_;std::unique_ptr<PhysicsWorld>physics_;std::unique_ptr<GameplayWorld>gameplay_;SceneGraph scene_;LightingWorld lighting_;std::unordered_map<EntityId,BuildingInstance>buildings_;std::unordered_map<EntityId,VehicleInstance>vehicles_;std::unordered_map<EntityId,CharacterId>gameplay_characters_;std::unordered_map<EntityId,float>character_heading_;std::unordered_map<EntityId,std::unique_ptr<VehicleDynamicsController>>vehicle_dynamics_;ExSound sound_{};std::unique_ptr<AndroidAudioBackend> audio_;bool loaded_=false;
};
}