#include "curvature.h"

#include "core/math/math_funcs.h"
#include <chrono>
#include <mutex>
#include <shared_mutex>

const char *BetterCurve::SIGNAL_RANGE_CHANGED = "range_changed";
const char *BetterCurve::SIGNAL_BAKED = "baked";

BetterCurve::BetterCurve() {
}

BetterCurve::~BetterCurve() {
	if (_update_thread.joinable()) {
		_update_thread.join();
	}
}

void BetterCurve::set_point_count(int p_count) {
	ERR_FAIL_COND(p_count < 0);
	int old_size = _points.size();
	if (old_size == p_count) {
		return;
	}

	{
		std::unique_lock<std::mutex> lock(_update_param_mutex);
		if (old_size > p_count) {
			_points.resize(p_count);
		} else {
			for (int i = p_count - old_size; i > 0; i--) {
				_add_point(Vector2());
			}
		}
	}

	_queue_update();
	notify_property_list_changed();
}

int BetterCurve::_add_point(Vector2 p_position, real_t p_left_tangent, real_t p_right_tangent, TangentMode p_left_mode, TangentMode p_right_mode) {
	int ret = -1;
	{
		std::unique_lock<std::mutex> lock(_update_param_mutex);

		// Add a point and preserve order

		// Curve bounds is in 0..1
		if (p_position.x > MAX_X) {
			p_position.x = MAX_X;
		} else if (p_position.x < MIN_X) {
			p_position.x = MIN_X;
		}

		if (_points.size() == 0) {
			_points.push_back(Point(p_position, p_left_tangent, p_right_tangent, p_left_mode, p_right_mode));
			ret = 0;

		} else if (_points.size() == 1) {
			// TODO Is the `else` able to handle this block already?

			real_t diff = p_position.x - _points[0].position.x;

			if (diff > 0) {
				_points.push_back(Point(p_position, p_left_tangent, p_right_tangent, p_left_mode, p_right_mode));
				ret = 1;
			} else {
				_points.insert(0, Point(p_position, p_left_tangent, p_right_tangent, p_left_mode, p_right_mode));
				ret = 0;
			}

		} else {
			int i = get_index(p_position.x);

			if (i == 0 && p_position.x < _points[0].position.x) {
				// Insert before anything else
				_points.insert(0, Point(p_position, p_left_tangent, p_right_tangent, p_left_mode, p_right_mode));
				ret = 0;
			} else {
				// Insert between i and i+1
				++i;
				_points.insert(i, Point(p_position, p_left_tangent, p_right_tangent, p_left_mode, p_right_mode));
				ret = i;
			}
		}

		update_auto_tangents(ret);
	}

	_queue_update();

	return ret;
}

int BetterCurve::add_point(Vector2 p_position, real_t p_left_tangent, real_t p_right_tangent, TangentMode p_left_mode, TangentMode p_right_mode) {
	int ret = _add_point(p_position, p_left_tangent, p_right_tangent, p_left_mode, p_right_mode);
	notify_property_list_changed();

	return ret;
}

// TODO: Needed to make the curve editor function properly until https://github.com/godotengine/godot/issues/76985 is fixed.
int BetterCurve::add_point_no_update(Vector2 p_position, real_t p_left_tangent, real_t p_right_tangent, TangentMode p_left_mode, TangentMode p_right_mode) {
	int ret = _add_point(p_position, p_left_tangent, p_right_tangent, p_left_mode, p_right_mode);

	return ret;
}

int BetterCurve::get_index(real_t p_offset) const {
	// Lower-bound float binary search

	int imin = 0;
	int imax = _points.size() - 1;

	while (imax - imin > 1) {
		int m = (imin + imax) / 2;

		real_t a = _points[m].position.x;
		real_t b = _points[m + 1].position.x;

		if (a < p_offset && b < p_offset) {
			imin = m;

		} else if (a > p_offset) {
			imax = m;

		} else {
			return m;
		}
	}

	// Will happen if the offset is out of bounds
	if (p_offset > _points[imax].position.x) {
		return imax;
	}
	return imin;
}

void BetterCurve::clean_dupes() {
	bool dirty = false;
	{
		std::unique_lock<std::mutex> lock(_update_param_mutex);

		for (int i = 1; i < _points.size(); ++i) {
			real_t diff = _points[i - 1].position.x - _points[i].position.x;
			if (diff <= CMP_EPSILON) {
				_points.remove_at(i);
				--i;
				dirty = true;
			}
		}
	}

	if (dirty) {
		_queue_update();
	}
}

void BetterCurve::set_point_left_tangent(int p_index, real_t p_tangent) {
	{
		std::unique_lock<std::mutex> lock(_update_param_mutex);

		ERR_FAIL_INDEX(p_index, _points.size());
		_points.write[p_index].left_tangent = p_tangent;
		_points.write[p_index].left_mode = TANGENT_FREE;
	}
	_queue_update();
}

void BetterCurve::set_point_right_tangent(int p_index, real_t p_tangent) {
	{
		std::unique_lock<std::mutex> lock(_update_param_mutex);

		ERR_FAIL_INDEX(p_index, _points.size());
		_points.write[p_index].right_tangent = p_tangent;
		_points.write[p_index].right_mode = TANGENT_FREE;
	}
	_queue_update();
}

void BetterCurve::set_point_left_mode(int p_index, TangentMode p_mode) {
	{
		std::unique_lock<std::mutex> lock(_update_param_mutex);

		ERR_FAIL_INDEX(p_index, _points.size());
		_points.write[p_index].left_mode = p_mode;
		if (p_index > 0) {
			if (p_mode == TANGENT_LINEAR) {
				Vector2 v = (_points[p_index - 1].position - _points[p_index].position).normalized();
				_points.write[p_index].left_tangent = v.y / v.x;
			}
		}
	}
	_queue_update();
}

void BetterCurve::set_point_right_mode(int p_index, TangentMode p_mode) {
	{
		std::unique_lock<std::mutex> lock(_update_param_mutex);

		ERR_FAIL_INDEX(p_index, _points.size());
		_points.write[p_index].right_mode = p_mode;
		if (p_index + 1 < _points.size()) {
			if (p_mode == TANGENT_LINEAR) {
				Vector2 v = (_points[p_index + 1].position - _points[p_index].position).normalized();
				_points.write[p_index].right_tangent = v.y / v.x;
			}
		}
	}
	_queue_update();
}

real_t BetterCurve::get_point_left_tangent(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, _points.size(), 0);
	return _points[p_index].left_tangent;
}

real_t BetterCurve::get_point_right_tangent(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, _points.size(), 0);
	return _points[p_index].right_tangent;
}

BetterCurve::TangentMode BetterCurve::get_point_left_mode(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, _points.size(), TANGENT_FREE);
	return _points[p_index].left_mode;
}

BetterCurve::TangentMode BetterCurve::get_point_right_mode(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, _points.size(), TANGENT_FREE);
	return _points[p_index].right_mode;
}

void BetterCurve::_remove_point(int p_index) {
	{
		std::unique_lock<std::mutex> lock(_update_param_mutex);

		ERR_FAIL_INDEX(p_index, _points.size());
		_points.remove_at(p_index);
	}
	_queue_update();
}

void BetterCurve::remove_point(int p_index) {
	_remove_point(p_index);
	notify_property_list_changed();
}

void BetterCurve::clear_points() {
	if (_points.is_empty()) {
		return;
	}
	{
		std::unique_lock<std::mutex> lock(_update_param_mutex);

		_points.clear();
	}
	_queue_update();
	notify_property_list_changed();
}

void BetterCurve::set_point_value(int p_index, real_t p_position) {
	{
		std::unique_lock<std::mutex> lock(_update_param_mutex);

		ERR_FAIL_INDEX(p_index, _points.size());
		_points.write[p_index].position.y = p_position;
		update_auto_tangents(p_index);
	}
	_queue_update();
}

int BetterCurve::set_point_offset(int p_index, real_t p_offset) {
	ERR_FAIL_INDEX_V(p_index, _points.size(), -1);
	Point p = _points[p_index];
	_remove_point(p_index);
	int i = _add_point(Vector2(p_offset, p.position.y));
	_points.write[i].left_tangent = p.left_tangent;
	_points.write[i].right_tangent = p.right_tangent;
	_points.write[i].left_mode = p.left_mode;
	_points.write[i].right_mode = p.right_mode;
	if (p_index != i) {
		update_auto_tangents(p_index);
	}
	update_auto_tangents(i);
	return i;
}

Vector2 BetterCurve::get_point_position(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, _points.size(), Vector2(0, 0));
	return _points[p_index].position;
}

BetterCurve::Point BetterCurve::get_point(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, _points.size(), Point());
	return _points[p_index];
}

void BetterCurve::update_auto_tangents(int p_index) {
	Point &p = _points.write[p_index];

	if (p_index > 0) {
		if (p.left_mode == TANGENT_LINEAR) {
			Vector2 v = (_points[p_index - 1].position - p.position).normalized();
			p.left_tangent = v.y / v.x;
		}
		if (_points[p_index - 1].right_mode == TANGENT_LINEAR) {
			Vector2 v = (_points[p_index - 1].position - p.position).normalized();
			_points.write[p_index - 1].right_tangent = v.y / v.x;
		}
	}

	if (p_index + 1 < _points.size()) {
		if (p.right_mode == TANGENT_LINEAR) {
			Vector2 v = (_points[p_index + 1].position - p.position).normalized();
			p.right_tangent = v.y / v.x;
		}
		if (_points[p_index + 1].left_mode == TANGENT_LINEAR) {
			Vector2 v = (_points[p_index + 1].position - p.position).normalized();
			_points.write[p_index + 1].left_tangent = v.y / v.x;
		}
	}
}

#define MIN_Y_RANGE 0.01

void BetterCurve::set_min_value(real_t p_min) {
	if (_minmax_set_once & 0b11 && p_min > _max_value - MIN_Y_RANGE) {
		_min_value = _max_value - MIN_Y_RANGE;
	} else {
		_minmax_set_once |= 0b10; // first bit is "min set"
		_min_value = p_min;
	}
	// Note: min and max are indicative values,
	// it's still possible that existing points are out of range at this point.
	emit_signal(SNAME(SIGNAL_RANGE_CHANGED));
}

void BetterCurve::set_max_value(real_t p_max) {
	if (_minmax_set_once & 0b11 && p_max < _min_value + MIN_Y_RANGE) {
		_max_value = _min_value + MIN_Y_RANGE;
	} else {
		_minmax_set_once |= 0b01; // second bit is "max set"
		_max_value = p_max;
	}
	emit_signal(SNAME(SIGNAL_RANGE_CHANGED));
}

real_t BetterCurve::sample(real_t p_offset) const {
	return sample_baked(p_offset);
}

Array BetterCurve::get_data() const {
	Array output;
	const unsigned int ELEMS = 5;
	output.resize(_points.size() * ELEMS);

	for (int j = 0; j < _points.size(); ++j) {
		const Point p = _points[j];
		int i = j * ELEMS;

		output[i] = p.position;
		output[i + 1] = p.left_tangent;
		output[i + 2] = p.right_tangent;
		output[i + 3] = p.left_mode;
		output[i + 4] = p.right_mode;
	}

	return output;
}

void BetterCurve::set_data(const Array p_input) {
	int old_size = _points.size();
	int new_size = 0;
	{
		std::unique_lock<std::mutex> lock(_update_param_mutex);

		const unsigned int ELEMS = 5;
		ERR_FAIL_COND(p_input.size() % ELEMS != 0);

		// Validate input
		for (int i = 0; i < p_input.size(); i += ELEMS) {
			ERR_FAIL_COND(p_input[i].get_type() != Variant::VECTOR2);
			ERR_FAIL_COND(!p_input[i + 1].is_num());
			ERR_FAIL_COND(p_input[i + 2].get_type() != Variant::FLOAT);

			ERR_FAIL_COND(p_input[i + 3].get_type() != Variant::INT);
			int left_mode = p_input[i + 3];
			ERR_FAIL_COND(left_mode < 0 || left_mode >= TANGENT_MODE_COUNT);

			ERR_FAIL_COND(p_input[i + 4].get_type() != Variant::INT);
			int right_mode = p_input[i + 4];
			ERR_FAIL_COND(right_mode < 0 || right_mode >= TANGENT_MODE_COUNT);
		}
		new_size = p_input.size() / ELEMS;
		if (old_size != new_size) {
			_points.resize(new_size);
		}

		for (int j = 0; j < _points.size(); ++j) {
			Point &p = _points.write[j];
			int i = j * ELEMS;

			p.position = p_input[i];
			p.left_tangent = p_input[i + 1];
			p.right_tangent = p_input[i + 2];
			int left_mode = p_input[i + 3];
			int right_mode = p_input[i + 4];
			p.left_mode = (TangentMode)left_mode;
			p.right_mode = (TangentMode)right_mode;
		}
	}

	_queue_update();
	if (old_size != new_size) {
		notify_property_list_changed();
	}
}

void BetterCurve::bake() {
	_baked_cache.clear();

	_baked_cache.resize(_bake_resolution);

	for (int i = 1; i < _bake_resolution - 1; ++i) {
		real_t x = i / static_cast<real_t>(_bake_resolution - 1);
		real_t y = sample(x);
		_baked_cache.write[i] = y;
	}

	if (_points.size() != 0) {
		_baked_cache.write[0] = _points[0].position.y;
		_baked_cache.write[_baked_cache.size() - 1] = _points[_points.size() - 1].position.y;
	}

	_baked_cache_dirty = false;
}

void BetterCurve::set_bake_resolution(int p_resolution) {
	ERR_FAIL_COND(p_resolution < 1);
	ERR_FAIL_COND(p_resolution > 1000);
	_bake_resolution = p_resolution;
	_baked_cache_dirty = true;
}

real_t BetterCurve::sample_baked(real_t p_offset) const {
	std::shared_lock<std::shared_mutex> lock(*(const_cast<std::shared_mutex *>(&_getter_mutex)));
	// Special cases if the cache is too small
	if (_baked_cache.size() == 0) {
		if (_points.size() == 0) {
			return 0;
		}
		return _points[0].position.y;
	} else if (_baked_cache.size() == 1) {
		return _baked_cache[0];
	}

	// Get interpolation index
	real_t fi = p_offset * (_baked_cache.size() - 1);
	int i = Math::floor(fi);
	if (i < 0) {
		i = 0;
		fi = 0;
	} else if (i >= _baked_cache.size()) {
		i = _baked_cache.size() - 1;
		fi = 0;
	}

	// Sample
	if (i + 1 < _baked_cache.size()) {
		real_t t = fi - i;
		return Math::lerp(_baked_cache[i], _baked_cache[i + 1], t);
	} else {
		return _baked_cache[_baked_cache.size() - 1];
	}
}

void BetterCurve::ensure_default_setup(real_t p_min, real_t p_max) {
	if (_points.size() == 0 && _min_value == 0 && _max_value == 1) {
		add_point(Vector2(0, 1));
		add_point(Vector2(1, 1));
		set_min_value(p_min);
		set_max_value(p_max);
	}
}

bool BetterCurve::_set(const StringName &p_name, const Variant &p_value) {
	Vector<String> components = String(p_name).split("/", true, 2);
	if (components.size() >= 2 && components[0].begins_with("point_") && components[0].trim_prefix("point_").is_valid_int()) {
		int point_index = components[0].trim_prefix("point_").to_int();
		const String &property = components[1];
		if (property == "position") {
			Vector2 position = p_value.operator Vector2();
			set_point_offset(point_index, position.x);
			set_point_value(point_index, position.y);
			return true;
		} else if (property == "left_tangent") {
			set_point_left_tangent(point_index, p_value);
			return true;
		} else if (property == "left_mode") {
			int mode = p_value;
			set_point_left_mode(point_index, (TangentMode)mode);
			return true;
		} else if (property == "right_tangent") {
			set_point_right_tangent(point_index, p_value);
			return true;
		} else if (property == "right_mode") {
			int mode = p_value;
			set_point_right_mode(point_index, (TangentMode)mode);
			return true;
		}
	}
	return false;
}

bool BetterCurve::_get(const StringName &p_name, Variant &r_ret) const {
	Vector<String> components = String(p_name).split("/", true, 2);
	if (components.size() >= 2 && components[0].begins_with("point_") && components[0].trim_prefix("point_").is_valid_int()) {
		int point_index = components[0].trim_prefix("point_").to_int();
		const String &property = components[1];
		if (property == "position") {
			r_ret = get_point_position(point_index);
			return true;
		} else if (property == "left_tangent") {
			r_ret = get_point_left_tangent(point_index);
			return true;
		} else if (property == "left_mode") {
			r_ret = get_point_left_mode(point_index);
			return true;
		} else if (property == "right_tangent") {
			r_ret = get_point_right_tangent(point_index);
			return true;
		} else if (property == "right_mode") {
			r_ret = get_point_right_mode(point_index);
			return true;
		}
	}
	return false;
}

void BetterCurve::_get_property_list(List<PropertyInfo> *p_list) const {
	for (int i = 0; i < _points.size(); i++) {
		PropertyInfo pi = PropertyInfo(Variant::VECTOR2, vformat("point_%d/position", i));
		pi.usage &= ~PROPERTY_USAGE_STORAGE;
		p_list->push_back(pi);

		if (i != 0) {
			pi = PropertyInfo(Variant::FLOAT, vformat("point_%d/left_tangent", i));
			pi.usage &= ~PROPERTY_USAGE_STORAGE;
			p_list->push_back(pi);

			pi = PropertyInfo(Variant::INT, vformat("point_%d/left_mode", i), PROPERTY_HINT_ENUM, "Free,Linear");
			pi.usage &= ~PROPERTY_USAGE_STORAGE;
			p_list->push_back(pi);
		}

		if (i != _points.size() - 1) {
			pi = PropertyInfo(Variant::FLOAT, vformat("point_%d/right_tangent", i));
			pi.usage &= ~PROPERTY_USAGE_STORAGE;
			p_list->push_back(pi);

			pi = PropertyInfo(Variant::INT, vformat("point_%d/right_mode", i), PROPERTY_HINT_ENUM, "Free,Linear");
			pi.usage &= ~PROPERTY_USAGE_STORAGE;
			p_list->push_back(pi);
		}
	}
}

void BetterCurve::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_point_count"), &BetterCurve::get_point_count);
	ClassDB::bind_method(D_METHOD("set_point_count", "count"), &BetterCurve::set_point_count);
	ClassDB::bind_method(D_METHOD("add_point", "position", "left_tangent", "right_tangent", "left_mode", "right_mode"), &BetterCurve::add_point, DEFVAL(0), DEFVAL(0), DEFVAL(TANGENT_FREE), DEFVAL(TANGENT_FREE));
	ClassDB::bind_method(D_METHOD("remove_point", "index"), &BetterCurve::remove_point);
	ClassDB::bind_method(D_METHOD("clear_points"), &BetterCurve::clear_points);
	ClassDB::bind_method(D_METHOD("get_point_position", "index"), &BetterCurve::get_point_position);
	ClassDB::bind_method(D_METHOD("set_point_value", "index", "y"), &BetterCurve::set_point_value);
	ClassDB::bind_method(D_METHOD("set_point_offset", "index", "offset"), &BetterCurve::set_point_offset);
	ClassDB::bind_method(D_METHOD("sample", "offset"), &BetterCurve::sample);
	ClassDB::bind_method(D_METHOD("sample_baked", "offset"), &BetterCurve::sample_baked);
	ClassDB::bind_method(D_METHOD("get_point_left_tangent", "index"), &BetterCurve::get_point_left_tangent);
	ClassDB::bind_method(D_METHOD("get_point_right_tangent", "index"), &BetterCurve::get_point_right_tangent);
	ClassDB::bind_method(D_METHOD("get_point_left_mode", "index"), &BetterCurve::get_point_left_mode);
	ClassDB::bind_method(D_METHOD("get_point_right_mode", "index"), &BetterCurve::get_point_right_mode);
	ClassDB::bind_method(D_METHOD("set_point_left_tangent", "index", "tangent"), &BetterCurve::set_point_left_tangent);
	ClassDB::bind_method(D_METHOD("set_point_right_tangent", "index", "tangent"), &BetterCurve::set_point_right_tangent);
	ClassDB::bind_method(D_METHOD("set_point_left_mode", "index", "mode"), &BetterCurve::set_point_left_mode);
	ClassDB::bind_method(D_METHOD("set_point_right_mode", "index", "mode"), &BetterCurve::set_point_right_mode);
	ClassDB::bind_method(D_METHOD("get_min_value"), &BetterCurve::get_min_value);
	ClassDB::bind_method(D_METHOD("set_min_value", "min"), &BetterCurve::set_min_value);
	ClassDB::bind_method(D_METHOD("get_max_value"), &BetterCurve::get_max_value);
	ClassDB::bind_method(D_METHOD("set_max_value", "max"), &BetterCurve::set_max_value);
	ClassDB::bind_method(D_METHOD("clean_dupes"), &BetterCurve::clean_dupes);
	ClassDB::bind_method(D_METHOD("bake"), &BetterCurve::bake);
	ClassDB::bind_method(D_METHOD("get_bake_resolution"), &BetterCurve::get_bake_resolution);
	ClassDB::bind_method(D_METHOD("set_bake_resolution", "resolution"), &BetterCurve::set_bake_resolution);
	ClassDB::bind_method(D_METHOD("_get_data"), &BetterCurve::get_data);
	ClassDB::bind_method(D_METHOD("_set_data", "data"), &BetterCurve::set_data);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_value", PROPERTY_HINT_RANGE, "-1024,1024,0.01"), "set_min_value", "get_min_value");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_value", PROPERTY_HINT_RANGE, "-1024,1024,0.01"), "set_max_value", "get_max_value");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "bake_resolution", PROPERTY_HINT_RANGE, "1,1000,1"), "set_bake_resolution", "get_bake_resolution");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "_data", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL), "_set_data", "_get_data");
	ADD_ARRAY_COUNT("Points", "point_count", "set_point_count", "get_point_count", "point_");

	ADD_SIGNAL(MethodInfo(SIGNAL_RANGE_CHANGED));
	ADD_SIGNAL(MethodInfo(SIGNAL_BAKED));

	BIND_ENUM_CONSTANT(TANGENT_FREE);
	BIND_ENUM_CONSTANT(TANGENT_LINEAR);
	BIND_ENUM_CONSTANT(TANGENT_MODE_COUNT);
}

void BetterCurve::_queue_update() {
	_update_queue_mutex.lock();
	bool start = true;
	if (_update_thread.joinable()) {
		if (!_update_queued) {
			_update_thread.join();
		} else {
			start = false;
		}
	}
	if (start) {
		_update_queued = true;
		_update_thread = std::thread(BetterCurve::_update_bake, this);
	}
	_update_queue_mutex.unlock();
	emit_changed();
}

#define STREAM_UPDATE_WAIT 50

void BetterCurve::_update_bake(void *data) {
	BetterCurve *curve = reinterpret_cast<BetterCurve *>(data);

	while (curve->_update_queued) {
		while (curve->_update_queued) {
			curve->_update_queue_mutex.lock();
			curve->_update_queued = false;
			curve->_update_queue_mutex.unlock();
			std::this_thread::sleep_for(std::chrono::milliseconds(STREAM_UPDATE_WAIT));
			//std::cout << "No update since " << STREAM_UPDATE_WAIT << "ms. Proceed" << std::endl;
		}
		Vector<Point> local_points;
		curve->_update_param_mutex.lock();
		local_points = curve->_points.duplicate();
		curve->_update_param_mutex.unlock();
		if (curve->_update_queued) {
			continue;
		}

		Vector<real_t> cache;
		int resolution = curve->_bake_resolution;
		cache.resize(resolution);

		int last_point_id = 0;

		for (int i = 1; i < resolution - 1; ++i) {
			real_t x = i / static_cast<real_t>(resolution - 1);
			// Find next point.
			for (; last_point_id < local_points.size() && local_points[last_point_id].position.x < x;
					++last_point_id) {
			}
			if (last_point_id > 0) {
				--last_point_id;
			}
			real_t y = _sample(x, local_points, last_point_id);
			cache.write[i] = y;
		}

		if (local_points.size() != 0) {
			cache.write[0] = local_points[0].position.y;
			cache.write[cache.size() - 1] = local_points[local_points.size() - 1].position.y;
		}

		{
			std::unique_lock<std::shared_mutex> cache_lock(curve->_getter_mutex);
			curve->_baked_cache.resize(resolution);
			for (int i = 0; i < resolution; ++i) {
				curve->_baked_cache.set(i, cache[i]);
			}
		}
	}
	curve->emit_signal(SIGNAL_BAKED);
}

real_t BetterCurve::_sample(real_t offset, const Vector<Point> &points, const int idx) {
	if (points.size() == 0) {
		return 0;
	}
	if (points.size() == 1) {
		return points[0].position.y;
	}

	if (idx == points.size() - 1) {
		return points[idx].position.y;
	}

	real_t local = offset - points[idx].position.x;

	if (idx == 0 && local <= 0) {
		return points[0].position.y;
	}

	return _sample_local_nocheck(idx, local, points);
}

real_t BetterCurve::sample_local_nocheck(int p_index, real_t p_local_offset) const {
	return _sample_local_nocheck(p_index, p_local_offset, _points);
}

real_t BetterCurve::_sample_local_nocheck(int p_index, real_t p_local_offset, const Vector<Point> &points) {
	const Point a = points[p_index];
	const Point b = points[p_index + 1];

	/* Cubic bézier
	 *
	 *       ac-----bc
	 *      /         \
	 *     /           \     Here with a.right_tangent > 0
	 *    /             \    and b.left_tangent < 0
	 *   /               \
	 *  a                 b
	 *
	 *  |-d1--|-d2--|-d3--|
	 *
	 * d1 == d2 == d3 == d / 3
	 */

	// Control points are chosen at equal distances
	real_t d = b.position.x - a.position.x;
	if (Math::is_zero_approx(d)) {
		return b.position.y;
	}
	p_local_offset /= d;
	d /= 3.0;
	real_t yac = a.position.y + d * a.right_tangent;
	real_t ybc = b.position.y - d * b.left_tangent;

	real_t y = Math::bezier_interpolate(a.position.y, yac, ybc, b.position.y, p_local_offset);

	return y;
}
