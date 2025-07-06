#ifndef CURVATURE_EDITOR_PLUGIN_H
#define CURVATURE_EDITOR_PLUGIN_H

#include "../curvature.h"
#include "editor/inspector/editor_inspector.h"
#include "editor/inspector/editor_resource_preview.h"
#include "editor/plugins/editor_plugin.h"

class EditorSpinSlider;
class MenuButton;
class PopupMenu;

class BetterCurveEdit : public Control {
	GDCLASS(BetterCurveEdit, Control);

public:
	BetterCurveEdit();

	void set_snap_enabled(bool p_enabled);
	void set_snap_count(int p_snap_count);
	void use_preset(int p_preset_id);

	void set_curve(Ref<BetterCurve> p_curve);
	Ref<BetterCurve> get_curve();

	Size2 get_minimum_size() const override;

	enum PresetID {
		PRESET_CONSTANT = 0,
		PRESET_LINEAR,
		PRESET_EASE_IN,
		PRESET_EASE_OUT,
		PRESET_SMOOTHSTEP,
		PRESET_COUNT
	};

	enum TangentIndex {
		TANGENT_NONE = -1,
		TANGENT_LEFT = 0,
		TANGENT_RIGHT = 1
	};

protected:
	void _notification(int p_what);
	static void _bind_methods();

private:
	virtual void gui_input(const Ref<InputEvent> &p_event) override;
	void _curve_changed();

	int get_point_at(Vector2 p_pos) const;
	TangentIndex get_tangent_at(Vector2 p_pos) const;

	float get_offset_without_collision(int p_current_index, float p_offset, bool p_prioritize_right = true);

	void add_point(Vector2 p_pos);
	void remove_point(int p_index);
	void set_point_position(int p_index, Vector2 p_pos);

	void set_point_tangents(int p_index, float p_left, float p_right);
	void set_point_left_tangent(int p_index, float p_tangent);
	void set_point_right_tangent(int p_index, float p_tangent);
	void toggle_linear(int p_index, TangentIndex p_tangent = TANGENT_NONE);

	void update_view_transform();

	void set_selected_index(int p_index);
	void set_selected_tangent_index(TangentIndex p_tangent);

	Vector2 get_tangent_view_pos(int p_index, TangentIndex p_tangent) const;
	Vector2 get_view_pos(Vector2 p_world_pos) const;
	Vector2 get_world_pos(Vector2 p_view_pos) const;

	void _redraw();

private:
	const float ASPECT_RATIO = 6.f / 13.f;

	Transform2D _world_to_view;

	Ref<BetterCurve> curve;
	PopupMenu *_presets_menu = nullptr;

	int selected_index = -1;
	int hovered_index = -1;
	TangentIndex selected_tangent_index = TANGENT_NONE;
	TangentIndex hovered_tangent_index = TANGENT_NONE;

	// Make sure to use the scaled values below.
	const int BASE_POINT_RADIUS = 4;
	const int BASE_HOVER_RADIUS = 10;
	const int BASE_TANGENT_RADIUS = 3;
	const int BASE_TANGENT_HOVER_RADIUS = 8;
	const int BASE_TANGENT_LENGTH = 36;

	int point_radius = BASE_POINT_RADIUS;
	int hover_radius = BASE_HOVER_RADIUS;
	int tangent_radius = BASE_TANGENT_RADIUS;
	int tangent_hover_radius = BASE_TANGENT_HOVER_RADIUS;
	int tangent_length = BASE_TANGENT_LENGTH;

	enum GrabMode {
		GRAB_NONE,
		GRAB_ADD,
		GRAB_MOVE
	};
	GrabMode grabbing = GRAB_NONE;
	Vector2 initial_grab_pos;
	int initial_grab_index{ -1 };
	float initial_grab_left_tangent{ 0. };
	float initial_grab_right_tangent{ 0. };

	bool snap_enabled{ false };
	int snap_count{ 10 };
};

// BetterCurveEdit + toolbar
class BetterCurveEditor : public VBoxContainer {
	GDCLASS(BetterCurveEditor, VBoxContainer);

	// Make sure to use the scaled values below.
	const int BASE_SPACING = 4;
	int spacing = BASE_SPACING;

	Button *snap_button = nullptr;
	EditorSpinSlider *snap_count_edit = nullptr;
	MenuButton *presets_button = nullptr;
	BetterCurveEdit *curve_editor_rect = nullptr;

	void _set_snap_enabled(bool p_enabled);
	void _set_snap_count(int p_snap_count);
	void _on_preset_item_selected(int p_preset_id);

protected:
	void _notification(int p_what);

public:
	static const int DEFAULT_SNAP;
	void set_curve(const Ref<BetterCurve> &p_curve);

	BetterCurveEditor();
};

class EditorInspectorPluginBetterCurve : public EditorInspectorPlugin {
	GDCLASS(EditorInspectorPluginBetterCurve, EditorInspectorPlugin);

public:
	virtual bool can_handle(Object *p_object) override;
	virtual void parse_begin(Object *p_object) override;
};

class BetterCurveEditorPlugin : public EditorPlugin {
	GDCLASS(BetterCurveEditorPlugin, EditorPlugin);

public:
	BetterCurveEditorPlugin();

	virtual String get_plugin_name() const override { return "BetterCurve"; }
};

class BetterCurvePreviewGenerator : public EditorResourcePreviewGenerator {
	GDCLASS(BetterCurvePreviewGenerator, EditorResourcePreviewGenerator);

public:
	virtual bool handles(const String &p_type) const override;
	virtual Ref<Texture2D> generate(const Ref<Resource> &p_from, const Size2 &p_size, Dictionary &p_metadata) const override;
};

#endif // CURVE_EDITOR_PLUGIN_H
