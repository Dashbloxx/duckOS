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

#ifndef ELF_H
#define ELF_H

#include <kernel/kstddef.h>
#include <kernel/kstdio.h>
#include <kernel/filesystem/FileDescriptor.h>

#define ELF_MAGIC 0x464C457F //0x7F followed by 'ELF'

#define ELF32 1
#define ELF64 2

#define ELF_LITTLE_ENDIAN 1
#define ELF_BIG_ENDIAN 2

#define ELF_TYPE_RELOCATABLE 1
#define ELF_TYPE_EXECUTABLE 2
#define ELF_TYPE_SHARED 3
#define ELF_TYPE_CORE 4

#define ELF_NO_ARCH 0
#define ELF_X86 3

#define ELF_PT_NULL 0
#define ELF_PT_LOAD 1
#define ELF_PT_DYNAMIC 2
#define ELF_PT_INTERP 3
#define ELF_PT_NOTE 4
#define ELF_PT_SHLIB 5
#define ELF_PT_PHDR 6

#define ELF_PF_X 1u
#define ELF_PF_W 2u
#define ELF_PF_R 4u

#include <common/vector.hpp>

namespace ELF {
	typedef struct __attribute__((packed)) elf32_header {
		uint32_t magic;
		uint8_t bits; //1 == 32 bit, 2 == 64 bit
		uint8_t endianness;
		uint8_t header_version;
		uint8_t os_abi;
		uint8_t padding[8];
		uint16_t type;
		uint16_t instruction_set;
		uint32_t elf_version;
		uint32_t program_entry_position;
		uint32_t program_header_table_position;
		uint32_t section_header_table_position;
		uint32_t flags; //Not used in x86 ELFs
		uint16_t header_size;
		uint16_t program_header_table_entry_size;
		uint16_t program_header_table_entries;
		uint16_t section_header_table_entry_size;
		uint16_t section_header_table_entries;
		uint16_t section_names_index;
	} elf32_header;

	typedef struct __attribute__((packed)) elf32_segment_header {
		uint32_t p_type;
		uint32_t p_offset;
		uint32_t p_vaddr;
		uint32_t p_paddr;
		uint32_t p_filesz;
		uint32_t p_memsz;
		uint32_t p_flags;
		uint32_t p_align;
	} elf32_segment_header;

	bool is_valid_elf_header(elf32_header* header);
	bool can_execute(elf32_header* header);

	/**
	 * Reads the program headers of an ELF file.
	 * @param fd The file descriptor of the ELF file.
	 * @param header The elf32_header of the file.
	 * @return An error or a vector containing the segment headers.
	 */
	ResultRet<DC::vector<ELF::elf32_segment_header>> read_program_headers(FileDescriptor& fd, elf32_header* header);

	/**
	 * Reads the INTERP section of an ELF file.
	 * @param fd The file descriptor of the ELF file.
	 * @param headers The program headers (loaded by ELF::read_program_headers)
	 * @return An error or the INTERP section. -ENOENT if there is no INTERP section.
	 */
	ResultRet<DC::string> read_interp(FileDescriptor& fd, DC::vector<elf32_segment_header>& headers);

	/**
	 * Loads the sections of an ELF file into memory.
	 * @param fd The file descriptor of the ELF file.
	 * @param headers The program headers (loaded by ELF::read_program_headers)
	 * @param page_directory The page directory to load the program into.
	 * @return An error or the program break.
	 */
	ResultRet<size_t> load_sections(FileDescriptor& fd, DC::vector<elf32_segment_header>& headers, PageDirectory* page_directory);
}

#endif
