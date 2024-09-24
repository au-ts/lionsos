const std = @import("std");
const builtin = @import("builtin");

const mod_sdf = @import("sdf");
const lionsos = mod_sdf.lionsos;
const sddf = mod_sdf.sddf;
const dtb = mod_sdf.dtb;

const ArrayList = std.ArrayList;
const Allocator = std.mem.Allocator;

const SystemDescription = mod_sdf.sdf.SystemDescription;
const Pd = SystemDescription.ProtectionDomain;
const Vm = SystemDescription.VirtualMachine;
const Mr = SystemDescription.MemoryRegion;
const Map = SystemDescription.Map;
const Irq = SystemDescription.Interrupt;
const Channel = SystemDescription.Channel;

const VirtualMachineSystem = mod_sdf.vmm.VirtualMachineSystem;

const MicrokitBoard = enum {
    qemu_virt_aarch64,
    maaxboard,

    pub fn fromStr(str: []const u8) !MicrokitBoard {
        inline for (std.meta.fields(MicrokitBoard)) |field| {
            if (std.mem.eql(u8, str, field.name)) {
                return @enumFromInt(field.value);
            }
        }

        return error.BoardNotFound;
    }

    pub fn arch(b: MicrokitBoard) SystemDescription.Arch {
        return switch (b) {
            .qemu_virt_aarch64, .maaxboard => .aarch64,
        };
    }

    pub fn printFields() void {
        comptime var i: usize = 0;
        const fields = @typeInfo(@This()).Enum.fields;
        inline while (i < fields.len) : (i += 1) {
            std.debug.print("{s}\n", .{fields[i].name});
        }
    }
};


var xml_out_path: []const u8 = undefined;
var sddf_path: []const u8 = "sddf";
var dtbs_path: []const u8 = "dtbs";
var board: MicrokitBoard = undefined;

const usage_text =
    \\Usage sdfgen --board [BOARD] --example [EXAMPLE SYSTEM] [options]
    \\
    \\Generates a Microkit system description file programatically
    \\
    \\ Options:
    \\ --board <board>
    \\      The possible values for this option are: {s}
    \\ --sdf <path>     (default: ./example.system) Path to output the generated system description file
    \\ --sddf <path>    (default: ./sddf/) Path to the sDDF repository
    \\ --dtbs <path>     (default: ./dtbs/) Path to directory of Device Tree Blobs
    \\
;

const usage_text_formatted = std.fmt.comptimePrint(usage_text, .{ MicrokitBoard.fields() });

fn parseArgs(args: []const []const u8, allocator: Allocator) !void {
    const stdout = std.io.getStdOut();

    const board_fields = comptime std.meta.fields(MicrokitBoard);
    var board_options = ArrayList(u8).init(allocator);
    defer board_options.deinit();
    inline for (board_fields) |field| {
        try board_options.appendSlice("\n           ");
        try board_options.appendSlice(field.name);
    }

    const usage_text_fmt = try std.fmt.allocPrint(allocator, usage_text, .{ board_options.items });
    defer allocator.free(usage_text_fmt);

    var board_given = false;

    var arg_i: usize = 1;
    while (arg_i < args.len) : (arg_i += 1) {
        const arg = args[arg_i];
        if (std.mem.eql(u8, arg, "-h") or std.mem.eql(u8, arg, "--help")) {
            try stdout.writeAll(usage_text_fmt);
            std.process.exit(0);
        } else if (std.mem.eql(u8, arg, "--sdf")) {
            arg_i += 1;
            if (arg_i >= args.len) {
                std.debug.print("'{s}' requires an argument.\n{s}", .{ arg, usage_text_fmt });
                std.process.exit(1);
            }
            xml_out_path = args[arg_i];
        } else if (std.mem.eql(u8, arg, "--board")) {
            arg_i += 1;
            if (arg_i >= args.len) {
                std.debug.print("'{s}' requires an argument.\n{s}", .{ arg, usage_text_fmt });
                std.process.exit(1);
            }
            board = MicrokitBoard.fromStr(args[arg_i]) catch {
                std.debug.print("Invalid board '{s}' given\n", .{args[arg_i]});
                std.process.exit(1);
            };
            board_given = true;
        } else if (std.mem.eql(u8, arg, "--sddf")) {
            arg_i += 1;
            if (arg_i >= args.len) {
                std.debug.print("'{s}' requires a path to the sDDF repository.\n{s}", .{ arg, usage_text_fmt });
                std.process.exit(1);
            }
            sddf_path = args[arg_i];
        } else if (std.mem.eql(u8, arg, "--dtbs")) {
            arg_i += 1;
            if (arg_i >= args.len) {
                std.debug.print("'{s}' requires a path to the directory holding all the DTBs.\n{s}", .{ arg, usage_text_fmt });
                std.process.exit(1);
            }
            dtbs_path = args[arg_i];
        } else {
            std.debug.print("unrecognized argument: '{s}'\n{s}", .{ arg, usage_text_fmt });
            std.process.exit(1);
        }
    }

    if (arg_i == 1) {
        try stdout.writeAll(usage_text_fmt);
        std.process.exit(1);
    }

    if (!board_given) {
        std.debug.print("Missing '--board' argument\n", .{});
        std.process.exit(1);
    }
}

pub fn main() !void {
    // An arena allocator makes much more sense for our purposes, all we're doing is doing a bunch
    // of allocations in a linear fashion and then just tearing everything down. This has better
    // performance than something like the General Purpose Allocator.
    // TODO: have a build argument that swaps the allocator.
    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    const allocator = arena.allocator();
    defer arena.deinit();

    const args = try std.process.argsAlloc(allocator);
    defer std.process.argsFree(allocator, args);
    try parseArgs(args, allocator);

    // Check that path to sDDF exists
    std.fs.cwd().access(sddf_path, .{}) catch |err| {
        switch (err) {
            error.FileNotFound => {
                std.debug.print("Path to sDDF '{s}' does not exist\n", .{sddf_path});
                std.process.exit(1);
            },
            else => {
                std.debug.print("Could not access sDDF directory '{s}' due to error: {}\n", .{ sddf_path, err });
                std.process.exit(1);
            },
        }
    };

    // Check that path to DTB exists
    const board_dtb_path = try std.fmt.allocPrint(allocator, "{s}/{s}.dtb", .{ dtbs_path, @tagName(board) });
    defer allocator.free(board_dtb_path);
    std.fs.cwd().access(board_dtb_path, .{}) catch |err| {
        switch (err) {
            error.FileNotFound => {
                std.debug.print("Path to board DTB '{s}' does not exist\n", .{board_dtb_path});
                std.process.exit(1);
            },
            else => {
                std.debug.print("Could not access DTB directory '{s}' due to error: {}\n", .{ board_dtb_path, err });
                std.process.exit(1);
            },
        }
    };
    std.debug.print("Reading DTB: '{s}'\n", .{ board_dtb_path });

    // Read the DTB contents
    const dtb_file = try std.fs.cwd().openFile(board_dtb_path, .{});
    const dtb_size = (try dtb_file.stat()).size;
    const blob_bytes = try dtb_file.reader().readAllAlloc(allocator, dtb_size);
    // Parse the DTB
    var blob = try dtb.parse(allocator, blob_bytes);
    // TODO: the allocator should already be known by the DTB...
    defer blob.deinit(allocator);

    // Before doing any kind of XML generation we should probe sDDF for
    // configuration files etc
    try sddf.probe(allocator, sddf_path);

    const compatible_drivers = try sddf.compatibleDrivers(allocator);
    defer allocator.free(compatible_drivers);

    std.debug.print("sDDF drivers found:\n", .{});
    for (compatible_drivers) |driver| {
        std.debug.print("   - {s}\n", .{driver});
    }

    var sdf = try SystemDescription.create(allocator, board.arch());

    const uart_node = switch (board) {
        .qemu_virt_aarch64 => blob.child("pl011@9000000").?,
        .maaxboard => blob.child("soc@0").?.child("bus@30800000").?.child("serial@30860000").?,
    };
    const blk_node = switch (board) {
        .qemu_virt_aarch64 => blob.child("virtio_mmio@a003e00").?,
        .maaxboard => blob.child("soc@0").?.child("bus@30800000").?.child("mmc@30b40000").?,
    };
    const net_node = switch (board) {
        .qemu_virt_aarch64 => blob.child("virtio_mmio@a003c00").?,
        .maaxboard => blob.child("soc@0").?.child("bus@30800000").?.child("ethernet@30be0000").?,
    };
    const uart_driver_elf = switch (board) {
        .qemu_virt_aarch64 => "driver_uart_arm.elf",
        .maaxboard => "driver_uart_imx.elf",
    };
    const blk_driver_elf = switch (board) {
        .qemu_virt_aarch64 => "driver_blk_virtio.elf",
        .maaxboard => "driver_blk_mmc_imx.elf",
    };
    const net_driver_elf = switch (board) {
        .qemu_virt_aarch64 => "driver_net_virtio.elf",
        .maaxboard => "driver_net_imx.elf",
    };

    var vmm = Pd.create(&sdf, "vmm", "vmm.elf");
    var vm = Vm.create(&sdf, "linux", &.{ .{ .id = 0, .cpu = 0 }});

    var uart_driver = Pd.create(&sdf, "uart", uart_driver_elf);
    var serial_virt_tx = Pd.create(&sdf, "serial_virt_tx", "serial_virt_tx.elf");
    var serial_virt_rx = Pd.create(&sdf, "serial_virt_rx", "serial_virt_rx.elf");

    var net_driver = Pd.create(&sdf, "net_driver", net_driver_elf);
    var net_virt_tx = Pd.create(&sdf, "net_virt_tx", "net_virt_tx.elf");
    var net_virt_rx = Pd.create(&sdf, "net_virt_rx", "net_virt_rx.elf");
    var vmm_net_copy = Pd.create(&sdf, "vmm_net_copy", "net_copy.elf");

    var blk_driver = Pd.create(&sdf, "blk_driver", blk_driver_elf);
    var blk_virt = Pd.create(&sdf, "blk_virt", "blk_virt.elf");

    // TODO: fix path
    const guest_dtb_file = try std.fs.cwd().openFile("zig-out/linux.dtb", .{});
    const guest_dtb_size = (try guest_dtb_file.stat()).size;
    const guest_blob_bytes = try guest_dtb_file.reader().readAllAlloc(allocator, guest_dtb_size);
    // Parse the DTB
    var guest_dtb_blob = try dtb.parse(allocator, guest_blob_bytes);
    defer guest_dtb_blob.deinit(allocator);

    var vmm_system = VirtualMachineSystem.init(allocator, &sdf, &vmm, &vm, guest_dtb_blob);

    var serial_system = try sddf.SerialSystem.init(allocator, &sdf, uart_node, &uart_driver, &serial_virt_tx, &serial_virt_rx, .{});
    serial_system.addClient(&vmm);

    var blk_system = sddf.BlockSystem.init(allocator, &sdf, blk_node, &blk_driver, &blk_virt, .{});
    blk_system.addClient(&vmm);

    var net_system = sddf.NetworkSystem.init(allocator, &sdf, net_node, &net_driver, &net_virt_rx, &net_virt_tx, .{});
    net_system.addClientWithCopier(&vmm, &vmm_net_copy);

    var timer_system: sddf.TimerSystem = undefined;
    if (board == .maaxboard) {
        // The MaaXBoard driver requires access to a timer, right now we do not
        // track dependencies of drivers on other drivers so we manually create a TimerSystem
        // and give the driver access.
        var timer_driver = Pd.create(&sdf, "timer_driver", "driver_timer_imx.elf");
        const timer_node = blob.child("soc@0").?.child("bus@30000000").?.child("gpt@302d0000").?;
        timer_system = sddf.TimerSystem.init(allocator, &sdf, &timer_driver, timer_node);

        timer_system.addClient(&blk_driver);
        sdf.addProtectionDomain(&timer_driver);
    }

    sdf.addProtectionDomain(&vmm);
    sdf.addProtectionDomain(&uart_driver);
    sdf.addProtectionDomain(&serial_virt_tx);
    sdf.addProtectionDomain(&serial_virt_rx);
    sdf.addProtectionDomain(&blk_driver);
    sdf.addProtectionDomain(&blk_virt);
    sdf.addProtectionDomain(&net_driver);
    sdf.addProtectionDomain(&net_virt_tx);
    sdf.addProtectionDomain(&net_virt_rx);
    sdf.addProtectionDomain(&vmm_net_copy);

    uart_driver.priority = 200;
    blk_driver.priority = 100;
    net_driver.priority = 100;
    net_driver.budget = 100;
    net_driver.period = 400;

    net_virt_rx.budget = 100;
    net_virt_rx.period = 500;

    net_virt_tx.budget = 100;
    net_virt_tx.period = 500;

    serial_virt_rx.priority = 199;
    serial_virt_tx.priority = 199;
    blk_virt.priority = 99;
    net_virt_rx.priority = 98;
    net_virt_tx.priority = 99;

    vmm_net_copy.priority = 2;
    vmm_net_copy.budget = 20000;

    vmm.priority = 1;

    try vmm_system.connect();
    try serial_system.connect();
    try blk_system.connect();
    try net_system.connect();
    // TODO: we have a problem with ID allocation of PDs.
    // If we call timer_system.connect too early, the block driver's
    // channel ID of zero will be allocated but we need channel zero
    // for the block device IRQ.
    if (board == .maaxboard) {
        try timer_system.connect();
    }
    const xml = try sdf.toXml();

    var xml_file = try std.fs.cwd().createFile(xml_out_path, .{});
    defer xml_file.close();
    _ = try xml_file.write(xml);
}
