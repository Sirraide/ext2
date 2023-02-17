#include <clopts.hh>
#include <ext2++/core.hh>
#include <fmt/chrono.h>
#include <fmt/format.h>

namespace chr = std::chrono;
using namespace command_line_options;
using options = clopts< // clang-format off
    positional<"drive", "The path to the drive to use", std::string, true>
>; // clang-format on

int main(int argc, char** argv) {
    options::parse(argc, argv);

    /// Open the file.
    auto Path = *options::get<"drive">();
    auto Fd = open(Path.c_str(), O_RDWR);
    if (Fd < 0) {
        fmt::print(stderr, "Failed to open drive: {}\n", strerror(errno));
        return 1;
    }

    /// Try to mount the drive.
    auto Drive = Ext2::Drive::TryMount(Fd);
    if (not Drive) {
        fmt::print(stderr, "Failed to mount drive\n");
        return 1;
    }

    /// Stat a directory.
    auto st = Drive->Stat("/");
    if (not st) {
        fmt::print(stderr, "Failed to stat inode\n");
        return 1;
    }
    fmt::print("Inode {}\n"
               "    Size: {}\n"
               "    Blocks: {}\n"
               "    Links: {}\n"
               "    Mode: {:o}\n"
               "    UID: {}\n"
               "    GID: {}\n"
               "    Access: {:%Y/%m/%d %T} UTC\n"
               "    Modify: {:%Y/%m/%d %T} UTC\n"
               "    Change: {:%Y/%m/%d %T} UTC\n",
               st->st_ino, st->st_size, st->st_blocks, st->st_nlink, //
               st->st_mode, st->st_uid, st->st_gid,                  //
               chr::system_clock::from_time_t(st->st_atime),         //
               chr::system_clock::from_time_t(st->st_mtime),         //
               chr::system_clock::from_time_t(st->st_ctime)          //
    );

    /// Read a directory.
    auto Dir = Drive->OpenDir("/");
    if (not Dir) {
        fmt::print(stderr, "Failed to open directory\n");
        return 1;
    }

    fmt::print("Directory:\n");
    for (const auto& Entry : *Dir) {
        fmt::print("    {}\n", Entry->name);
    }
}