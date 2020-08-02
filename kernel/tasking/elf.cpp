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

#include <kernel/tasking/elf.h>
#include <common/defines.h>
#include <kernel/filesystem/VFS.h>
#include <kernel/memory/Memory.h>
#include "TaskManager.h"

bool ELF::is_valid_elf_header(elf32_header* header) {
	return header->magic == ELF_MAGIC;
}

bool ELF::can_execute(elf32_header *header) {
	if(!is_valid_elf_header(header)) return false;
	if(header->bits != ELF32) return false;
	if(header->instruction_set != ELF_X86) return false;
	if(header->elf_version != 0x1) return false;
	if(header->header_version != 0x1) return false;
	if(header->type != ELF_TYPE_EXECUTABLE) return false;
	return header->endianness == ELF_LITTLE_ENDIAN;
}

ResultRet<DC::vector<ELF::elf32_segment_header>> ELF::read_program_headers(FileDescriptor& fd, elf32_header* header) {
	uint32_t pheader_loc = header->program_header_table_position;
	uint32_t pheader_size = header->program_header_table_entry_size;
	uint32_t num_pheaders = header->program_header_table_entries;

	//Seek to the pheader_loc
	auto res = fd.seek(pheader_loc, SEEK_SET);
	if(res < 0) return res;

	//Create the segment header vector and read the headers into it
	DC::vector<elf32_segment_header> program_headers(num_pheaders);
	res = fd.read((uint8_t*)program_headers.storage(), pheader_size * num_pheaders);
	if(res <= 0) {
		if(res < 0) return res;
		else return -EIO;
	}

	return program_headers;
}

ResultRet<DC::string> ELF::read_interp(FileDescriptor& fd, DC::vector<elf32_segment_header>& headers) {
	for(size_t i = 0; i < headers.size(); i++) {
		elf32_segment_header& header = headers[i];
		if (header.p_type == ELF_PT_INTERP) {
			//Seek to interpreter section
			auto res = fd.seek(header.p_offset, SEEK_SET);
			if(res < 0) return res;

			//Read the interpreter
			auto* interp = new char[header.p_filesz];
			res = fd.read((uint8_t*) interp, header.p_filesz);
			if(res <= 0) {
				if(res < 0) return res;
				else return -EIO;
			}

			//Return it
			DC::string ret(interp);
			delete[] interp;
			return DC::move(ret);
		}
	}

	//No interpreter
	return -ENOENT;
}

ResultRet<size_t> ELF::load_sections(FileDescriptor& fd, DC::vector<elf32_segment_header>& headers, PageDirectory* page_directory) {
	uint32_t current_brk = 0;

	for(uint32_t i = 0; i < headers.size(); i++) {
		auto& header = headers[i];
		if(header.p_type == ELF_PT_LOAD) {
			size_t loadloc_pagealigned = (header.p_vaddr/PAGE_SIZE) * PAGE_SIZE;
			size_t loadsize_pagealigned = header.p_memsz + (header.p_vaddr % PAGE_SIZE);

			//Allocate a kernel memory region to load the section into
			LinkedMemoryRegion tmp_region = PageDirectory::k_alloc_region(loadsize_pagealigned);

			//Read the section into the region
			fd.seek(header.p_offset, SEEK_SET);
			fd.read((uint8_t*) tmp_region.virt->start + (header.p_vaddr - loadloc_pagealigned), header.p_filesz);

			//Allocate a program vmem region
			MemoryRegion* vmem_region = page_directory->vmem_map().allocate_region(loadloc_pagealigned, loadsize_pagealigned);
			if(!vmem_region) {
				//If we failed to allocate the program vmem region, free the tmp region
				Memory::pmem_map().free_region(tmp_region.phys);
				printf("FATAL: Failed to allocate a vmem region in load_elf!\n");
				return -ENOMEM;
			}

			//Unmap the region from the kernel
			PageDirectory::k_unmap_region(tmp_region);
			PageDirectory::kernel_vmem_map.free_region(tmp_region.virt);

			//Map the physical region to the program's vmem region
			LinkedMemoryRegion prog_region(tmp_region.phys, vmem_region);
			page_directory->map_region(prog_region, header.p_flags & ELF_PF_W);

			if(current_brk < header.p_vaddr + header.p_memsz)
				current_brk = header.p_vaddr + header.p_memsz;
		}
	}

	return current_brk;
}