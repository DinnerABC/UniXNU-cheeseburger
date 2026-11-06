// Host model for CheeseburgerLaunchdCache: builds the system service cache from
// an Apple binary plist through CoreFoundation (an independent parser), adds one
// daemon with the kernel editor, and checks that every original job and the new
// job are still present with their values. The synthetic cache uses several
// hundred jobs so the object reference width is two bytes, like the real file.
#include <CoreFoundation/CoreFoundation.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
struct CheeseburgerLaunchdCacheStats {
	uint64_t input_bytes, output_bytes, objects, daemons;
	uint32_t offset_size, ref_size;
	int error;
};
int cheeseburger_launchd_cache_size(const uint8_t *, uint64_t, const char *, const char *,
    uint64_t *, struct CheeseburgerLaunchdCacheStats *);
int cheeseburger_launchd_cache_build(const uint8_t *, uint64_t, const char *, const char *,
    uint8_t *, uint64_t, struct CheeseburgerLaunchdCacheStats *);
}

static int failures;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x); failures++; } } while (0)

static std::string str(CFStringRef s)
{
	char buf[256];
	if (!CFStringGetCString(s, buf, sizeof buf, kCFStringEncodingUTF8)) {
		return "<bad>";
	}
	return buf;
}

static CFMutableDictionaryRef make_job(const std::string &label, const std::string &path)
{
	CFMutableDictionaryRef job = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
	    &kCFTypeDictionaryValueCallBacks);
	CFStringRef l = CFStringCreateWithCString(nullptr, label.c_str(), kCFStringEncodingUTF8);
	CFStringRef p = CFStringCreateWithCString(nullptr, path.c_str(), kCFStringEncodingUTF8);
	CFStringRef label_key = CFSTR("Label"), args_key = CFSTR("ProgramArguments"), run_key = CFSTR("RunAtLoad");
	const void *one = p;
	CFArrayRef args = CFArrayCreate(nullptr, &one, 1, &kCFTypeArrayCallBacks);
	CFDictionarySetValue(job, label_key, l);
	CFDictionarySetValue(job, args_key, args);
	CFDictionarySetValue(job, run_key, kCFBooleanTrue);
	CFRelease(args);
	CFRelease(p);
	CFRelease(l);
	return job;
}

static std::vector<uint8_t> build_cache(int daemons, bool &ok)
{
	CFMutableDictionaryRef root = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
	    &kCFTypeDictionaryValueCallBacks);
	CFMutableDictionaryRef jobs = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
	    &kCFTypeDictionaryValueCallBacks);
	for (int i = 0; i < daemons; i++) {
		char label[64], path[96], key[160];
		std::snprintf(label, sizeof label, "com.example.job%03d", i);
		std::snprintf(path, sizeof path, "/usr/libexec/job%03d", i);
		std::snprintf(key, sizeof key, "/System/Library/LaunchDaemons/com.example.job%03d.plist", i);
		CFMutableDictionaryRef job = make_job(label, path);
		CFStringRef k = CFStringCreateWithCString(nullptr, key, kCFStringEncodingUTF8);
		CFDictionarySetValue(jobs, k, job);
		CFRelease(k);
		CFRelease(job);
	}
	CFDictionarySetValue(root, CFSTR("LaunchDaemons"), jobs);
	const void *vers = CFSTR("7");
	CFDictionarySetValue(root, CFSTR("VersionNumber"), vers);
	CFDictionarySetValue(root, CFSTR("Symlinks"), CFDictionaryCreate(nullptr, nullptr, nullptr, 0,
	    &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
	CFErrorRef error = nullptr;
	CFDataRef data = CFPropertyListCreateData(nullptr, root, kCFPropertyListBinaryFormat_v1_0, 0, &error);
	ok = data != nullptr;
	std::vector<uint8_t> out;
	if (data) {
		out.assign(CFDataGetBytePtr(data), CFDataGetBytePtr(data) + CFDataGetLength(data));
		CFRelease(data);
	}
	if (error) {
		CFRelease(error);
	}
	CFRelease(jobs);
	CFRelease(root);
	return out;
}

static CFDictionaryRef parse(const std::vector<uint8_t> &blob)
{
	CFDataRef data = CFDataCreate(nullptr, blob.data(), (CFIndex)blob.size());
	CFPropertyListRef p = CFPropertyListCreateWithData(nullptr, data, 0, nullptr, nullptr);
	CFRelease(data);
	return (CFDictionaryRef)p;
}

static void one(int daemons, const char *path, const char *program, const char *label)
{
	bool ok = false;
	std::vector<uint8_t> in = build_cache(daemons, ok);
	CHECK(ok);
	if (!ok) {
		return;
	}
	CFDictionaryRef before = parse(in);
	CHECK(before != nullptr);
	CFDictionaryRef jobs_before = (CFDictionaryRef)CFDictionaryGetValue(before, CFSTR("LaunchDaemons"));
	CHECK(jobs_before && CFDictionaryGetCount(jobs_before) == daemons);

	struct CheeseburgerLaunchdCacheStats st = {};
	uint64_t need = 0;
	int rc = cheeseburger_launchd_cache_size(in.data(), in.size(), path, program, &need, &st);
	std::printf("size rc=%d err=%d need=%llu in=%zu objects=%llu daemons=%llu os=%u rs=%u\n", rc, st.error,
	    (unsigned long long)need, in.size(), (unsigned long long)st.objects, (unsigned long long)st.daemons,
	    st.offset_size, st.ref_size);
	CHECK(rc == 0);
	if (rc != 0) {
		CFRelease(before);
		return;
	}
	CHECK(need > in.size() && need < in.size() + (1u << 20));
	CHECK(st.daemons == (uint64_t)daemons);
	std::vector<uint8_t> out(need);
	struct CheeseburgerLaunchdCacheStats built = {};
	rc = cheeseburger_launchd_cache_build(in.data(), in.size(), path, program, out.data(), out.size(), &built);
	std::printf("build rc=%d err=%d need=%llu outbytes=%llu\n", rc, built.error,
	    (unsigned long long)need, (unsigned long long)built.output_bytes);
	CHECK(rc == 0);
	if (rc != 0) {
		CFRelease(before);
		return;
	}
	CHECK(built.error == 0);
	CHECK(built.output_bytes == need);

	CFDictionaryRef after = parse(out);
	CHECK(after != nullptr);
	if (!after) {
		CFRelease(before);
		return;
	}
	CFDictionaryRef jobs_after = (CFDictionaryRef)CFDictionaryGetValue(after, CFSTR("LaunchDaemons"));
	CHECK(jobs_after && CFDictionaryGetCount(jobs_after) == daemons + 1);
	// Every original entry is unchanged.
	CFIndex count = CFDictionaryGetCount(jobs_before);
	std::vector<const void *> keys(count), values(count);
	CFDictionaryGetKeysAndValues(jobs_before, keys.data(), values.data());
	for (CFIndex i = 0; i < count; i++) {
		const void *v = CFDictionaryGetValue(jobs_after, keys[i]);
		CHECK(v && CFEqual(v, values[i]));
	}
	CFStringRef new_key = CFStringCreateWithCString(nullptr, path, kCFStringEncodingUTF8);
	CFDictionaryRef job = (CFDictionaryRef)CFDictionaryGetValue(jobs_after, new_key);
	CHECK(job != nullptr);
	if (job) {
		CFStringRef l = (CFStringRef)CFDictionaryGetValue(job, CFSTR("Label"));
		CHECK(l && str(l) == label);
		CFArrayRef args = (CFArrayRef)CFDictionaryGetValue(job, CFSTR("ProgramArguments"));
		CHECK(args && CFArrayGetCount(args) == 1);
		if (args && CFArrayGetCount(args) == 1) {
			CHECK(str((CFStringRef)CFArrayGetValueAtIndex(args, 0)) == program);
		}
		CFBooleanRef run = (CFBooleanRef)CFDictionaryGetValue(job, CFSTR("RunAtLoad"));
		CHECK(run == kCFBooleanTrue);
		CHECK(str((CFStringRef)CFDictionaryGetValue(job, CFSTR("POSIXSpawnType"))) == "Interactive");
	}
	CFRelease(new_key);
	// Unrelated top-level keys survive.
	CHECK(CFEqual(CFDictionaryGetValue(after, CFSTR("VersionNumber")), CFSTR("7")));
	CHECK(CFDictionaryGetValue(after, CFSTR("Symlinks")) != nullptr);
	CFRelease(after);
	CFRelease(before);
}

int main()
{
	one(20, "/System/Library/LaunchDaemons/org.cheeseburger.adrenoprobed.plist",
	    "/private/var/mobile/ngpu/bin/adrenoprobed", "org.cheeseburger.adrenoprobed");
	one(600, "/System/Library/LaunchDaemons/org.cheeseburger.adrenoprobed.plist",
	    "/private/var/mobile/ngpu/bin/adrenoprobed", "org.cheeseburger.adrenoprobed");
	// Refuse a non-plist and an impossible path before writing anything.
	struct CheeseburgerLaunchdCacheStats st = {};
	uint64_t need = 0;
	uint8_t junk[64] = {'n', 'o', 't', ' ', 'a', ' ', 'p', 'l', 'i', 's', 't'};
	CHECK(cheeseburger_launchd_cache_size(junk, sizeof junk, "/x", "/y", &need, &st) != 0);
	std::vector<uint8_t> small;
	bool ok = false;
	small = build_cache(2, ok);
	CHECK(ok);
	std::string long_path(200, 'a');
	CHECK(cheeseburger_launchd_cache_size(small.data(), small.size(), long_path.c_str(), "/y", &need, &st) != 0);
	std::printf("%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
