#include "exgine/gpu.hpp"
#include "exgine/shaders.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>
namespace exgine { namespace {
constexpr GlEnum COLOR=0x4000u,DEPTH=0x100u,DEPTH_TEST=0xB71u,CULL=0xB44u,BLEND=0xBE2u,LESS=0x201u,SRC_ALPHA=0x302u,ONE_MINUS_SRC_ALPHA=0x303u,VS=0x8B31u,FS=0x8B30u,COMPILE=0x8B81u,LINK=0x8B82u,LOG_LEN=0x8B84u,ARRAY=0x8892u,ELEMENT=0x8893u,STATIC_DRAW=0x88E4u,FLOAT=0x1406u,UINT=0x1405u,TRIANGLES=4u,TEX2D=0xDE1u,TEX0=0x84C0u,MIN_FILTER=0x2801u,MAG_FILTER=0x2800u,WRAP_S=0x2802u,WRAP_T=0x2803u,LINEAR=0x2601u,LINEAR_MIPMAP_LINEAR=0x2703u,REPEAT=0x2901u,RGBA8=0x8058u,RGBA=0x1908u,U8=0x1401u;
std::uint64_t mk(const RenderDrawCall&d)noexcept{return reinterpret_cast<std::uintptr_t>(d.geometry.get())^(reinterpret_cast<std::uintptr_t>(d.skinned_mesh.get())<<1)^d.part_index;}
std::uint64_t tex_key(const std::shared_ptr<const Texture2D>&t)noexcept{if(!t)return 0;std::uint64_t k=reinterpret_cast<std::uintptr_t>(t.get());k^=static_cast<std::uint64_t>(t->width)*0x9E3779B185EBCA87ull;k^=static_cast<std::uint64_t>(t->height)<<17;k^=static_cast<std::uint64_t>(t->channels)<<33;k^=static_cast<std::uint64_t>(t->data.size())<<41;return k? k:1;}
GlInt ul(const OpenGLESApi&a,GlUInt p,const char*n)noexcept{return a.GetUniformLocation?a.GetUniformLocation(p,n):-1;}
std::shared_ptr<const Texture2D> solid(std::initializer_list<float>v){auto t=std::make_shared<Texture2D>();t->width=t->height=1;t->channels=static_cast<std::uint32_t>(v.size());t->format=t->channels==1?TextureFormat::R32F:t->channels==2?TextureFormat::RG32F:t->channels==3?TextureFormat::RGB32F:TextureFormat::RGBA32F;t->data.assign(v);return t;}
}
bool OpenGLESApi::complete()const noexcept{return Clear&&ClearColor&&Viewport&&Enable&&Disable&&DepthFunc&&BlendFunc&&CreateShader&&ShaderSource&&CompileShader&&GetShaderiv&&GetShaderInfoLog&&DeleteShader&&CreateProgram&&AttachShader&&LinkProgram&&GetProgramiv&&GetProgramInfoLog&&UseProgram&&DeleteProgram&&GetUniformLocation&&UniformMatrix4fv&&Uniform3f&&Uniform4f&&Uniform1f&&Uniform1i&&GenBuffers&&BindBuffer&&BufferData&&DeleteBuffers&&GenVertexArrays&&BindVertexArray&&DeleteVertexArrays&&EnableVertexAttribArray&&VertexAttribPointer&&DrawElements;}
bool OpenGLESRenderer::ready()const noexcept{return api_.complete()&&program_;}bool OpenGLESRenderer::texture_ready()const noexcept{return ready()&&textured_program_&&api_.GenTextures&&api_.BindTexture&&api_.TexParameteri&&api_.TexImage2D&&api_.GenerateMipmap&&api_.ActiveTexture&&api_.DeleteTextures;}
bool OpenGLESRenderer::compile_shader(GlEnum type,const char*s,GlUInt&out,std::string&e){out=api_.CreateShader(type);if(!out){e="glCreateShader failed";return false;}GlInt n=static_cast<GlInt>(std::strlen(s));api_.ShaderSource(out,1,&s,&n);api_.CompileShader(out);GlInt ok=0;api_.GetShaderiv(out,COMPILE,&ok);if(ok)return true;GlInt len=0,w=0;api_.GetShaderiv(out,LOG_LEN,&len);std::string log(static_cast<std::size_t>(std::max(1,len)),char(0));api_.GetShaderInfoLog(out,len,&w,log.data());e=log;api_.DeleteShader(out);out=0;return false;}
bool OpenGLESRenderer::link_program(const char*,GlUInt v,const char*f,GlUInt&p,std::string&e){GlUInt fs=0;if(!compile_shader(FS,f,fs,e)){api_.DeleteShader(v);return false;}p=api_.CreateProgram();if(!p){api_.DeleteShader(v);api_.DeleteShader(fs);e="glCreateProgram failed";return false;}api_.AttachShader(p,v);api_.AttachShader(p,fs);api_.LinkProgram(p);api_.DeleteShader(v);api_.DeleteShader(fs);GlInt ok=0;api_.GetProgramiv(p,LINK,&ok);if(ok)return true;GlInt len=0,w=0;api_.GetProgramiv(p,LOG_LEN,&len);std::string log(static_cast<std::size_t>(std::max(1,len)),char(0));api_.GetProgramInfoLog(p,len,&w,log.data());e=log;api_.DeleteProgram(p);p=0;return false;}
bool OpenGLESRenderer::ensure_programs(std::string& e) {
    // Retry only for programs that are missing and not marked permanently
    // unavailable (driver rejected them — e.g. uniform limits on low-end GPUs).
    const bool skinned_pending = !skinned_program_ && !skinned_ok_;
    const bool textured_pending = api_.GenTextures && !textured_program_ && !textured_ok_;
    const bool skinned_textured_pending =
        api_.GenTextures && !skinned_textured_program_ && !skinned_textured_ok_;
    if (program_ && !skinned_pending && !textured_pending && !skinned_textured_pending)
        return true;
    if (!api_.complete()) { e = "incomplete GLES API"; return false; }
    auto build = [&](const ShaderProgram& s, GlUInt& p) {
        GlUInt v = 0;
        if (!compile_shader(VS, s.vertex_source.c_str(), v, e)) return false;
        return link_program(s.vertex_source.c_str(), v, s.fragment_source.c_str(), p, e);
    };

    // 1) Base (unskinned, untextured) program — REQUIRED. Without it nothing
    //    can render, so failure here is fatal for the frame.
    if (!program_) {
        if (!build(make_mobile_pbr_shader(), program_)) { base_ok_ = false; return false; }
        base_ok_ = true;
        u_vp_ = ul(api_, program_, "u_view_projection");
        u_model_ = ul(api_, program_, "u_model");
        u_camera_ = ul(api_, program_, "u_camera_position");
        u_ambient_ = ul(api_, program_, "u_ambient_intensity");
        u_base_ = ul(api_, program_, "u_base_roughness");
        u_metal_ = ul(api_, program_, "u_metal_specular");
        u_emission_ = ul(api_, program_, "u_emission_opacity");
        u_light_dir_ = ul(api_, program_, "u_light_direction");
        u_light_color_ = ul(api_, program_, "u_light_color");
        u_light_intensity_ = ul(api_, program_, "u_light_intensity");
        u_time_ = ul(api_, program_, "u_time");
        u_water_ = ul(api_, program_, "u_water");
    }

    // 2) Skinned program — OPTIONAL. If the driver refuses it (uniform limits,
    //    precision, etc.) the world still renders; skinned draws are skipped.
    if (!skinned_program_ && !skinned_ok_) {
        GlUInt p = 0;
        if (build(make_mobile_skinned_pbr_shader(), p)) {
            skinned_program_ = p;
            skinned_ok_ = true;
            s_vp_ = ul(api_, p, "u_view_projection");
            s_model_ = ul(api_, p, "u_model");
            s_camera_ = ul(api_, p, "u_camera_position");
            s_ambient_ = ul(api_, p, "u_ambient_intensity");
            s_base_ = ul(api_, p, "u_base_roughness");
            s_metal_ = ul(api_, p, "u_metal_specular");
            s_emission_ = ul(api_, p, "u_emission_opacity");
            s_light_dir_ = ul(api_, p, "u_light_direction");
            s_light_color_ = ul(api_, p, "u_light_color");
            s_light_intensity_ = ul(api_, p, "u_light_intensity");
            s_bones_ = ul(api_, p, "u_bones[0]");
            s_bone_count_ = ul(api_, p, "u_bone_count");
        } else {
            skinned_program_ = 0;
            skinned_ok_ = false;
            e.clear(); // not fatal
        }
    }

    // 3) Textured variants — OPTIONAL as well.
    if (api_.GenTextures && api_.BindTexture && api_.TexParameteri && api_.TexImage2D &&
        api_.GenerateMipmap && api_.ActiveTexture && api_.DeleteTextures) {
        if (!textured_program_ && !textured_ok_) {
            GlUInt p = 0;
            if (build(make_mobile_textured_pbr_shader(), p)) {
                textured_program_ = p;
                textured_ok_ = true;
                tu_vp_ = ul(api_, p, "u_view_projection");
                tu_model_ = ul(api_, p, "u_model");
                tu_camera_ = ul(api_, p, "u_camera_position");
                tu_ambient_ = ul(api_, p, "u_ambient_intensity");
                tu_base_ = ul(api_, p, "u_base_roughness");
                tu_rough_ = tu_base_;
                tu_metal_ = ul(api_, p, "u_metal_specular");
                tu_emission_ = ul(api_, p, "u_emission_opacity");
                tu_light_dir_ = ul(api_, p, "u_light_direction");
                tu_light_color_ = ul(api_, p, "u_light_color");
                tu_light_intensity_ = ul(api_, p, "u_light_intensity");
                tu_time_ = ul(api_, p, "u_time");
                tu_water_ = ul(api_, p, "u_water");
                t_base_ = ul(api_, p, "u_base_texture");
                t_rough_ = ul(api_, p, "u_roughness_texture");
                t_metal_ = ul(api_, p, "u_metallic_texture");
                t_normal_ = ul(api_, p, "u_normal_texture");
                t_ao_ = ul(api_, p, "u_ao_texture");
                t_emission_ = ul(api_, p, "u_emission_texture");
                t_opacity_ = ul(api_, p, "u_opacity_texture");
                t_use_ = ul(api_, p, "u_use_textures");
            } else {
                textured_program_ = 0;
                textured_ok_ = false;
                e.clear();
            }
        }
        if (!skinned_textured_program_ && !skinned_textured_ok_) {
            GlUInt p = 0;
            if (build(make_mobile_skinned_textured_pbr_shader(), p)) {
                skinned_textured_program_ = p;
                skinned_textured_ok_ = true;
                stu_vp_ = ul(api_, p, "u_view_projection");
                stu_model_ = ul(api_, p, "u_model");
                stu_camera_ = ul(api_, p, "u_camera_position");
                stu_ambient_ = ul(api_, p, "u_ambient_intensity");
                stu_base_ = ul(api_, p, "u_base_roughness");
                stu_metal_ = ul(api_, p, "u_metal_specular");
                stu_emission_ = ul(api_, p, "u_emission_opacity");
                stu_light_dir_ = ul(api_, p, "u_light_direction");
                stu_light_color_ = ul(api_, p, "u_light_color");
                stu_light_intensity_ = ul(api_, p, "u_light_intensity");
                stu_bones_ = ul(api_, p, "u_bones[0]");
                stu_bone_count_ = ul(api_, p, "u_bone_count");
                st_base_ = ul(api_, p, "u_base_texture");
                st_rough_ = ul(api_, p, "u_roughness_texture");
                st_metal_ = ul(api_, p, "u_metallic_texture");
                st_normal_ = ul(api_, p, "u_normal_texture");
                st_ao_ = ul(api_, p, "u_ao_texture");
                st_emission_ = ul(api_, p, "u_emission_texture");
                st_opacity_ = ul(api_, p, "u_opacity_texture");
                st_use_ = ul(api_, p, "u_use_textures");
            } else {
                skinned_textured_program_ = 0;
                skinned_textured_ok_ = false;
                e.clear();
            }
        }
    }
    return true;
}
bool OpenGLESRenderer::ensure_mesh(const RenderDrawCall&d,std::uint64_t frame,GpuCachedMesh*&out,std::string&e){const auto k=mk(d);if(auto it=meshes_.find(k);it!=meshes_.end()){it->second.last_used_frame=frame;out=&it->second;return true;}const bool sk=d.animated();std::vector<float>v;std::vector<std::uint32_t>ix;if(sk){if(!d.skinned_mesh){e="missing skinned mesh";return false;}for(const auto&x:d.skinned_mesh->vertices)v.insert(v.end(),{x.position.x,x.position.y,x.position.z,x.normal.x,x.normal.y,x.normal.z,x.uv.x,x.uv.y,float(x.skin.bones[0]),float(x.skin.bones[1]),float(x.skin.bones[2]),float(x.skin.bones[3]),x.skin.weights[0],x.skin.weights[1],x.skin.weights[2],x.skin.weights[3]});ix=d.skinned_mesh->indices;}else{if(!d.geometry||d.part_index>=d.geometry->parts.size()||!d.geometry->parts[d.part_index].mesh.valid()){e="invalid mesh";return false;}const auto&m=d.geometry->parts[d.part_index].mesh;for(const auto&x:m.vertices)v.insert(v.end(),{x.position.x,x.position.y,x.position.z,x.normal.x,x.normal.y,x.normal.z,x.uv.x,x.uv.y});ix=m.indices;}GpuCachedMesh c;c.animated=sk;c.index_count=ix.size();c.vertex_count=v.size()/(sk?16u:8u);api_.GenVertexArrays(1,&c.vao);api_.GenBuffers(1,&c.vertex_buffer);api_.GenBuffers(1,&c.index_buffer);if(!c.valid()){e="GPU buffer allocation failed";return false;}api_.BindVertexArray(c.vao);api_.BindBuffer(ARRAY,c.vertex_buffer);api_.BufferData(ARRAY,static_cast<GlSize>(v.size()*sizeof(float)),v.data(),STATIC_DRAW);api_.BindBuffer(ELEMENT,c.index_buffer);api_.BufferData(ELEMENT,static_cast<GlSize>(ix.size()*sizeof(std::uint32_t)),ix.data(),STATIC_DRAW);const GlInt stride=static_cast<GlInt>((sk?16u:8u)*sizeof(float));api_.EnableVertexAttribArray(0);api_.VertexAttribPointer(0,3,FLOAT,0,stride,nullptr);api_.EnableVertexAttribArray(1);api_.VertexAttribPointer(1,3,FLOAT,0,stride,reinterpret_cast<const void*>(3*sizeof(float)));api_.EnableVertexAttribArray(2);api_.VertexAttribPointer(2,2,FLOAT,0,stride,reinterpret_cast<const void*>(6*sizeof(float)));if(sk){api_.EnableVertexAttribArray(3);api_.VertexAttribPointer(3,4,FLOAT,0,stride,reinterpret_cast<const void*>(8*sizeof(float)));api_.EnableVertexAttribArray(4);api_.VertexAttribPointer(4,4,FLOAT,0,stride,reinterpret_cast<const void*>(12*sizeof(float)));}auto[it,ok]=meshes_.emplace(k,c);if(!ok){e="mesh cache insertion failed";return false;}out=&it->second;return true;}
bool OpenGLESRenderer::ensure_texture(const std::shared_ptr<const Texture2D>&t,std::uint64_t,GpuCachedTexture*&out,std::string&e){if(!texture_ready()||!t||!t->valid()){e="invalid texture";return false;}const auto k=tex_key(t);if(auto it=textures_.find(k);it!=textures_.end()){out=&it->second;return true;}std::vector<std::uint8_t>px(static_cast<std::size_t>(t->width)*t->height*4,255);for(std::uint32_t y=0;y<t->height;++y)for(std::uint32_t x=0;x<t->width;++x){const auto s=t->index(x,y,0),d=(static_cast<std::size_t>(y)*t->width+x)*4;auto c=[&](std::uint32_t n,std::uint8_t f){return n<t->channels?static_cast<std::uint8_t>(std::clamp(t->data[s+n],0.f,1.f)*255.f+.5f):f;};px[d]=c(0,255);px[d+1]=c(1,px[d]);px[d+2]=c(2,px[d]);px[d+3]=c(3,255);}GpuCachedTexture c;c.width=t->width;c.height=t->height;api_.GenTextures(1,&c.handle);if(!c.handle){e="texture allocation failed";return false;}api_.ActiveTexture(TEX0);api_.BindTexture(TEX2D,c.handle);api_.TexParameteri(TEX2D,MIN_FILTER,LINEAR_MIPMAP_LINEAR);api_.TexParameteri(TEX2D,MAG_FILTER,LINEAR);api_.TexParameteri(TEX2D,WRAP_S,REPEAT);api_.TexParameteri(TEX2D,WRAP_T,REPEAT);api_.TexImage2D(TEX2D,0,RGBA8,static_cast<GlInt>(t->width),static_cast<GlInt>(t->height),0,RGBA,U8,px.data());api_.GenerateMipmap(TEX2D);auto[it,ok]=textures_.emplace(k,c);if(!ok){e="texture cache insertion failed";return false;}out=&it->second;return true;}
GpuSubmitResult OpenGLESRenderer::submit(const RenderFrame& f) {
    GpuSubmitResult r;
    if (!f.valid()) { r.error = "invalid render frame"; return r; }
    if (f.config.backend != RenderBackend::OpenGLES) { r.error = "wrong render backend"; return r; }
    std::string e;
    if (!ensure_programs(e)) { r.error = e; return r; }
    api_.Viewport(0, 0, static_cast<GlInt>(f.config.width), static_cast<GlInt>(f.config.height));
    const auto sky = f.lighting.environment.sky_color;
    api_.ClearColor(sky.r, sky.g, sky.b, 1);
    api_.Clear(COLOR | DEPTH);
    api_.Enable(DEPTH_TEST);
    api_.DepthFunc(LESS);
    api_.Enable(CULL);
    // Drain any stale GL error from previous frames so it cannot poison us.
    if (api_.GetError) (void)api_.GetError();
    static const auto white = solid({1.f});
    static const auto black = solid({0.f});
    static const auto normal = solid({.5f, .5f, 1.f});
    std::size_t skipped_gpu_draws = 0;
    for (const auto& d : f.draws) {
        const bool sk = d.animated();
        // Skinned draws need a skinned program; if the driver refused to build
        // one, skip ONLY this draw — the rest of the world keeps rendering.
        if (sk && !skinned_program_ && !skinned_textured_program_) { ++skipped_gpu_draws; continue; }
        // Oversized bone palettes cannot upload (u_bones[32]); skip safely.
        if (sk && d.bone_palette.size() > 32) { ++skipped_gpu_draws; continue; }
        GpuCachedMesh* m = nullptr;
        if (!ensure_mesh(d, f.frame_id, m, e)) { ++skipped_gpu_draws; e.clear(); continue; }
        const bool tx = texture_ready() && d.material.textures && d.material.textures->valid();
        GpuCachedTexture* mp[7]{};
        if (tx) {
            const auto& t = d.material.textures;
            const std::shared_ptr<const Texture2D> a[7] = {
                t->base_color, t->roughness ? t->roughness : white,
                t->metallic ? t->metallic : black, t->normal ? t->normal : normal,
                t->ambient_occlusion ? t->ambient_occlusion : white,
                t->emission ? t->emission : black, t->opacity ? t->opacity : white};
            bool tex_ok = true;
            for (int i = 0; i < 7; ++i)
                if (!ensure_texture(a[i], f.frame_id, mp[i], e)) { tex_ok = false; break; }
            if (!tex_ok) { ++skipped_gpu_draws; e.clear(); continue; }
        }
        const GlUInt p = sk ? (tx ? skinned_textured_program_ : skinned_program_)
                            : (tx ? textured_program_ : program_);
        if (!p) { ++skipped_gpu_draws; continue; }
        api_.UseProgram(p);
        const GlInt vp = ul(api_, p, "u_view_projection"), mo = ul(api_, p, "u_model"),
                    ca = ul(api_, p, "u_camera_position"), am = ul(api_, p, "u_ambient_intensity"),
                    ba = ul(api_, p, "u_base_roughness"), me = ul(api_, p, "u_metal_specular"),
                    em = ul(api_, p, "u_emission_opacity"), ld = ul(api_, p, "u_light_direction"),
                    lc = ul(api_, p, "u_light_color"), li = ul(api_, p, "u_light_intensity"),
                    tm = ul(api_, p, "u_time"), wa = ul(api_, p, "u_water");
        if (vp >= 0) api_.UniformMatrix4fv(vp, 1, 0, f.view_projection.m.data());
        if (mo >= 0) api_.UniformMatrix4fv(mo, 1, 0, d.model.m.data());
        if (ca >= 0) api_.Uniform3f(ca, f.camera.position.x, f.camera.position.y, f.camera.position.z);
        const auto& mat = d.material.material;
        if (am >= 0) api_.Uniform1f(am, f.lighting.environment.ambient_intensity);
        if (ba >= 0) api_.Uniform4f(ba, mat.base_color.r, mat.base_color.g, mat.base_color.b,
                                    std::clamp(mat.roughness, .045f, 1.f));
        if (me >= 0) api_.Uniform4f(me, mat.metallic, mat.specular, 0, 0);
        if (em >= 0) api_.Uniform4f(em, mat.emission.r, mat.emission.g, mat.emission.b,
                                    std::clamp(mat.opacity, 0.f, 1.f));
        if (!f.lights.empty()) {
            const auto& l = f.lights.front();
            if (ld >= 0) api_.Uniform3f(ld, l.direction.x, l.direction.y, l.direction.z);
            if (lc >= 0) api_.Uniform3f(lc, l.color.r, l.color.g, l.color.b);
            if (li >= 0) api_.Uniform1f(li, l.intensity);
        }
        if (tm >= 0) api_.Uniform1f(tm, float(f.frame_id) * .016f);
        if (wa >= 0) api_.Uniform1f(wa, mat.name == "water" ? 1.f : 0.f);
        {
            const GlInt fc = ul(api_, p, "u_fog_color"), fd = ul(api_, p, "u_fog_density");
            if (fc >= 0) {
                const auto& fog = f.lighting.fog;
                const auto c = fog.enabled ? fog.color : f.lighting.environment.sky_color;
                api_.Uniform3f(fc, c.r, c.g, c.b);
            }
            if (fd >= 0) api_.Uniform1f(fd, f.lighting.fog.enabled ? f.lighting.fog.density : 0.f);
        }
        if (sk) {
            const auto n = static_cast<GlInt>(std::min<std::size_t>(d.bone_palette.size(), 32));
            const auto bl = ul(api_, p, "u_bones[0]");
            if (n > 0 && bl >= 0) api_.UniformMatrix4fv(bl, n, 0, d.bone_palette.front().m.data());
            const auto bc = ul(api_, p, "u_bone_count");
            if (bc >= 0) api_.Uniform1i(bc, n);
        }
        if (tx) {
            const GlInt ls[7] = {ul(api_, p, "u_base_texture"), ul(api_, p, "u_roughness_texture"),
                                 ul(api_, p, "u_metallic_texture"), ul(api_, p, "u_normal_texture"),
                                 ul(api_, p, "u_ao_texture"), ul(api_, p, "u_emission_texture"),
                                 ul(api_, p, "u_opacity_texture")};
            for (int i = 0; i < 7; ++i) {
                api_.ActiveTexture(TEX0 + static_cast<GlEnum>(i));
                api_.BindTexture(TEX2D, mp[i]->handle);
                if (ls[i] >= 0) api_.Uniform1i(ls[i], i);
            }
            const auto use = ul(api_, p, "u_use_textures");
            if (use >= 0) api_.Uniform1i(use, 1);
        }
        api_.BindVertexArray(m->vao);
        if (d.pass == RenderPass::Transparent || mat.opacity < .999f) {
            api_.Enable(BLEND);
            api_.BlendFunc(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);
        } else
            api_.Disable(BLEND);
        api_.DrawElements(TRIANGLES, static_cast<GlInt>(m->index_count), UINT, nullptr);
        ++r.draw_calls;
        r.triangles += m->index_count / 3;
        if (sk) ++r.animated_draw_calls;
        if (tx) ++r.textured_draw_calls;
        // GL error policy: tolerate transient errors (skip this draw only);
        // only report failure after a long streak, so one bad frame never
        // freezes the whole presentation pipeline.
        if (api_.GetError) {
            const GlEnum err = api_.GetError();
            if (err != 0) {
                ++gl_error_streak_;
                r.last_error = err;
                if (gl_error_streak_ > 512) {
                    r.error = "persistent OpenGL ES errors";
                    return r;
                }
            } else {
                gl_error_streak_ = 0;
            }
        }
    }
    if (skipped_gpu_draws > 0 && r.draw_calls == 0) {
        r.error = "all draws skipped on GPU submit";
        return r;
    }
    r.success = true;
    return r;
}
void OpenGLESRenderer::release_meshes()noexcept{if(!api_.DeleteVertexArrays||!api_.DeleteBuffers)return;for(auto&[k,m]:meshes_){(void)k;if(m.vao)api_.DeleteVertexArrays(1,&m.vao);if(m.vertex_buffer)api_.DeleteBuffers(1,&m.vertex_buffer);if(m.index_buffer)api_.DeleteBuffers(1,&m.index_buffer);}meshes_.clear();}
void OpenGLESRenderer::release_textures()noexcept{if(!api_.DeleteTextures)return;for(auto&[k,t]:textures_){(void)k;if(t.handle)api_.DeleteTextures(1,&t.handle);}textures_.clear();}
void OpenGLESRenderer::release()noexcept{release_meshes();release_textures();if(api_.DeleteProgram){if(program_)api_.DeleteProgram(program_);if(skinned_program_)api_.DeleteProgram(skinned_program_);if(textured_program_)api_.DeleteProgram(textured_program_);if(skinned_textured_program_)api_.DeleteProgram(skinned_textured_program_);}program_=skinned_program_=textured_program_=skinned_textured_program_=0;}
}
