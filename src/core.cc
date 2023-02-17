#include <ext2++/core.hh>
#include <fmt/chrono.h>
#include <optional>
#include <utility>

#define DEBUG_EXT2

#ifdef DEBUG_EXT2
#    define DEBUG(fmt, ...) Log(fmt, __VA_ARGS__)
#else
#    define DEBUG(fmt, ...)
#endif
namespace Ext2 {

/// ===========================================================================
///  FS Utils.
/// ===========================================================================
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
bool Read(FdType Fd, usz Offs, void* Dest, usz Size) {
    /// Seek to the offset.
    if (lseek64(Fd, isz(Offs), SEEK_SET) < 0) {
        Log("Failed to seek in file: {}", strerror(errno));
        return false;
    }

    /// Read from the file.
    auto BytesRead = ReadReentrant(Fd, Dest, Size);
    if (BytesRead < 0) {
        Log("Failed to read from file: {}", strerror(errno));
        return false;
    }
    if (usz(BytesRead) != Size) {
        Log("Failed to read from file: Unexpected EOF");
        return false;
    }
    return true;
}

/// Write data to a file.
bool Write(FdType Fd, isz Offs, void const* Src, usz Size) {
    /// Seek to the offset.
    if (lseek64(Fd, Offs, SEEK_SET) < 0) {
        Log("Failed to seek in file: {}", strerror(errno));
        return false;
    }

    /// Write to the file.
    auto BytesWritten = WriteReentrant(Fd, Src, Size);
    if (BytesWritten < 0 or usz(BytesWritten) != Size) {
        Log("Failed to write to file: {}", strerror(errno));
        return false;
    }
    return true;
}

/// Remove leading slashes from a path.
void RemoveLeadingSlashes(std::string_view& Path) {
    while (not Path.empty() and Path[0] == '/') Path.remove_prefix(1);
}

/// ===========================================================================
///  Constants and enums.
/// ===========================================================================
/// EXT2 magic number.
constexpr inline usz SUPERBLOCK_OFFSET = 1024;
constexpr inline u16 EXT2_SUPER_MAGIC = 0xEF53;
constexpr inline usz DIRECT_BLOCK_COUNT = 12;
constexpr inline usz INDIRECT_BLOCK_INDEX = 12;
constexpr inline usz DOUBLY_INDIRECT_BLOCK_INDEX = 13;
constexpr inline usz TRIPLY_INDIRECT_BLOCK_INDEX = 14;
constexpr inline InodeNumberType ROOT_INODE_NUMBER = 2;

template <typename T>
requires std::is_enum_v<T>
constexpr inline auto operator&(std::underlying_type_t<T> Lhs, T Rhs) {
    return Lhs & std::underlying_type_t<T>(Rhs);
}

} // namespace

/// ===========================================================================
///  Drive implementation.
/// ===========================================================================
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
            UUID[14], UUID[15]
        );
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

/// ===========================================================================
///  Inodes and other tables.
/// ===========================================================================
auto Drive::ComputeInodeOffset(u32 InodeNumber) -> std::optional<usz> {
    /// Check that the inode number is valid.
    if (InodeNumber == 0 or InodeNumber > Sb.s_inodes_count) return {};

    /// Determine the block group containing the inode.
    /// Note that inode numbers start at 1.
    /// The computed block group is zero-based.
    u32 BlockGroup = (InodeNumber - 1) / Sb.s_inodes_per_group;

    /// We also need the local index into the groups inode table.
    u32 LocalIndex = (InodeNumber - 1) % Sb.s_inodes_per_group;

    /// Read the block group descriptor.
    auto dt = ReadDescriptorTable(BlockGroup);
    if (not dt) return {};

    /// Finally, compute the offset of the inode.
    u32 Offset = dt->bg_inode_table * Sb.block_size() + LocalIndex * Sb.s_inode_size;
    return Offset;
}

auto Drive::FindDirectoryEntry(Inode& I, std::string_view Name) -> std::optional<LinkedDirEntryHeader> {
    if (not I.Is(Inode::Directory)) return {};
    std::vector<char> EntryName;
    usz Offset = 0;

    /// Iterate over all directory entries until we find the one we want.
    while (Offset < I.i_size) {
        /// Read the directory entry header.
        LinkedDirEntryHeader H{};
        if (not ReadInodeData(I, Offset, &H, sizeof H)) return {};

        /// Check if the name matches.
        if (H.name_len == Name.size()) {
            EntryName.clear();
            EntryName.resize(H.name_len);
            if (not ReadInodeData(I, Offset + sizeof H, EntryName.data(), H.name_len)) return {};
            if (std::string_view{EntryName.data(), EntryName.size()} == Name)
                return H;
        }

        /// Skip to the next entry.
        Offset += H.rec_len;
        if (H.rec_len == 0) break;
    }

    return {};
}

auto Drive::InodeFromPath(std::string_view Path, std::string_view OriginPath) -> std::optional<InodeNumberType> {
    /// Path may not be empty.
    if (Path.empty()) {
        Log("Cannot resolve empty path.");
        return {};
    }

    /// Absolute path.
    if (Path.starts_with("/")) {
        RemoveLeadingSlashes(Path);
        return InodeFromPath(Path, ROOT_INODE_NUMBER);
    }

    /// Relative Path.
    else {
        /// Origin cannot be empty as relative paths must be
        /// relative to something.
        if (OriginPath.empty()) {
            Log("Cannot resolve relative path without origin.");
            return {};
        }

        /// Origin must be absolute.
        if (not OriginPath.starts_with("/")) {
            Log("Origin must be absolute.");
            return {};
        }

        /// Get the origin inode number.
        auto OriginInode = InodeFromPath(OriginPath);
        if (not OriginInode) {
            Log("Failed to resolve origin path.");
            return {};
        }

        /// Resolve the path relative to the origin.
        return InodeFromPath(Path, *OriginInode);
    }
}

auto Drive::InodeFromPath(std::string_view Path, InodeNumberType Origin) -> std::optional<InodeNumberType> {
    if (not Path.empty()) {
        /// Invariant: Path is not empty at the beginning of
        /// each iteration of this loop.
        for (;;) {
            /// Get the next path component.
            auto Slash = Path.find('/');
            auto Component = Path.substr(0, Slash);
            Path.remove_prefix(Slash == std::string_view::npos ? Path.size() : Slash + 1);

            /// Get the origin inode.
            auto OriginInode = ReadInode(Origin);
            if (not OriginInode) {
                Log("Failed to read inode {}.", Origin);
                return {};
            }

            /// Inode must be a directory.
            if (not OriginInode->Is(Inode::Directory)) {
                Log("Inode is not a directory.");
                return {};
            }

            /// Get the directory entry.
            auto Entry = FindDirectoryEntry(*OriginInode, Component);
            if (not Entry) {
                Log("Failed to find entry {} in directory {}.", Component, Origin);
                return {};
            }

            /// That entry is our new origin.
            Origin = Entry->inode;

            /// If there are trailing slashes, then this inode
            /// must be a directory.
            if (Path.starts_with("/")) {
                if (auto FF = GetFileFormat(*Entry); not FF or *FF != Inode::Directory) {
                    Log("Inode is not a directory.");
                    return {};
                }
                RemoveLeadingSlashes(Path);
            }

            /// If there are no more path components, then we are done.
            if (Path.empty()) break;
        }
    }

    /// Return the current origin when we’re done walking the path.
    return Origin;
}

auto Drive::GetFileFormat(LinkedDirEntryHeader Hdr) -> std::optional<Inode::FileFormat> {
    if (Sb.s_rev_level == RevisionLevel::DynamicRev) {
        switch (Hdr.file_type) {
            case 0: return Inode::Unknown;
            case 1: return Inode::RegularFile;
            case 2: return Inode::Directory;
            case 3: return Inode::CharacterDevice;
            case 4: return Inode::BlockDevice;
            case 5: return Inode::Fifo;
            case 6: return Inode::Socket;
            case 7: return Inode::SymbolicLink;

            /// Invalid entry. Log and attempt to get the type from the inode.
            default:
                Log("Invalid file type {} in directory entry for {}.", Hdr.file_type, Hdr.inode);
                break;
        }
    }

    /// If the revision level is not dynamic, then we can only
    /// determine the file format by looking at the inode.
    auto Inode = ReadInode(Hdr.inode);
    if (not Inode) return {};
    return static_cast<Inode::FileFormat>(Inode->i_mode & Inode::FileFormatMask);
}

auto Drive::ReadDescriptorTable(u32 BlockGroupIndex) -> std::optional<BlockGroupDescriptor> {
    /// The descriptor table is stored in the first block group after the superblock.
    BlockGroupDescriptor Table;
    auto Offset = SUPERBLOCK_OFFSET + Sb.block_size() + BlockGroupIndex * sizeof Table;
    if (not Read(FileHandle, Offset, &Table, sizeof Table)) return {};
    return Table;
}

auto Drive::ReadInode(u32 InodeNumber) -> std::optional<Inode> {
    auto Offset = ComputeInodeOffset(InodeNumber);
    if (not Offset) return {};

    /// Read the inode.
    Inode Inode;
    if (not Read(FileHandle, *Offset, &Inode, sizeof Inode)) return {};
    return Inode;
}

bool Drive::ReadInodeData(Inode& I, usz Offset, void* BufferRaw, usz Size) { // clang-format off
    /// Compute the block index and offset into the block.
    auto Buffer = static_cast<u8*>(BufferRaw);
    auto BlockIndex = Offset / Sb.block_size();
    auto BlockOffset = Offset % Sb.block_size();

    const usz IndirectBlockCount = Sb.block_size() / sizeof(u32);
    const usz DoublyIndirectBlockCount = IndirectBlockCount * IndirectBlockCount;
    const usz TriplyIndirectBlockCount = IndirectBlockCount * IndirectBlockCount * IndirectBlockCount;

    /// Return value of the read loop.
    enum struct Result {
        Done,
        Continue,
        Error,
    };

    /// Helper to read from a list of direct, indirect, doubly indirect, or triply indirect blocks.
    auto read_loop = [&](auto nth_block_number, usz limit) -> Result {
        /// Read from the first direct block.
        usz BlockNumber;
        if (not nth_block_number(BlockIndex, BlockNumber)) return Result::Error;
        usz ToRead = std::min(Size, Sb.block_size() - BlockOffset);
        if (not Read(FileHandle, BlockNumber * Sb.block_size() + BlockOffset, Buffer, ToRead))
            return Result::Error;

        /// We may be done already.
        if (ToRead == Size) return Result::Done;

        /// Otherwise, read from the other direct blocks.
        BlockIndex++;
        Buffer += ToRead;
        Size -= ToRead;
        if (not nth_block_number(BlockIndex, BlockNumber)) return Result::Error;
        while (BlockIndex < limit and Size > 0) {
            ToRead = std::min<usz>(Size, Sb.block_size());
            if (not Read(FileHandle, BlockNumber * Sb.block_size(), Buffer, ToRead))
                return Result::Error;
            BlockIndex++;
            Buffer += ToRead;
            Size -= ToRead;
        }

        /// If the size is zero, we're done.
        if (Size == 0) return Result::Done;
        BlockOffset = 0;
        return Result::Continue;
    };

    /// Offset is within the first 12 direct blocks.
    if (BlockIndex < DIRECT_BLOCK_COUNT) {
        switch (read_loop([&](usz n, usz& out) {
            out = I.i_block[n];
            return true;
        }, DIRECT_BLOCK_COUNT)) {
            case Result::Done: return true;
            case Result::Error: return false;
            case Result::Continue: break;
        }
    }

    /// Read from the indirect block.
    if (BlockIndex < DIRECT_BLOCK_COUNT + IndirectBlockCount) {
        /// Read the indirect block.
        std::vector<u8> IndirectBlock(Sb.block_size());
        if (not Read(
            FileHandle,
            I.i_block[INDIRECT_BLOCK_INDEX] * Sb.block_size(),
            IndirectBlock.data(),
            IndirectBlock.size())
        ) return false;

        /// Read from the indirect block.
        switch (read_loop([&](usz n, usz& out) {
            out = reinterpret_cast<u32*>(IndirectBlock.data())[n - DIRECT_BLOCK_COUNT];
            return true;
        }, DIRECT_BLOCK_COUNT + IndirectBlockCount)) {
            case Result::Done: return true;
            case Result::Error: return false;
            case Result::Continue: break;
        }
    }

    /// Read from the doubly indirect block.
    if (BlockIndex < DIRECT_BLOCK_COUNT + IndirectBlockCount + DoublyIndirectBlockCount) {
        /// Read the doubly indirect block.
        std::vector<u8> IndirectBlock(Sb.block_size());
        std::vector<u8> DoublyIndirectBlock(Sb.block_size());
        if (not Read(
            FileHandle,
            I.i_block[DOUBLY_INDIRECT_BLOCK_INDEX] * Sb.block_size(),
            DoublyIndirectBlock.data(),
            DoublyIndirectBlock.size()
        )) return false;

        /// Read from the triply indirect block.
        switch (read_loop([&](usz n, usz& out) {
            /// Read the next indirect block if necessary.
            usz blocks_per_block = Sb.block_size() / sizeof(u32);
            if (((n - DIRECT_BLOCK_COUNT) % blocks_per_block) == 0) {
                if (not Read(
                    FileHandle,
                    reinterpret_cast<u32*>(DoublyIndirectBlock.data())[(n - DIRECT_BLOCK_COUNT) / blocks_per_block] * Sb.block_size(),
                    IndirectBlock.data(),
                    IndirectBlock.size()
                )) return false;
            }

            /// Read the block number.
            out = reinterpret_cast<u32*>(IndirectBlock.data())[(n - DIRECT_BLOCK_COUNT) % blocks_per_block];
            return true;
        }, DIRECT_BLOCK_COUNT + IndirectBlockCount + DoublyIndirectBlockCount)) {
            case Result::Done: return true;
            case Result::Error: return false;
            case Result::Continue: break;
        }
    }

    /// Read the triply indirect block.
    std::vector<u8> IndirectBlock(Sb.block_size());
    std::vector<u8> DoublyIndirectBlock(Sb.block_size());
    std::vector<u8> TriplyIndirectBlock(Sb.block_size());
    if (not Read(
        FileHandle,
        I.i_block[TRIPLY_INDIRECT_BLOCK_INDEX] * Sb.block_size(),
        TriplyIndirectBlock.data(),
        TriplyIndirectBlock.size()
    )) return false;

    switch (read_loop([&](usz n, usz& out) {
        /// Read the next doubly indirect block if necessary.
        const usz blocks_per_block = Sb.block_size() / sizeof(u32);
        const usz blocks_per_doubly_indirect_block = blocks_per_block * blocks_per_block;
        if (((n - DIRECT_BLOCK_COUNT) % blocks_per_doubly_indirect_block) == 0) {
            if (not Read(
                FileHandle,
                reinterpret_cast<u32*>(TriplyIndirectBlock.data())[(n - DIRECT_BLOCK_COUNT) / blocks_per_doubly_indirect_block] * Sb.block_size(),
                DoublyIndirectBlock.data(),
                DoublyIndirectBlock.size()
            )) return false;
        }

        /// Read the next indirect block if necessary.
        if (((n - DIRECT_BLOCK_COUNT) % blocks_per_block) == 0) {
            if (not Read(
                FileHandle,
                reinterpret_cast<u32*>(DoublyIndirectBlock.data())[(n - DIRECT_BLOCK_COUNT) / blocks_per_block] * Sb.block_size(),
                IndirectBlock.data(),
                IndirectBlock.size()
            )) return false;
        }

        /// Read the block number.
        out = reinterpret_cast<u32*>(IndirectBlock.data())[(n - DIRECT_BLOCK_COUNT) % blocks_per_block];
        return true;
    }, DIRECT_BLOCK_COUNT + IndirectBlockCount + DoublyIndirectBlockCount + TriplyIndirectBlockCount)) {
        case Result::Done: return true;
        case Result::Error: return false;
        case Result::Continue: break;
    }

    /// We should never get here.
    Log("Sorry, file too large to be stored in an EXT2 filesystem.");
    return false;
} // clang-format on

bool Drive::WriteInode(u32 InodeNumber, const Inode& i) {
    auto Offset = ComputeInodeOffset(InodeNumber);
    if (not Offset) return false;

    /// Write the inode.
    return Write(FileHandle, isz(*Offset), &i, sizeof i);
}

bool Drive::WriteDescriptorTable(u32 BlockGroupIndex, const BlockGroupDescriptor& Table) {
    /// The descriptor table is stored in the first block group after the superblock.
    auto Offset = SUPERBLOCK_OFFSET + Sb.block_size() + BlockGroupIndex * sizeof Table;
    return Write(FileHandle, isz(Offset), &Table, sizeof Table);
}

/// ===========================================================================
///  Directory handle API.
/// ===========================================================================
Dir::Dir(Inode I_, InodeNumberType INum_, std::shared_ptr<Drive> Drv_)
    : I(I_),
      InodeNumber(INum_),
      Drv(std::move(Drv_)) {}

/// ===========================================================================
///  File API.
/// ===========================================================================
File::File(InodeNumberType INum, std::shared_ptr<Drive> Drv_) : InodeNumber(INum), Drv(std::move(Drv_)) { }

auto File::Read(void* Buf, usz Len) -> std::optional<usz> {
    /// Get the inode.
    auto I = Drv->ReadInode(InodeNumber);
    if (not I) return {};

    /// Read the data.
    auto ToRead = std::min<usz>(Len, I->i_size - Offset);
    if (not Drv->ReadInodeData(*I, Offset, Buf, ToRead)) return {};

    /// Update the offset.
    Offset += ToRead;
    return ToRead;
}

/// ===========================================================================
///  Drive API.
/// ===========================================================================
/// Open a directory.
auto Drive::OpenDir(std::string_view FilePath, std::string_view Origin) -> std::unique_ptr<Dir> {
    auto INum = InodeFromPath(FilePath, Origin);
    if (not INum) return {};

    auto I = ReadInode(*INum);
    if (not I) return {};

    return std::unique_ptr<Dir>{::new Dir{*I, *INum, This.lock()}};
}

/// Open a file.
auto Drive::OpenFile(std::string_view FilePath, std::string_view Origin) -> std::unique_ptr<File> {
    auto INum = InodeFromPath(FilePath, Origin);
    if (not INum) return {};
    return std::unique_ptr<File>{::new File{*INum, This.lock()}};
}

/// Initialise a directory iterator.
Dir::Iterator::Iterator(Dir* D_) : D(D_) {
    if (not D) return;
    Done = false;
    ++*this;
}

/// Advance a directory iterator.
Dir::Iterator Dir::Iterator::operator++() {
    /// If the offset is past the end of the file, we're done.
    if (NextOffset >= D->I.i_size) {
        Done = true;
        return *this;
    }

    /// Read the next header.
    if (not D->Drv->ReadInodeData(D->I, NextOffset, &Hdr, sizeof Hdr)) {
        std::exchange(*this, {});
        return *this;
    }

    /// Nothing more to read.
    if (Hdr.rec_len == 0) {
        Done = true;
        return *this;
    }

    /// Skip entries with an inode number of zero.
    if (Hdr.inode == 0) {
        NextOffset += Hdr.rec_len;
        return ++*this;
    }

    /// Read the name.
    if (not D->Drv->ReadInodeData(D->I, NextOffset + sizeof Hdr, Name.data(), Hdr.name_len)) {
        std::exchange(*this, {});
        return *this;
    }

    /// Update the iterator.
    NextOffset += Hdr.rec_len;
    return *this;
}

/// Stat a file.
auto Drive::Stat(std::string_view FilePath, std::string_view Origin) -> std::optional<struct stat> {
    /// Get the inode number.
    auto INum = InodeFromPath(FilePath, Origin);
    if (not INum) return {};

    /// Read the inode.
    auto I = ReadInode(*INum);
    if (not I) {
        Log("Failed to read inode {} for file '{}'", *INum, FilePath);
        return {};
    }

    /// Update the access time.
    I->i_atime = (u32) std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    if (not WriteInode(*INum, *I)) return {};

    /// Extract properties.
    struct stat st {};
    st.st_ino = *INum;
    st.st_mode = I->i_mode;
    st.st_nlink = I->i_links_count;
    st.st_uid = I->i_uid;
    st.st_gid = I->i_gid;
    st.st_size = I->i_size;
    st.st_blksize = Sb.block_size();
    st.st_blocks = I->i_blocks;
    st.st_atime = I->i_atime;
    st.st_mtime = I->i_mtime;
    st.st_ctime = I->i_ctime;
    return st;
}

/// Attempt to mount a drive.
auto Drive::TryMount(FdType Fd) -> std::shared_ptr<Drive> {
    /// Read the superblock.
    Superblock sb;
    if (not Read(Fd, SUPERBLOCK_OFFSET, &sb, sizeof sb)) {
        Log("Drive is too small to contain a valid ext2 filesystem.");
        return nullptr;
    }

    /// Validate the superblock.
    if (sb.s_magic != EXT2_SUPER_MAGIC) {
        Log("Invalid magic number: 0x{:04x}", sb.s_magic);
        return nullptr;
    }

    /// Check for incompatible or read-only features.
    if (sb.s_feature_incompat or sb.s_feature_ro_compat) {
        Log("Incompatible or read-only features are enabled. Refusing to mount.");
        return nullptr;
    }

    /// Check for errors.
    if (sb.s_state == FsState::HasErrors) {
        Log("Filesystem has errors. Refusing to mount.");
        return nullptr;
    }

    /// Set the error flag. We'll clear it when we unmount the drive.
    sb.s_state = FsState::HasErrors;

    /// Create the drive.
    auto ptr = std::shared_ptr<Drive>{::new Drive(Fd, std::move(sb))};
    ptr->This = ptr;
    return ptr;
}

} // namespace Ext2