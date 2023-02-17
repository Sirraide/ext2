#include <clopts.hh>
#include <ext2++/core.hh>
#include <fmt/format.h>

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
        fmt::print(stderr, "Failed to mount drive: {}\n", strerror(errno));
        return 1;
    }
}