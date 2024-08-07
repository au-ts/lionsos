// Zig build script for building FatFs
// zig build -Dsdk=/home/li/Sel4/microkit-sdk-1.2.6 -Dboard=odroidc4
const std = @import("std");

const MicrokitBoard = enum { qemu_arm_virt, odroidc4, maaxboard };

const Target = struct {
    board: MicrokitBoard,
    zig_target: std.zig.CrossTarget,
};

const targets = [_]Target{ .{
    .board = MicrokitBoard.qemu_arm_virt,
    .zig_target = std.zig.CrossTarget{
        .cpu_arch = .aarch64,
        .cpu_model = .{ .explicit = &std.Target.arm.cpu.cortex_a53 },
        .os_tag = .freestanding,
        .abi = .none,
        .ofmt = .elf,
    },
}, .{
    .board = MicrokitBoard.odroidc4,
    .zig_target = std.zig.CrossTarget{
        .cpu_arch = .aarch64,
        .cpu_model = .{ .explicit = &std.Target.arm.cpu.cortex_a55 },
        .os_tag = .freestanding,
        .abi = .none,
        .ofmt = .elf,
    },
}, .{
    .board = MicrokitBoard.maaxboard,
    .zig_target = std.zig.CrossTarget{
        .cpu_arch = .aarch64,
        .cpu_model = .{ .explicit = &std.Target.arm.cpu.cortex_a53 },
        .os_tag = .freestanding,
        .abi = .none,
        .ofmt = .elf,
    },
} };

fn findTarget(board: MicrokitBoard) std.zig.CrossTarget {
    for (targets) |target| {
        if (board == target.board) {
            return target.zig_target;
        }
    }

    std.log.err("Board '{}' is not supported\n", .{board});
    std.posix.exit(1);
}

const ConfigOptions = enum { debug, release, benchmark };

// Function to collect all .c files in a directory to a [_][]const u8
fn collectCSourceFilesFromDir(dir: []const u8, allocator: *const std.mem.Allocator) ![]const []const u8 {
    const fs = std.fs;

    // Initialize an ArrayList to store the file paths
    var files = std.ArrayList([]const u8).init(allocator);

    // Open the directory
    const directory = try fs.cwd().openDir(dir, .{});
    defer directory.close();

    // Iterate over the directory entries
    var it = try directory.iterate();
    while (try it.next()) |entry| {
        // Check if the entry is a .c file
        if (entry.kind == .File and std.mem.endsWith(u8, entry.name, ".c")) {
            // Join the directory and file name to get the full path
            const file_path = try std.fs.path.join(&[_][]const u8{ dir, entry.name });
            // Append the file path to the list
            try files.append(file_path);
        }
    }

    // Return the list of file paths as a slice
    return files.toOwnedSlice();
}

pub fn build(b: *std.Build) void {
    const optimize = b.standardOptimizeOption(.{});

    const microkit_sdk_arg = b.option([]const u8, "sdk", "Path to Microkit SDK");
    if (microkit_sdk_arg == null) {
        std.log.err("Missing -Dsdk=/path/to/sdk argument being passed\n", .{});
        std.posix.exit(1);
    }
    const microkit_sdk = microkit_sdk_arg.?;

    const microkit_config_option = b.option(ConfigOptions, "config", "Microkit config to build for") orelse ConfigOptions.debug;
    const microkit_config = @tagName(microkit_config_option);

    // Get the Microkit SDK board we want to target
    const microkit_board_option = b.option(MicrokitBoard, "board", "Microkit board to target");

    if (microkit_board_option == null) {
        std.log.err("Missing -Dboard=<BOARD> argument being passed\n", .{});
        std.posix.exit(1);
    }
    const target = b.resolveTargetQuery(findTarget(microkit_board_option.?));
    const microkit_board = @tagName(microkit_board_option.?);

    // Since we are relying on Zig to produce the final ELF, it needs to do the
    // linking step as well.
    const microkit_board_dir = b.fmt("{s}/board/{s}/{s}", .{ microkit_sdk, microkit_board, microkit_config });
    const libmicrokit = b.fmt("{s}/lib/libmicrokit.a", .{microkit_board_dir});
    const libmicrokit_linker_script = b.fmt("{s}/lib/microkit.ld", .{microkit_board_dir});
    const libmicrokit_include = b.fmt("{s}/include", .{microkit_board_dir});

    const fs = b.addExecutable(.{
        .name = "fatfs.elf",
        .target = target,
        .optimize = optimize,
        .strip = false,
    });

    const FiberPool_deps = b.dependency("FiberPool", .{
        .optimize = optimize,
    });

    const FiberPool = FiberPool_deps.artifact("FiberPool");

    fs.linkLibrary(FiberPool);

    // Add sddf include directory
    fs.addIncludePath(.{ .cwd_relative = "../../../dep/sddf/include/" });

    fs.addIncludePath(.{ .cwd_relative = libmicrokit_include });

    fs.addObjectFile(.{ .cwd_relative = libmicrokit });

    // Command to compile musl libc
    // const make_command = b.fmt("CONFIG_USER_DEBUG_BUILD=y CONFIG_ARCH_AARCH64=y C_COMPILER=aarch64-none-elf-gcc TOOLPREFIX=aarch64-none-elf- SOURCE_DIR=. STAGE_DIR=../../components/fs/fat/build make");
    // const make_command = b.fmt("CONFIG_USER_DEBUG_BUILD=y CONFIG_ARCH_AARCH64=y C_COMPILER=clang TOOLPREFIX= NK_CFLAGS=\"-mstrict-align -ffreestanding -g -O0 -Wall -Wno-unused-function -target aarch64-none-elf -no-integrated-as\" SOURCE_DIR=. STAGE_DIR=../../components/fs/fat/build make", .{});

    // const make_step = b.addSystemCommand(&[_][]const u8{
    //     "sh", "-c", make_command,
    // });
    // make_step.setCwd(.{ .path = "../../../dep/musllibc/" });

    // b.default_step = &(make_step.step);

    // Include musllibc
    fs.addIncludePath(.{ .cwd_relative = "build/include/" });
    fs.addObjectFile(.{ .cwd_relative = "build/lib/libc.a" });

    // Add all file system related source files
    fs.addCSourceFile(.{ .file = b.path("fatfs_event.c"), .flags = &.{"-mstrict-align"} });
    fs.addCSourceFile(.{ .file = b.path("fatfs_op.c"), .flags = &.{"-mstrict-align"} });
    fs.addCSourceFile(.{ .file = b.path("fs_diskio.c"), .flags = &.{"-mstrict-align"} });
    fs.addCSourceFile(.{ .file = b.path("co_helper.c"), .flags = &.{"-mstrict-align"} });
    fs.addCSourceFile(.{
        .file = b.path("ff15/source/ff.c"),
        .flags = &.{ "-mstrict-align", "-nostdlib" },
    });
    fs.addCSourceFile(.{ .file = b.path("ff15/source/ffunicode.c"), .flags = &.{"-mstrict-align"} });

    fs.addCSourceFile(.{ .file = b.path("../../../dep/sddf/util/printf.c"), .flags = &.{"-mstrict-align"} });
    fs.addCSourceFile(.{ .file = b.path("../../../dep/sddf/util/putchar_debug.c"), .flags = &.{"-mstrict-align"} });

    // For printf
    // const printf_src_file = collectCSourceFilesFromDir("../../../dep/sddf/util/", &std.heap.page_allocator);
    // fs.addCSourceFiles(.{
    //    .files = &printf_src_file,
    //    .flags = &.{
    //        "-mstrict-align",
    //    },
    // });

    fs.setLinkerScript(.{ .cwd_relative = libmicrokit_linker_script });

    b.installArtifact(fs);
}
