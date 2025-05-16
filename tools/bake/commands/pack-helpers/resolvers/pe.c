/**
 * Copyright, Philip Meulengracht
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
#include <string.h>

extern int __resolve_load_file(const char* path, void** bufferOut, size_t* sizeOut);
extern int __resolve_add_dependency(struct list* dependencies, const char* library);

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

static char* __rva_to_file_offset(struct __pe_address_mapping* mappings, uintptr_t rva)
{
    int i = 0;
    while (mappings[i].valid) {
        if (rva >= mappings[i].voffset && rva < mappings[i].voffset + mappings[i].size) {
            return (char*)mappings[i].data + (rva - mappings[i].voffset);
        }
        i++;
    }
    return NULL;
}

static int __read_sections(const char* buffer, struct __pe_address_mapping** mappingsOut)
{
    struct __pe_address_mapping* mappings;
    PeHeader*         pe       = __get_peheader(buffer);
    PeOptionalHeader* optional = __get_optionalheader(buffer);
    PeSectionHeader*  section = (PeSectionHeader*)((char*)pe + sizeof(PeHeader) + pe->SizeOfOptionalHeader);
    
    mappings = (struct __pe_address_mapping*)calloc(sizeof(struct __pe_address_mapping), pe->NumSections + 1);
    if (!mappings) {
        fprintf(stderr, "Failed to allocate memory for section mappings\n");
        return -1;
    }

    for (uint16_t i = 0; i < pe->NumSections; i++, section++) {
        mappings[i].data = buffer + section->RawAddress;
        mappings[i].voffset = section->VirtualAddress;
        mappings[i].size = section->RawSize;
        mappings[i].valid = 1;
    }

    *mappingsOut = mappings;
    return 0;
}

static int __parse_imports(struct __pe_address_mapping* mappings, const char* contents, struct list* dependencies)
{
    PeImportDescriptor* descriptor = (PeImportDescriptor*)contents;
    while (descriptor->ImportAddressTable != 0) {
        const char* library = __rva_to_file_offset(mappings, descriptor->ModuleName);
        if (library) {
            if (__resolve_add_dependency(dependencies, library) != 0) {
                fprintf(stderr, "Failed to add dependency %s\n", library);
                return -1;
            }
        }
        descriptor++;
    }
    return 0;
}

static int __parse_dependencies_64(const char* buffer, size_t bufferSize, struct list* dependencies)
{
    PeHeader*                    pe     = __get_peheader(buffer);
    PeOptionalHeader64*          header = (PeOptionalHeader64*)__get_optionalheader(buffer);
    PeDataDirectory*             directory;
    struct __pe_address_mapping* mappings;
    int                          status;

    status = __read_sections(buffer, &mappings);
    if (status != 0) {
        return status;
    }

    directory = &header->Directories[PE_SECTION_IMPORT];
    if (directory->Size == 0) {
        free(mappings);
        return 0;
    }

    status = __parse_imports(mappings, __rva_to_file_offset(mappings, directory->AddressRVA), dependencies);
    free(mappings);
    return status;
}

static int __parse_dependencies_32(const char* buffer, size_t bufferSize, struct list* dependencies)
{
    PeHeader*                    pe     = __get_peheader(buffer);
    PeOptionalHeader32*          header = (PeOptionalHeader32*)__get_optionalheader(buffer);
    PeDataDirectory*             directory;
    struct __pe_address_mapping* mappings;
    int                          status;

    status = __read_sections(buffer, &mappings);
    if (status != 0) {
        return status;
    }

    directory = &header->Directories[PE_SECTION_IMPORT];
    if (directory->Size == 0) {
        free(mappings);
        return 0;
    }

    status = __parse_imports(mappings, __rva_to_file_offset(mappings, directory->AddressRVA), dependencies);
    free(mappings);
    return status;
}

static int __parse_dependencies(const char* buffer, size_t bufferSize, struct list* dependencies)
{
    PeOptionalHeader* optional = __get_optionalheader(buffer);
    if (optional->Architecture == PE_ARCHITECTURE_64) {
        return __parse_dependencies_64(buffer, bufferSize, dependencies);
    }
    return __parse_dependencies_32(buffer, bufferSize, dependencies);
}

static enum kitchen_resolve_arch __pe_machine_to_arch(uint16_t pe_arch)
{
    switch (pe_arch) {
        case IMAGE_FILE_MACHINE_AMD64:
            return KITCHEN_RESOLVE_ARCH_X86_64;
        case IMAGE_FILE_MACHINE_ARMNT:
        case IMAGE_FILE_MACHINE_ARM:
            return KITCHEN_RESOLVE_ARCH_ARM;
        case IMAGE_FILE_MACHINE_ARM64:
            return KITCHEN_RESOLVE_ARCH_ARM64;
        case IMAGE_FILE_MACHINE_I386:
            return KITCHEN_RESOLVE_ARCH_X86;
        case IMAGE_FILE_MACHINE_POWERPC:
            return KITCHEN_RESOLVE_ARCH_PPC;            
        case IMAGE_FILE_MACHINE_RISCV32:
            return KITCHEN_RESOLVE_ARCH_RISCV32;
        case IMAGE_FILE_MACHINE_RISCV64:
            return KITCHEN_RESOLVE_ARCH_RISCV64;
        case IMAGE_FILE_MACHINE_RISCV128:
            return KITCHEN_RESOLVE_ARCH_RISCV128;
        default:
            return KITCHEN_RESOLVE_ARCH_UNKNOWN;
    }
}

int pe_is_valid(const char* path, enum kitchen_resolve_arch* arch)
{
    MzHeader* mz;
    PeHeader* pe;
    FILE*     file;
    size_t    read;
    char      buffer[0x200];

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
    if (pe->Magic != PE_MAGIC) {
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

    status = __resolve_load_file(path, (void**)&buffer, &bufferSize);
    if (status) {
        fprintf(stderr, "oven: failed to load file: %s\n", path);
        return status;
    }

    status = __parse_dependencies(buffer, bufferSize, dependencies);
    free(buffer);
    return 0;
}

#ifdef TEST
#include "common.c"
// gcc -I../../platform/include -DTEST ./pe.c
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
