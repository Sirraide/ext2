#include <ext2++/core.hh>
#include <fmt/chrono.h>

#define DEBUG_EXT2

#ifdef DEBUG_EXT2
#    define DEBUG(fmt, ...) Log(fmt, __VA_ARGS__)
#else
#    define DEBUG(fmt, ...)
#endif
namespace Ext2 {

namespace {
/// Wrapper around read.
isz ReadReentrant(FdType fd, void* Dest, usz Size) {
    auto* DestPtr = reinterpret_cast<u8*>(Dest);
    const usz Sz = Size;
    while (Size > 0) {
        auto BytesRead = read(fd, DestPtr, Size);
        if (BytesRead < 0) {
            if (errno == EINTR or errno == EAGAIN) continue;
            return BytesRead;
        }
        if (BytesRead == 0) return isz(Size);
        DestPtr += BytesRead;
        Size -= usz(BytesRead);
    }
    return isz(Sz);
}

/// Wrapper around write.
isz WriteReentrant(FdType fd, const void* Src, usz Size) {
    auto* SrcPtr = static_cast<const u8*>(Src);
    const usz Sz = Size;
    while (Size > 0) {
        auto BytesWritten = write(fd, SrcPtr, Size);
        if (BytesWritten < 0) {
            if (errno == EINTR or errno == EAGAIN) continue;
            return BytesWritten;
        }
        if (BytesWritten == 0) return isz(Size);
        SrcPtr += BytesWritten;
        Size -= usz(BytesWritten);
    }
    return isz(Sz);
}

/// Read data from a file.
bool Read(FdType Fd, isz Offs, void* Dest, usz Size) {
    /// Seek to the offset.
    if (lseek64(Fd, Offs, SEEK_SET) < 0) {
        Log("Failed to seek in file: {}\n", strerror(errno));
        return false;
    }

    /// Read from the file.
    auto BytesRead = ReadReentrant(Fd, Dest, Size);
    if (BytesRead < 0) {
        Log("Failed to read from file: {}\n", strerror(errno));
        return false;
    }
    if (usz(BytesRead) != Size) {
        Log("Failed to read from file: Unexpected EOF\n");
        return false;
    }
    return true;
}

/// Write data to a file.
bool Write(FdType Fd, isz Offs, void const* Src, usz Size) {
    /// Seek to the offset.
    if (lseek64(Fd, Offs, SEEK_SET) < 0) {
        Log("Failed to seek in file: {}\n", strerror(errno));
        return false;
    }

    /// Write to the file.
    auto BytesWritten = WriteReentrant(Fd, Src, Size);
    if (BytesWritten < 0 or usz(BytesWritten) != Size) {
        Log("Failed to write to file: {}\n", strerror(errno));
        return false;
    }
    return true;
}

/// ===========================================================================
///  Constants and enums.
/// ===========================================================================
/// EXT2 magic number.
constexpr inline usz SUPERBLOCK_OFFSET = 1024;
constexpr inline u16 EXT2_SUPER_MAGIC = 0xEF53;

template <typename T>
requires std::is_enum_v<T>
constexpr inline auto operator&(std::underlying_type_t<T> Lhs, T Rhs) {
    return Lhs & std::underlying_type_t<T>(Rhs);
}

} // namespace

/// Attempt to mount a drive.
auto Drive::TryMount(FdType Fd) -> std::unique_ptr<Drive> {
    /// Read the superblock.
    Superblock sb{};
    if (not Read(Fd, SUPERBLOCK_OFFSET, &sb, sizeof sb)) {
        Log("Drive is too small to contain a valid ext2 filesystem.\n");
        return nullptr;
    }

    /// Validate the superblock.
    if (sb.s_magic != EXT2_SUPER_MAGIC) {
        Log("Invalid magic number: 0x{:04x}\n", sb.s_magic);
        return nullptr;
    }

    /// Check for incompatible or read-only features.
    if (sb.s_feature_incompat or sb.s_feature_ro_compat) {
        Log("Incompatible or read-only features are enabled. Refusing to mount\n");
        return nullptr;
    }

    /// Check for errors.
    if (sb.s_state == FsState::HasErrors) {
        Log("Filesystem has errors. Refusing to mount\n");
        return nullptr;
    }

    /// Set the error flag. We'll clear it when we unmount the drive.
    sb.s_state = FsState::HasErrors;

    /// Create the drive.
    return std::unique_ptr<Drive>{::new Drive(Fd, std::move(sb))};
}

Drive::Drive(FdType Fd_, Superblock&& Sb_) : FileHandle(Fd_), Sb(std::move(Sb_)) {
    [[maybe_unused]] auto FormatErrorHandling = [](ErrorHandling e) {
        switch (e) {
            case ErrorHandling::Ignore: return "Ignore";
            case ErrorHandling::RemountReadOnly: return "Remount read-only";
            case ErrorHandling::KernelPanic: return "Kernel panic";
        }
        return "Unknown";
    };

    [[maybe_unused]] auto FormatRevisionLevel = [](RevisionLevel r) {
        switch (r) {
            case RevisionLevel::GoodOldRev: return "Good old revision 0";
            case RevisionLevel::DynamicRev: return "Dynamic revision";
        }
        return "Unknown";
    };

    [[maybe_unused]] auto FormatCreatorOS = [](CreatorOS os) {
        switch (os) {
            case CreatorOS::Linux: return "Linux";
            case CreatorOS::Hurd: return "GNU Hurd";
            case CreatorOS::Masix: return "Masix";
            case CreatorOS::FreeBSD: return "FreeBSD";
            case CreatorOS::Lites: return "Lites";
        }
        return "Unknown";
    };

    [[maybe_unused]] auto FormatUUID = [](const u8* UUID) {
        return fmt::format(
            "{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-"
            "{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
            UUID[0], UUID[1], UUID[2], UUID[3], UUID[4], UUID[5], UUID[6],
            UUID[7], UUID[8], UUID[9], UUID[10], UUID[11], UUID[12], UUID[13],
            UUID[14], UUID[15]);
    };

    DEBUG(
        "Mounting Ext2 drive with\n"
        "    inodes:           {} ({} free)\n"
        "    blocks:           {} ({} free)\n"
        "    block groups:     {}\n"
        "    inodes per group: {}\n"
        "    blocks per group: {}\n"
        "    inode size:       {} bytes\n"
        "    block size:       {} bytes\n"
        "    last mount time:  {:%Y/%m/%d %T} UTC\n"
        "    mount count:      {}\n"
        "    error handling:   {}\n"
        "    minor revision:   {}\n"
        "    revision:         {}\n"
        "    last check time:  {:%Y/%m/%d %T} UTC\n"
        "    check interval:   {}\n"
        "    created on:       {}\n"
        "    resuid/resgid:    {}/{}\n"
        "    volume uuid:      {}\n"
        "    volume name:      {}\n"
        "    last mount path:  {}\n"
        "    prealloc blocks:  {}\n"
        "    prealloc dirs:    {}\n"
        "    compression algorithms:\n"
        "        LZV1:   {}\n"
        "        LZRW3A: {}\n"
        "        GZIP:   {}\n"
        "        BZIP2:  {}\n"
        "        LZO:    {}\n"
        "    compatible features:\n"
        "        DIR_PREALLOC:  {}\n"
        "        IMAGIC_INODES: {}\n"
        "        HAS_JOURNAL:   {}\n"
        "        EXT_ATTR:      {}\n"
        "        RESIZE_INO:    {}\n"
        "        DIR_INDEX:     {}\n"
        "    incompatible features:\n"
        "        COMPRESSION:   {}\n"
        "        FILETYPE:      {}\n"
        "        RECOVER:       {}\n"
        "        JOURNAL_DEV:   {}\n"
        "        META_BG:       {}\n"
        "    read-only features:\n"
        "        SPARSE_SUPER:  {}\n"
        "        LARGE_FILE:    {}\n"
        "        BTREE_DIR:     {}\n",
        Sb.s_inodes_count, Sb.s_free_inodes_count,
        Sb.s_blocks_count, Sb.s_free_blocks_count,
        Sb.block_groups(),
        Sb.s_inodes_per_group,
        Sb.s_blocks_per_group,
        Sb.s_inode_size,
        Sb.block_size(),
        std::chrono::system_clock::from_time_t(std::time_t(Sb.s_mtime)),
        Sb.s_mnt_count,
        FormatErrorHandling(Sb.s_errors),
        Sb.s_minor_rev_level,
        FormatRevisionLevel(Sb.s_rev_level),
        std::chrono::system_clock::from_time_t(std::time_t(Sb.s_lastcheck)),
        Sb.s_checkinterval,
        FormatCreatorOS(Sb.s_creator_os),
        Sb.s_def_resuid, Sb.s_def_resgid,
        FormatUUID(Sb.s_uuid),
        std::string_view{(char*) &Sb.s_volume_name[0], sizeof Sb.s_volume_name / sizeof Sb.s_volume_name[0]},
        std::string_view{(char*) &Sb.s_last_mounted[0], sizeof Sb.s_last_mounted / sizeof Sb.s_last_mounted[0]},
        Sb.s_prealloc_blocks,
        Sb.s_prealloc_dir_blocks,
        Sb.s_algo_bitmap & CompressionAlgorithm::LZV1 ? "yes" : "no",
        Sb.s_algo_bitmap & CompressionAlgorithm::LZRW3A ? "yes" : "no",
        Sb.s_algo_bitmap & CompressionAlgorithm::GZIP ? "yes" : "no",
        Sb.s_algo_bitmap & CompressionAlgorithm::BZIP2 ? "yes" : "no",
        Sb.s_algo_bitmap & CompressionAlgorithm::LZO ? "yes" : "no",
        Sb.s_feature_compat & CompatFeature::DirPrealloc ? "yes" : "no",
        Sb.s_feature_compat & CompatFeature::ImagicInodes ? "yes" : "no",
        Sb.s_feature_compat & CompatFeature::HasJournal ? "yes" : "no",
        Sb.s_feature_compat & CompatFeature::ExtAttr ? "yes" : "no",
        Sb.s_feature_compat & CompatFeature::ResizeIno ? "yes" : "no",
        Sb.s_feature_compat & CompatFeature::DirIndex ? "yes" : "no",
        Sb.s_feature_incompat & IncompatFeature::Compression ? "yes" : "no",
        Sb.s_feature_incompat & IncompatFeature::FileType ? "yes" : "no",
        Sb.s_feature_incompat & IncompatFeature::Recover ? "yes" : "no",
        Sb.s_feature_incompat & IncompatFeature::JournalDev ? "yes" : "no",
        Sb.s_feature_incompat & IncompatFeature::MetaBg ? "yes" : "no",
        Sb.s_feature_ro_compat & RoFeature::SparseSuper ? "yes" : "no",
        Sb.s_feature_ro_compat & RoFeature::LargeFile ? "yes" : "no",
        Sb.s_feature_ro_compat & RoFeature::BtreeDir ? "yes" : "no"
    );

    /// Set the last mount time.
    Sb.s_mtime = (u32) std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    /// Increment the mount count.
    Sb.s_mnt_count++;
}

Drive::~Drive() {
    /// TODO: Unmount and sync.
    /// Write the superblock back to disk.
    if (FileHandle) {
        Sb.s_state = FsState::Valid;
        Write(FileHandle, SUPERBLOCK_OFFSET, &Sb, sizeof Sb);
        close(FileHandle);
    }
}

/// Stat a file.
auto Drive::Stat(u32 InodeNumber) -> struct stat {
    return {};
    //stat s;
}

} // namespace Ext2