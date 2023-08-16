const std = @import("std");
var general_purpose_allocator = std.heap.GeneralPurposeAllocator(.{}){};
const gpa = general_purpose_allocator.allocator();

const example_target = std.zig.CrossTarget{
    .cpu_arch = .aarch64,
    .cpu_model = .{ .explicit = &std.Target.arm.cpu.cortex_a55 },
    .os_tag = .freestanding,
    .abi = .none,
};

const configOptions = enum {
    debug,
    release,
    benchmark,
};

fn concatStr(allocator: std.mem.Allocator, strings: []const []const u8) []const u8 {
    return std.mem.concat(allocator, u8, strings) catch "";
}

pub fn build(b: *std.Build) !void {
    const target = b.standardTargetOptions(.{ .default_target = example_target });
    const optimize = b.standardOptimizeOption(.{});

    const exe = b.addExecutable(.{
        .name = "main.elf",
        .target = target,
        .optimize = optimize,
    });

    exe.addCSourceFiles(&.{"src/main.c"}, &.{
        "-Wall",
        "-Werror",
        "-Wno-unused-function",
        "-mstrict-align",
    });

    const sel4cp_sdk = b.option([]const u8, "sdk", "Path to seL4CP SDK") orelse null;
    if (sel4cp_sdk == null) {
        std.log.err("Missing -Dsdk=/path/to/sdk argument being passed\n", .{});
        std.os.exit(1);
    }

    const sel4cp_config_option = b.option(configOptions, "config", "seL4CP config to build for") orelse configOptions.debug;
    const sel4cp_config = @tagName(sel4cp_config_option);
    // By default we target the Odroid-C4, but it's possible to override the target if we need to
    // test on QEMU or something
    const sel4cp_board = b.option([]const u8, "board", "seL4CP board to target") orelse "odroidc4";
    // Since we are relying on Zig to produce the final ELF, it needs to do the
    // linking step as well.
    const sdk_board_dir = concatStr(gpa, &[_][]const u8{ sel4cp_sdk.?, "/board/", sel4cp_board, "/", sel4cp_config });
    const libsel4cp = concatStr(gpa, &[_][]const u8{ sdk_board_dir, "/lib/libsel4cp.a" });
    const libsel4cp_linker_script = concatStr(gpa, &[_][]const u8{ sdk_board_dir, "/lib/sel4cp.ld" });

    exe.addObjectFile(libsel4cp);
    exe.setLinkerScriptPath(.{ .path = libsel4cp_linker_script });
    exe.addIncludePath("src/");
    exe.addIncludePath(concatStr(gpa, &[_][]const u8{ sdk_board_dir, "/include" }));

    b.installArtifact(exe);

    const system_description = "kitty.system";
    const sel4cp_tool_cmd = b.addSystemCommand(&[_][]const u8{
       concatStr(gpa, &[_][]const u8{ sel4cp_sdk.?, "/bin/sel4cp" }),
       system_description,
       "--search-path",
       "zig-out/bin",
       "--board",
       sel4cp_board,
       "--config",
       sel4cp_config,
       "-o",
       "zig-out/bin/loader.img",
       "-r",
       "zig-out/report.txt",
    });
    sel4cp_tool_cmd.step.dependOn(b.getInstallStep());
    // Add the "sel4cp" step, and make it the default step when we execute `zig build`>
    const sel4cp_step = b.step("sel4cp", "Compile and build the final bootable image");
    sel4cp_step.dependOn(&sel4cp_tool_cmd.step);
    b.default_step = sel4cp_step;
}
