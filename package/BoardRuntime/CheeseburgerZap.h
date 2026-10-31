// SPDX-License-Identifier: GPL-2.0-only
// Qualcomm MDT layout for the GPU ZAP, ported from the pinned sagit reference
// (QcomGPU/AdrenoFirmware.h) and Linux v6.12 soc/qcom/mdt_loader.c.
//
// The archive is only a transport container. The signed ELF header, the hash
// segment and every image byte are copied verbatim; only the secure monitor can
// authenticate the image. The parser is host-testable and performs no I/O.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace CheeseburgerZap {

struct Bytes { const uint8_t *data = nullptr; uint32_t bytes = 0; };

// QZAP container: magic, version, entry count, total length, entry size 16 and
// three reserved zero words. Entry 0 is the MDT/ELF file (id UINT32_MAX); the
// remaining entries are the split segment files b00, b01, ... indexed by their
// program-header index, as mdt_load_split_segment() names them.
class Archive {
    const uint8_t *data_ = nullptr;
    uint32_t bytes_ = 0, count_ = 0;
    static uint32_t word(const uint8_t *p) {
        return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    }
public:
    static constexpr uint32_t Magic = 0x50415a51, MDT = UINT32_MAX, Maximum = 32 * 1024 * 1024;

    bool open(const void *blob, size_t length) {
        data_ = nullptr; bytes_ = count_ = 0;
        if (!blob || length < 48 || length > Maximum) return false;
        const auto *p = static_cast<const uint8_t *>(blob);
        const auto entries = word(p + 8);
        if (word(p) != Magic || word(p + 4) != 1 || !entries || entries > 101 ||
            word(p + 12) != length || word(p + 16) != 16 || word(p + 20) || word(p + 24) || word(p + 28) ||
            32 + entries * 16 > length) return false;
        uint64_t end = 32 + uint64_t(entries) * 16, previous = 0;
        for (unsigned i = 0; i < entries; ++i) {
            const auto *e = p + 32 + i * 16;
            const auto id = word(e), offset = word(e + 4), size = word(e + 8);
            if ((i ? id > 99 || (i > 1 && id <= previous) : id != MDT) || word(e + 12) || !size ||
                (offset & 15) || offset < end || offset > length || size > length - offset) return false;
            previous = id; end = uint64_t(offset) + size;
        }
        if (end != length) return false;
        data_ = p; bytes_ = uint32_t(length); count_ = entries;
        return true;
    }
    uint32_t bytes() const { return bytes_; }
    uint32_t count() const { return count_; }
    Bytes file(uint32_t id) const {
        if (!data_) return {};
        for (unsigned i = 0; i < count_; ++i) {
            const auto *e = data_ + 32 + i * 16;
            if (word(e) == id) return {data_ + word(e + 4), word(e + 8)};
        }
        return {};
    }
};

// ELF32 program headers as used by Qualcomm: p_flags bits 24..26 carry the
// segment type (2 = hash), bit 27 marks a relocatable image, and p_paddr is
// the link-time address. The hash segment is not loaded as code; PAS receives
// a copy of the ELF header followed immediately by the hash segment.
class Image {
    struct Segment { uint32_t address, memory, bytes, index; Bytes source; };
    Segment segments_[100]{};
    Bytes header_{}, hash_{};
    uint32_t count_ = 0, first_ = 0, extent_ = 0, metadata_ = 0, alignment_ = 4096;
    bool relocate_ = false, valid_ = false;

    static uint16_t half(const uint8_t *p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
    static uint32_t word(const uint8_t *p) {
        return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    }
    static bool range(uint64_t size, uint64_t start, uint64_t length) {
        return start <= size && length <= size - start;
    }
    static void copy(uint8_t *out, const uint8_t *source, uint32_t bytes) {
        for (uint32_t i = 0; i < bytes; ++i) out[i] = source[i];
    }
public:
    static constexpr uint32_t MaximumMetadata = 4 * 1024 * 1024, MaximumImage = 16 * 1024 * 1024;

    bool parse(const Archive &archive) {
        valid_ = false; count_ = first_ = extent_ = metadata_ = 0; alignment_ = 4096; relocate_ = false;
        const auto mdt = archive.file(Archive::MDT);
        if (!mdt.data || mdt.bytes < 52 || mdt.bytes > Archive::Maximum) return false;
        const auto *p = mdt.data;
        if (word(p) != 0x464c457f || p[4] != 1 || p[5] != 1 || p[6] != 1 || word(p + 20) != 1 ||
            word(p + 28) != 52 || half(p + 40) != 52 || half(p + 42) != 32) return false;
        const unsigned phnum = half(p + 44);
        if (phnum < 3 || phnum > 100 || !range(mdt.bytes, 52, phnum * 32)) return false;
        const auto *ph = p + 52;
        const uint32_t headerBytes = word(ph + 16);
        if (word(ph) == 1 || word(ph + 4) || headerBytes < 52 + phnum * 32 || headerBytes > mdt.bytes ||
            headerBytes > MaximumMetadata) return false;
        bool split = false; unsigned hashIndex = 0;
        for (unsigned i = 0; i < phnum; ++i) {
            const auto *entry = ph + i * 32;
            if (word(entry + 16) && !range(mdt.bytes, word(entry + 4), word(entry + 16))) split = true;
            if ((word(entry + 24) & (7u << 24)) == (2u << 24)) {
                if (!i || hashIndex) return false;
                hashIndex = i;
            }
        }
        if (!hashIndex) return false;
        const auto *hashEntry = ph + hashIndex * 32;
        const uint32_t hashBytes = word(hashEntry + 16);
        if (!hashBytes || hashBytes > MaximumMetadata - headerBytes) return false;
        header_ = {p, headerBytes}; metadata_ = headerBytes + hashBytes;
        if (mdt.bytes == metadata_) hash_ = {p + headerBytes, hashBytes};
        else if (range(mdt.bytes, word(hashEntry + 4), hashBytes)) hash_ = {p + word(hashEntry + 4), hashBytes};
        else { hash_ = archive.file(hashIndex); if (hash_.bytes != hashBytes) return false; }

        uint64_t low = UINT32_MAX, high = 0;
        for (unsigned i = 0; i < phnum; ++i) {
            const auto *e = ph + i * 32;
            const uint32_t type = word(e), flags = word(e + 24), memory = word(e + 20);
            if (type != 1 || (flags & (7u << 24)) == (2u << 24) || !memory) continue;
            const uint32_t address = word(e + 12), size = word(e + 16), align = word(e + 28);
            const uint64_t end = uint64_t(address) + memory;
            if (size > memory || end > (1ULL << 32) ||
                (align > 1 && (align & (align - 1))) || align > MaximumImage) return false;
            for (unsigned j = 0; j < count_; ++j)
                if (address < uint64_t(segments_[j].address) + segments_[j].memory &&
                    segments_[j].address < end) return false;
            Bytes source{};
            if (size) {
                if (split) {
                    source = archive.file(i);
                    if (!source.data || source.bytes != size) return false;
                } else {
                    if (!range(mdt.bytes, word(e + 4), size)) return false;
                    source = {p + word(e + 4), size};
                }
            }
            segments_[count_++] = {address, memory, size, i, source};
            if (address < low) low = address;
            if (end > high) high = end;
            if (align > alignment_) alignment_ = align;
            relocate_ |= flags & (1u << 27);
        }
        high = (high + 4095) & ~4095ULL;
        // A relocatable image links at a page-aligned offset (the A540 ZAP uses
        // 0x5000) and its alignment applies to the destination region, as in
        // qcom_mdt_load(); a fixed image must keep its link address.
        if (!count_ || (low & 4095) || high <= low || high - low > MaximumImage ||
            (!relocate_ && low == 0)) return false;
        first_ = uint32_t(low); extent_ = uint32_t(high - low); valid_ = true;
        return true;
    }

    bool valid() const { return valid_; }
    uint32_t segmentCount() const { return valid_ ? count_ : 0; }
    uint32_t metadataBytes() const { return valid_ ? metadata_ : 0; }
    uint32_t imageBytes() const { return valid_ ? extent_ : 0; }
    uint32_t physicalBase() const { return valid_ ? first_ : 0; }
    uint32_t imageAlignment() const { return valid_ ? alignment_ : 0; }
    bool relocatable() const { return relocate_; }

    bool metadataInto(void *destination, uint32_t bytes) const {
        if (!valid_ || !destination || bytes < metadata_) return false;
        auto *out = static_cast<uint8_t *>(destination);
        copy(out, header_.data, header_.bytes);
        copy(out + header_.bytes, hash_.data, hash_.bytes);
        return true;
    }
    bool imageInto(void *destination, uint32_t bytes, uint64_t physical) const {
        // Link-time addresses are 32-bit, but the destination is a physical
        // region and the SMC64 PAS ABI carries a 40-bit physical address: on
        // this board PMS init and mem_setup both accepted a region above 4 GiB,
        // so the earlier 32-bit destination bound only blocked our own copy.
        if (!valid_ || !destination || bytes < extent_ || !physical || (physical & 4095) ||
            physical > (1ULL << 40) - extent_ ||
            (relocate_ ? (physical & (uint64_t(alignment_) - 1)) != 0 : physical != first_)) return false;
        auto *out = static_cast<uint8_t *>(destination);
        for (uint32_t i = 0; i < extent_; ++i) out[i] = 0;  // BSS, holes and final padding
        for (unsigned i = 0; i < count_; ++i) {
            const auto &s = segments_[i];
            if (s.bytes) copy(out + (s.address - first_), s.source.data, s.bytes);
        }
        return true;
    }
};

}  // namespace CheeseburgerZap
