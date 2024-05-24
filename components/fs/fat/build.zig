// Zig build system, not work yet
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
    },
}, .{
    .board = MicrokitBoard.odroidc4,
    .zig_target = std.zig.CrossTarget{
        .cpu_arch = .aarch64,
        .cpu_model = .{ .explicit = &std.Target.arm.cpu.cortex_a55 },
        .os_tag = .freestanding,
        .abi = .none,
    },
}, .{
    .board = MicrokitBoard.maaxboard,
    .zig_target = std.zig.CrossTarget{
        .cpu_arch = .aarch64,
        .cpu_model = .{ .explicit = &std.Target.arm.cpu.cortex_a53 },
        .os_tag = .freestanding,
        .abi = .none,
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
    const microkit_tool = b.fmt("{s}/bin/microkit", .{microkit_sdk});
    const libmicrokit = b.fmt("{s}/lib/libmicrokit.a", .{microkit_board_dir});
    const libmicrokit_linker_script = b.fmt("{s}/lib/microkit.ld", .{microkit_board_dir});
    const libmicrokit_include = b.fmt("{s}/include", .{microkit_board_dir});

    const fs = b.addExecutable(.{
        .name = "FatFs",
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
    fs.addIncludePath(.{ .path = "../../../dep/sddf/include/" });

    fs.addIncludePath(.{ .path = libmicrokit_include });

    fs.addObjectFile(.{ .path = libmicrokit });

    // Add all source files
    fs.addCSourceFile(.{ .file = b.path("AsyncFATFs.c"), .flags = &.{"-mstrict-align"} });
    fs.addCSourceFile(.{ .file = b.path("AsyncFATFunc.c"), .flags = &.{"-mstrict-align"} });
    fs.addCSourceFile(.{ .file = b.path("Asyncdiskio.c"), .flags = &.{"-mstrict-align"} });
    fs.addCSourceFile(.{ .file = b.path("ff15/source/ff.c"), .flags = &.{"-mstrict-align -nostdlib"} });
    fs.addCSourceFile(.{ .file = b.path("ff15/source/ffunicode.c"), .flags = &.{"-mstrict-align"} });

    b.installArtifact(fs);
}
