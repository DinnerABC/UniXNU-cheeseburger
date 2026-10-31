// Host model for the Qualcomm ZAP archive/MDT parser. It validates the layout
// rules against synthetic images, and when the private device files are
// present it also parses the real a540_zap archive and checks the derived
// metadata/segment placement. This proves the software path only; it cannot
// prove that the secure monitor authenticates the image.
#include "../package/BoardRuntime/CheeseburgerZap.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace CheeseburgerZap;
using Blob = std::vector<uint8_t>;

static unsigned failures = 0;

static void check(bool value, const char *name) {
    if (value) {
        std::printf("PASS %s\n", name);
    } else {
        std::printf("FAIL %s\n", name);
        ++failures;
    }
}

struct Entry { uint32_t id; Blob data; };

static void put32(Blob &b, size_t at, uint32_t value) {
    b[at] = uint8_t(value), b[at + 1] = uint8_t(value >> 8);
    b[at + 2] = uint8_t(value >> 16), b[at + 3] = uint8_t(value >> 24);
}

static Blob container(const std::vector<Entry> &files) {
    size_t at = 32 + files.size() * 16;
    std::vector<size_t> offsets;
    for (const auto &f : files) {
        at = (at + 15) & ~size_t(15);
        offsets.push_back(at);
        at += f.data.size();
    }
    Blob b(at, 0);
    put32(b, 0, Archive::Magic); put32(b, 4, 1);
    put32(b, 8, uint32_t(files.size())); put32(b, 12, uint32_t(at));
    put32(b, 16, 16);
    for (size_t i = 0; i < files.size(); ++i) {
        put32(b, 32 + i * 16, files[i].id);
        put32(b, 32 + i * 16 + 4, uint32_t(offsets[i]));
        put32(b, 32 + i * 16 + 8, uint32_t(files[i].data.size()));
        std::memcpy(&b[offsets[i]], files[i].data.data(), files[i].data.size());
    }
    return b;
}

struct Ph { uint32_t type, off, vaddr, paddr, filesz, memsz, flags, align; };

static Blob elf(const std::vector<Ph> &ph) {
    Blob b(52 + ph.size() * 32, 0);
    b[0] = 0x7f; b[1] = 'E'; b[2] = 'L'; b[3] = 'F';
    b[4] = 1; b[5] = 1; b[6] = 1;
    put32(b, 20, 1); put32(b, 28, 52);
    b[40] = 52; b[42] = 32; b[44] = uint8_t(ph.size());
    for (size_t i = 0; i < ph.size(); ++i) {
        const size_t at = 52 + i * 32;
        put32(b, at + 0, ph[i].type); put32(b, at + 4, ph[i].off);
        put32(b, at + 8, ph[i].vaddr); put32(b, at + 12, ph[i].paddr);
        put32(b, at + 16, ph[i].filesz); put32(b, at + 20, ph[i].memsz);
        put32(b, at + 24, ph[i].flags); put32(b, at + 28, ph[i].align);
    }
    return b;
}

static Blob pattern(size_t bytes, uint8_t seed) {
    Blob b(bytes);
    for (size_t i = 0; i < bytes; ++i) b[i] = uint8_t(seed + i * 7);
    return b;
}

// The real A540 ZAP shape: 0x94 metadata header, in-file 0x1988 hash segment,
// one relocatable PT_LOAD at 0x5000 (0x1334 bytes, 1 MiB alignment) whose data
// lives in b02, and unused b00/b01 split files.
struct Fixture {
    Blob mdt, b00, b01, b02;
    std::vector<Entry> entries;
    uint32_t headerBytes = 0x94;
};

static Fixture fixture(bool hashInline = true) {
    Fixture f;
    const std::vector<Ph> ph = {
        {0, 0, 0, 0, 0x94, 0, 0x07000000, 0},
        {0, 0x1000, 0x7000, 0x7000, 0x1988, 0x2000, 0x02200000, 0x1000},
        {1, 0x3000, 0x5000, 0x5000, 0x1334, 0x1334, 0x08000007, 0x100000},
    };
    Blob body = elf(ph);
    f.b00 = body;
    f.b01 = pattern(0x1988, 0x5a);
    f.b02 = pattern(0x1334, 0x11);
    if (hashInline) {
        f.mdt = body;
        f.mdt.insert(f.mdt.end(), f.b01.begin(), f.b01.end());
    } else {
        f.mdt = body;
    }
    f.entries = {{Archive::MDT, f.mdt}, {0, f.b00}, {1, f.b01}, {2, f.b02}};
    return f;
}

static bool parses(const std::vector<Entry> &entries, Image &image) {
    const Blob box = container(entries);
    Archive archive;
    return archive.open(box.data(), box.size()) && image.parse(archive);
}

static void test_valid_layout() {
    Fixture f = fixture();
    const Blob box = container(f.entries);
    Archive archive;
    check(archive.open(box.data(), box.size()), "archive/valid");
    check(archive.count() == 4, "archive/count");
    const auto mdt = archive.file(Archive::MDT);
    check(mdt.data && mdt.bytes == f.mdt.size(), "archive/mdt-file");
    const auto b02 = archive.file(2);
    check(b02.data && b02.bytes == 0x1334, "archive/b02-file");
    check(!archive.file(3).data, "archive/missing-id");

    Image image;
    check(image.parse(archive), "image/parse");
    check(image.metadataBytes() == 0x1a1c, "image/metadata=0x1a1c");
    check(image.imageBytes() == 0x2000, "image/extent=0x2000");
    check(image.physicalBase() == 0x5000, "image/base=0x5000");
    check(image.imageAlignment() == 0x100000, "image/alignment=1MiB");
    check(image.relocatable(), "image/relocatable");
    check(image.segmentCount() == 1, "image/one-load-segment");

    Blob metadata(0x1a1c, 0);
    check(image.metadataInto(metadata.data(), uint32_t(metadata.size())), "metadata/into");
    check(std::memcmp(metadata.data(), f.mdt.data(), 0x94) == 0, "metadata/header");
    check(std::memcmp(metadata.data() + 0x94, f.b01.data(), 0x1988) == 0, "metadata/hash");
    check(!image.metadataInto(metadata.data(), 0x1a1b), "metadata/short-buffer");

    Blob region(0x2000, 0xee);
    check(image.imageInto(region.data(), uint32_t(region.size()), 0x200000), "image/into");
    check(std::memcmp(region.data(), f.b02.data(), 0x1334) == 0, "image/segment-at-0");
    bool zero = true;
    for (size_t i = 0x1334; i < region.size(); ++i) zero = zero && region[i] == 0;
    check(zero, "image/hole-and-padding-zero");
    check(!image.imageInto(region.data(), uint32_t(region.size()), 0x201000), "image/reject-unaligned");
    check(!image.imageInto(region.data(), 0x1000, 0x200000), "image/reject-short-region");
    check(image.imageInto(region.data(), uint32_t(region.size()), 0x300000), "image/relocate-second-page");
    // The device's iOS arena is above 4 GiB and PAS accepted a 40-bit region.
    check(image.imageInto(region.data(), uint32_t(region.size()), 0x111300000ull), "image/high-physical");
    check(!image.imageInto(region.data(), uint32_t(region.size()), 0x10000000000ull),
          "image/reject-over-40-bit");
}

static void test_archive_rejections() {
    Fixture f = fixture();
    Blob box = container(f.entries);
    Archive archive;
    check(!archive.open(box.data(), box.size() - 1), "archive/short");
    Blob bad = box;
    put32(bad, 0, 0x12345678);
    check(!archive.open(bad.data(), bad.size()), "archive/magic");
    bad = box;
    put32(bad, 32 + 16, 2);  // swap ids 0 and 1
    check(!archive.open(bad.data(), bad.size()), "archive/order");
    bad = box;
    put32(bad, 32 + 4, 35);  // misaligned offset
    check(!archive.open(bad.data(), bad.size()), "archive/alignment");
    bad = box;
    put32(bad, 12, uint32_t(bad.size() - 16));
    check(!archive.open(bad.data(), bad.size()), "archive/total");
    bad = box;
    put32(bad, 16, 32);
    check(!archive.open(bad.data(), bad.size()), "archive/entry-size");
}

static void test_image_rejections() {
    Fixture f = fixture();
    Image image;

    auto mutate = [&](Blob mdt, std::vector<Entry> entries) {
        for (auto &e : entries) if (e.id == Archive::MDT) e.data = mdt;
        return parses(entries, image);
    };
    auto ph = [](unsigned i) { return size_t(52 + i * 32); };

    Blob mdt = f.mdt;
    put32(mdt, ph(0), 1);  // ph0 must not be PT_LOAD
    check(!mutate(mdt, f.entries), "image/ph0-load");
    mdt = f.mdt;
    put32(mdt, ph(0) + 4, 16);  // header offset must be zero
    check(!mutate(mdt, f.entries), "image/ph0-offset");
    mdt = f.mdt;
    put32(mdt, ph(1) + 24, 0x00200000);  // no hash segment
    check(!mutate(mdt, f.entries), "image/no-hash");
    mdt = f.mdt;
    put32(mdt, ph(2) + 24, 0x02200007);  // two hash segments
    check(!mutate(mdt, f.entries), "image/two-hash");
    mdt = f.mdt;
    put32(mdt, ph(1) + 24, 0x00200000);
    put32(mdt, ph(0) + 24, 0x02200000);  // hash at index 0
    check(!mutate(mdt, f.entries), "image/hash-at-zero");
    mdt = f.mdt;
    put32(mdt, ph(2) + 16, 0x1335);  // filesz > memsz
    check(!mutate(mdt, f.entries), "image/filesz-gt-memsz");
    mdt = f.mdt;
    put32(mdt, ph(2) + 28, 0x3000);  // not a power of two
    check(!mutate(mdt, f.entries), "image/bad-align");
    mdt = f.mdt;
    put32(mdt, ph(2) + 12, 0xfffff000);  // 32-bit overflow
    check(!mutate(mdt, f.entries), "image/address-overflow");
    mdt = f.mdt;
    put32(mdt, ph(2) + 12, 0x5001);  // relocatable low must be page aligned
    check(!mutate(mdt, f.entries), "image/unaligned-low");
    mdt = f.mdt;
    put32(mdt, ph(2) + 24, 0x00000007);  // not relocatable, low is 0x5000
    check(mutate(mdt, f.entries) && !image.relocatable(), "image/fixed-address");
    mdt = f.mdt;
    put32(mdt, ph(2) + 12, 0);  // fixed image at address zero
    put32(mdt, ph(2) + 24, 0x00000007);
    check(!mutate(mdt, f.entries), "image/fixed-zero-base");
    mdt = f.mdt;
    put32(mdt, ph(2) + 20, 0x1001000);  // extent over 16 MiB
    check(!mutate(mdt, f.entries), "image/extent-limit");

    std::vector<Entry> entries = f.entries;
    entries.pop_back();  // drop b02, the split segment source
    check(!parses(entries, image), "image/missing-split-file");
    entries = f.entries;
    entries.back().data.pop_back();  // wrong split segment size
    check(!parses(entries, image), "image/split-size");
}

static void test_overlap() {
    // Two load segments that overlap must be rejected.
    Fixture f = fixture();
    Blob mdt = f.mdt;
    // Grow the program header table by one entry: ph0 header covers 0x94 -> add
    // a segment at 0x6000 that overlaps ph2 [0x5000, 0x6334).
    const size_t oldPh = 3;
    Blob grown(52 + 4 * 32, 0);
    std::memcpy(grown.data(), mdt.data(), 52 + oldPh * 32);
    grown[44] = 4;
    const size_t at = 52 + 3 * 32;
    put32(grown, at + 0, 1); put32(grown, at + 4, 0x3000);
    put32(grown, at + 8, 0x6000); put32(grown, at + 12, 0x6000);
    put32(grown, at + 16, 0x100); put32(grown, at + 20, 0x100);
    put32(grown, at + 24, 0x08000007); put32(grown, at + 28, 0x1000);
    put32(grown, 52 + 16, 52 + 4 * 32);  // ph0 header bytes
    Blob rest(mdt.begin() + 52 + oldPh * 32, mdt.end());
    grown.insert(grown.end(), rest.begin(), rest.end());
    std::vector<Entry> entries = f.entries;
    for (auto &e : entries) if (e.id == Archive::MDT) e.data = grown;
    entries.push_back({3, pattern(0x100, 0x77)});
    Image image;
    check(!parses(entries, image), "image/overlap");
}

static Blob readFile(const std::string &path, bool &ok) {
    std::ifstream in(path, std::ios::binary);
    ok = in.good();
    if (!ok) return {};
    return Blob(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static void test_device_files() {
    const std::string base = "out/p21/gpu/firmware/a540_zap.";
    bool ok = false;
    Blob mdt = readFile(base + "mdt", ok);
    if (!ok) {
        std::printf("SKIP device/a540_zap (private firmware not present)\n");
        return;
    }
    bool ok00 = false, ok01 = false, ok02 = false;
    Blob b00 = readFile(base + "b00", ok00);
    Blob b01 = readFile(base + "b01", ok01);
    Blob b02 = readFile(base + "b02", ok02);
    check(ok00 && ok01 && ok02, "device/all-files");
    if (!ok00 || !ok01 || !ok02) return;
    const Blob box = container({{Archive::MDT, mdt}, {0, b00}, {1, b01}, {2, b02}});
    Archive archive;
    check(archive.open(box.data(), box.size()), "device/archive");
    Image image;
    check(image.parse(archive), "device/parse");
    check(image.metadataBytes() == 0x1a1c, "device/metadata=0x1a1c");
    check(image.imageBytes() == 0x2000, "device/extent=0x2000");
    check(image.physicalBase() == 0x5000, "device/base=0x5000");
    check(image.imageAlignment() == 0x100000, "device/alignment=1MiB");
    check(image.relocatable(), "device/relocatable");
    check(image.segmentCount() == 1, "device/one-load-segment");
    check(mdt.size() == 0x1a1c && b01.size() == 0x1988 && b02.size() == 0x1334, "device/file-sizes");
    check(std::memcmp(mdt.data() + 0x94, b01.data(), 0x1988) == 0, "device/mdt-hash-is-b01");
    Blob metadata(0x1a1c, 0);
    check(image.metadataInto(metadata.data(), uint32_t(metadata.size())), "device/metadata-into");
    check(std::memcmp(metadata.data() + 0x94, b01.data(), 0x1988) == 0, "device/metadata-hash");
    Blob region(0x2000, 0);
    check(image.imageInto(region.data(), uint32_t(region.size()), 0x200000), "device/image-into");
    check(std::memcmp(region.data(), b02.data(), b02.size()) == 0, "device/image-code");
}

int main() {
    test_valid_layout();
    test_archive_rejections();
    test_image_rejections();
    test_overlap();
    test_device_files();
    if (failures) {
        std::printf("ZAP_MODEL_FAIL %u\n", failures);
        return 1;
    }
    std::printf("ZAP_MODEL_PASS\n");
    return 0;
}
