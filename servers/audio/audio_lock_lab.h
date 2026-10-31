// Local opt-in audio-lock diagnostic. Not a proposed public engine API.
#pragma once

#ifdef DEBUG_ENABLED
#include "core/os/os.h"
#include "core/os/thread.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"

#include <array>

namespace AudioLockLab {
enum Phase { TOTAL,
	INTERNAL_STOP,
	LOCK1_WAIT,
	LOCK1_HOLD,
	RESIZE,
	STREAM_REF,
	FACTORY_ASSIGN,
	LOCK2_WAIT,
	LOCK2_HOLD,
	RESAMPLER,
	HEADERS,
	CODECS,
	TEXTURE,
	AUDIO_BUFFERS,
	DURATION_SCAN,
	INITIAL_SEEK,
	CLEANUP,
	CONSTRUCT,
	POST_SETUP,
	COUNT };
inline const char *names[COUNT] = { "total", "internal_stop", "lock1_wait", "lock1_hold", "resize", "stream_ref",
	"factory_and_assignment", "lock2_wait", "lock2_hold", "resampler", "headers", "codecs",
	"texture", "audio_buffers", "duration_scan", "initial_seek", "cleanup", "construct", "post_setup" };
struct Record {
	uint64_t player = 0, sequence = 0, timestamp = 0;
	std::array<uint64_t, COUNT> us{};
	std::array<bool, COUNT> present{};
};
inline std::array<Record, 1024> records{};
inline std::array<Record, 4> stack{};
inline bool enabled = false;
inline unsigned count = 0, depth = 0, overflow = 0, nested_dropped = 0;
inline uint64_t sequence = 0, run = 0;
inline bool active() {
	return Thread::is_main_thread() && enabled && depth > 0 && depth <= stack.size();
}
inline uint64_t stamp() {
	return active() ? OS::get_singleton()->get_ticks_usec() : 0;
}
inline void elapsed(Phase p, uint64_t start) {
	if (!active()) {
		return;
	}
	Record &r = stack[depth - 1];
	r.us[p] += OS::get_singleton()->get_ticks_usec() - start;
	r.present[p] = true;
}
inline void mark(Phase p, uint64_t &start) {
	elapsed(p, start);
	start = stamp();
}
struct Assignment {
	bool tracking = false;
	Assignment(uint64_t player) {
		if (!Thread::is_main_thread() || !enabled) {
			return;
		}
		tracking = true;
		++depth;
		if (depth > stack.size()) {
			++nested_dropped;
			return;
		}
		Record &r = stack[depth - 1];
		r = Record{};
		r.player = player;
		r.sequence = ++sequence;
		r.timestamp = OS::get_singleton()->get_ticks_usec();
	}
	~Assignment() {
		if (!tracking) {
			return;
		}
		if (depth <= stack.size()) {
			Record &r = stack[depth - 1];
			r.us[TOTAL] = OS::get_singleton()->get_ticks_usec() - r.timestamp;
			r.present[TOTAL] = true;
			if (count < records.size()) {
				records[count++] = r;
			} else {
				++overflow;
			}
		}
		--depth;
	}
};
inline bool begin() {
	if (!Thread::is_main_thread() || depth != 0 || enabled) {
		return false;
	}
	count = overflow = nested_dropped = 0;
	sequence = 0;
	++run;
	enabled = true;
	return true;
}
inline Dictionary end() {
	Dictionary out;
	if (!Thread::is_main_thread() || depth != 0) {
		out["error"] = "collection is busy";
		return out;
	}
	enabled = false;
	Array rows;
	for (unsigned i = 0; i < count; ++i) {
		const Record &r = records[i];
		Dictionary row, phases;
		row["player_id"] = r.player;
		row["sequence"] = r.sequence;
		row["timestamp_usec"] = r.timestamp;
		for (unsigned p = 0; p < COUNT; ++p) {
			if (r.present[p]) {
				phases[names[p]] = r.us[p];
			}
		}
		row["inclusive_phase_usec"] = phases;
		rows.push_back(row);
	}
	out["schema"] = 1;
	out["variant"] = "instrumented_baseline";
	out["run_id"] = run;
	out["records"] = rows;
	out["overflow"] = overflow;
	out["nested_dropped"] = nested_dropped;
	return out;
}
} // namespace AudioLockLab
#endif
