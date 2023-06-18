//Copyright � 2023 Roope Palmroos, Cory Petkovsek, and Contributors. All rights reserved. See LICENSE.
#include <godot_cpp/core/class_db.hpp>

#include "terrain_editor.h"

void Terrain3DEditor::Brush::set_data(Dictionary p_data) {
	size = p_data["size"];
	index = p_data["index"];
	opacity = p_data["opacity"];
	gamma = p_data["gamma"];
	height = p_data["height"];
	jitter = p_data["jitter"];
	image = p_data["image"];
	img_size = Vector2(image->get_size());
	align_to_view = p_data["align_with_view"];
	auto_regions = p_data["automatic_regions"];
}

Terrain3DEditor::Terrain3DEditor() {
}

Terrain3DEditor::~Terrain3DEditor() {
}

void Terrain3DEditor::set_tool(Tool p_tool) {
	tool = p_tool;
}

Terrain3DEditor::Tool Terrain3DEditor::get_tool() const {
	return tool;
}

void Terrain3DEditor::set_operation(Operation p_operation) {
	operation = p_operation;
}

Terrain3DEditor::Operation Terrain3DEditor::get_operation() const {
	return operation;
}

void Terrain3DEditor::set_brush_data(Dictionary p_data) {
	if (p_data.is_empty()) {
		return;
	}
	brush.set_data(p_data);
}

void Terrain3DEditor::operate(Vector3 p_global_position, float p_camera_direction, bool p_continuous_operation) {
	if (operation_position == Vector3()) {
		operation_position = p_global_position;
	}
	operation_interval = p_global_position.distance_to(operation_position);
	operation_position = p_global_position;

	switch (tool) {
		case REGION:
			if (!p_continuous_operation) {
				_operate_region(p_global_position);
			}
			break;
		case HEIGHT:
			if (p_continuous_operation) {
				_operate_map(Terrain3DStorage::TYPE_HEIGHT, p_global_position, p_camera_direction);
			}
			break;
		case TEXTURE:
			if (p_continuous_operation) {
				_operate_map(Terrain3DStorage::TYPE_CONTROL, p_global_position, p_camera_direction);
			}
			break;
		case COLOR:
			if (p_continuous_operation) {
				_operate_map(Terrain3DStorage::TYPE_COLOR, p_global_position, p_camera_direction);
			}
			break;
		default:
			break;
	}
}

void Terrain3DEditor::set_terrain(Terrain3D *p_terrain) {
	terrain = p_terrain;
}

Terrain3D *Terrain3DEditor::get_terrain() const {
	return terrain;
}

void Terrain3DEditor::_operate_region(Vector3 p_global_position) {
	bool has_region = terrain->get_storage()->has_region(p_global_position);

	if (operation == ADD) {
		if (!has_region) {
			terrain->get_storage()->add_region(p_global_position);
		}
	}
	if (operation == SUBTRACT) {
		if (has_region) {
			terrain->get_storage()->remove_region(p_global_position);
		}
	}
}

void Terrain3DEditor::_operate_map(Terrain3DStorage::MapType p_map_type, Vector3 p_global_position, float p_camera_direction) {
	int region_size = terrain->get_storage()->get_region_size();
	int region_index = terrain->get_storage()->get_region_index(p_global_position);

	if (region_index == -1) {
		return;
	}

	Ref<Image> map = terrain->get_storage()->get_map_region(p_map_type, region_index);
	int s = brush.get_size();
	int index = brush.get_index();
	Vector2 is = brush.get_image_size();
	float o = brush.get_opacity();
	float h = brush.get_height() / float(Terrain3DStorage::TERRAIN_MAX_HEIGHT);
	float g = brush.get_gamma();
	float randf = UtilityFunctions::randf();
	float rot = randf * Math_PI * brush.get_jitter();

	if (brush.is_aligned_to_view()) {
		rot += p_camera_direction;
	}
	for (int x = 0; x < s; x++) {
		for (int y = 0; y < s; y++) {
			Vector2i brush_offset = Vector2i(x, y) - (Vector2i(s, s) / 2);
			Vector3 brush_global_position = Vector3(p_global_position.x + float(brush_offset.x), p_global_position.y, p_global_position.z + float(brush_offset.y));

			int new_region_index = terrain->get_storage()->get_region_index(brush_global_position);

			if (new_region_index == -1) {
				if (!brush.auto_regions_enabled()) {
					continue;
				}
				Error err = terrain->get_storage()->add_region(brush_global_position);
				if (err) {
					continue;
				}
				new_region_index = terrain->get_storage()->get_region_index(brush_global_position);
			}

			if (new_region_index != region_index) {
				region_index = new_region_index;
				map = terrain->get_storage()->get_map_region(p_map_type, region_index);
			}

			Vector2 uv_position = _get_uv_position(brush_global_position, region_size);
			Vector2i map_pixel_position = Vector2i(uv_position * region_size);

			if (_is_in_bounds(map_pixel_position, Vector2i(region_size, region_size))) {
				Vector2 brush_uv = Vector2(x, y) / float(s);
				Vector2i brush_pixel_position = Vector2i(_rotate_uv(brush_uv, rot) * is);

				if (!_is_in_bounds(brush_pixel_position, Vector2i(is))) {
					continue;
				}

				float alpha = float(Math::pow(double(brush.get_alpha(brush_pixel_position)), double(g)));
				Color src = map->get_pixelv(map_pixel_position);
				Color dest = src;

				if (p_map_type == Terrain3DStorage::TYPE_HEIGHT) {
					float srcf = src.r;
					float destf = dest.r;

					switch (operation) {
						case Terrain3DEditor::ADD:
							destf = srcf + (h * alpha * o);
							break;
						case Terrain3DEditor::SUBTRACT:
							destf = srcf - (h * alpha * o);
							break;
						case Terrain3DEditor::MULTIPLY:
							destf = srcf * (alpha * h * o + 1.0f);
							break;
						case Terrain3DEditor::REPLACE:
							destf = Math::lerp(srcf, h, alpha);
							break;
						default:
							break;
					}
					dest = Color(CLAMP(destf, 0.0f, 1.0f), 0.0f, 0.0f, 1.0f);

				} else if (p_map_type == Terrain3DStorage::TYPE_CONTROL) {
					float alpha_clip = (alpha < 0.1f) ? 0.0f : 1.0f;
					int index_base = int(src.r * 255.0f);
					int index_overlay = int(src.g * 255.0f);
					int dest_index = 0;

					switch (operation) {
						case Terrain3DEditor::ADD:
							dest_index = int(Math::lerp(index_overlay, index, alpha_clip));

							if (dest_index == index_base) {
								dest.b = Math::lerp(src.b, 0.0f, alpha_clip);
							} else {
								dest.g = float(dest_index) / 255.0f;
								dest.b = Math::lerp(src.b, CLAMP(src.b + o * alpha, 0.0f, 1.0f), alpha_clip);
							}
							break;
						case Terrain3DEditor::REPLACE:
							dest_index = int(Math::lerp(index_base, index, alpha_clip));

							dest.r = float(dest_index) / 255.0f;
							dest.b = Math::lerp(src.b, 0.0f, alpha_clip);

							break;
						default:
							break;
					}
				}

				map->set_pixelv(map_pixel_position, dest);
			}
		}
	}
	terrain->get_storage()->force_update_maps(p_map_type);
}

bool Terrain3DEditor::_is_in_bounds(Vector2i p_position, Vector2i p_max_position) {
	bool more_than_min = p_position.x >= 0 && p_position.y >= 0;
	bool less_than_max = p_position.x < p_max_position.x && p_position.y < p_max_position.y;
	return more_than_min && less_than_max;
}

Vector2 Terrain3DEditor::_get_uv_position(Vector3 p_global_position, int p_region_size) {
	Vector2 global_position_2d = Vector2(p_global_position.x, p_global_position.z);
	Vector2 region_position = global_position_2d / float(p_region_size) + Vector2(0.5, 0.5);
	region_position = region_position.floor();
	Vector2 uv_position = ((global_position_2d / float(p_region_size)) + Vector2(0.5, 0.5)) - region_position;

	return uv_position;
}

Vector2 Terrain3DEditor::_rotate_uv(Vector2 p_uv, float p_angle) {
	Vector2 rotation_offset = Vector2(0.5, 0.5);
	p_uv = (p_uv - rotation_offset).rotated(p_angle) + rotation_offset;
	return p_uv.clamp(Vector2(0, 0), Vector2(1, 1));
}

void Terrain3DEditor::_bind_methods() {
	BIND_ENUM_CONSTANT(ADD);
	BIND_ENUM_CONSTANT(SUBTRACT);
	BIND_ENUM_CONSTANT(MULTIPLY);
	BIND_ENUM_CONSTANT(REPLACE);

	BIND_ENUM_CONSTANT(REGION);
	BIND_ENUM_CONSTANT(HEIGHT);
	BIND_ENUM_CONSTANT(TEXTURE);
	BIND_ENUM_CONSTANT(COLOR);

	ClassDB::bind_method(D_METHOD("operate", "position", "camera_direction", "continuous_operation"), &Terrain3DEditor::operate);
	ClassDB::bind_method(D_METHOD("set_brush_data", "data"), &Terrain3DEditor::set_brush_data);
	ClassDB::bind_method(D_METHOD("set_tool", "tool"), &Terrain3DEditor::set_tool);
	ClassDB::bind_method(D_METHOD("get_tool"), &Terrain3DEditor::get_tool);
	ClassDB::bind_method(D_METHOD("set_operation", "operation"), &Terrain3DEditor::set_operation);
	ClassDB::bind_method(D_METHOD("get_operation"), &Terrain3DEditor::get_operation);

	ClassDB::bind_method(D_METHOD("set_terrain", "terrain"), &Terrain3DEditor::set_terrain);
}
