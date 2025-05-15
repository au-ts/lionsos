//
// Copyright 2024, UNSW
// SPDX-License-Identifier: BSD-2-Clause
//
const std = @import("std");
const Step = std.Build.Step;
const LazyPath = std.Build.LazyPath;

const MicrokitBoard = enum {
    qemu_virt_aarch64,
};

const Target = struct {
    board: MicrokitBoard,
    zig_target: std.Target.Query,
};

const targets = [_]Target{
    .{
        .board = .qemu_virt_aarch64,
        .zig_target = std.Target.Query{
            .cpu_arch = .aarch64,
            .cpu_model = .{ .explicit = &std.Target.aarch64.cpu.cortex_a53 },
            .cpu_features_add = std.Target.aarch64.featureSet(&[_]std.Target.aarch64.Feature{ .strict_align }),
            .os_tag = .freestanding,
            .abi = .none,
        },
    },
};

fn findTarget(board: MicrokitBoard) std.Target.Query {
    for (targets) |target| {
        if (board == target.board) {
            return target.zig_target;
        }
    }

    std.log.err("Board '{}' is not supported\n", .{board});
    std.posix.exit(1);
}

const ConfigOptions = enum { debug, release, benchmark };

fn updateSectionObjcopy(b: *std.Build, section: []const u8, data_output: std.Build.LazyPath, data: []const u8, elf: []const u8) *Step.Run {
    const run_objcopy = b.addSystemCommand(&[_][]const u8{
        "llvm-objcopy",
    });
    run_objcopy.addArg("--update-section");
    const data_full_path = data_output.join(b.allocator, data) catch @panic("OOM");
    run_objcopy.addPrefixedFileArg(b.fmt("{s}=", .{ section }), data_full_path);
    run_objcopy.addFileArg(.{ .cwd_relative = b.getInstallPath(.bin, elf) });

    // We need the ELFs we talk about to be in the install directory first.
    run_objcopy.step.dependOn(b.getInstallStep());

    return run_objcopy;
}

const lwext4_src = [_][]const u8 {
    "ext4_bitmap.c",
    "ext4_crc32.c",
    "ext4_dir.c",
    "ext4_hash.c",
    "ext4_journal.c",
    "ext4_super.c",
    "ext4.c",
    "ext4_balloc.c",
    "ext4_block_group.c",
    "ext4_debug.c",
    "ext4_extent.c",
    "ext4_ialloc.c",
    "ext4_mbr.c",
    "ext4_trans.c",
    "ext4_bcache.c",
    "ext4_blockdev.c",
    "ext4_dir_idx.c",
    "ext4_fs.c",
    "ext4_inode.c",
    "ext4_mkfs.c",
    "ext4_xattr.c",
};

pub fn build(b: *std.Build) !void {
    const optimize = b.standardOptimizeOption(.{});

    const default_python = if (std.posix.getenv("PYTHON")) |p| p else "python3";
    const python = b.option([]const u8, "python", "Path to Python to use") orelse default_python;

    const partition = b.option(usize, "partition", "Block device partition for client to use") orelse null;

    // Getting the path to the Microkit SDK before doing anything else
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

    const microkit_board_dir = b.fmt("{s}/board/{s}/{s}", .{ microkit_sdk, microkit_board, microkit_config });
    const microkit_tool = b.fmt("{s}/bin/microkit", .{microkit_sdk});
    const libmicrokit = b.fmt("{s}/lib/libmicrokit.a", .{microkit_board_dir});
    const libmicrokit_include = b.fmt("{s}/include", .{microkit_board_dir});
    const libmicrokit_linker_script = b.fmt("{s}/lib/microkit.ld", .{microkit_board_dir});

    const sddf_dep = b.dependency("sddf", .{
        .target = target,
        .optimize = optimize,
        .libmicrokit = @as([]const u8, libmicrokit),
        .libmicrokit_include = @as([]const u8, libmicrokit_include),
        .libmicrokit_linker_script = @as([]const u8, libmicrokit_linker_script),
    });

    const blk_driver_class = switch (microkit_board_option.?) {
        .qemu_virt_aarch64 => "virtio"
    };

    const lwext4 = b.dependency("lwext4", .{
        .target = target,
        .optimize = optimize,
    });

    const ext4 = b.addExecutable(.{
        .name = "ext4.elf",
        .target = target,
        .optimize = optimize,
        .strip = false,
    });

    ext4.addCSourceFiles(.{
        .files = &.{ "ext4.c", "ext4_blockdev.c" },
        .flags = &.{ "-DCONFIG_USE_DEFAULT_CFG", "-DVERSIOn=\"1.0.0\"" },
    });
    ext4.addCSourceFiles(.{
        .root = lwext4.path("src"),
        .files = &lwext4_src,
        .flags = &.{ "-DCONFIG_USE_DEFAULT_CFG", "-DVERSIOn=\"1.0.0\"" },
    });
    ext4.addIncludePath(lwext4.path("include"));

    const picolibc = b.dependency("microkit_libc", .{
        .target = target,
        .optimize = optimize,
    });

    ext4.addIncludePath(sddf_dep.path("include"));
    ext4.addIncludePath(sddf_dep.path("include/microkit"));
    ext4.linkLibrary(sddf_dep.artifact("util"));
    ext4.linkLibrary(sddf_dep.artifact("util_putchar_debug"));

    ext4.addIncludePath(picolibc.path("newlib/libc/include"));
    ext4.addIncludePath(picolibc.path("newlib/libc/tinystdio"));
    ext4.addIncludePath(picolibc.path(""));
    ext4.linkLibrary(picolibc.artifact("c"));

    ext4.addIncludePath(.{ .cwd_relative = libmicrokit_include });
    ext4.addObjectFile(.{ .cwd_relative = libmicrokit });
    ext4.setLinkerScript(.{ .cwd_relative = libmicrokit_linker_script });

    const blk_driver = sddf_dep.artifact(b.fmt("driver_blk_{s}.elf", .{ blk_driver_class }));

    b.installArtifact(ext4);
    b.installArtifact(blk_driver);
    b.installArtifact(sddf_dep.artifact("blk_virt.elf"));

    // Because our SDF expects a different ELF name for the block driver, we have this extra step.
    const blk_driver_install = b.addInstallArtifact(blk_driver, .{ .dest_sub_path = "blk_driver.elf" });

    // For compiling the DTS into a DTB
    const dts = sddf_dep.path(b.fmt("dts/{s}.dts", .{ microkit_board }));
    const dtc_cmd = b.addSystemCommand(&[_][]const u8{
        "dtc", "-q", "-I", "dts", "-O", "dtb"
    });
    dtc_cmd.addFileInput(dts);
    dtc_cmd.addFileArg(dts);
    const dtb = dtc_cmd.captureStdOut();

    // Run the metaprogram to get sDDF configuration binary files and the SDF file.
    const metaprogram = b.path("meta.py");
    const run_metaprogram = b.addSystemCommand(&[_][]const u8{
        python,
    });
    run_metaprogram.addFileArg(metaprogram);
    run_metaprogram.addFileInput(metaprogram);
    run_metaprogram.addPrefixedDirectoryArg("--sddf=", sddf_dep.path(""));
    run_metaprogram.addPrefixedDirectoryArg("--dtb=", dtb);
    const meta_output = run_metaprogram.addPrefixedOutputDirectoryArg("--output=", "meta_output");
    run_metaprogram.addArg("--board");
    run_metaprogram.addArg(microkit_board);
    run_metaprogram.addArg("--sdf");
    run_metaprogram.addArg("blk.system");
    if (partition) |p| {
        run_metaprogram.addArg("--partition");
        run_metaprogram.addArg(b.fmt("{}", .{ p }));
    }
    _ = try run_metaprogram.step.addDirectoryWatchInput(sddf_dep.path(""));

    const meta_output_install = b.addInstallDirectory(.{
        .source_dir = meta_output,
        .install_dir = .prefix,
        .install_subdir = "meta_output",
    });

    const ext4_objcopy = updateSectionObjcopy(b, ".blk_client_config", meta_output, "blk_client_ext4.data", "ext4.elf");
    const virt_objcopy = updateSectionObjcopy(b, ".blk_virt_config", meta_output, "blk_virt.data", "blk_virt.elf");
    const driver_resources_objcopy = updateSectionObjcopy(b, ".device_resources", meta_output, "blk_driver_device_resources.data", "blk_driver.elf");
    const driver_config_objcopy = updateSectionObjcopy(b, ".blk_driver_config", meta_output, "blk_driver.data", "blk_driver.elf");
    driver_resources_objcopy.step.dependOn(&blk_driver_install.step);
    driver_config_objcopy.step.dependOn(&blk_driver_install.step);
    const objcopys = .{ ext4_objcopy, virt_objcopy, driver_resources_objcopy, driver_config_objcopy };

    const final_image_dest = b.getInstallPath(.bin, "./loader.img");
    const microkit_tool_cmd = b.addSystemCommand(&[_][]const u8{
        microkit_tool,
        b.getInstallPath(.{ .custom = "meta_output" }, "blk.system"),
        "--search-path", b.getInstallPath(.bin, ""),
        "--board", microkit_board,
        "--config", microkit_config,
        "-o", final_image_dest,
        "-r", b.getInstallPath(.prefix, "./report.txt")
    });
    inline for (objcopys) |objcopy| {
        microkit_tool_cmd.step.dependOn(&objcopy.step);
    }
    microkit_tool_cmd.step.dependOn(&meta_output_install.step);
    microkit_tool_cmd.step.dependOn(b.getInstallStep());
    microkit_tool_cmd.setEnvironmentVariable("MICROKIT_SDK", microkit_sdk);

    const microkit_step = b.step("microkit", "Compile and build the final bootable image");
    microkit_step.dependOn(&microkit_tool_cmd.step);
    b.default_step = microkit_step;

    // This is setting up a `qemu` command for running the system using QEMU,
    // which we only want to do when we have a board that we can actually simulate.
    if (microkit_board_option.? == .qemu_virt_aarch64 or microkit_board_option.? == .qemu_virt_riscv64) {
        const create_disk_cmd = b.addSystemCommand(&[_][]const u8{
            "bash",
        });
        const mkvirtdisk = sddf_dep.path("tools/mkvirtdisk");
        create_disk_cmd.addFileArg(mkvirtdisk);
        create_disk_cmd.addFileInput(mkvirtdisk);
        const disk = create_disk_cmd.addOutputFileArg("disk");
        create_disk_cmd.addArgs(&[_][]const u8{
            "1", "512", b.fmt("{}", .{ 1024 * 1024 * 16 }),
        });
        const disk_install = b.addInstallFile(disk, "disk");
        disk_install.step.dependOn(&create_disk_cmd.step);

        var qemu_cmd: *Step.Run = undefined;
        if (microkit_board_option.? == .qemu_virt_aarch64) {
            const loader_arg = b.fmt("loader,file={s},addr=0x70000000,cpu-num=0", .{ final_image_dest });
            qemu_cmd = b.addSystemCommand(&[_][]const u8{
                "qemu-system-aarch64",
                "-machine", "virt,virtualization=on",
                "-cpu", "cortex-a53",
                "-serial", "mon:stdio",
                "-device", loader_arg,
                "-m", "2G",
                "-nographic",
            });
        } else if (microkit_board_option.? == .qemu_virt_riscv64) {
            qemu_cmd = b.addSystemCommand(&[_][]const u8{
                "qemu-system-riscv64",
                "-machine",
                "virt",
                "-serial",
                "mon:stdio",
                "-kernel",
                final_image_dest,
                "-m",
                "2G",
                "-nographic",
            });
        }

        const blk_device_args = &.{
            "-global", "virtio-mmio.force-legacy=false",
            "-drive", b.fmt("file={s},if=none,format=raw,id=hd", .{ b.getInstallPath(.prefix, "disk") }),
            "-device", "virtio-blk-device,drive=hd",
        };
        qemu_cmd.addArgs(blk_device_args);

        qemu_cmd.step.dependOn(b.default_step);
        qemu_cmd.step.dependOn(&disk_install.step);

        const simulate_step = b.step("qemu", "Simulate the image using QEMU");
        simulate_step.dependOn(&qemu_cmd.step);
    }
}
