#ifndef CURVATURE_RESOURCE_H
#define CURVATURE_RESOURCE_H

#include "core/io/resource.h"
#include "core/object/object.h"
#include "core/os/mutex.h"
#include "core/os/thread.h"

#include <mutex>
#include <shared_mutex>
#include <thread>

/*
class Curvature : public Resource {
	GDCLASS(Curvature, Resource);
	OBJ_SAVE_TYPE(Curvature);

public:
	struct ControlPoint {
		Vector2 position;
		Vector2 left_handle;
		Vector2 right_handle;

		ControlPoint() {}

		ControlPoint(
				const Vector2 &p,
				const Vector2 &l = { 0., 0. },
				const Vector2 &r = { 0., 0. }) :
				position(p), left_handle(l), right_handle(r) {}
	};

public:
	Curvature();
	virtual ~Curvature();

	void clear();

	size_t get_point_count() const;
	void set_point_count(size_t c);

	int add_point(Vector2 p,
			Vector2 l = { 0., 0. },
			Vector2 r = { 0., 0. });
	void remove_point(size_t idx);

	void set_point_position(size_t i, Vector2 p);
	Vector2 get_point_position(size_t i) const;

	Vector2 set_point_left_handle(size_t i, Vector2 l);
	Vector2 get_point_left_handle(size_t i) const;

	Vector2 set_point_right_handle(size_t i, Vector2 r);
	Vector2 get_point_right_handle(size_t i) const;

	void set_y_min_value(real_t y);
	real_t get_y_min_value() const;

	void set_y_max_value(real_t y);
	real_t get_y_max_value() const;

	void set_x_min_value(real_t x);
	real_t get_x_min_value() const;

	void set_x_max_value(real_t x);
	real_t get_x_max_value() const;

	void set_cache_resolution(size_t c);
	size_t get_cache_resolution() const;

	real_t get_value(real_t x);

	real_t get_cached_value(real_t x);

	Array get_data() const;
	void set_data(Array input);

	bool _set(const StringName &p_name, const Variant &p_value);
	bool _get(const StringName &p_name, Variant &r_ret) const;
	void _get_property_list(List<PropertyInfo> *p_list) const;

public:
	friend void compute_curvature_cache(void *);

protected:
	static void _bind_methods();

private:
	void queue_update();
	static void compute_cache(void *);

private:
	std::vector<ControlPoint> control_points;
	std::vector<real_t> cache;
	real_t x_min{ 0. };
	real_t x_max{ 1. };
	real_t y_min{ 0. };
	real_t y_max{ 1. };
	size_t resolution{ 100 };
	Thread update_thread;
	Mutex queue_mutex;
	std::shared_mutex shared_mutex;
};
*/

// y(x) curve
class BetterCurve : public Resource {
	GDCLASS(BetterCurve, Resource);

public:
	static const int MIN_X = 0.f;
	static const int MAX_X = 1.f;

	static const char *SIGNAL_RANGE_CHANGED;
	static const char *SIGNAL_BAKED;

	enum TangentMode {
		TANGENT_FREE = 0,
		TANGENT_LINEAR,
		TANGENT_MODE_COUNT
	};

	struct Point {
		Vector2 position;
		real_t left_tangent = 0.0;
		real_t right_tangent = 0.0;
		TangentMode left_mode = TANGENT_FREE;
		TangentMode right_mode = TANGENT_FREE;

		Point() {
		}

		Point(const Vector2 &p_position,
				real_t p_left = 0.0,
				real_t p_right = 0.0,
				TangentMode p_left_mode = TANGENT_FREE,
				TangentMode p_right_mode = TANGENT_FREE) {
			position = p_position;
			left_tangent = p_left;
			right_tangent = p_right;
			left_mode = p_left_mode;
			right_mode = p_right_mode;
		}
	};

	BetterCurve();

	~BetterCurve();

	int get_point_count() const { return _points.size(); }

	void set_point_count(int p_count);

	int add_point(Vector2 p_position,
			real_t left_tangent = 0,
			real_t right_tangent = 0,
			TangentMode left_mode = TANGENT_FREE,
			TangentMode right_mode = TANGENT_FREE);
	int add_point_no_update(Vector2 p_position,
			real_t left_tangent = 0,
			real_t right_tangent = 0,
			TangentMode left_mode = TANGENT_FREE,
			TangentMode right_mode = TANGENT_FREE);
	void remove_point(int p_index);
	void clear_points();

	int get_index(real_t p_offset) const;

	void set_point_value(int p_index, real_t p_position);
	int set_point_offset(int p_index, real_t p_offset);
	Vector2 get_point_position(int p_index) const;

	Point get_point(int p_index) const;

	real_t get_min_value() const { return _min_value; }
	void set_min_value(real_t p_min);

	real_t get_max_value() const { return _max_value; }
	void set_max_value(real_t p_max);

	real_t get_range() const { return _max_value - _min_value; }

	real_t sample(real_t p_offset) const;
	real_t sample_local_nocheck(int p_index, real_t p_local_offset) const;

	void clean_dupes();

	void set_point_left_tangent(int p_index, real_t p_tangent);
	void set_point_right_tangent(int p_index, real_t p_tangent);
	void set_point_left_mode(int p_index, TangentMode p_mode);
	void set_point_right_mode(int p_index, TangentMode p_mode);

	real_t get_point_left_tangent(int p_index) const;
	real_t get_point_right_tangent(int p_index) const;
	TangentMode get_point_left_mode(int p_index) const;
	TangentMode get_point_right_mode(int p_index) const;

	void update_auto_tangents(int i);

	Array get_data() const;
	void set_data(Array input);

	void bake();
	int get_bake_resolution() const { return _bake_resolution; }
	void set_bake_resolution(int p_resolution);
	real_t sample_baked(real_t p_offset) const;

	void ensure_default_setup(real_t p_min, real_t p_max);

	bool _set(const StringName &p_name, const Variant &p_value);
	bool _get(const StringName &p_name, Variant &r_ret) const;
	void _get_property_list(List<PropertyInfo> *p_list) const;

protected:
	static void _bind_methods();

private:
	void mark_dirty();
	int _add_point(Vector2 p_position,
			real_t left_tangent = 0,
			real_t right_tangent = 0,
			TangentMode left_mode = TANGENT_FREE,
			TangentMode right_mode = TANGENT_FREE);
	void _remove_point(int p_index);

	void _queue_update();

	static void _update_bake(void *);
	static real_t _sample(real_t offset, const Vector<Point> &points, const int idx);
	static real_t _sample_local_nocheck(int idx, real_t local_offset, const Vector<Point> &points);

	Vector<Point> _points;
	bool _baked_cache_dirty = false;
	Vector<real_t> _baked_cache;
	int _bake_resolution = 100;
	real_t _min_value = 0.0;
	real_t _max_value = 1.0;
	int _minmax_set_once = 0b00; // Encodes whether min and max have been set a first time, first bit for min and second for max.

	bool _update_queued{ false };

	std::thread _update_thread;
	std::mutex _update_queue_mutex;
	std::mutex _update_param_mutex;
	std::shared_mutex _getter_mutex;
};

VARIANT_ENUM_CAST(BetterCurve::TangentMode)

#endif