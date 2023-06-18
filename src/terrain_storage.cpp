//Copyright © 2023 Roope Palmroos, Cory Petkovsek, and Contributors. All rights reserved. See LICENSE.
#include <godot_cpp/core/class_db.hpp>

#include "terrain_logger.h"
#include "terrain_storage.h"

void Terrain3DStorage::Generated::create(const TypedArray<Image> &p_layers) {
	if (!p_layers.is_empty()) {
		rid = RenderingServer::get_singleton()->texture_2d_layered_create(p_layers, RenderingServer::TEXTURE_LAYERED_2D_ARRAY);
		dirty = false;
	} else {
		clear();
	}
}

void Terrain3DStorage::Generated::create(const Ref<Image> &p_image) {
	image = p_image;
	rid = RenderingServer::get_singleton()->texture_2d_create(image);
	dirty = false;
}

void Terrain3DStorage::Generated::clear() {
	if (rid.is_valid()) {
		RenderingServer::get_singleton()->free_rid(rid);
	}
	if (image.is_valid()) {
		image.unref();
	}
	rid = RID();
	dirty = true;
}

Terrain3DStorage::Terrain3DStorage() {
	if (!_initialized) {
		LOG(INFO, "Initializing terrain storage");
		_update_material();
		_initialized = true;
	}
}

Terrain3DStorage::~Terrain3DStorage() {
	if (_initialized) {
		_clear();
	}
}

void Terrain3DStorage::_clear() {
	RenderingServer::get_singleton()->free_rid(material);
	RenderingServer::get_singleton()->free_rid(shader);

	generated_height_maps.clear();
	generated_control_maps.clear();
	generated_color_maps.clear();
	generated_albedo_textures.clear();
	generated_normal_textures.clear();
	generated_region_map.clear();
	generated_region_blend_map.clear();
}

void Terrain3DStorage::print_audit_data() {
	LOG(INFO, "Dumping storage data");

	LOG(INFO, "_initialized: ", _initialized);
	LOG(INFO, "region_offsets(", region_offsets.size(), "): ", region_offsets);

	LOG(INFO, "Map type height size: ", height_maps.size(), " ", height_maps);
	LOG(INFO, "Map type control size: ", control_maps.size(), " ", control_maps);
	LOG(INFO, "Map type color size: ", color_maps.size(), " ", color_maps);

	LOG(INFO, "generated_region_map RID: ", generated_region_map.get_rid(), " dirty: ", generated_region_map.is_dirty(), ", image: ", generated_region_map.get_image());
	LOG(INFO, "generated_region_blend_map RID: ", generated_region_blend_map.get_rid(), ", dirty: ", generated_region_blend_map.is_dirty(), ", image: ", generated_region_blend_map.get_image());
	LOG(INFO, "generated_height_maps RID: ", generated_height_maps.get_rid(), ", dirty: ", generated_height_maps.is_dirty(), ", image: ", generated_height_maps.get_image());
	LOG(INFO, "generated_control_maps RID: ", generated_control_maps.get_rid(), ", dirty: ", generated_control_maps.is_dirty(), ", image: ", generated_control_maps.get_image());
	LOG(INFO, "generated_color_maps RID: ", generated_color_maps.get_rid(), ", dirty: ", generated_color_maps.is_dirty(), ", image: ", generated_color_maps.get_image());
	LOG(INFO, "generated_albedo_textures RID: ", generated_albedo_textures.get_rid(), ", dirty: ", generated_albedo_textures.is_dirty(), ", image: ", generated_albedo_textures.get_image());
	LOG(INFO, "generated_normal_textures RID: ", generated_normal_textures.get_rid(), ", dirty: ", generated_normal_textures.is_dirty(), ", image: ", generated_normal_textures.get_image());
}

void Terrain3DStorage::set_region_size(RegionSize p_size) {
	LOG(INFO, "Setting region size: ", p_size);
	ERR_FAIL_COND(p_size < SIZE_64);
	ERR_FAIL_COND(p_size > SIZE_2048);

	region_size = p_size;
	region_vsize = Vector2i(region_size, region_size);
	RenderingServer::get_singleton()->material_set_param(material, "region_size", float(region_size));
	RenderingServer::get_singleton()->material_set_param(material, "region_pixel_size", 1.0f / float(region_size));
}

Vector2i Terrain3DStorage::_get_offset_from(Vector3 p_global_position) {
	return Vector2i((Vector2(p_global_position.x, p_global_position.z) / float(region_size) + Vector2(0.5, 0.5)).floor());
}

Error Terrain3DStorage::add_region(Vector3 p_global_position) {
	if (has_region(p_global_position)) {
		return FAILED;
	}
	Vector2i uv_offset = _get_offset_from(p_global_position);

	if (ABS(uv_offset.x) > REGION_MAP_SIZE / 2 || ABS(uv_offset.y) > REGION_MAP_SIZE / 2) {
		return FAILED;
	}

	LOG(INFO, "Adding region at: ", uv_offset);
	Ref<Image> hmap_img = Image::create(region_size, region_size, false, FORMAT[TYPE_HEIGHT]);
	Ref<Image> conmap_img = Image::create(region_size, region_size, false, FORMAT[TYPE_CONTROL]);
	Ref<Image> clrmap_img = Image::create(region_size, region_size, false, FORMAT[TYPE_COLOR]);

	hmap_img->fill(COLOR[TYPE_HEIGHT]);
	conmap_img->fill(COLOR[TYPE_CONTROL]);
	clrmap_img->fill(COLOR[TYPE_COLOR]);

	height_maps.push_back(hmap_img);
	LOG(DEBUG, "Height maps size after pushback: ", height_maps.size());
	control_maps.push_back(conmap_img);
	LOG(DEBUG, "Control maps size after pushback: ", control_maps.size());
	color_maps.push_back(clrmap_img);
	LOG(DEBUG, "Color maps size after pushback: ", color_maps.size());
	region_offsets.push_back(uv_offset);
	LOG(DEBUG, "Total regions after pushback: ", region_offsets.size());

	generated_height_maps.clear();
	generated_control_maps.clear();
	generated_color_maps.clear();
	generated_region_map.clear();
	generated_region_blend_map.clear();

	_update_regions();

	notify_property_list_changed();
	emit_changed();
	return OK;
}

void Terrain3DStorage::remove_region(Vector3 p_global_position) {
	if (get_region_count() == 1) {
		return;
	}

	int index = get_region_index(p_global_position);
	ERR_FAIL_COND_MSG(index == -1, "Map does not exist.");

	LOG(INFO, "Removing region at: ", _get_offset_from(p_global_position));
	region_offsets.remove_at(index);
	LOG(DEBUG, "Removing region_offsets, size after removal: ", region_offsets.size());
	height_maps.remove_at(index);
	LOG(DEBUG, "Removing heightmaps, size after removal: ", height_maps.size());
	control_maps.remove_at(index);
	LOG(DEBUG, "Removing control maps, size after removal: ", control_maps.size());
	color_maps.remove_at(index);
	LOG(DEBUG, "Removing colormaps, size after removal: ", color_maps.size());

	generated_height_maps.clear();
	generated_control_maps.clear();
	generated_color_maps.clear();
	generated_region_map.clear();
	generated_region_blend_map.clear();

	_update_regions();

	notify_property_list_changed();
	emit_changed();
}

bool Terrain3DStorage::has_region(Vector3 p_global_position) {
	return get_region_index(p_global_position) != -1;
}

int Terrain3DStorage::get_region_index(Vector3 p_global_position) {
	Vector2i uv_offset = _get_offset_from(p_global_position);
	int index = -1;

	if (ABS(uv_offset.x) > REGION_MAP_SIZE / 2 || ABS(uv_offset.y) > REGION_MAP_SIZE / 2) {
		return index;
	}

	Ref<Image> img = generated_region_map.get_image();

	if (img.is_valid()) {
		index = int(img->get_pixelv(uv_offset + (REGION_MAP_VSIZE / 2)).r * 255.0) - 1;
	} else {
		for (int i = 0; i < region_offsets.size(); i++) {
			Vector2i ofs = region_offsets[i];
			if (ofs == uv_offset) {
				index = i;
				break;
			}
		}
	}
	return index;
}

void Terrain3DStorage::set_region_offsets(const TypedArray<Vector2i> &p_array) {
	LOG(INFO, "Setting region offsets with array sized: ", p_array.size());
	region_offsets = p_array;

	generated_region_map.clear();
	generated_region_blend_map.clear();
	_update_regions();
}

void Terrain3DStorage::set_map_region(MapType p_map_type, int p_region_index, const Ref<Image> p_image) {
	switch (p_map_type) {
		case TYPE_HEIGHT:
			if (p_region_index >= 0 && p_region_index < height_maps.size()) {
				height_maps[p_region_index] = p_image;
			} else {
				LOG(ERROR, "Requested index is out of bounds. height_maps size: ", height_maps.size());
			}
			break;
		case TYPE_CONTROL:
			if (p_region_index >= 0 && p_region_index < control_maps.size()) {
				control_maps[p_region_index] = p_image;
			} else {
				LOG(ERROR, "Requested index is out of bounds. control_maps size: ", control_maps.size());
			}
			break;
		case TYPE_COLOR:
			if (p_region_index >= 0 && p_region_index < color_maps.size()) {
				color_maps[p_region_index] = p_image;
			} else {
				LOG(ERROR, "Requested index is out of bounds. color_maps size: ", color_maps.size());
			}
			break;
		default:
			LOG(ERROR, "Requested map type is invalid");
			break;
	}
}

Ref<Image> Terrain3DStorage::get_map_region(MapType p_map_type, int p_region_index) const {
	switch (p_map_type) {
		case TYPE_HEIGHT:
			if (p_region_index >= 0 && p_region_index < height_maps.size()) {
				return height_maps[p_region_index];
			} else {
				LOG(ERROR, "Requested index is out of bounds. height_maps size: ", height_maps.size());
			}
			break;
		case TYPE_CONTROL:
			if (p_region_index >= 0 && p_region_index < control_maps.size()) {
				return control_maps[p_region_index];
			} else {
				LOG(ERROR, "Requested index is out of bounds. control_maps size: ", control_maps.size());
			}
			break;
		case TYPE_COLOR:
			if (p_region_index >= 0 && p_region_index < color_maps.size()) {
				return color_maps[p_region_index];
			} else {
				LOG(ERROR, "Requested index is out of bounds. color_maps size: ", color_maps.size());
			}
			break;
		default:
			LOG(ERROR, "Requested map type is invalid");
			break;
	}
	return Ref<Image>();
}

void Terrain3DStorage::set_maps(MapType p_map_type, const TypedArray<Image> &p_maps) {
	ERR_FAIL_COND_MSG(p_map_type < 0 || p_map_type >= TYPE_MAX, "Specified map type out of range");
	switch (p_map_type) {
		case TYPE_HEIGHT:
			set_height_maps(p_maps);
			break;
		case TYPE_CONTROL:
			set_control_maps(p_maps);
			break;
		case TYPE_COLOR:
			set_color_maps(p_maps);
			break;
		default:
			break;
	}
}

TypedArray<Image> Terrain3DStorage::get_maps(MapType p_map_type) const {
	if (p_map_type < 0 || p_map_type >= TYPE_MAX) {
		LOG(ERROR, "Specified map type out of range");
		return Ref<Image>();
	}
	switch (p_map_type) {
		case TYPE_HEIGHT:
			return get_height_maps();
			break;
		case TYPE_CONTROL:
			return get_control_maps();
			break;
		case TYPE_COLOR:
			return get_color_maps();
			break;
		default:
			break;
	}
	return Ref<Image>();
}

TypedArray<Image> Terrain3DStorage::get_maps_copy(MapType p_map_type) const {
	if (p_map_type < 0 || p_map_type >= TYPE_MAX) {
		LOG(ERROR, "Specified map type out of range");
		return TypedArray<Image>();
	}

	TypedArray<Image> maps = get_maps(p_map_type);
	TypedArray<Image> newmaps;
	newmaps.resize(maps.size());
	for (int i = 0; i < maps.size(); i++) {
		Ref<Image> img;
		img.instantiate();
		img->copy_from(maps[i]);
		newmaps[i] = img;
	}
	return newmaps;
}

void Terrain3DStorage::set_height_maps(const TypedArray<Image> &p_maps) {
	LOG(INFO, "Setting height maps: ", p_maps.size());
	height_maps = p_maps;
	force_update_maps(TYPE_HEIGHT);
}

void Terrain3DStorage::set_control_maps(const TypedArray<Image> &p_maps) {
	LOG(INFO, "Setting control maps: ", p_maps.size());
	control_maps = p_maps;
	force_update_maps(TYPE_CONTROL);
}

void Terrain3DStorage::set_color_maps(const TypedArray<Image> &p_maps) {
	LOG(INFO, "Setting color maps: ", p_maps.size());
	color_maps = p_maps;
	force_update_maps(TYPE_COLOR);
}

void Terrain3DStorage::force_update_maps(MapType p_map_type) {
	switch (p_map_type) {
		case TYPE_HEIGHT:
			generated_height_maps.clear();
			break;
		case TYPE_CONTROL:
			generated_control_maps.clear();
			break;
		case TYPE_COLOR:
			generated_color_maps.clear();
			break;
		default:
			generated_height_maps.clear();
			generated_control_maps.clear();
			generated_color_maps.clear();
			break;
	}
	_update_regions();
}

void Terrain3DStorage::set_shader_override(const Ref<Shader> &p_shader) {
	LOG(INFO, "Setting override shader");
	shader_override = p_shader;
	_update_material();
}

void Terrain3DStorage::enable_shader_override(bool p_enabled) {
	LOG(INFO, "Enable shader override: ", p_enabled);
	shader_override_enabled = p_enabled;
	if (shader_override_enabled && shader_override.is_null()) {
		String code = _generate_shader_code();
		Ref<Shader> shader_res;
		shader_res.instantiate();
		shader_res->set_code(code);
		set_shader_override(shader_res);
	} else {
		_update_material();
	}
}

void Terrain3DStorage::set_noise_enabled(bool p_enabled) {
	LOG(INFO, "Enable noise: ", p_enabled);
	noise_enabled = p_enabled;
	_update_material();
	if (noise_enabled) {
		generated_region_map.clear();
		generated_region_blend_map.clear();
		_update_regions();
	}
}

void Terrain3DStorage::set_noise_scale(float p_scale) {
	LOG(INFO, "Setting noise scale: ", p_scale);
	noise_scale = p_scale;
	RenderingServer::get_singleton()->material_set_param(material, "noise_scale", noise_scale);
}

void Terrain3DStorage::set_noise_height(float p_height) {
	LOG(INFO, "Setting noise height: ", p_height);
	noise_height = p_height;
	RenderingServer::get_singleton()->material_set_param(material, "noise_height", noise_height);
}

void Terrain3DStorage::set_noise_blend_near(float p_near) {
	LOG(INFO, "Setting noise blend near: ", p_near);
	noise_blend_near = p_near;
	if (noise_blend_near > noise_blend_far) {
		set_noise_blend_far(noise_blend_near);
	}
	RenderingServer::get_singleton()->material_set_param(material, "noise_blend_near", noise_blend_near);
}

void Terrain3DStorage::set_noise_blend_far(float p_far) {
	LOG(INFO, "Setting noise blend far: ", p_far);
	noise_blend_far = p_far;
	if (noise_blend_far < noise_blend_near) {
		set_noise_blend_near(noise_blend_far);
	}
	RenderingServer::get_singleton()->material_set_param(material, "noise_blend_far", noise_blend_far);
}

void Terrain3DStorage::set_surface(const Ref<Terrain3DSurface> &p_material, int p_index) {
	LOG(INFO, "Setting surface index: ", p_index);
	if (p_index < get_surface_count()) {
		if (p_material.is_null()) {
			Ref<Terrain3DSurface> surface = surfaces[p_index];
			surface->disconnect("texture_changed", Callable(this, "update_surface_textures"));
			surface->disconnect("value_changed", Callable(this, "update_surface_values"));
			surfaces.remove_at(p_index);
		} else {
			surfaces[p_index] = p_material;
		}
	} else {
		surfaces.push_back(p_material);
	}
	_update_surfaces();
	notify_property_list_changed();
}

void Terrain3DStorage::set_surfaces(const TypedArray<Terrain3DSurface> &p_surfaces) {
	LOG(INFO, "Setting surfaces");
	surfaces = p_surfaces;
	_update_surfaces();
}

void Terrain3DStorage::update_surface_textures() {
	generated_albedo_textures.clear();
	generated_normal_textures.clear();
	_update_surface_data(true, false);
}

void Terrain3DStorage::update_surface_values() {
	_update_surface_data(false, true);
}

void Terrain3DStorage::_update_surfaces() {
	LOG(INFO, "Regenerating material surfaces");

	for (int i = 0; i < get_surface_count(); i++) {
		Ref<Terrain3DSurface> surface = surfaces[i];

		if (surface.is_null()) {
			continue;
		}
		if (!surface->is_connected("texture_changed", Callable(this, "update_surface_textures"))) {
			surface->connect("texture_changed", Callable(this, "update_surface_textures"));
		}
		if (!surface->is_connected("value_changed", Callable(this, "update_surface_values"))) {
			surface->connect("value_changed", Callable(this, "update_surface_values"));
		}
	}
	generated_albedo_textures.clear();
	generated_normal_textures.clear();

	_update_surface_data(true, true);
}

void Terrain3DStorage::_update_surface_data(bool p_update_textures, bool p_update_values) {
	if (p_update_textures) {
		LOG(INFO, "Regenerating terrain textures");
		// Update materials to enable sub-materials if albedo is available
		// and 'surfaces_enabled' changes from previous state

		bool was_surfaces_enabled = surfaces_enabled;
		surfaces_enabled = false;

		Vector2i albedo_size = Vector2i(0, 0);
		Vector2i normal_size = Vector2i(0, 0);

		// Get image size
		for (int i = 0; i < get_surface_count(); i++) {
			Ref<Terrain3DSurface> surface = surfaces[i];

			if (surface.is_null()) {
				continue;
			}

			Ref<Texture2D> alb_tex = surface->get_albedo_texture();
			Ref<Texture2D> nor_tex = surface->get_normal_texture();

			if (alb_tex.is_valid()) {
				Vector2i tex_size = alb_tex->get_size();
				if (albedo_size.length() == 0.0) {
					albedo_size = tex_size;
				} else {
					ERR_FAIL_COND_MSG(tex_size != albedo_size, "Albedo textures do not have same size!");
				}
			}
			if (nor_tex.is_valid()) {
				Vector2i tex_size = nor_tex->get_size();
				if (normal_size.length() == 0.0) {
					normal_size = tex_size;
				} else {
					ERR_FAIL_COND_MSG(tex_size != normal_size, "Normal map textures do not have same size!");
				}
			}
		}

		if (normal_size == Vector2i(0, 0)) {
			normal_size = albedo_size;
		} else if (albedo_size == Vector2i(0, 0)) {
			albedo_size = normal_size;
		}

		// Generate TextureArrays and replace nulls with a empty image
		if (generated_albedo_textures.is_dirty() && albedo_size != Vector2i(0, 0)) {
			LOG(INFO, "Regenerating terrain albedo arrays");

			Array albedo_texture_array;

			for (int i = 0; i < get_surface_count(); i++) {
				Ref<Terrain3DSurface> surface = surfaces[i];

				if (surface.is_null()) {
					continue;
				}

				Ref<Texture2D> tex = surface->get_albedo_texture();
				Ref<Image> img;

				if (tex.is_null()) {
					img = Image::create(albedo_size.x, albedo_size.y, true, Image::FORMAT_RGBA8);
					img->fill(COLOR_RB);
					img->generate_mipmaps();
					img->compress(Image::COMPRESS_S3TC, Image::COMPRESS_SOURCE_SRGB);
				} else {
					img = tex->get_image();
				}

				albedo_texture_array.push_back(img);
			}

			if (!albedo_texture_array.is_empty()) {
				generated_albedo_textures.create(albedo_texture_array);
				surfaces_enabled = true;
			}
		}

		if (generated_normal_textures.is_dirty() && normal_size != Vector2i(0, 0)) {
			LOG(INFO, "Regenerating terrain normal arrays");

			Array normal_texture_array;

			for (int i = 0; i < get_surface_count(); i++) {
				Ref<Terrain3DSurface> surface = surfaces[i];

				if (surface.is_null()) {
					continue;
				}

				Ref<Texture2D> tex = surface->get_normal_texture();
				Ref<Image> img;

				if (tex.is_null()) {
					img = Image::create(normal_size.x, normal_size.y, true, Image::FORMAT_RGBA8);
					img->fill(COLOR_NORMAL);
					img->generate_mipmaps();
					img->compress(Image::COMPRESS_S3TC, Image::COMPRESS_SOURCE_SRGB);
				} else {
					img = tex->get_image();
				}

				normal_texture_array.push_back(img);
			}
			if (!normal_texture_array.is_empty()) {
				generated_normal_textures.create(normal_texture_array);
			}
		}

		if (was_surfaces_enabled != surfaces_enabled) {
			_update_material();
		}

		RenderingServer::get_singleton()->material_set_param(material, "texture_array_albedo", generated_albedo_textures.get_rid());
		RenderingServer::get_singleton()->material_set_param(material, "texture_array_normal", generated_normal_textures.get_rid());
	}

	if (p_update_values) {
		LOG(INFO, "Updating terrain color and scale arrays");
		PackedVector3Array uv_scales;
		PackedColorArray colors;

		for (int i = 0; i < get_surface_count(); i++) {
			Ref<Terrain3DSurface> surface = surfaces[i];

			if (surface.is_null()) {
				continue;
			}
			uv_scales.push_back(surface->get_uv_scale());
			colors.push_back(surface->get_albedo());
		}

		RenderingServer::get_singleton()->material_set_param(material, "texture_uv_scale_array", uv_scales);
		RenderingServer::get_singleton()->material_set_param(material, "texture_color_array", colors);
	}
}

void Terrain3DStorage::_update_regions() {
	if (generated_height_maps.is_dirty()) {
		LOG(INFO, "Regenerating height layered texture from ", height_maps.size(), " maps");
		generated_height_maps.create(height_maps);
		RenderingServer::get_singleton()->material_set_param(material, "height_maps", generated_height_maps.get_rid());
	}

	if (generated_control_maps.is_dirty()) {
		LOG(INFO, "Regenerating control layered texture from ", control_maps.size(), " maps");
		generated_control_maps.create(control_maps);
		RenderingServer::get_singleton()->material_set_param(material, "control_maps", generated_control_maps.get_rid());
	}

	if (generated_color_maps.is_dirty()) {
		LOG(INFO, "Regenerating color layered texture from ", color_maps.size(), " maps");
		generated_color_maps.create(color_maps);
		// Enable when colormaps are in the shader
		//RenderingServer::get_singleton()->material_set_param(material, "color_maps", generated_color_maps.get_rid());
	}

	if (generated_region_map.is_dirty()) {
		LOG(INFO, "Regenerating ", REGION_MAP_VSIZE, " region map");
		Ref<Image> region_map_img = Image::create(REGION_MAP_SIZE, REGION_MAP_SIZE, false, Image::FORMAT_RG8);
		region_map_img->fill(COLOR_BLACK);

		for (int i = 0; i < region_offsets.size(); i++) {
			Vector2i ofs = region_offsets[i];

			Color col = Color(float(i + 1) / 255.0, 1.0, 0, 1);
			region_map_img->set_pixelv(ofs + (REGION_MAP_VSIZE / 2), col);
		}
		generated_region_map.create(region_map_img);
		RenderingServer::get_singleton()->material_set_param(material, "region_map", generated_region_map.get_rid());
		RenderingServer::get_singleton()->material_set_param(material, "region_map_size", REGION_MAP_SIZE);
		RenderingServer::get_singleton()->material_set_param(material, "region_offsets", region_offsets);

		if (noise_enabled) {
			LOG(INFO, "Regenerating ", Vector2i(512, 512), " region blend map");
			Ref<Image> region_blend_img = Image::create(REGION_MAP_SIZE, REGION_MAP_SIZE, false, Image::FORMAT_RH);
			for (int y = 0; y < region_map_img->get_height(); y++) {
				for (int x = 0; x < region_map_img->get_width(); x++) {
					Color c = region_map_img->get_pixel(x, y);
					c.r = c.g;
					region_blend_img->set_pixel(x, y, c);
				}
			}
			//region_blend_img->resize(512, 512, Image::INTERPOLATE_NEAREST); // No blur for use with Gaussian blur to add later
			region_blend_img->resize(512, 512, Image::INTERPOLATE_LANCZOS); // Basic blur w/ subtle artifacts

			generated_region_blend_map.create(region_blend_img);
			RenderingServer::get_singleton()->material_set_param(material, "region_blend_map", generated_region_blend_map.get_rid());
		}
	}
}

void Terrain3DStorage::_update_material() {
	LOG(INFO, "Updating material");
	if (!material.is_valid()) {
		material = RenderingServer::get_singleton()->material_create();
	}

	if (!shader.is_valid()) {
		shader = RenderingServer::get_singleton()->shader_create();
	}

	if (shader_override_enabled && shader_override.is_valid()) {
		RenderingServer::get_singleton()->material_set_shader(material, shader_override->get_rid());
	} else {
		RenderingServer::get_singleton()->shader_set_code(shader, _generate_shader_code());
		RenderingServer::get_singleton()->material_set_shader(material, shader);
	}

	RenderingServer::get_singleton()->material_set_param(material, "terrain_height", TERRAIN_MAX_HEIGHT);
	RenderingServer::get_singleton()->material_set_param(material, "region_size", region_size);
	RenderingServer::get_singleton()->material_set_param(material, "region_pixel_size", 1.0f / float(region_size));
}

String Terrain3DStorage::_generate_shader_code() {
	LOG(INFO, "Generating default shader code");
	String code = "shader_type spatial;\n";
	code += "render_mode depth_draw_opaque, diffuse_burley;\n";
	code += "\n";

	//Uniforms
	code += "uniform float terrain_height = 512.0;\n";
	code += "uniform float region_size = 1024.0;\n";
	code += "uniform float region_pixel_size = 1.0;\n";
	code += "uniform int region_map_size = 16;\n";
	code += "\n\n";

	code += "uniform sampler2D region_map : hint_default_black, filter_linear, repeat_disable;\n";
	code += "uniform vec2 region_offsets[256];\n";
	code += "uniform sampler2DArray height_maps : filter_linear_mipmap, repeat_disable;\n";
	code += "uniform sampler2DArray control_maps : filter_linear_mipmap, repeat_disable;\n";
	code += "\n\n";

	if (surfaces_enabled) {
		if (surfaces_enabled) {
			LOG(INFO, "Surfaces enabled");
		}

		code += "uniform sampler2DArray texture_array_albedo : source_color, filter_linear_mipmap_anisotropic, repeat_enable;\n";
		code += "uniform sampler2DArray texture_array_normal : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;\n";
		code += "uniform vec3 texture_uv_scale_array[256];\n";
		code += "uniform vec3 texture_3d_projection_array[256];\n";
		code += "uniform vec4 texture_color_array[256];\n";
		code += "\n\n";
	}

	if (noise_enabled) {
		code += "uniform sampler2D region_blend_map : hint_default_black, filter_linear, repeat_disable;\n";
		code += "uniform float noise_scale = 2.0;\n";
		code += "uniform float noise_height = 1.0;\n";
		code += "uniform float noise_blend_near = 0.5;\n";
		code += "uniform float noise_blend_far = 1.0;\n";
		code += "\n\n";

		code += "float hashv2(vec2 v) {\n ";
		code += "	return fract(1e4 * sin(17.0 * v.x + v.y * 0.1) * (0.1 + abs(sin(v.y * 13.0 + v.x))));\n ";
		code += "}\n\n";

		code += "float noise2D(vec2 st) {\n";
		code += "	vec2 i = floor(st);\n";
		code += "	vec2 f = fract(st);\n";
		code += "\n";
		code += "	// Four corners in 2D of a tile\n";
		code += "	float a = hashv2(i);\n";
		code += "	float b = hashv2(i + vec2(1.0, 0.0));\n";
		code += "	float c = hashv2(i + vec2(0.0, 1.0));\n";
		code += "	float d = hashv2(i + vec2(1.0, 1.0));\n";
		code += "\n";
		code += "	// Cubic Hermine Curve.  Same as SmoothStep()\n";
		code += "	vec2 u = f * f * (3.0 - 2.0 * f);\n";
		code += "\n";
		code += "	// Mix 4 corners percentages\n";
		code += "	return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;\n";
		code += "}\n\n";
	}
	code += "\n";

	//Functions

	code += "vec3 unpack_normal(vec4 rgba) {\n";
	code += "	vec3 n = rgba.xzy * 2.0 - vec3(1.0);\n";
	code += "	n.z *= -1.0;\n";
	code += "	return n;\n";
	code += "}\n\n";

	code += "vec4 pack_normal(vec3 n, float a) {\n";
	code += "	n.z *= -1.0;\n";
	code += "	return vec4((n.xzy + vec3(1.0)) * 0.5, a);\n";
	code += "}\n\n";

	code += "// takes in world uv, returns non - normalized tex coords in region space\n";
	code += "ivec3 get_region(vec2 uv) {\n";
	code += "	float index = floor(texelFetch(region_map, ivec2(floor(uv)) + (region_map_size / 2), 0).r * 255.0) - 1.0;\n";
	code += "	return ivec3(ivec2((uv - region_offsets[int(index)]) * region_size), int(index));\n";
	code += "}\n\n";

	code += "// takes in world uv, returns uv in region space\n";
	code += "vec3 get_regionf(vec2 uv) {\n";
	code += "	float index = floor(texelFetch(region_map, ivec2(floor(uv)) + (region_map_size / 2), 0).r * 255.0) - 1.0;\n";
	code += "	return vec3(uv - region_offsets[int(index)], index);\n";
	code += "}\n\n";

	code += "float get_height(vec2 uv, bool linear) {\n";
	code += "	float height = 0.0;\n\n";
	code += "	if (!linear) {\n";
	code += "		ivec3 region = get_region(uv);\n";
	code += "		height = texelFetch(height_maps, region, 0).r;\n";
	code += "	}\n\n";
	code += "	if (linear) {\n";
	code += "		vec3 region = get_regionf(uv);\n";
	code += "		height = texture(height_maps, region).r;\n";
	code += "	}\n";

	if (noise_enabled) {
		code += "	float weight = texture(region_blend_map, (uv/float(region_map_size))+0.5).r;\n";
		code += "	height = mix(height, noise2D(uv * noise_scale) * noise_height, \n";
		code += "		clamp(smoothstep(noise_blend_near, noise_blend_far, 1.0 - weight), 0.0, 1.0));\n ";
	}

	code += "	return height * terrain_height;\n";
	code += "}\n\n";

	if (surfaces_enabled) {
		code += "float random(in vec2 xy) {\n";
		code += "	return fract(sin(dot(xy, vec2(12.9898, 78.233))) * 43758.5453);\n";
		code += "}\n\n";

		code += "float blend_weights(float weight, float detail) {\n";
		code += "	weight = sqrt(weight * 0.5);\n";
		code += "	float result = max(0.1 * weight, 10.0 * (weight + detail) + 1.0f - (detail + 10.0));\n";
		code += "	return result;\n";
		code += "}\n\n";

		code += "vec4 depth_blend(vec4 a_value, float a_bump, vec4 b_value, float b_bump, float t) {\n";
		code += "	float ma = max(a_bump + (1.0 - t), b_bump + t) - 0.1;\n";
		code += "	float ba = max(a_bump + (1.0 - t) - ma, 0.0);\n";
		code += "	float bb = max(b_bump + t - ma, 0.0);\n";
		code += "	return (a_value * ba + b_value * bb) / (ba + bb);\n";
		code += "}\n\n";

		code += "vec2 rotate(vec2 v, float cosa, float sina) {\n";
		code += "	return vec2(cosa * v.x - sina * v.y, sina * v.x + cosa * v.y);\n";
		code += "}\n\n";

		code += "// One big mess here.Optimized version of what it was in my GDScript terrain plugin.- outobugi\n";
		code += "// Using 'else' caused fps drops.If - else works the same as a ternary, where both outcomes are evaluated. Right?\n";

		code += "vec4 get_material(vec2 uv, vec4 index, vec2 uv_center, float weight, inout float total_weight, inout vec4 out_normal) {\n";
		code += "	float material = index.r * 255.0;\n";
		code += "	float materialOverlay = index.g * 255.0;\n";
		code += "	float rand = random(uv_center) * PI;\n";
		code += "	vec2 rot = vec2(sin(rand), cos(rand));\n";
		code += "	vec2 matUV = rotate(uv, rot.x, rot.y) * texture_uv_scale_array[int(material)].xy;\n";
		code += "	vec2 ddx = dFdx(uv);\n";
		code += "	vec2 ddy = dFdy(uv);\n";
		code += "	vec4 albedo = vec4(1.0);\n";
		code += "	vec4 normal = vec4(0.5);\n\n";
		code += "	if (index.b == 0.0) {\n";
		code += "		albedo = textureGrad(texture_array_albedo, vec3(matUV, material), ddx, ddy);\n";
		code += "		normal = textureGrad(texture_array_normal, vec3(matUV, material), ddx, ddy);\n";
		code += "	}\n\n";
		code += "	if (index.b > 0.0) {\n";
		code += "		albedo = textureGrad(texture_array_albedo, vec3(matUV, material), ddx, ddy);\n";
		code += "		normal = textureGrad(texture_array_normal, vec3(matUV, material), ddx, ddy);\n";
		code += "		vec4 albedo2 = textureGrad(texture_array_albedo, vec3(matUV, materialOverlay), ddx, ddy);\n";
		code += "		vec4 normal2 = textureGrad(texture_array_normal, vec3(matUV, materialOverlay), ddx, ddy);\n";
		code += "		albedo = depth_blend(albedo, albedo.a, albedo2, albedo2.a, index.b);\n";
		code += "		normal = depth_blend(normal, albedo.a, normal2, albedo.a, index.b);\n";
		code += "	}\n\n";
		code += "	vec3 n = unpack_normal(normal);\n";
		code += "	n.xz = rotate(n.xz, rot.x, -rot.y);\n";
		code += "	normal = pack_normal(n, normal.a);\n";
		code += "	weight = blend_weights(weight, albedo.a);\n";
		code += "	out_normal += normal * weight;\n";
		code += "	total_weight += weight;\n";
		code += "	return albedo * weight;\n";
		code += "}\n\n";
	}

	// Vertex Shader
	code += "void vertex() {\n";
	code += "	vec3 world_vertex = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;\n";
	code += "	UV2 = (world_vertex.xz / vec2(region_size)) + vec2(0.5);\n";
	code += "	UV = world_vertex.xz * 0.5;\n\n";

	code += "	VERTEX.y = get_height(UV2, false);\n";
	code += "	NORMAL = vec3(0, 1, 0);\n";
	code += "	TANGENT = cross(NORMAL, vec3(0, 0, 1));\n";
	code += "	BINORMAL = cross(NORMAL, TANGENT);\n";
	code += "}\n\n";

	// Fragment Shader
	code += "void fragment() {\n";

	code += "// Normal calc\n";
	code += "// Control map is also sampled 4 times, so in theory we could reduce the region samples to 4 from 8,\n";
	code += "// but control map sampling is slightly different with the mirroring and doesn't work here.\n";
	code += "// The region map is very, very small, so maybe the performance cost isn't too high\n\n";

	code += "	float left = get_height(UV2 + vec2(-region_pixel_size, 0), true);\n";
	code += "	float right = get_height(UV2 + vec2(region_pixel_size, 0), true);\n";
	code += "	float back = get_height(UV2 + vec2(0, -region_pixel_size), true);\n";
	code += "	float fore = get_height(UV2 + vec2(0, region_pixel_size), true);\n\n";

	code += "	vec3 horizontal = vec3(2.0, right - left, 0.0);\n";
	code += "	vec3 vertical = vec3(0.0, back - fore, 2.0);\n";
	code += "	vec3 normal = normalize(cross(vertical, horizontal));\n";
	code += "	normal.z *= -1.0;\n\n";

	code += "	NORMAL = mat3(VIEW_MATRIX) * normal;\n";
	code += "\n";

	if (surfaces_enabled) {
		code += "// source : https://github.com/cdxntchou/IndexMapTerrain\n";
		code += "// black magic which I don't understand at all. Seems simple but what and why?\n";

		code += "	vec2 pos_texel = UV2 * region_size + 0.5;\n";
		code += "	vec2 pos_texel00 = floor(pos_texel);\n";
		code += "	vec4 mirror = vec4(fract(pos_texel00 * 0.5) * 2.0, 1.0, 1.0);\n";
		code += "	mirror.zw = vec2(1.0) - mirror.xy;\n\n";

		code += "	ivec3 index00UV = get_region((pos_texel00 + mirror.xy) * region_pixel_size);\n";
		code += "	ivec3 index01UV = get_region((pos_texel00 + mirror.xw) * region_pixel_size);\n";
		code += "	ivec3 index10UV = get_region((pos_texel00 + mirror.zy) * region_pixel_size);\n";
		code += "	ivec3 index11UV = get_region((pos_texel00 + mirror.zw) * region_pixel_size);\n\n";

		code += "	vec4 index00 = texelFetch(control_maps, index00UV, 0);\n";
		code += "	vec4 index01 = texelFetch(control_maps, index01UV, 0);\n";
		code += "	vec4 index10 = texelFetch(control_maps, index10UV, 0);\n";
		code += "	vec4 index11 = texelFetch(control_maps, index11UV, 0);\n\n";

		code += "	vec2 weights1 = clamp(pos_texel - pos_texel00, 0, 1);\n";
		code += "	weights1 = mix(weights1, vec2(1.0) - weights1, mirror.xy);\n";
		code += "	vec2 weights0 = vec2(1.0) - weights1;\n\n";

		code += "	float total_weight = 0.0;\n";
		code += "	vec4 in_normal = vec4(0.0);\n";
		code += "	vec3 color = vec3(0.0);\n\n";

		code += "	color = get_material(UV, index00, vec2(index00UV.xy), weights0.x * weights0.y, total_weight, in_normal).rgb;\n";
		code += "	color += get_material(UV, index01, vec2(index01UV.xy), weights0.x * weights1.y, total_weight, in_normal).rgb;\n";
		code += "	color += get_material(UV, index10, vec2(index10UV.xy), weights1.x * weights0.y, total_weight, in_normal).rgb;\n";
		code += "	color += get_material(UV, index11, vec2(index11UV.xy), weights1.x * weights1.y, total_weight, in_normal).rgb;\n";

		code += "	total_weight = 1.0 / total_weight;\n";
		code += "	in_normal *= total_weight;\n";
		code += "	color *= total_weight;\n\n";

		code += "	ALBEDO = color;\n";
		code += "	ROUGHNESS = in_normal.a;\n";
		code += "	NORMAL_MAP = in_normal.rgb;\n";
		code += "	NORMAL_MAP_DEPTH = 1.0;\n";

	} else {
		code += "	vec2 p = UV * 4.0;\n";
		code += "	vec2 ddx = dFdx(p);\n";
		code += "	vec2 ddy = dFdy(p);\n";
		code += "	vec2 w = max(abs(ddx), abs(ddy)) + 0.01;\n";
		code += "	vec2 i = 2.0 * (abs(fract((p - 0.5 * w) / 2.0) - 0.5) - abs(fract((p + 0.5 * w) / 2.0) - 0.5)) / w;\n";
		code += "	ALBEDO = vec3((0.5 - 0.5 * i.x * i.y) * 0.2 + 0.2);\n";
		code += "\n";
	}
	code += "}\n\n";

	return String(code);
}

void Terrain3DStorage::_bind_methods() {
	BIND_ENUM_CONSTANT(TYPE_HEIGHT);
	BIND_ENUM_CONSTANT(TYPE_CONTROL);
	BIND_ENUM_CONSTANT(TYPE_COLOR);
	BIND_ENUM_CONSTANT(TYPE_MAX);

	BIND_ENUM_CONSTANT(SIZE_64);
	BIND_ENUM_CONSTANT(SIZE_128);
	BIND_ENUM_CONSTANT(SIZE_256);
	BIND_ENUM_CONSTANT(SIZE_512);
	BIND_ENUM_CONSTANT(SIZE_1024);
	BIND_ENUM_CONSTANT(SIZE_2048);

	BIND_CONSTANT(REGION_MAP_SIZE);
	BIND_CONSTANT(TERRAIN_MAX_HEIGHT);

	ClassDB::bind_method(D_METHOD("set_region_size", "size"), &Terrain3DStorage::set_region_size);
	ClassDB::bind_method(D_METHOD("get_region_size"), &Terrain3DStorage::get_region_size);

	ClassDB::bind_method(D_METHOD("set_shader_override", "shader"), &Terrain3DStorage::set_shader_override);
	ClassDB::bind_method(D_METHOD("get_shader_override"), &Terrain3DStorage::get_shader_override);
	ClassDB::bind_method(D_METHOD("enable_shader_override", "enabled"), &Terrain3DStorage::enable_shader_override);
	ClassDB::bind_method(D_METHOD("is_shader_override_enabled"), &Terrain3DStorage::is_shader_override_enabled);

	ClassDB::bind_method(D_METHOD("get_region_blend_map"), &Terrain3DStorage::get_region_blend_map);
	ClassDB::bind_method(D_METHOD("set_noise_enabled", "enabled"), &Terrain3DStorage::set_noise_enabled);
	ClassDB::bind_method(D_METHOD("get_noise_enabled"), &Terrain3DStorage::get_noise_enabled);
	ClassDB::bind_method(D_METHOD("set_noise_scale", "scale"), &Terrain3DStorage::set_noise_scale);
	ClassDB::bind_method(D_METHOD("get_noise_scale"), &Terrain3DStorage::get_noise_scale);
	ClassDB::bind_method(D_METHOD("set_noise_height", "height"), &Terrain3DStorage::set_noise_height);
	ClassDB::bind_method(D_METHOD("get_noise_height"), &Terrain3DStorage::get_noise_height);
	ClassDB::bind_method(D_METHOD("set_noise_blend_near", "fade"), &Terrain3DStorage::set_noise_blend_near);
	ClassDB::bind_method(D_METHOD("get_noise_blend_near"), &Terrain3DStorage::get_noise_blend_near);
	ClassDB::bind_method(D_METHOD("set_noise_blend_far", "sharpness"), &Terrain3DStorage::set_noise_blend_far);
	ClassDB::bind_method(D_METHOD("get_noise_blend_far"), &Terrain3DStorage::get_noise_blend_far);

	ClassDB::bind_method(D_METHOD("set_surface", "material", "index"), &Terrain3DStorage::set_surface);
	ClassDB::bind_method(D_METHOD("get_surface", "index"), &Terrain3DStorage::get_surface);
	ClassDB::bind_method(D_METHOD("set_surfaces", "surfaces"), &Terrain3DStorage::set_surfaces);
	ClassDB::bind_method(D_METHOD("get_surfaces"), &Terrain3DStorage::get_surfaces);
	ClassDB::bind_method(D_METHOD("get_surface_count"), &Terrain3DStorage::get_surface_count);

	ClassDB::bind_method(D_METHOD("update_surface_textures"), &Terrain3DStorage::update_surface_textures);
	ClassDB::bind_method(D_METHOD("update_surface_values"), &Terrain3DStorage::update_surface_values);

	ClassDB::bind_method(D_METHOD("add_region", "global_position"), &Terrain3DStorage::add_region);
	ClassDB::bind_method(D_METHOD("remove_region", "global_position"), &Terrain3DStorage::remove_region);
	ClassDB::bind_method(D_METHOD("has_region", "global_position"), &Terrain3DStorage::has_region);
	ClassDB::bind_method(D_METHOD("get_region_index", "global_position"), &Terrain3DStorage::get_region_index);
	ClassDB::bind_method(D_METHOD("set_region_offsets", "offsets"), &Terrain3DStorage::set_region_offsets);
	ClassDB::bind_method(D_METHOD("get_region_offsets"), &Terrain3DStorage::get_region_offsets);
	ClassDB::bind_method(D_METHOD("get_region_count"), &Terrain3DStorage::get_region_count);

	ClassDB::bind_method(D_METHOD("set_map_region", "map_type", "region_index", "image"), &Terrain3DStorage::set_map_region);
	ClassDB::bind_method(D_METHOD("get_map_region", "map_type", "region_index"), &Terrain3DStorage::get_map_region);
	ClassDB::bind_method(D_METHOD("set_maps", "map_type", "maps"), &Terrain3DStorage::set_maps);
	ClassDB::bind_method(D_METHOD("get_maps", "map_type"), &Terrain3DStorage::get_maps);
	ClassDB::bind_method(D_METHOD("get_maps_copy", "map_type"), &Terrain3DStorage::get_maps_copy);
	ClassDB::bind_method(D_METHOD("set_height_maps", "maps"), &Terrain3DStorage::set_height_maps);
	ClassDB::bind_method(D_METHOD("get_height_maps"), &Terrain3DStorage::get_height_maps);
	ClassDB::bind_method(D_METHOD("set_control_maps", "maps"), &Terrain3DStorage::set_control_maps);
	ClassDB::bind_method(D_METHOD("get_control_maps"), &Terrain3DStorage::get_control_maps);
	ClassDB::bind_method(D_METHOD("set_color_maps", "maps"), &Terrain3DStorage::set_color_maps);
	ClassDB::bind_method(D_METHOD("get_color_maps"), &Terrain3DStorage::get_color_maps);
	ClassDB::bind_method(D_METHOD("force_update_maps", "map_type"), &Terrain3DStorage::force_update_maps, DEFVAL(TYPE_MAX));

	ADD_PROPERTY(PropertyInfo(Variant::INT, "region_size", PROPERTY_HINT_ENUM, "64:64, 128:128, 256:256, 512:512, 1024:1024, 2048:2048"), "set_region_size", "get_region_size");

	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "region_offsets", PROPERTY_HINT_ARRAY_TYPE, vformat("%tex_size/%tex_size:%tex_size", Variant::VECTOR2, PROPERTY_HINT_NONE)), "set_region_offsets", "get_region_offsets");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "height_maps", PROPERTY_HINT_ARRAY_TYPE, vformat("%tex_size/%tex_size:%tex_size", Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "Image")), "set_height_maps", "get_height_maps");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "control_maps", PROPERTY_HINT_ARRAY_TYPE, vformat("%tex_size/%tex_size:%tex_size", Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "Image")), "set_control_maps", "get_control_maps");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "color_maps", PROPERTY_HINT_ARRAY_TYPE, vformat("%tex_size/%tex_size:%tex_size", Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "Image")), "set_color_maps", "get_color_maps");

	ADD_GROUP("Noise", "noise_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "noise_enabled", PROPERTY_HINT_NONE), "set_noise_enabled", "get_noise_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "noise_scale", PROPERTY_HINT_RANGE, "0.0, 10.0"), "set_noise_scale", "get_noise_scale");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "noise_height", PROPERTY_HINT_RANGE, "0.0, 10.0"), "set_noise_height", "get_noise_height");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "noise_blend_near", PROPERTY_HINT_RANGE, "0.0, 1.0"), "set_noise_blend_near", "get_noise_blend_near");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "noise_blend_far", PROPERTY_HINT_RANGE, "0.0, 1.0"), "set_noise_blend_far", "get_noise_blend_far");

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shader_override_enabled", PROPERTY_HINT_NONE), "enable_shader_override", "is_shader_override_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "shader_override", PROPERTY_HINT_RESOURCE_TYPE, "Shader"), "set_shader_override", "get_shader_override");

	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "surfaces", PROPERTY_HINT_ARRAY_TYPE, vformat("%tex_size/%tex_size:%tex_size", Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "Terrain3DSurface")), "set_surfaces", "get_surfaces");
}
