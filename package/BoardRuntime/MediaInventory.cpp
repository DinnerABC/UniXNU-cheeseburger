// P17-R2 read-only media inventory sampler.
//
// Earlier evidence was transcribed from photographs, and the values that matter
// for the root-mount diagnosis (which disk carries which UUID, the exact byte
// sizes, and which volume the kernel is actually waiting for) cannot be trusted
// from a transcription. This object reads the service tree during boot and
// stores a short labelled list in a static buffer, which the panic funnel prints
// on the device so a person can read the values straight off the screen.
//
// It only reads properties: it issues no I/O, claims no media, mounts nothing
// and changes no boot decision. If thread creation fails the buffer says so and
// the boot behaves exactly as before. Screen space is limited, so only one full
// list is stored; the other samples keep just the media count.
#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <IOKit/IOBSD.h>
#include <kern/thread.h>
#include <kern/clock.h>
#include <libkern/c++/OSBoolean.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSIterator.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSString.h>

// Fixed XNU private continuation exit; exact declaration from osfmk/kern/thread.h.
extern "C" void thread_terminate_self(void);
extern "C" {
char p17_media_text[3072];
volatile uint32_t p17_media_text_size;
}

static unsigned p17_len(const char *s)
{
	unsigned n = 0;

	while (s[n]) {
		n++;
	}
	return n;
}

static bool p17_same(const char *a, const char *b)
{
	if (!a || !b) {
		return false;
	}
	for (unsigned i = 0;; i++) {
		if (a[i] != b[i]) {
			return false;
		}
		if (!a[i]) {
			return true;
		}
	}
}

static void emit(const char *s)
{
	uint32_t at = p17_media_text_size;
	unsigned n = p17_len(s);

	if (at >= sizeof(p17_media_text) - 1 || !n) {
		return;
	}
	if (n > sizeof(p17_media_text) - 1 - at) {
		n = sizeof(p17_media_text) - 1 - at;
	}
	for (unsigned i = 0; i < n; i++) {
		p17_media_text[at + i] = s[i];
	}
	p17_media_text_size = at + n;
	p17_media_text[p17_media_text_size] = 0;
}

// Decimal, one fact per line: a wrapped number is exactly what made the earlier
// transcription unreliable.
static void emit_dec(const char *prefix, const char *suffix, uint64_t v)
{
	char digits[24];
	unsigned nd = 0;

	if (!v) {
		digits[nd++] = '0';
	}
	while (v) {
		digits[nd++] = char('0' + int(v % 10));
		v /= 10;
	}
	char line[160];
	unsigned k = 0;

	for (unsigned i = 0; prefix[i] && k < 140; i++) {
		line[k++] = prefix[i];
	}
	while (nd && k < 150) {
		line[k++] = digits[--nd];
	}
	for (unsigned i = 0; suffix && suffix[i] && k < 158; i++) {
		line[k++] = suffix[i];
	}
	line[k] = 0;
	emit(line);
	emit("\n");
}

static void emit_str(const char *prefix, const char *value)
{
	char line[160];
	unsigned k = 0;

	for (unsigned i = 0; prefix[i] && k < 120; i++) {
		line[k++] = prefix[i];
	}
	if (value) {
		for (unsigned i = 0; value[i] && k < 158; i++) {
			line[k++] = value[i];
		}
	} else {
		const char none[] = "none";

		for (unsigned i = 0; none[i] && k < 158; i++) {
			line[k++] = none[i];
		}
	}
	line[k] = 0;
	emit(line);
	emit("\n");
}

// Content hints are printed as short words so a line cannot be misread.
static const char *role_of(const char *content)
{
	if (!content) {
		return "none";
	}
	if (p17_same(content, "41504653-0000-11AA-AA11-00306543ECAC")) {
		return "APFS-volume";
	}
	if (p17_same(content, "EF57347C-0000-11AA-AA11-00306543ECAC")) {
		return "APFS-container";
	}
	if (p17_same(content, "48465300-0000-11AA-AA11-00306543ECAC")) {
		return "HFS";
	}
	return content;
}

static void describe(unsigned index, IOService *media)
{
	OSString *bsd = OSDynamicCast(OSString, media->getProperty(kIOBSDNameKey));
	OSNumber *size = OSDynamicCast(OSNumber, media->getProperty("Size"));
	OSObject *whole = media->getProperty("Whole");
	bool is_whole = false;

	if (OSBoolean *flag = OSDynamicCast(OSBoolean, whole)) {
		is_whole = flag->isTrue();
	} else if (OSNumber *num = OSDynamicCast(OSNumber, whole)) {
		is_whole = num->unsigned32BitValue() != 0;
	}
	OSString *content = OSDynamicCast(OSString, media->getProperty("Content"));
	OSString *uuid = OSDynamicCast(OSString, media->getProperty("UUID"));

	char head[160];
	unsigned k = 0;

	head[k++] = 'M';
	head[k++] = char('0' + index);
	head[k++] = ' ';
	if (bsd) {
		const char *name = bsd->getCStringNoCopy();

		for (unsigned i = 0; name && name[i] && k < 110; i++) {
			head[k++] = name[i];
		}
	} else {
		head[k++] = '-';
	}
	const char middle[] = " b=";

	for (unsigned i = 0; middle[i] && k < 150; i++) {
		head[k++] = middle[i];
	}
	head[k] = 0;

	char tail[32];

	k = 0;
	tail[k++] = ' ';
	tail[k++] = 'w';
	tail[k++] = '=';
	tail[k++] = is_whole ? '1' : '0';
	tail[k++] = ' ';
	tail[k] = 0;

	const char *role = role_of(content ? content->getCStringNoCopy() : 0);
	emit_dec(head, tail, size ? size->unsigned64BitValue() : 0);
	emit_str("  content=", role);
	emit_str("  uuid=", uuid ? uuid->getCStringNoCopy() : 0);
}

// UUID inputs below are copied from existing signed policy/source and seed reports,
// never from a photograph. Presence is observation, not a boot acceptance rule.
static const char *wanted[3]={"04769D26-2CA0-4825-B443-E2E5F88300F2",
 "8846FE78-2910-4297-AF97-D59BB6F8FC16","D2420479-F58E-44C5-AB79-1815EC32C686"};
static char summaries[3][112];
static void sample(unsigned index, uint64_t elapsed_ms, bool full)
{
 (void)full;
 IOService *items[128];unsigned count=0;bool present[3]={false,false,false};
 OSDictionary *matching=IOService::serviceMatching("IOMedia");
 OSIterator *iterator=matching?IOService::getMatchingServices(matching):nullptr;
 IOService *media=nullptr;
 while(iterator && count<128 && (media=OSDynamicCast(IOService,iterator->getNextObject()))) {
  items[count++]=media;
  OSString *uuid=OSDynamicCast(OSString,media->getProperty("UUID"));
  for(unsigned j=0;j<3;j++)if(uuid && p17_same(uuid->getCStringNoCopy(),wanted[j]))present[j]=true;
 }
 snprintf(summaries[index-1],sizeof(summaries[0]),"S%u ms=%llu n=%u O=%u S=%u D=%u\n",index,(unsigned long long)elapsed_ms,count,present[0],present[1],present[2]);
 p17_media_text_size=0;p17_media_text[0]=0;
 emit("O=OS S=SystemOS D=Data (UUID presence)\n");
 for(unsigned j=0;j<index;j++)emit(summaries[j]);
 // Put the three policy/seed UUIDs first, then remaining media, maximum8.
 unsigned shown=0;bool selected[128]={false};
 for(unsigned pass=0;pass<2;pass++)for(unsigned i=0;i<count && shown<8;i++) {
  OSString *uuid=OSDynamicCast(OSString,items[i]->getProperty("UUID"));bool target=false;
  for(unsigned j=0;j<3;j++)if(uuid && p17_same(uuid->getCStringNoCopy(),wanted[j]))target=true;
  if((pass==0 && target)||(pass==1 && !selected[i])){describe(shown++,items[i]);selected[i]=true;}
 }
 emit_dec("Media shown=", "",shown);emit_dec("Media omitted=","",count-shown);
 OSSafeReleaseNULL(iterator);OSSafeReleaseNULL(matching);
}

static void report_boot_uuid(void)
{
	OSDictionary *matching = IOService::serviceMatching("IOResources");
	OSIterator *iterator = matching ? IOService::getMatchingServices(matching) : nullptr;
	IOService *resources = iterator ? OSDynamicCast(IOService, iterator->getNextObject()) : nullptr;
	OSString *uuid = resources ? OSDynamicCast(OSString, resources->getProperty("boot-uuid")) : nullptr;
	OSString *preboot = resources ? OSDynamicCast(OSString, resources->getProperty("apfs-preboot-uuid")) : nullptr;

	emit_str("R bootuuid=", uuid ? uuid->getCStringNoCopy() : nullptr);
	emit_str("R preboot=", preboot ? preboot->getCStringNoCopy() : nullptr);
	OSSafeReleaseNULL(iterator);
	OSSafeReleaseNULL(matching);
}

static void sampler(void *, int)
{
	// Each sample preserves UUID presence; latest complete sample replaces the full list.
	const unsigned delays[3] = { 10000, 20000, 25000 };
	uint64_t started=0;clock_get_uptime(&started);

	for (unsigned i = 0; i < 3; i++) {
		IOSleep(delays[i]);
		uint64_t now=0,ns=0;clock_get_uptime(&now);absolutetime_to_nanoseconds(now-started,&ns);uint64_t elapsed=ns/1000000;
		if (i == 0) {
			report_boot_uuid();
		}
		sample(i + 1, elapsed, true);
		report_boot_uuid();
	}
	thread_terminate_self();
}

extern "C" void p17_media_inventory_start(void)
{
	thread_t thread = nullptr;

	p17_media_text[0] = 0;
	p17_media_text_size = 0;
	if (kernel_thread_start(sampler, nullptr, &thread) != KERN_SUCCESS) {
		emit("S0 sampler start failed\n");
	} else { thread_deallocate(thread); }
}
