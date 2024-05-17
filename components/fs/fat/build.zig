// Zig build system, not work yet
const Builder = @import("std").build.Builder;

pub fn build(b: *Builder) void {
    const target = b.standardTargetOptions(.{});
    const mode = b.standardReleaseOptions();

    const exe = b.addExecutable("FatFs", null);
    exe.setTarget(target);
    exe.setBuildMode(mode);

    // Add all source files
    exe.addCSourceFile("AsyncFATFs.c", &[_][]const u8{"-ffreestanding"});
    exe.addCSourceFile("AsyncFATFunc.c", &[_][]const u8{"-ffreestanding"});
    exe.addCSourceFile("Asyncdiskio.c", &[_][]const u8{"-ffreestanding"});
    exe.addCSourceFile("ff15/source/diskio.c", &[_][]const u8{"-ffreestanding"});
    exe.addCSourceFile("ff15/source/ff.c", &[_][]const u8{"-ffreestanding"});
    exe.addCSourceFile("ff15/source/ffsystem.c", &[_][]const u8{"-ffreestanding"});
    exe.addCSourceFile("ff15/source/ffunicode.c", &[_][]const u8{"-ffreestanding"});

    // Set include directory for musl headers if needed
    exe.addIncludeDir("../../../dep/musllibc/include/");

    // Link musl libc
    // exe.addLibPath("/path/to/musl/lib");
    // exe.linkLib("c");

    // Add linker script if needed
    // exe.addLinkerArgs(&[_][]const u8{"-T", "path/to/linker.ld"});

    // Additional linker flags if needed for ELF output
    exe.addLinkerArgs(&[_][]const u8{ "-nostdlib", "-nodefaultlibs", "-nostartfiles", "-ffreestanding" });

    // Set output format to ELF
    exe.setOutputFormat(.Elf);

    b.installArtifact(exe);
}
