// SPDX-License-Identifier: MIT
// Adds one launchd daemon to the sealed system service cache
// (/System/Library/xpc/launchd.plist) without modifying any original object.
//
// iOS 16 and later create the system domain's daemons from that signed cache;
// a plist that appears only on disk (for example through the owner union mount)
// is not discovered at boot. The cache is a binary property list. This editor
// appends the new key, the job dictionary and a copy of the LaunchDaemons
// dictionary that gains one pair, then points the root dictionary at the copy.
// Every original object keeps its bytes; the offset table and trailer are
// rebuilt. All reads are bounds checked, and the output size depends only on
// the input and the two strings, so the caller can size once and allocate once.
#include <stdint.h>
#include <string.h>

#define CB_LC_MAX_OBJECTS (1u << 24)
#define CB_LC_MAX_PATH 192u
#define CB_LC_MAX_PROGRAM 192u
#define CB_LC_ADDED 12

struct CheeseburgerLaunchdCacheStats {
	uint64_t input_bytes, output_bytes, objects, daemons;
	uint32_t offset_size, ref_size;
	int error;
};

enum {
	CB_LC_OK = 0,
	CB_LC_BAD_MAGIC = 1,
	CB_LC_BAD_TRAILER = 2,
	CB_LC_BAD_OFFSETS = 3,
	CB_LC_NO_LAUNCHDAEMONS = 4,
	CB_LC_BAD_DICT = 5,
	CB_LC_REF_OVERFLOW = 6,
	CB_LC_TOO_LARGE = 7,
	CB_LC_BAD_INPUT = 8,
};

static uint64_t
cb_lc_be(const uint8_t *p, uint32_t n)
{
	uint64_t v = 0;
	for (uint32_t i = 0; i < n; i++) {
		v = (v << 8) | p[i];
	}
	return v;
}

static void
cb_lc_put_be(uint8_t *p, uint64_t v, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++) {
		p[n - 1 - i] = (uint8_t)(v >> (8 * i));
	}
}

static int
cb_lc_int(const uint8_t *in, uint64_t end, uint64_t at, uint64_t *value, uint64_t *next)
{
	if (at >= end || (in[at] >> 4) != 0x1) {
		return -1;
	}
	uint32_t bytes = 1u << (in[at] & 0xf);
	if (bytes > 8 || at + 1 + bytes > end) {
		return -1;
	}
	*value = cb_lc_be(in + at + 1, bytes);
	*next = at + 1 + bytes;
	return 0;
}

// ASCII string object: text/length cover the payload, next is past the object.
static int
cb_lc_ascii_at(const uint8_t *in, uint64_t end, uint64_t at, const uint8_t **text, uint64_t *length, uint64_t *next)
{
	if (at >= end || (in[at] >> 4) != 0x5) {
		return -1;
	}
	uint32_t lo = in[at] & 0xf;
	uint64_t n = lo, start = at + 1;
	if (lo == 0xf && cb_lc_int(in, end, at + 1, &n, &start)) {
		return -1;
	}
	if (start + n > end) {
		return -1;
	}
	*text = in + start;
	*length = n;
	*next = start + n;
	return 0;
}

// Dictionary object: count entries and header size (marker plus any count).
static int
cb_lc_dict_at(const uint8_t *in, uint64_t end, uint64_t at, uint32_t ref_size, uint64_t *count, uint64_t *header)
{
	if (at >= end || (in[at] >> 4) != 0xd) {
		return -1;
	}
	uint32_t lo = in[at] & 0xf;
	uint64_t n = lo, next = at + 1;
	if (lo == 0xf && cb_lc_int(in, end, at + 1, &n, &next)) {
		return -1;
	}
	if (n > CB_LC_MAX_OBJECTS || next - at + 2 * n * ref_size > end - at) {
		return -1;
	}
	*count = n;
	*header = next - at;
	return 0;
}

static uint64_t
cb_lc_count_bytes(uint64_t count)
{
	if (count < 15) {
		return 1;
	}
	if (count <= 0xff) {
		return 3;
	}
	if (count <= 0xffff) {
		return 4;
	}
	return 6;
}

static uint64_t
cb_lc_count_write(uint8_t *out, uint8_t marker, uint64_t count)
{
	if (count < 15) {
		out[0] = (uint8_t)(marker | count);
		return 1;
	}
	out[0] = (uint8_t)(marker | 0xf);
	if (count <= 0xff) {
		out[1] = 0x10;
		out[2] = (uint8_t)count;
		return 3;
	}
	if (count <= 0xffff) {
		out[1] = 0x11;
		cb_lc_put_be(out + 2, count, 2);
		return 4;
	}
	out[1] = 0x12;
	cb_lc_put_be(out + 2, count, 4);
	return 6;
}

static uint64_t
cb_lc_ascii_bytes(uint64_t length)
{
	if (length < 15) {
		return 1 + length;
	}
	if (length <= 0xff) {
		return 3 + length;
	}
	return 4 + length;
}

static uint64_t
cb_lc_ascii_write(uint8_t *out, const char *text)
{
	uint64_t n = strlen(text);
	if (n < 15) {
		out[0] = (uint8_t)(0x50 | n);
		memcpy(out + 1, text, n);
		return 1 + n;
	}
	out[0] = 0x5f;
	if (n <= 0xff) {
		out[1] = 0x10;
		out[2] = (uint8_t)n;
		memcpy(out + 3, text, n);
		return 3 + n;
	}
	out[1] = 0x11;
	cb_lc_put_be(out + 2, n, 2);
	memcpy(out + 4, text, n);
	return 4 + n;
}

static int
cb_lc_locate(const uint8_t *in, uint64_t in_size, uint64_t *offtab, uint32_t *offset_size,
    uint32_t *ref_size, uint64_t *objects, uint64_t *top)
{
	if (in_size < 40 || memcmp(in, "bplist00", 8)) {
		return CB_LC_BAD_MAGIC;
	}
	const uint8_t *tr = in + in_size - 32;
	uint32_t os = tr[6], rs = tr[7];
	uint64_t n = cb_lc_be(tr + 8, 8), t = cb_lc_be(tr + 16, 8), ot = cb_lc_be(tr + 24, 8);
	if ((os != 1 && os != 2 && os != 4 && os != 8) || (rs != 1 && rs != 2 && rs != 4 && rs != 8) ||
	    !n || n > CB_LC_MAX_OBJECTS || t >= n || ot < 8 || ot > in_size - 32) {
		return CB_LC_BAD_TRAILER;
	}
	if (n > (in_size - 32 - ot) / os) {
		return CB_LC_BAD_TRAILER;
	}
	for (uint64_t i = 0; i < n; i++) {
		uint64_t off = cb_lc_be(in + ot + i * os, os);
		if (off < 8 || off >= ot) {
			return CB_LC_BAD_OFFSETS;
		}
	}
	*offtab = ot;
	*offset_size = os;
	*ref_size = rs;
	*objects = n;
	*top = t;
	return CB_LC_OK;
}

// Finds the LaunchDaemons value in the root dictionary. Returns its object
// offset, entry count, entry header size and the byte offsets of the key/value
// reference arrays.
struct cb_lc_root {
	uint64_t daemons_off, count, header, keys, values, value_slot;
};

static int
cb_lc_root_find(const uint8_t *in, uint64_t in_size, uint32_t os, uint32_t rs, uint64_t offtab,
    uint64_t objects, uint64_t top, struct cb_lc_root *root)
{
	uint64_t top_off = cb_lc_be(in + offtab + top * os, os), count = 0, header = 0;
	if (cb_lc_dict_at(in, offtab, top_off, rs, &count, &header)) {
		return CB_LC_BAD_DICT;
	}
	uint64_t keys = top_off + header, values = keys + count * rs;
	int found = 0;
	for (uint64_t i = 0; i < count; i++) {
		uint64_t key_ref = cb_lc_be(in + keys + i * rs, rs);
		uint64_t value_ref = cb_lc_be(in + values + i * rs, rs);
		if (key_ref >= objects || value_ref >= objects) {
			return CB_LC_BAD_DICT;
		}
		const uint8_t *text;
		uint64_t length, next;
		if (cb_lc_ascii_at(in, offtab, cb_lc_be(in + offtab + key_ref * os, os), &text, &length, &next)) {
			return CB_LC_BAD_DICT;
		}
		if (length == sizeof("LaunchDaemons") - 1 && !memcmp(text, "LaunchDaemons", length)) {
			root->daemons_off = cb_lc_be(in + offtab + value_ref * os, os);
			root->value_slot = values + i * rs;
			found = 1;
		}
	}
	if (!found) {
		return CB_LC_NO_LAUNCHDAEMONS;
	}
	(void)in_size;
	if (cb_lc_dict_at(in, offtab, root->daemons_off, rs, &root->count, &root->header)) {
		return CB_LC_BAD_DICT;
	}
	root->keys = root->daemons_off + root->header;
	root->values = root->keys + root->count * rs;
	return CB_LC_OK;
}

int
cheeseburger_launchd_cache_size(const uint8_t *in, uint64_t in_size, const char *plist_path,
    const char *program, uint64_t *out_size, struct CheeseburgerLaunchdCacheStats *st)
{
	uint64_t offtab = 0, objects = 0, top = 0;
	uint32_t os = 0, rs = 0;
	int error = cb_lc_locate(in, in_size, &offtab, &os, &rs, &objects, &top);
	struct cb_lc_root root = {};
	if (!error) {
		error = cb_lc_root_find(in, in_size, os, rs, offtab, objects, top, &root);
	}
	if (!error && (!plist_path || !*plist_path || !program || !*program ||
	    strlen(plist_path) > CB_LC_MAX_PATH || strlen(program) > CB_LC_MAX_PROGRAM)) {
		error = CB_LC_BAD_INPUT;
	}
	if (!error && objects + CB_LC_ADDED > (1ull << (8 * rs))) {
		error = CB_LC_REF_OVERFLOW;
	}
	if (!error) {
		uint64_t n = root.count;
		uint64_t dict = cb_lc_count_bytes(n + 1) + 2ull * (n + 1) * rs;
		uint64_t extra = cb_lc_ascii_bytes(strlen(plist_path)) + cb_lc_ascii_bytes(strlen(program)) +
		    cb_lc_ascii_bytes(sizeof("org.cheeseburger.adrenoprobed") - 1) +
		    cb_lc_ascii_bytes(sizeof("Label") - 1) + cb_lc_ascii_bytes(sizeof("ProgramArguments") - 1) +
		    cb_lc_ascii_bytes(sizeof("RunAtLoad") - 1) + cb_lc_ascii_bytes(sizeof("POSIXSpawnType") - 1) +
		    cb_lc_ascii_bytes(sizeof("Interactive") - 1) + (1 + rs) + 1 + (1 + 8ull * rs) + dict;
		uint64_t size = offtab + extra + (objects + CB_LC_ADDED) * os + 32;
		if (size > in_size + (16ull << 20)) {
			error = CB_LC_TOO_LARGE;
		} else {
			*out_size = size;
			if (st) {
				st->input_bytes = in_size;
				st->output_bytes = size;
				st->objects = objects;
				st->daemons = n;
				st->offset_size = os;
				st->ref_size = rs;
				st->error = 0;
			}
		}
	}
	if (error && st) {
		st->input_bytes = in_size;
		st->error = error;
	}
	return error;
}

int
cheeseburger_launchd_cache_build(const uint8_t *in, uint64_t in_size, const char *plist_path,
    const char *program, uint8_t *out, uint64_t out_cap, struct CheeseburgerLaunchdCacheStats *st)
{
	static const char label[] = "org.cheeseburger.adrenoprobed";
	uint64_t need = 0;
	int error = cheeseburger_launchd_cache_size(in, in_size, plist_path, program, &need, st);
	if (error) {
		return error;
	}
	if (!out || out_cap < need) {
		if (st) {
			st->error = CB_LC_TOO_LARGE;
		}
		return CB_LC_TOO_LARGE;
	}
	uint64_t offtab = 0, objects = 0, top = 0;
	uint32_t os = 0, rs = 0;
	struct cb_lc_root root = {};
	if (cb_lc_locate(in, in_size, &offtab, &os, &rs, &objects, &top) ||
	    cb_lc_root_find(in, in_size, os, rs, offtab, objects, top, &root)) {
		return CB_LC_BAD_INPUT;
	}
	uint64_t n = root.count;

	memcpy(out, in, offtab);
	uint64_t at = offtab, refs[CB_LC_ADDED];
	uint8_t *base = out;
	refs[0] = objects + 0;
	at += cb_lc_ascii_write(base + at, label);
	refs[1] = objects + 1;
	at += cb_lc_ascii_write(base + at, program);
	refs[2] = objects + 2;
	base[at++] = 0xa1;
	cb_lc_put_be(base + at, refs[1], rs);
	at += rs;
	refs[3] = objects + 3;
	at += cb_lc_ascii_write(base + at, "Label");
	refs[4] = objects + 4;
	at += cb_lc_ascii_write(base + at, "ProgramArguments");
	refs[5] = objects + 5;
	at += cb_lc_ascii_write(base + at, "RunAtLoad");
	refs[6] = objects + 6;
	at += cb_lc_ascii_write(base + at, "POSIXSpawnType");
	refs[7] = objects + 7;
	at += cb_lc_ascii_write(base + at, "Interactive");
	refs[8] = objects + 8;
	refs[9] = objects + 9;
	refs[10] = objects + 10;
	refs[11] = objects + 11;
	at += cb_lc_count_write(base + at, 0xd0, 4);
	for (int i = 0; i < 4; i++) {
		cb_lc_put_be(base + at, refs[3 + i], rs);
		at += rs;
	}
	cb_lc_put_be(base + at, refs[0], rs);
	at += rs;
	cb_lc_put_be(base + at, refs[2], rs);
	at += rs;
	cb_lc_put_be(base + at, refs[9], rs);
	at += rs;
	cb_lc_put_be(base + at, refs[7], rs);
	at += rs;
	base[at++] = 0x09;
	at += cb_lc_ascii_write(base + at, plist_path);
	uint64_t appended_offsets[CB_LC_ADDED];
	appended_offsets[0] = offtab;
	for (uint64_t i = 1; i < CB_LC_ADDED; i++) {
		appended_offsets[i] = 0;
	}
	uint64_t dict_start = at;
	at += cb_lc_count_write(base + at, 0xd0, n + 1);
	for (uint64_t i = 0; i < n; i++) {
		cb_lc_put_be(base + at, cb_lc_be(in + root.keys + i * rs, rs), rs);
		at += rs;
	}
	cb_lc_put_be(base + at, refs[10], rs);
	at += rs;
	for (uint64_t i = 0; i < n; i++) {
		cb_lc_put_be(base + at, cb_lc_be(in + root.values + i * rs, rs), rs);
		at += rs;
	}
	cb_lc_put_be(base + at, refs[8], rs);
	at += rs;
	cb_lc_put_be(base + root.value_slot, refs[11], rs);

	// Appended object offsets are the sizes written above, in order.
	uint64_t walk = offtab;
	appended_offsets[0] = walk;
	walk += cb_lc_ascii_bytes(strlen(label));
	appended_offsets[1] = walk;
	walk += cb_lc_ascii_bytes(strlen(program));
	appended_offsets[2] = walk;
	walk += 1 + rs;
	appended_offsets[3] = walk;
	walk += cb_lc_ascii_bytes(sizeof("Label") - 1);
	appended_offsets[4] = walk;
	walk += cb_lc_ascii_bytes(sizeof("ProgramArguments") - 1);
	appended_offsets[5] = walk;
	walk += cb_lc_ascii_bytes(sizeof("RunAtLoad") - 1);
	appended_offsets[6] = walk;
	walk += cb_lc_ascii_bytes(sizeof("POSIXSpawnType") - 1);
	appended_offsets[7] = walk;
	walk += cb_lc_ascii_bytes(sizeof("Interactive") - 1);
	appended_offsets[8] = walk;
	walk += cb_lc_count_bytes(4) + 8 * rs;
	appended_offsets[9] = walk;
	walk += 1;
	appended_offsets[10] = walk;
	walk += cb_lc_ascii_bytes(strlen(plist_path));
	appended_offsets[11] = walk;
	walk += cb_lc_count_bytes(n + 1) + 2ull * (n + 1) * rs;
	if (walk != dict_start + cb_lc_count_bytes(n + 1) + 2ull * (n + 1) * rs) {
		return CB_LC_BAD_INPUT;
	}

	uint64_t table = at;
	for (uint64_t i = 0; i < objects; i++) {
		cb_lc_put_be(base + at, cb_lc_be(in + offtab + i * os, os), os);
		at += os;
	}
	for (uint64_t i = 0; i < CB_LC_ADDED; i++) {
		cb_lc_put_be(base + at, appended_offsets[i], os);
		at += os;
	}
	uint8_t trailer[32];
	memset(trailer, 0, sizeof trailer);
	trailer[6] = (uint8_t)os;
	trailer[7] = (uint8_t)rs;
	cb_lc_put_be(trailer + 8, objects + CB_LC_ADDED, 8);
	cb_lc_put_be(trailer + 16, top, 8);
	cb_lc_put_be(trailer + 24, table, 8);
	if (at + 32 != need) {
		return CB_LC_TOO_LARGE;
	}
	memcpy(base + at, trailer, 32);
	if (st) {
		st->error = 0;
	}
	return CB_LC_OK;
}
