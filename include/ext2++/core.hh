#ifndef EXT2_CORE_HH
#define EXT2_CORE_HH

#include <ext2++/bits/utils.hh>

namespace Ext2 {
#ifdef __linux__
using FdType = int;
#endif

constexpr inline usz MAX_PATH = 255;

using InodeNumberType = u32;

/// Forward declaration.
class Drive;

/// Filesystem states.
enum struct FsState : u16 {
    Valid = 1,
    HasErrors = 2,
};

/// Error handling methods.
enum struct ErrorHandling : u16 {
    Ignore = 1,
    RemountReadOnly = 2,
    KernelPanic = 3,
};

/// Creator OS.
enum struct CreatorOS : u32 {
    Linux = 0,
    Hurd = 1,
    Masix = 2,
    FreeBSD = 3,
    Lites = 4,
};

/// Revision levels.
enum struct RevisionLevel : u32 {
    GoodOldRev = 0,
    DynamicRev = 1,
};

/// Compat features.
enum struct CompatFeature : u32 {
    DirPrealloc = 0x0001,
    ImagicInodes = 0x0002,
    HasJournal = 0x0004,
    ExtAttr = 0x0008,
    ResizeIno = 0x0010,
    DirIndex = 0x0020,
};

/// Incompat features.
enum struct IncompatFeature : u32 {
    Compression = 0x0001,
    FileType = 0x0002,
    Recover = 0x0004,
    JournalDev = 0x0008,
    MetaBg = 0x0010,
};

/// Read-only features.
enum struct RoFeature : u32 {
    SparseSuper = 0x0001,
    LargeFile = 0x0002,
    BtreeDir = 0x0004,
};

/// Compression algorithms.
enum struct CompressionAlgorithm : u32 {
    LZV1 = 1 << 0,
    LZRW3A = 1 << 1,
    GZIP = 1 << 2,
    BZIP2 = 1 << 3,
    LZO = 1 << 4,
};

/// Ext2 Superblock.
struct Superblock {
    u32 s_inodes_count;
    u32 s_blocks_count;
    u32 s_r_blocks_count;
    u32 s_free_blocks_count;
    u32 s_free_inodes_count;
    u32 s_first_data_block;
    u32 s_log_block_size;
    u32 s_log_frag_size;
    u32 s_blocks_per_group;
    u32 s_frags_per_group;
    u32 s_inodes_per_group;
    u32 s_mtime;
    u32 s_wtime;
    u16 s_mnt_count;
    u16 s_max_mnt_count;
    u16 s_magic;
    FsState s_state;
    ErrorHandling s_errors;
    u16 s_minor_rev_level;
    u32 s_lastcheck;
    u32 s_checkinterval;
    CreatorOS s_creator_os;
    RevisionLevel s_rev_level;
    u16 s_def_resuid;
    u16 s_def_resgid;

    /// EXT2_DYNAMIC_REV Specific fields.
    u32 s_first_ino;
    u16 s_inode_size;
    u16 s_block_group_nr;
    u32 s_feature_compat;
    u32 s_feature_incompat;
    u32 s_feature_ro_compat;
    u8 s_uuid[16];
    u8 s_volume_name[16];
    u8 s_last_mounted[64];
    u32 s_algo_bitmap;

    /// Performance hints.
    u8 s_prealloc_blocks;
    u8 s_prealloc_dir_blocks;
    u16 _padding1_;

    /// Journaling support.
    u8 s_journal_uuid[16];
    u32 s_journal_inum;
    u32 s_journal_dev;
    u32 s_last_orphan;

    /// Directory indexing support.
    u32 s_hash_seed[4];
    u8 s_def_hash_version;
    u8 _padding2_[3];

    /// Other options.
    u32 s_default_mount_options;
    u32 s_first_meta_bg;
    u8 _padding3_[760];

    /// Size of a block.
    [[nodiscard]] u32 block_size() const { return 1024u << s_log_block_size; }

    /// Number of block groups.
    [[nodiscard]] u32 block_groups() const { return (s_blocks_count / s_blocks_per_group) + (s_blocks_count % s_blocks_per_group ? 1 : 0); }
};

/// Linked directory entry.
struct LinkedDirEntryHeader {
    InodeNumberType inode;
    u16 rec_len;
    u8 name_len;

    /// Only meaningful in revision 1.
    u8 file_type;
};

/// Index node.
struct Inode {
    static constexpr u16 FileFormatMask = 0xF000;

    enum FileFormat : u16 {
        Socket = 0xC000,
        SymbolicLink = 0xA000,
        RegularFile = 0x8000,
        BlockDevice = 0x6000,
        Directory = 0x4000,
        CharacterDevice = 0x2000,
        Fifo = 0x1000,
        Unknown = 0x0000,
    };

    u16 i_mode;
    u16 i_uid;
    u32 i_size;
    u32 i_atime;
    u32 i_ctime;
    u32 i_mtime;
    u32 i_dtime;
    u16 i_gid;
    u16 i_links_count;
    u32 i_blocks;
    u32 i_flags;
    u32 i_osd1;
    u32 i_block[15];
    u32 i_generation;
    u32 i_file_acl;
    u32 i_dir_acl;
    u32 i_faddr;
    u8 i_osd2[12];

    /// Check the file format of this inode.
    [[nodiscard]] bool Is(FileFormat FF) const {
        return (i_mode & FileFormatMask) == static_cast<u16>(FF);
    }
};

/// Block group descriptor.
struct BlockGroupDescriptor {
    u32 bg_block_bitmap;
    u32 bg_inode_bitmap;
    u32 bg_inode_table;
    u16 bg_free_blocks_count;
    u16 bg_free_inodes_count;
    u16 bg_used_dirs_count;
    u16 bg_pad;
    u8 bg_reserved[12];
};

/// Directory handle.
class Dir {
    /// The inode for this directory.
    Inode I;
    InodeNumberType InodeNumber;

    /// Inode offset.
    u32 InodeOffset;

    /// The drive this directory is on.
    std::shared_ptr<Drive> Drv;

    /// Create a new directory handle.
    Dir(Inode, InodeNumberType, std::shared_ptr<Drive>);

public:
    /// Directory entry.
    struct Entry {
        std::string name;
    };

    /// Directory iterator.
    class Iterator {
        Dir* D{};
        LinkedDirEntryHeader Hdr{};
        std::array<char, 256> Name{};
        usz NextOffset{};
        bool Done = true;

        Iterator(Dir* D_ = nullptr);

    public:
        friend class Dir;
        Iterator operator++();
        Iterator operator++(int) {
            auto tmp = *this;
            ++*this;
            return tmp;
        }
        auto operator*() const -> std::optional<Entry> { return Entry{std::string{Name.data(), std::min<usz>(MAX_PATH, Hdr.name_len)}}; }
        bool operator==(std::default_sentinel_t) const { return Done; }
    };

    friend class Drive;
    Dir(const Dir&) = delete;
    Dir(Dir&&) = delete;
    Dir& operator=(const Dir&) = delete;
    Dir& operator=(Dir&&) = delete;

    Iterator begin() { return {this}; }
    std::default_sentinel_t end() { return {}; }
};

/// A handle to a drive.
class Drive final {
    FdType FileHandle;
    Superblock Sb;

    Drive(FdType, Superblock&&);

    /// Compute the offset of an inode.
    auto ComputeInodeOffset(InodeNumberType InodeNumber) -> std::optional<usz>;

    /// Find a directory entry.
    [[nodiscard]] auto FindDirectoryEntry(Inode& I, std::string_view Name) -> std::optional<LinkedDirEntryHeader>;

    /// Get the type of a dir entry. This is a function because although the
    /// header contains the file type, it is only valid for revision 1, so in
    /// revision 0, we have to read the inode instead.
    auto GetFileFormat(LinkedDirEntryHeader Hdr) -> std::optional<Inode::FileFormat>;

    /// Get an inode from a path.
    auto InodeFromPath(std::string_view, std::string_view Origin = "") -> std::optional<InodeNumberType>;
    auto InodeFromPath(std::string_view, InodeNumberType Origin) -> std::optional<InodeNumberType>;

    /// Get a descriptor table from a block group index.
    auto ReadDescriptorTable(u32 BlockGroupIndex) -> std::optional<BlockGroupDescriptor>;

    /// Get an Inode from an Inode number.
    auto ReadInode(InodeNumberType InodeNumber) -> std::optional<Inode>;

    /// Read inode data at an offset relative to the beginning of the inode.
    bool ReadInodeData(Inode& Inode, usz Offset, void* Buffer, usz Size);

    /// Write a descriptor table to a block group index.
    bool WriteDescriptorTable(u32 BlockGroupIndex, const BlockGroupDescriptor&);

    /// Write an Inode to an Inode number.
    bool WriteInode(InodeNumberType InodeNumber, const Inode&);

    /// Weak pointer to this.
    std::weak_ptr<Drive> This;

public:
    friend class Dir::Iterator;
    ~Drive();
    Drive(const Drive&) = delete;
    Drive(Drive&&) = delete;
    Drive& operator=(const Drive&) = delete;
    Drive& operator=(Drive&&) = delete;

    /// Open a directory.
    auto OpenDir(std::string_view FilePath, std::string_view origin = "") -> std::unique_ptr<Dir>;

    /// Stat an inode.
    auto Stat(std::string_view FilePath, std::string_view origin = "") -> std::optional<struct stat>;

    /// Try to mount a drive.
    static auto TryMount(FdType Fd) -> std::shared_ptr<Drive>;
};

} // namespace Ext2

#endif // EXT2_CORE_HH
