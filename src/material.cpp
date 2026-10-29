#include "exgine/material.hpp"
#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>
namespace exgine {
namespace {
std::uint64_t hash_bytes(std::string_view value) noexcept { std::uint64_t h=1469598103934665603ULL; for(const unsigned char c:value){h^=c;h*=1099511628211ULL;} return h; }
std::uint64_t hash_seed(std::uint64_t x) noexcept{x+=0x9e3779b97f4a7c15ULL;x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;x=(x^(x>>27))*0x94d049bb133111ebULL;return x^(x>>31);} float unit(std::uint64_t seed) noexcept{return static_cast<float>(hash_seed(seed)>>40)/static_cast<float>(1u<<24);}
}
bool Material::valid() const noexcept{if(name.empty()||!std::isfinite(roughness)||!std::isfinite(metallic)||!std::isfinite(specular)||!std::isfinite(opacity))return false;return roughness>=0&&roughness<=1&&metallic>=0&&metallic<=1&&specular>=0&&specular<=1&&opacity>=0&&opacity<=1;}
bool MaterialLibrary::define(Material material){if(!material.valid())return false;materials_.insert_or_assign(material.name,std::move(material));return true;}
bool MaterialLibrary::erase(std::string_view name) noexcept{return materials_.erase(std::string{name})!=0;}
const Material* MaterialLibrary::find(std::string_view name) const noexcept{const auto it=materials_.find(std::string{name});return it==materials_.end()?nullptr:&it->second;}
Material make_real_world_material(std::string_view name,std::uint64_t seed){Material m;m.name=std::string{name};const std::uint64_t h=hash_seed(seed^hash_bytes(name));const float j=(unit(h)-0.5f)*0.08f;
if(name=="wood"){m.base_color={0.42f+j,0.20f+j*0.5f,0.07f};m.roughness=.62f;m.layers={{"grain",7,.8f,seed}};}
else if(name=="steel"){m.base_color={.48f,.50f,.53f};m.roughness=.34f;m.metallic=.92f;m.layers={{"scratches",28,.18f,seed},{"noise",4,.12f,seed+1}};}
else if(name=="aluminium"){m.base_color={.67f,.69f,.71f};m.roughness=.29f;m.metallic=.96f;}
else if(name=="copper"){m.base_color={.68f,.27f,.10f};m.roughness=.31f;m.metallic=.94f;m.layers={{"oxidation",2,.08f,seed}};}
else if(name=="glass"){m.base_color={.93f,.98f,1};m.roughness=.06f;m.specular=.9f;m.opacity=.18f;}
else if(name=="rubber"){m.base_color={.025f,.025f,.023f};m.roughness=.82f;m.layers={{"grain",18,.25f,seed}};}
else if(name=="water"){m.base_color={.03f,.23f,.34f};m.roughness=.04f;m.specular=.95f;m.opacity=.72f;m.layers={{"waves",1.5f,.35f,seed},{"noise",5,.12f,seed+1}};}
else if(name=="grass"){m.base_color={.12f,.34f,.06f};m.roughness=.88f;m.layers={{"fbm",3,.32f,seed},{"grain",24,.18f,seed+1}};}
else if(name=="sand"){m.base_color={.70f,.55f,.32f};m.roughness=.91f;m.layers={{"noise",7,.24f,seed},{"grain",38,.20f,seed+1}};}
else if(name=="concrete"){m.base_color={.48f,.47f,.44f};m.roughness=.83f;m.layers={{"noise",5,.24f,seed},{"cracks",9,.12f,seed+1}};}
else if(name=="snow"){m.base_color={.92f,.95f,1};m.roughness=.93f;m.layers={{"fbm",2,.16f,seed}};}
// --- World-surface materials (terrain per biome; the old "everything else is gray" fallback made the whole world monochrome) ---
else if(name=="terrain"||name=="terrain_plains"){m.base_color={.24f+j,.38f+j*.6f,.13f};m.roughness=.9f;m.layers={{"fbm",3,.3f,seed},{"grain",26,.16f,seed+1}};}
else if(name=="terrain_forest"){m.base_color={.13f+j,.30f,.09f};m.roughness=.92f;m.layers={{"fbm",4,.34f,seed},{"grain",30,.18f,seed+1}};}
else if(name=="terrain_desert"){m.base_color={.74f+j,.58f,.34f};m.roughness=.92f;m.layers={{"noise",6,.22f,seed},{"grain",36,.18f,seed+1}};}
else if(name=="terrain_beach"){m.base_color={.80f,.70f,.48f};m.roughness=.94f;m.layers={{"grain",40,.2f,seed}};}
else if(name=="terrain_snow"){m.base_color={.90f,.93f,.97f};m.roughness=.9f;m.layers={{"fbm",2,.14f,seed}};}
else if(name=="terrain_rock"){m.base_color={.42f+j,.40f,.37f};m.roughness=.88f;m.layers={{"noise",7,.3f,seed},{"cracks",11,.16f,seed+1}};}
else if(name=="terrain_wetland"){m.base_color={.22f,.28f,.14f};m.roughness=.86f;m.layers={{"fbm",3,.3f,seed},{"noise",6,.2f,seed+1}};}
else if(name=="asphalt"){m.base_color={.10f,.10f,.11f};m.roughness=.78f;m.layers={{"grain",22,.16f,seed},{"cracks",12,.10f,seed+1}};}
else if(name=="brick"){m.base_color={.55f+j*.5f,.26f,.18f};m.roughness=.85f;m.layers={{"grain",16,.22f,seed}};}
// --- People & props (per-seed variation so characters/traffic don't clone) ---
else if(name=="skin"){m.base_color={.62f+j,.42f+j*.7f,.30f+j*.6f};m.roughness=.62f;}
else if(name=="hair"){m.base_color={.10f+j*.3f,.07f+j*.25f,.05f};m.roughness=.55f;}
else if(name=="fabric"){const std::uint64_t pick=hash_seed(h*3);const float p=unit(pick);if(p<.2f)m.base_color={.78f,.20f,.18f};else if(p<.4f)m.base_color={.16f,.38f,.70f};else if(p<.55f)m.base_color={.90f,.88f,.84f};else if(p<.7f)m.base_color={.94f,.62f,.10f};else if(p<.85f)m.base_color={.20f,.55f,.32f};else m.base_color={.55f,.20f,.60f};m.roughness=.8f;}
else if(name=="denim"){m.base_color={.20f,.30f,.46f};m.roughness=.82f;m.layers={{"grain",14,.14f,seed}};}
else if(name=="leather"){m.base_color={.36f+j*.4f,.20f,.10f};m.roughness=.55f;m.layers={{"grain",12,.14f,seed}};}
else if(name=="backpack_fabric"){m.base_color={.18f,.19f,.21f};m.roughness=.85f;}
else if(name=="watch_metal"){m.base_color={.75f,.76f,.78f};m.roughness=.2f;m.metallic=.95f;}
// --- Vehicle paint: saturated per-seed livery instead of raw steel ---
else if(name=="paint"){const std::uint64_t pick=hash_seed(h*7);const float p=unit(pick);if(p<.16f)m.base_color={.72f,.08f,.08f};else if(p<.3f)m.base_color={.85f,.35f,.05f};else if(p<.44f)m.base_color={.08f,.24f,.62f};else if(p<.58f)m.base_color={.90f,.90f,.92f};else if(p<.72f)m.base_color={.05f,.05f,.06f};else if(p<.84f)m.base_color={.55f,.56f,.58f};else if(p<.92f)m.base_color={.10f,.42f,.20f};else m.base_color={.75f,.60f,.05f};m.roughness=.28f;m.metallic=.55f;m.layers={{"scratches",18,.10f,seed}};}
// --- Nature props ---
else if(name=="foliage"){m.base_color={.16f+j,.42f+j*.5f,.12f};m.roughness=.9f;m.layers={{"fbm",3,.36f,seed}};}
else if(name=="trunk"){m.base_color={.34f+j*.5f,.24f,.14f};m.roughness=.9f;m.layers={{"grain",10,.3f,seed}};}
// --- Building wall palette (per-seed facade variety) ---
else if(name=="paint_sand"){m.base_color={.82f,.72f,.52f};m.roughness=.8f;m.layers={{"noise",5,.14f,seed}};}
else if(name=="paint_brick"){m.base_color={.58f,.30f,.22f};m.roughness=.85f;m.layers={{"grain",15,.2f,seed}};}
else if(name=="paint_slate"){m.base_color={.36f,.40f,.44f};m.roughness=.8f;m.layers={{"noise",5,.14f,seed}};}
else if(name=="paint_teal"){m.base_color={.20f,.48f,.46f};m.roughness=.8f;m.layers={{"noise",5,.14f,seed}};}
// --- UI / misc ---
else if(name=="hud"){m.base_color={.92f,.94f,.96f};m.roughness=.5f;m.opacity=.55f;m.emission={.55f,.6f,.65f};}
else if(name=="metal"){m.base_color={.62f,.64f,.66f};m.roughness=.3f;m.metallic=.9f;}
else {m.base_color={.5f+j,.5f+j,.5f+j};m.roughness=std::clamp(.5f+j,.05f,.95f);} return m;}
} // namespace exgine
