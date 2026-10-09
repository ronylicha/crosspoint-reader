#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "LibraryFormat.h"

using namespace library;

namespace {

// A header for N books whose sections are laid out consistently, i.e. one that
// validateHeader() must accept. Tests then damage exactly one thing.
ClixHeader makeHeader(const uint16_t books, const uint32_t folderBytes = 300, const uint32_t nameBytes = 0) {
  ClixHeader h{};
  memcpy(h.magic, CLIX_MAGIC, sizeof(CLIX_MAGIC));
  h.formatVersion = CLIX_FORMAT_VERSION;
  h.foldVersion = CLIX_FOLD_VERSION;
  h.bookCount = books;
  h.seriesGroupCount = books > 0 ? 1 : 0;
  h.folderCount = 4;
  layoutSections(h, folderBytes, nameBytes == 0 ? books * 80u : nameBytes);
  return h;
}

}  // namespace

TEST(LibraryFormat, StructSizesAreFrozen) {
  // These are the on-disk contract. A compiler that pads them silently would
  // produce an index this build writes and no other build can read.
  EXPECT_EQ(sizeof(ClixHeader), 64u);
  EXPECT_EQ(sizeof(ClixRecord), 128u);
  EXPECT_EQ(sizeof(ClixFolderHeader), 1u);
  EXPECT_EQ(CLIX_FORMAT_VERSION, 3u);
  EXPECT_EQ(CLIX_LEGACY_FORMAT_VERSION, 2u);
}

TEST(LibraryFormat, RecordsTileSectorsExactly) {
  // The whole streaming design rests on this: 32 records fill a 4096-byte
  // buffer with nothing left over, so a scan never has to handle a record split
  // across two reads.
  EXPECT_EQ(4096u % sizeof(ClixRecord), 0u);
  EXPECT_EQ(4096u / sizeof(ClixRecord), 32u);
  EXPECT_EQ(CLIX_ALIGN % sizeof(ClixRecord), 0u);
}

TEST(LibraryFormat, EverySectionStartsOnASectorBoundary) {
  for (const uint16_t n :
       {uint16_t{0}, uint16_t{1}, uint16_t{3}, uint16_t{60}, uint16_t{85}, uint16_t{86}, uint16_t{200}, uint16_t{255},
        uint16_t{256}, uint16_t{257}, uint16_t{2000}, uint16_t{4096}}) {
    const ClixHeader h = makeHeader(n, 29u * 4u);
    EXPECT_EQ(h.folderStart % CLIX_ALIGN, 0u) << "n=" << n;
    EXPECT_EQ(h.recordStart % CLIX_ALIGN, 0u) << "n=" << n;
    EXPECT_EQ(h.permStart % CLIX_ALIGN, 0u) << "n=" << n;
    EXPECT_EQ(h.seriesGroupsStart % CLIX_ALIGN, 0u) << "n=" << n;
    EXPECT_EQ(h.nameStart % CLIX_ALIGN, 0u) << "n=" << n;
  }
}

TEST(LibraryFormat, SectionsDoNotOverlap) {
  const ClixHeader h = makeHeader(200, 29u * 50u);
  EXPECT_GE(h.folderStart, sizeof(ClixHeader));
  EXPECT_GE(h.recordStart, h.folderStart + h.folderLen);
  EXPECT_GE(h.permStart, h.recordStart + 200u * sizeof(ClixRecord));
  EXPECT_GE(h.seriesGroupsStart, h.permStart + 200u * 3u * sizeof(uint16_t));
  EXPECT_GE(h.nameStart, h.seriesGroupsStart + 200u * sizeof(uint16_t));
  EXPECT_EQ(h.selfSize, h.nameStart + h.nameLen);
}

TEST(LibraryFormat, RecordOffsetsAreAlignedAndOrdered) {
  const ClixHeader h = makeHeader(64, 116);
  EXPECT_EQ(recordOffset(h, 0), h.recordStart);
  EXPECT_EQ(recordOffset(h, 1), h.recordStart + 128u);
  EXPECT_EQ(recordOffset(h, 63), h.recordStart + 63u * 128u);
  // Every 4th record starts on a sector boundary, by construction.
  for (uint16_t k = 0; k < 64; k += 4) EXPECT_EQ(recordOffset(h, k) % CLIX_ALIGN, 0u);
}

TEST(LibraryFormat, PermutationArraysDoNotOverlapEachOther) {
  const ClixHeader h = makeHeader(100, 116);
  EXPECT_EQ(authorOrderOffset(h, 0), h.permStart);
  EXPECT_EQ(authorOrderOffset(h, 99), h.permStart + 198u);
  EXPECT_EQ(arrivalOrderOffset(h, 0), h.permStart + 200u);
  EXPECT_GT(arrivalOrderOffset(h, 0), authorOrderOffset(h, h.bookCount - 1));
  EXPECT_EQ(seriesOrderOffset(h, 0), h.permStart + 400u);
  EXPECT_EQ(seriesOrderOffset(h, 99), h.permStart + 598u);
  EXPECT_GT(seriesOrderOffset(h, 0), arrivalOrderOffset(h, h.bookCount - 1));
  EXPECT_GT(h.seriesGroupsStart, seriesOrderOffset(h, h.bookCount - 1));
}

TEST(LibraryFormat, SizeArithmeticMatchesTheSpecTable) {
  // Sector-padded sections: header, folders, fixed records, three permutations,
  // the capacity-N series directory, and variable names.
  ClixHeader h{};
  memcpy(h.magic, CLIX_MAGIC, sizeof(CLIX_MAGIC));
  h.formatVersion = CLIX_FORMAT_VERSION;
  h.foldVersion = CLIX_FOLD_VERSION;
  h.bookCount = 200;
  h.seriesGroupCount = 1;
  layoutSections(h, 29u * 50u, 80u * 200u);
  EXPECT_EQ(h.folderStart, 512u);
  EXPECT_EQ(h.recordStart, 2048u);
  EXPECT_EQ(h.permStart, 2048u + 25600u);
  const uint32_t permutationBytes = alignUp(200u * 3u * sizeof(uint16_t));
  const uint32_t groupBytes = alignUp(200u * sizeof(uint16_t));
  EXPECT_EQ(h.seriesGroupsStart, h.permStart + permutationBytes);
  EXPECT_EQ(h.nameStart, h.seriesGroupsStart + groupBytes);
  EXPECT_EQ(h.selfSize, 512u + 1536u + 25600u + permutationBytes + groupBytes + 16000u);
}

TEST(LibraryFormatValidation, AcceptsAWellFormedHeader) {
  const ClixHeader h = makeHeader(60, 116);
  EXPECT_EQ(validateHeader(h, h.selfSize), ClixValidity::Ok);
}

TEST(LibraryFormatValidation, RejectsBadMagic) {
  ClixHeader h = makeHeader(60, 116);
  h.magic[3] = '2';
  EXPECT_EQ(validateHeader(h, h.selfSize), ClixValidity::BadMagic);
  ClixHeader zero{};
  EXPECT_EQ(validateHeader(zero, 0), ClixValidity::BadMagic);
}

TEST(LibraryFormatValidation, RejectsUnknownVersionsSeparately) {
  ClixHeader h = makeHeader(60, 116);
  h.formatVersion = library::CLIX_FORMAT_VERSION + 1;
  EXPECT_EQ(validateHeader(h, h.selfSize), ClixValidity::UnknownFormatVersion);

  // A fold change is recoverable — firstSeen is preserved across the rebuild —
  // so it must be distinguishable from an unreadable format.
  h = makeHeader(60, 116);
  h.foldVersion = CLIX_FOLD_VERSION + 1;
  EXPECT_EQ(validateHeader(h, h.selfSize), ClixValidity::StaleFoldVersion);
  EXPECT_EQ(validateHeaderStructure(h, h.selfSize), ClixValidity::Ok);

  // Reconciliation may ignore only the fold version, never damaged layout.
  EXPECT_EQ(validateHeaderStructure(h, h.selfSize - 1), ClixValidity::SizeMismatch);
}

TEST(LibraryFormatValidation, RejectsLengthsBeyondTheFile) {
  // folderLen and nameLen are attacker bytes; near-2^32 values used to wrap
  // the section sums and re-derive the same wrapped layout on both sides.
  ClixHeader h = makeHeader(60, 116);
  h.folderLen = 0xFFFFFF00u;
  EXPECT_EQ(validateHeader(h, h.selfSize), ClixValidity::SectionsInconsistent);
  h = makeHeader(60, 116);
  h.nameLen = 0xFFFFFF00u;
  EXPECT_EQ(validateHeader(h, h.selfSize), ClixValidity::SectionsInconsistent);
}

TEST(LibraryFormatValidation, RejectsTruncationInBothDirections) {
  const ClixHeader h = makeHeader(60, 116);
  // Power loss mid-build: the file is short.
  EXPECT_EQ(validateHeader(h, h.selfSize - 1), ClixValidity::SizeMismatch);
  EXPECT_EQ(validateHeader(h, h.selfSize / 2), ClixValidity::SizeMismatch);
  EXPECT_EQ(validateHeader(h, 0), ClixValidity::SizeMismatch);
  // Longer than declared is equally wrong: a stale tail from a previous build.
  EXPECT_EQ(validateHeader(h, h.selfSize + 512), ClixValidity::SizeMismatch);
}

TEST(LibraryFormatValidation, RejectsTamperedOffsets) {
  ClixHeader h = makeHeader(60, 116);
  h.recordStart += CLIX_ALIGN;  // plausible, aligned, and wrong
  EXPECT_EQ(validateHeader(h, h.selfSize), ClixValidity::SectionsInconsistent);
}

TEST(LibraryFormatValidation, RejectsAnImpossibleBookCount) {
  ClixHeader h = makeHeader(60, 116);
  h.bookCount = CLIX_MAX_RECORDS + 1;
  EXPECT_EQ(validateHeader(h, h.selfSize), ClixValidity::CountOutOfRange);
}

TEST(LibraryFormatValidation, RejectsInvalidMetadataMode) {
  ClixHeader h = makeHeader(60, 116);
  h.metadataEnabled = 2;
  EXPECT_EQ(validateHeader(h, h.selfSize), ClixValidity::SectionsInconsistent);
}

TEST(LibraryFormatValidation, AcceptsAnEmptyLibrary) {
  // A card with no books must produce a valid index, not a rebuild every boot.
  ClixHeader h = makeHeader(0, 0, 1);
  h.nameLen = 0;
  layoutSections(h, 0, 0);
  EXPECT_EQ(validateHeader(h, h.selfSize), ClixValidity::Ok);
  EXPECT_EQ(h.selfSize, CLIX_ALIGN);
}

TEST(LibraryHeaderFlags, DedupDegradationIsPersistedWithoutChangingTheLayout) {
  ClixHeader h = makeHeader(60, 116);
  h.flags = CLIX_FLAG_DEDUP_DEGRADED;

  EXPECT_EQ(validateHeader(h, h.selfSize), ClixValidity::Ok);
  EXPECT_NE(h.flags & CLIX_FLAG_DEDUP_DEGRADED, 0);
  EXPECT_EQ(sizeof(ClixHeader), 64u);
}

TEST(LibraryFormat, ByteImageIsStableAcrossBuilds) {
  // Guards the packing itself: if a compiler ever inserts padding, these field
  // offsets move and the on-disk format silently forks.
  EXPECT_EQ(offsetof(ClixRecord, nameOff), 0u);
  EXPECT_EQ(offsetof(ClixRecord, fileSize), 4u);
  EXPECT_EQ(offsetof(ClixRecord, firstSeen), 8u);
  EXPECT_EQ(offsetof(ClixRecord, folderId), 10u);
  EXPECT_EQ(offsetof(ClixRecord, nameLen), 12u);
  EXPECT_EQ(offsetof(ClixRecord, foldLen), 13u);
  EXPECT_EQ(offsetof(ClixRecord, authorKeyLen), 14u);
  EXPECT_EQ(offsetof(ClixRecord, metadataStatus), 15u);
  EXPECT_EQ(offsetof(ClixRecord, fold), 16u);
  EXPECT_EQ(offsetof(ClixRecord, authorKey), 112u);
  EXPECT_EQ(offsetof(ClixRecord, modificationTime), 124u);

  EXPECT_EQ(offsetof(ClixHeader, metadataEnabled), 7u);
  EXPECT_EQ(offsetof(ClixHeader, bookCount), 8u);
  EXPECT_EQ(offsetof(ClixHeader, folderStart), 16u);
  EXPECT_EQ(offsetof(ClixHeader, selfSize), 40u);
  EXPECT_EQ(offsetof(ClixHeader, seriesGroupsStart), 44u);
  EXPECT_EQ(offsetof(ClixHeader, seriesGroupCount), 48u);
  EXPECT_EQ(offsetof(ClixHeader, reserved), 50u);
}

TEST(LibraryFormat, SeriesDirectoryReservesOneEntryPerBookRegardlessOfGroupCount) {
  for (const uint16_t books :
       {uint16_t{1}, uint16_t{85}, uint16_t{86}, uint16_t{255}, uint16_t{256}, uint16_t{257}, uint16_t{4096}}) {
    ClixHeader h = makeHeader(books, 116);
    ASSERT_EQ(validateHeader(h, h.selfSize), ClixValidity::Ok);
    EXPECT_EQ(h.seriesGroupsStart, alignUp(h.permStart + books * 3u * sizeof(uint16_t)));
    EXPECT_EQ(h.nameStart, alignUp(h.seriesGroupsStart + books * sizeof(uint16_t)));
    const uint32_t nameStart = h.nameStart;
    h.seriesGroupCount = books;
    layoutSections(h, h.folderLen, h.nameLen);
    EXPECT_EQ(h.nameStart, nameStart);
    EXPECT_EQ(validateHeader(h, h.selfSize), ClixValidity::Ok);
  }
}

TEST(LibraryFormatValidation, RejectsMissingOrExcessSeriesGroups) {
  ClixHeader h = makeHeader(13);
  h.seriesGroupCount = 0;
  EXPECT_EQ(validateHeaderStructure(h, h.selfSize), ClixValidity::CountOutOfRange);
  EXPECT_EQ(validateHeaderStructureForReconciliation(h, h.selfSize), ClixValidity::CountOutOfRange);
  h.seriesGroupCount = 14;
  EXPECT_EQ(validateHeader(h, h.selfSize), ClixValidity::CountOutOfRange);
  h = makeHeader(0);
  h.seriesGroupCount = 1;
  EXPECT_EQ(validateHeader(h, h.selfSize), ClixValidity::CountOutOfRange);
}

TEST(LibraryFormatValidation, RejectsTamperedSeriesDirectoryOffsets) {
  ClixHeader h = makeHeader(86, 116);
  h.seriesGroupsStart += CLIX_ALIGN;
  EXPECT_EQ(validateHeaderStructure(h, h.selfSize), ClixValidity::SectionsInconsistent);
  EXPECT_EQ(validateHeaderStructureForReconciliation(h, h.selfSize), ClixValidity::SectionsInconsistent);
  h = makeHeader(86, 116);
  h.seriesGroupsStart = h.permStart;
  EXPECT_EQ(validateHeader(h, h.selfSize), ClixValidity::SectionsInconsistent);
  h = makeHeader(86, 116);
  h.nameStart = h.seriesGroupsStart;
  EXPECT_EQ(validateHeader(h, h.selfSize), ClixValidity::SectionsInconsistent);
}

TEST(LibraryFormatValidation, LegacyTwoPermutationLayoutIsAcceptedOnlyForReconciliation) {
  ClixHeader legacy = makeHeader(200, 29u * 50u, 80u * 200u);
  legacy.formatVersion = CLIX_LEGACY_FORMAT_VERSION;
  layoutSections(legacy, legacy.folderLen, legacy.nameLen);
  EXPECT_EQ(legacy.seriesGroupsStart, 0u);
  EXPECT_EQ(legacy.nameStart, legacy.permStart + alignUp(200u * 2u * sizeof(uint16_t)));
  EXPECT_EQ(legacy.selfSize, 44672u);
  EXPECT_EQ(validateHeaderStructure(legacy, legacy.selfSize), ClixValidity::UnknownFormatVersion);
  EXPECT_EQ(validateHeader(legacy, legacy.selfSize), ClixValidity::UnknownFormatVersion);
  EXPECT_EQ(validateHeaderStructureForReconciliation(legacy, legacy.selfSize), ClixValidity::Ok);
  // These 20 bytes were reserved in v2; no interpretation may reject history.
  memset(reinterpret_cast<uint8_t*>(&legacy) + offsetof(ClixHeader, seriesGroupsStart), 0xFF, 20);
  legacy.foldVersion = CLIX_FOLD_VERSION + 1;
  EXPECT_EQ(validateHeaderStructureForReconciliation(legacy, legacy.selfSize), ClixValidity::Ok);
  legacy.recordStart += CLIX_ALIGN;
  EXPECT_EQ(validateHeaderStructureForReconciliation(legacy, legacy.selfSize), ClixValidity::SectionsInconsistent);
}

TEST(LibraryFormatValidation, LegacyEmptyAndMaximumLibrariesRetainTheirOriginalLayout) {
  for (const uint16_t books : {uint16_t{0}, uint16_t{4096}}) {
    ClixHeader h = makeHeader(books, 0);
    h.formatVersion = CLIX_LEGACY_FORMAT_VERSION;
    layoutSections(h, 0, h.nameLen);
    EXPECT_EQ(h.nameStart, alignUp(h.permStart + books * 2u * sizeof(uint16_t)));
    EXPECT_EQ(validateHeaderStructureForReconciliation(h, h.selfSize), ClixValidity::Ok);
    EXPECT_EQ(validateHeaderStructure(h, h.selfSize), ClixValidity::UnknownFormatVersion);
    EXPECT_EQ(validateHeaderStructureForReconciliation(h, h.selfSize + 1), ClixValidity::SizeMismatch);
  }
}

TEST(LibraryFormatValidation, ReconciliationRejectsUnrecognizedFormats) {
  for (const uint8_t version : {uint8_t{0}, uint8_t{1}, uint8_t{4}, uint8_t{255}}) {
    ClixHeader h = makeHeader(1);
    h.formatVersion = version;
    EXPECT_EQ(validateHeaderStructureForReconciliation(h, h.selfSize), ClixValidity::UnknownFormatVersion);
  }
}

TEST(LibraryFormatValidation, WidenedValidationRejectsSelfConsistentWrappedSectionArithmetic) {
  for (const uint8_t version : {CLIX_LEGACY_FORMAT_VERSION, CLIX_FORMAT_VERSION}) {
    ClixHeader h = makeHeader(0);
    h.formatVersion = version;
    // Both lengths pass the file-size cap, while sector alignment wraps the
    // 32-bit writer arithmetic. The validator must derive the wider offsets.
    layoutSections(h, UINT32_MAX - CLIX_ALIGN, UINT32_MAX);
    ASSERT_EQ(h.recordStart, 0u);
    ASSERT_EQ(h.selfSize, UINT32_MAX);
    ASSERT_LE(h.folderLen, h.selfSize);
    ASSERT_LE(h.nameLen, h.selfSize);
    EXPECT_EQ(validateHeaderStructureForReconciliation(h, h.selfSize), ClixValidity::SectionsInconsistent);
    if (version == CLIX_FORMAT_VERSION) {
      EXPECT_EQ(validateHeaderStructure(h, h.selfSize), ClixValidity::SectionsInconsistent);
    }
  }
}
