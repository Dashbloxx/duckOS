/*
    This file is part of duckOS.

    duckOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    duckOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with duckOS.  If not, see <https://www.gnu.org/licenses/>.

    Copyright (c) Byteduck 2016-2020. All rights reserved.
*/

#include <kernel/kstddef.h>
#include <kernel/multiboot.h>
#include <kernel/kstdio.h>
#include <kernel/memory/Memory.h>
#include <kernel/interrupt/idt.h>
#include <kernel/interrupt/isr.h>
#include <kernel/interrupt/irq.h>
#include <kernel/filesystem/Ext2.h>
#include <kernel/shell.h>
#include <kernel/pit.h>
#include <kernel/tasking/TaskManager.h>
#include <kernel/device/PartitionDevice.h>
#include <kernel/kmain.h>
#include <kernel/filesystem/VFS.h>
#include <kernel/device/TTYDevice.h>
#include <kernel/device/KeyboardDevice.h>
#include <common/defines.h>
#include <kernel/device/BochsVGADevice.h>
#include <kernel/device/MultibootVGADevice.h>
#include <kernel/memory/PageDirectory.h>
#include <kernel/device/PATADevice.h>
#include "CommandLine.h"

uint8_t boot_disk;

int kmain(uint32_t mbootptr){
	clearScreen();
	printf("init: Starting duckOS...\n");
	struct multiboot_info mboot_header = parse_mboot(mbootptr);
	load_gdt();
	interrupts_init();
	Memory::setup_paging();
	Device::init();
	CommandLine::init(mboot_header);

	//Try setting up VGA
	BochsVGADevice* bochs_vga = BochsVGADevice::create();
	if(bochs_vga) {
		//If we found a bochs vga device, set it up
		set_graphical_mode(bochs_vga->get_framebuffer_width(), bochs_vga->get_framebuffer_height(), bochs_vga->get_framebuffer());
	} else {
		//Otherwise, try using the multiboot VGA device
		auto* mboot_vga = MultibootVGADevice::create(&mboot_header);
		if(mboot_vga) {
			//Set it up if found
			if(mboot_vga->is_textmode()) {
				set_text_mode(mboot_vga->get_framebuffer_width(), mboot_vga->get_framebuffer_height(), mboot_vga->get_framebuffer());
			} else {
				set_graphical_mode(mboot_vga->get_framebuffer_width(), mboot_vga->get_framebuffer_height(), mboot_vga->get_framebuffer());
			}
		} else {
			//Fallback to textmode if all else fails
			printf("vga: Falling back to text mode.\n");
			void* vidmem = (void*) PageDirectory::k_mmap(0xB8000, 0xFA0, true);
			set_text_mode(80, 25, vidmem);
		}
	}

	clearScreen();
#ifdef DEBUG
	printf("init: Debug mode is enabled.\n");
#endif
	printf("init: First stage complete.\ninit: Initializing tasking...\n");
	TaskManager::init();
	ASSERT(false) //We should never get here
	return 0;
}

void shell_process(){
	Shell().shell();
}

void kmain_late(){
	new KeyboardDevice;
	printf("init: Tasking initialized.\ninit: Initializing TTY...\n");

	auto* tty0 = new TTYDevice(0, "tty0", 4, 0);
	tty0->set_active();

	printf("init: TTY initialized.\ninit: Initializing disk...\n");

	//Setup the disk (Assumes we're using primary master drive
	auto disk = DC::shared_ptr<PATADevice>(PATADevice::find(
					PATADevice::PRIMARY,
					PATADevice::MASTER,
					CommandLine::has_option("use_pio") //Use PIO if the command line option is present
				));
	if(!disk) {
		printf("init: Couldn't find IDE controller! Hanging...\n");
		while(1);
	}
	//Find the LBA of the first partition
	auto* mbr_buf = new uint8_t[512];
	disk->read_block(0, mbr_buf);
	uint32_t part_offset = *((uint32_t*) &mbr_buf[0x1C6]);
	delete[] mbr_buf;

	//Set up the PartitionDevice with that LBA
	auto part = DC::make_shared<PartitionDevice>(3, 1, disk, part_offset);
	auto part_descriptor = DC::make_shared<FileDescriptor>(part);

	//Check if the filesystem is ext2
	if(Ext2Filesystem::probe(*part_descriptor)){
		printf("init: Partition is ext2 ");
	}else{
		println("init: Partition is not ext2! Hanging.");
		while(true);
	}

	//Setup the filesystem
	auto* ext2fs = new Ext2Filesystem(part_descriptor);
	ext2fs->init();
	if(ext2fs->superblock.version_major < 1){
		printf("init: Unsupported ext2 version %d.%d. Must be at least 1. Hanging.", ext2fs->superblock.version_major, ext2fs->superblock.version_minor);
		while(true);
	}
	printf("%d.%d\n", ext2fs->superblock.version_major, ext2fs->superblock.version_minor);
	if(ext2fs->superblock.inode_size != 128){
		printf("init: Unsupported inode size %d. DuckOS only supports an inode size of 128 at this time. Hanging.", ext2fs->superblock.inode_size);
	}

	//Setup the virtual filesystem and mount the ext2 filesystem as root
	VFS* vfs = new VFS();
	if(!vfs->mount_root(ext2fs)) {
		printf("init: Failed to mount root. Hanging.");
		while(true);
	}

	printf("init: Done!\n");

	//Create the shell process and kill the kinit process
	TaskManager::add_process(Process::create_kernel("kshell", shell_process));
	TaskManager::current_process()->kill();
}

struct multiboot_info parse_mboot(uint32_t physaddr){
	auto* header = (struct multiboot_info*) (physaddr + HIGHER_HALF);

	//Check boot disk
	if(header->flags & MULTIBOOT_INFO_BOOTDEV) {
		boot_disk = (header->boot_device & 0xF0000000u) >> 28u;
		printf("init: BIOS boot disk: 0x%x\n", boot_disk);
	} else {
		PANIC("MULTIBOOT_FAIL", "The multiboot header doesn't have boot device info. Cannot boot.\n", true);
	}

	//Parse memory map
	if(header->flags & MULTIBOOT_INFO_MEM_MAP) {
		auto* mmap_entry = (multiboot_mmap_entry*) (header->mmap_addr + HIGHER_HALF);
		Memory::parse_mboot_memory_map(header, mmap_entry);
	} else {
		PANIC("MULTIBOOT_FAIL", "The multiboot header doesn't have a memory map. Cannot boot.\n", true);
	}

	return *header;
}

void interrupts_init(){
	//Register the IDT
	Interrupt::register_idt();
	//Setup ISR handlers
	Interrupt::isr_init();
	//Setup the syscall handler
	Interrupt::idt_set_gate(0x80, (unsigned)asm_syscall_handler, 0x08, 0xEF);
	//Setup the PIT used for timing / preemption
	PIT::init();
	//Setup IRQ handlers
	Interrupt::irq_init();
	//Start interrupts
	sti();
}
