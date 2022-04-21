/**
 * Copyright 2022, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 * 
 */

#include "pe.h"
#include "resolvers.h"
#include <stdio.h>
#include <stdlib.h>

struct __pe_address_mapping {
    const char* data;
    size_t      voffset;
    size_t      size;
    int         valid;
};

static inline MzHeader* __get_mzheader(const char* buffer)
{
    return (MzHeader*)buffer;
}

static inline PeHeader* __get_peheader(const char* buffer)
{
    MzHeader* mz = __get_mzheader(buffer);
    return (PeHeader*)(buffer + mz->PeHeaderAddress);
}

static inline PeOptionalHeader* __get_optionalheader(const char* buffer)
{
    PeHeader* pe = __get_peheader(buffer);
    return (PeOptionalHeader*)((char*)pe + sizeof(PeHeader));
}

static void __read_sections(const char* buffer, struct __pe_address_mapping** mappingsOut)
{
    struct __pe_address_mapping* mappings;
    PeHeader*         pe       = __get_peheader(buffer);
    PeOptionalHeader* optional = __get_optionalheader(buffer);
    PeSectionHeader*  section = (PeSectionHeader*)((char*)pe + sizeof(PeHeader) + pe->SizeOfOptionalHeader);
    
    mappings = (struct __pe_address_mapping*)calloc(sizeof(struct __pe_address_mapping), pe->NumSections + 1);
    if (!mappings) {
        fprintf(stderr, "Failed to allocate memory for section mappings\n");
        return;
    }

    for (uint16_t i = 0; i < pe->NumSections; i++) {
        sections[i].name = section->Name;
        sections[i].virtual_address = section->VirtualAddress;
        sections[i].virtual_size = section->VirtualSize;
        sections[i].raw_address = section->PointerToRawData;
        sections[i].raw_size = section->SizeOfRawData;
        sections[i].characteristics = section->Characteristics;
        section++;
    }

    *mappingsOut = mappings;
}

static int __parse_dependencies_64(const char* buffer, size_t bufferSize, struct list* dependencies)
{
    PeHeader*                    pe     = __get_peheader(buffer);
    PeOptionalHeader64*          header = (PeOptionalHeader64*)__get_optionalheader(buffer);
    struct __pe_address_mapping* mappings;


}

static int __parse_dependencies_32(const char* buffer, size_t bufferSize, struct list* dependencies)
{
    PeOptionalHeader32* header = (PeOptionalHeader32*)__get_optionalheader(buffer);
}

static int __parse_dependencies(const char* buffer, size_t bufferSize, struct list* dependencies)
{
    PeOptionalHeader* optional = __get_optionalheader(buffer);
    if (optional->Architecture == PE_ARCHITECTURE_64) {
        return __parse_dependencies_64(buffer, bufferSize, dependencies);
    }
    return __parse_dependencies_32(buffer, bufferSize, dependencies);
}

static enum oven_resolve_arch __pe_machine_to_arch(uint16_t pe_arch)
{
    switch (pe_arch) {
        case EM_386:
            return OVEN_RESOLVE_ARCH_X86;
        case EM_X86_64:
            return OVEN_RESOLVE_ARCH_X86_64;
        case EM_ARM:
            return OVEN_RESOLVE_ARCH_ARM;
        case EM_AARCH64:
            return OVEN_RESOLVE_ARCH_ARM64;
        case EM_MIPS:
            return OVEN_RESOLVE_ARCH_MIPS;
        case EM_MIPS_X:
            return OVEN_RESOLVE_ARCH_MIPS64;
        case EM_PPC:
            return OVEN_RESOLVE_ARCH_PPC;
        case EM_PPC64:
            return OVEN_RESOLVE_ARCH_PPC64;
        case EM_SPARC:
            return OVEN_RESOLVE_ARCH_SPARC;
        case EM_SPARCV9:
            return OVEN_RESOLVE_ARCH_SPARV9;
        case EM_S390:
            return OVEN_RESOLVE_ARCH_S390;
        default:
            return OVEN_RESOLVE_ARCH_UNKNOWN;
    }
}

int pe_is_valid(const char* path, enum oven_resolve_arch* arch)
{
    MzHeader*         mz;
    PeHeader*         pe;
    PeOptionalHeader* optional;
    FILE*             file;
    size_t            read;
    char              buffer[0x200];

    file = fopen(path, "rb");
    if (file == NULL) {
        return -1;
    }

    read = fread(&buffer[0], 1, sizeof(buffer), file);
    fclose(file);
    
    if (read != sizeof(buffer)) {
        return -1;
    }

    // verify the validity of the mz header
    mz = __get_mzheader(buffer);
    if (mz->Signature != MZ_MAGIC) {
        return -1;
    }

    // verify the validity of the pe header
    pe = __get_peheader(buffer);
    if (pe->Signature != PE_MAGIC) {
        return -1;
    }

    *arch = __pe_machine_to_arch(pe->Machine);
    return 0;
}

int pe_resolve_dependencies(const char* path, struct list* dependencies)
{
    char*  buffer;
    size_t bufferSize;
    int    status;

    status = __load_file(path, (void**)&buffer, &bufferSize);
    if (status) {
        fprintf(stderr, "oven: failed to load file: %s\n", path);
        return status;
    }

    status = __parse_dependencies(buffer, bufferSize, dependencies);
    free(buffer);
    return 0;
}

#ifdef TEST

int main(int argc, char** argv)
{
    struct list dependencies;
    list_init(&dependencies);

    if (argc < 2) {
        fprintf(stderr, "usage: %s <pe file>\n", argv[0]);
        return 1;
    }

    if (pe_resolve_dependencies(argv[1], &dependencies)) {
        fprintf(stderr, "oven: failed to resolve dependencies\n");
        return 1;
    }

    return 0;
}

#endif
