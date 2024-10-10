// Copyright 2024, UNSW
// SPDX-License-Identifier: BSD-2-Clause

const std = @import("std");

const MicrokitBoard = enum {
    qemu_virt_aarch64,
    maaxboard,
    odroidc4,
};

const Target = struct {
    board: MicrokitBoard,
    zig_target: std.Target.Query,
};

const targets = [_]Target {
    .{
        .board = MicrokitBoard.qemu_virt_aarch64,
        .zig_target = std.Target.Query{
            .cpu_arch = .aarch64,
            .cpu_model = .{ .explicit = &std.Target.arm.cpu.cortex_a53 },
            .os_tag = .freestanding,
            .abi = .none,
        },
    },
    .{
        .board = MicrokitBoard.maaxboard,
        .zig_target = std.Target.Query{
            .cpu_arch = .aarch64,
            .cpu_model = .{ .explicit = &std.Target.arm.cpu.cortex_a53 },
            .os_tag = .freestanding,
            .abi = .none,
        },
    },
    .{
        .board = MicrokitBoard.odroidc4,
        .zig_target = std.Target.Query{
            .cpu_arch = .aarch64,
            .cpu_model = .{ .explicit = &std.Target.arm.cpu.cortex_a55 },
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

    std.log.err("Board '{}' is not supported\n", .{ board });
    std.posix.exit(1);
}

const ConfigOptions = enum {
    debug,
    release,
};

pub fn build(b: *std.Build) !void {
    const optimize = b.standardOptimizeOption(.{});

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

    const create_disk_option = b.option(bool, "disk-create", "Whether to create a new virtual disk for QEMU") orelse false;

    const target = b.resolveTargetQuery(findTarget(microkit_board_option.?));
    const microkit_board = @tagName(microkit_board_option.?);

    const microkit_board_dir = b.fmt("{s}/board/{s}/{s}", .{ microkit_sdk, microkit_board, microkit_config });
    const microkit_tool = b.fmt("{s}/bin/microkit", .{ microkit_sdk });
    const libmicrokit = b.fmt("{s}/lib/libmicrokit.a", .{ microkit_board_dir });
    const libmicrokit_linker_script = b.fmt("{s}/lib/microkit.ld", .{ microkit_board_dir });
    const libmicrokit_include = b.fmt("{s}/include", .{ microkit_board_dir });

    const arm_vgic_version: usize = switch (microkit_board_option.?) {
        .qemu_virt_aarch64, .odroidc4 => 2,
        .maaxboard => 3,
    };

    const sddf_dep = b.dependency("sddf", .{
        .target = target,
        .optimize = optimize,
        .libmicrokit = @as([]const u8, libmicrokit),
        .libmicrokit_include = @as([]const u8, libmicrokit_include),
        .libmicrokit_linker_script = @as([]const u8, libmicrokit_linker_script),
        .serial_config_include = @as([]const u8, "."),
    });

    const libvmm_dep = b.dependency("libvmm", .{
        .target = target,
        .optimize = optimize,
        .libmicrokit = @as([]const u8, libmicrokit),
        .libmicrokit_include = @as([]const u8, libmicrokit_include),
        .libmicrokit_linker_script = @as([]const u8, libmicrokit_linker_script),
        .arm_vgic_version = arm_vgic_version,
    });
    const libvmm = libvmm_dep.artifact("vmm");

    const vmm = b.addExecutable(.{
        .name = "vmm.elf",
        .target = target,
        .optimize = optimize,
        .strip = false,
    });

    const base_dts_path = b.fmt("board/{s}/linux/linux.dts", .{ microkit_board });
    const overlay = b.fmt("board/{s}/linux/overlay.dts", .{ microkit_board });
    const dts_cat_cmd = b.addSystemCommand(&[_][]const u8{
        "sh"
    });
    dts_cat_cmd.addFileArg(libvmm_dep.path("tools/dtscat"));
    dts_cat_cmd.addFileArg(b.path(base_dts_path));
    dts_cat_cmd.addFileArg(b.path(overlay));
    dts_cat_cmd.addFileInput(b.path(base_dts_path));
    dts_cat_cmd.addFileInput(b.path(overlay));
    const final_dts = dts_cat_cmd.captureStdOut();

    // For actually compiling the DTS into a DTB
    const dtc_cmd = b.addSystemCommand(&[_][]const u8{
        "dtc", "-q", "-I", "dts", "-O", "dtb"
    });
    dtc_cmd.addFileArg(.{ .cwd_relative = b.getInstallPath(.prefix, "final.dts") });
    dtc_cmd.step.dependOn(&b.addInstallFileWithDir(final_dts, .prefix, "final.dts").step);
    const dtb = dtc_cmd.captureStdOut();

    vmm.addIncludePath(.{ .cwd_relative = libmicrokit_include });
    vmm.addObjectFile(.{ .cwd_relative = libmicrokit });
    vmm.setLinkerScriptPath(.{ .cwd_relative = libmicrokit_linker_script });
    vmm.linkLibrary(libvmm);
    vmm.addCSourceFiles(.{
        .files = &.{"vmm.c"},
        .flags = &.{
            "-Wall",
            "-Werror",
            "-Wno-unused-function",
            "-mstrict-align",
            b.fmt("-DBOARD_{s}", .{ microkit_board })
        }
    });

    const guest_images = b.addObject(.{
        .name = "guest_images",
        .target = target,
        .optimize = optimize,
    });
    // We need to produce the DTB from the DTS before doing anything to produce guest_images
    guest_images.step.dependOn(&b.addInstallFileWithDir(dtb, .prefix, "linux.dtb").step);

    const linux_image_path = b.fmt("board/{s}/linux/linux", .{ microkit_board });
    const kernel_image_arg = b.fmt("-DGUEST_KERNEL_IMAGE_PATH=\"{s}\"", .{ linux_image_path });

    const initrd_name = switch (microkit_board_option.?) {
        .odroidc4 => "initrd.img-6.6.47-current-meson64",
        else => "rootfs.cpio.gz",
    };

    const initrd_image_path = b.fmt("board/{s}/linux/{s}", .{ microkit_board, initrd_name });
    const initrd_image_arg = b.fmt("-DGUEST_INITRD_IMAGE_PATH=\"{s}\"", .{ initrd_image_path });
    const dtb_image_arg = b.fmt("-DGUEST_DTB_IMAGE_PATH=\"{s}\"", .{ b.getInstallPath(.prefix, "linux.dtb") });

    var prng = std.Random.DefaultPrng.init(blk: {
        var seed: u64 = undefined;
        try std.posix.getrandom(std.mem.asBytes(&seed));
        break :blk seed;
    });
    const rand = prng.random();

    guest_images.addCSourceFile(.{
        .file = libvmm_dep.path("tools/package_guest_images.S"),
        .flags = &.{
            kernel_image_arg,
            dtb_image_arg,
            initrd_image_arg,
            b.fmt("-DRANDOM=\"{}\"", .{ rand.int(usize) }),
            "-x",
            "assembler-with-cpp",
        }
    });

    vmm.addObject(guest_images);
    b.installArtifact(vmm);

    const sddf_artifacts = &.{
        "driver_uart_arm.elf",
        "driver_uart_imx.elf",
        "serial_virt_rx.elf",
        "serial_virt_tx.elf",
        "driver_blk_virtio.elf",
        "driver_blk_mmc_imx.elf",
        "blk_virt.elf",
        "driver_net_virtio.elf",
        "driver_net_imx.elf",
        "net_virt_rx.elf",
        "net_virt_tx.elf",
        "net_copy.elf",
        "driver_timer_imx.elf",
    };

    inline for (sddf_artifacts) |artifact| {
        b.installArtifact(sddf_dep.artifact(artifact));
    }

    const sdf_dep = b.dependency("sdfgen", .{
        .target = b.host,
        .optimize = optimize,
    });
    const sdf = b.addExecutable(.{
        .name = "sdf",
        .root_source_file = b.path("sdf.zig"),
        .target = b.host,
    });

    sdf.root_module.addImport("sdf", sdf_dep.module("sdf"));

    const main_dts = b.path(b.fmt("dtbs/{s}.dts", .{ microkit_board }));
    const main_dtb_cmd = b.addSystemCommand(&[_][]const u8{
        "dtc", "-q", "-I", "dts", "-O", "dtb"
    });
    main_dtb_cmd.addFileArg(main_dts);
    main_dtb_cmd.addFileInput(main_dts);
    const main_dtb = main_dtb_cmd.captureStdOut();

    const sdf_step = b.addRunArtifact(sdf);
    sdf_step.addArg("--sddf");
    sdf_step.addDirectoryArg(b.path("../../dep/sddf"));
    sdf_step.addArg("--dtbs");
    sdf_step.addArg(b.install_path);
    sdf_step.addArg("--board");
    sdf_step.addArg(microkit_board);
    sdf_step.addArg("--sdf");
    sdf_step.addFileInput(dtb);
    const sdf_file = sdf_step.addOutputFileArg("presentation.system");

    sdf_step.step.dependOn(&b.addInstallFileWithDir(main_dtb, .prefix, b.fmt("{s}.dtb", .{ microkit_board })).step);

    const final_image_dest = b.getInstallPath(.bin, "./loader.img");
    const microkit_tool_cmd = b.addSystemCommand(&[_][]const u8{
       microkit_tool,
       b.getInstallPath(.prefix, "presentation.system"),
       "--search-path",
       b.getInstallPath(.bin, ""),
       "--board",
       microkit_board,
       "--config",
       microkit_config,
       "-o",
       final_image_dest,
       "-r",
       b.getInstallPath(.prefix, "./report.txt")
    });
    microkit_tool_cmd.step.dependOn(&b.addInstallFileWithDir(sdf_file, .prefix, "presentation.system").step);
    microkit_tool_cmd.step.dependOn(b.getInstallStep());
    const microkit_step = b.step("microkit", "Compile and build the final bootable image");
    microkit_step.dependOn(&microkit_tool_cmd.step);
    b.default_step = microkit_step;

    const loader_arg = b.fmt("loader,file={s},addr=0x70000000,cpu-num=0", .{ final_image_dest });
    if (microkit_board_option == .qemu_virt_aarch64) {

        const qemu_cmd = b.addSystemCommand(&[_][]const u8{
            "qemu-system-aarch64",
            "-machine", "virt,virtualization=on,highmem=off,secure=off",
            "-cpu", "cortex-a53",
            "-serial", "mon:stdio",
            "-device", loader_arg,
            "-m", "2G",
            "-dtb", b.getInstallPath(.prefix, "qemu_virt_aarch64.dtb"),
            "-nographic",
            "-global", "virtio-mmio.force-legacy=false",
            "-d", "guest_errors",
            "-drive", b.fmt("file={s},if=none,format=raw,id=hd", .{ b.getInstallPath(.prefix, "disk") }),
            "-device", "virtio-blk-device,drive=hd",
            "-device", "virtio-net-device,netdev=netdev0",
            "-netdev", "user,id=netdev0"
        });

        if (create_disk_option) {
            const create_disk_cmd = b.addSystemCommand(&[_][]const u8{
                "bash"
            });
            create_disk_cmd.addFileArg(libvmm_dep.path("tools/mkvirtdisk"));
            const disk = create_disk_cmd.addOutputFileArg("disk");
            create_disk_cmd.addArgs(&[_][]const u8{
                "1", "512", b.fmt("{}", .{ 1024 * 1024 * 16 }),
            });
            const disk_install = b.addInstallFile(disk, "disk");
            disk_install.step.dependOn(&create_disk_cmd.step);

            qemu_cmd.step.dependOn(&disk_install.step);
        }

        qemu_cmd.step.dependOn(b.default_step);
        // qemu_cmd.step.dependOn(&disk_install.step);
        const simulate_step = b.step("qemu", "Simulate the image using QEMU");
        simulate_step.dependOn(&qemu_cmd.step);
    }
}
