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

#include <errno.h>
#include "elf.h"
#include "resolvers.h"
#include <chef/list.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int __resolve_load_file(const char* path, void** bufferOut, size_t* sizeOut);
extern int __resolve_add_dependency(struct list* dependencies, const char* library);

struct __elf_address_mapping {
    const char* data;
    size_t      voffset;
    size_t      size;
    int         valid;
};

static const char* __get_string_from_strtab32(const char* strTable, Elf32_Xword index)
{
    return strTable + index;
}

static int __parse_dynamic_section32(struct __elf_address_mapping* mappings, const char* dynamicTable, size_t dynamicTableSize, struct list* dependencies)
{
    Elf32_Dyn*  di;
    size_t      i;
    size_t      strTableOffset = 0;
    const char* strTable       = NULL;

    for (i = 0, di = (Elf32_Dyn*)dynamicTable;
         i < dynamicTableSize; i += sizeof(Elf32_Dyn), di++) {
        if (di->d_tag == DT_STRTAB) {
            strTableOffset = di->d_un.d_ptr;
        } else if (di->d_tag == DT_NULL) {
            break;
        }
    }

    if (strTableOffset == 0) {
        fprintf(stderr, "oven: could not find string table offset\n");
        return -1;
    }

    // find the correct file offset
    for (i = 0; mappings[i].valid; i++) {
        if (strTableOffset >= mappings[i].voffset && strTableOffset < (mappings[i].voffset + mappings[i].size)) {
            strTable = mappings[i].data + (strTableOffset - mappings[i].voffset);
            break;
        }
    }

    if (strTable == NULL) {
        fprintf(stderr, "Could not find string table for dynamic section\n");
        return -1;
    }
    
    for (i = 0, di = (Elf32_Dyn*)dynamicTable;
         i < dynamicTableSize; i += sizeof(Elf32_Dyn), di++) {
        if (di->d_tag == DT_NEEDED) {
            if (__resolve_add_dependency(dependencies, __get_string_from_strtab32(strTable, di->d_un.d_val))) {
                return -1;
            }
        } else if (di->d_tag == DT_NULL) {
            break;
        }
    }

    return 0;
}

static int __parse_dependencies_32(const char* buffer, size_t bufferSize, struct list* dependencies)
{
    Elf32_Ehdr*             header = (Elf32_Ehdr*)buffer;
    Elf32_Phdr*             pi, *dynamic = NULL;
    struct __elf_address_mapping* mappings;
    int                     mi = 0;
    int                     status = 0;

    // must be executable or dynamic library
    if (header->e_type != ET_EXEC && header->e_type != ET_DYN) {
        fprintf(stderr, "invalid file type\n");
        return 0;
    }

    // no program header present? then we are done
    if (header->e_phoff == 0 || header->e_phentsize == 0 || header->e_phnum == 0) {
        fprintf(stderr, "No program header present\n");
        return 0;
    }

    // allocate space to keep mappings
    mappings = calloc(sizeof(struct __elf_address_mapping), header->e_phnum + 1);
    if (mappings == NULL) {
        errno = ENOMEM;
        return -1;
    }

    pi = (Elf32_Phdr*)(buffer + header->e_phoff);
    for (uint16_t i = 0; i < header->e_phnum; i++, pi++) {
        if (pi->p_type == PT_DYNAMIC) {
            dynamic = pi;
        } else if (pi->p_type == PT_LOAD) {
            mappings[mi].data    = buffer + pi->p_offset;
            mappings[mi].voffset = pi->p_vaddr;
            mappings[mi].size    = pi->p_filesz;
            mappings[mi].valid   = 1;
            mi++;
        }
    }

    if (dynamic) {
        status = __parse_dynamic_section32(mappings,
                                           buffer + dynamic->p_offset,
                                           dynamic->p_filesz,
                                           dependencies
       );
    }

    free(mappings);
    return status;
}

static const char* __get_string_from_strtab64(const char* strTable, Elf64_Xword index)
{
    return strTable + index;
}

static int __parse_dynamic_section64(struct __elf_address_mapping* mappings, const char* dynamicTable, size_t dynamicTableSize, struct list* dependencies)
{
    Elf64_Dyn*  di;
    size_t      i;
    size_t      strTableOffset = 0;
    const char* strTable       = NULL;

    for (i = 0, di = (Elf64_Dyn*)dynamicTable;
         i < dynamicTableSize; i += sizeof(Elf64_Dyn), di++) {
        if (di->d_tag == DT_STRTAB) {
            strTableOffset = di->d_un.d_ptr;
        } else if (di->d_tag == DT_NULL) {
            break;
        }
    }

    if (strTableOffset == 0) {
        fprintf(stderr, "oven: could not find string table offset\n");
        return -1;
    }

    // find the correct file offset
    for (i = 0; mappings[i].valid; i++) {
        if (strTableOffset >= mappings[i].voffset && strTableOffset < (mappings[i].voffset + mappings[i].size)) {
            strTable = mappings[i].data + (strTableOffset - mappings[i].voffset);
            break;
        }
    }

    if (strTable == NULL) {
        fprintf(stderr, "Could not find string table for dynamic section\n");
        return -1;
    }
    
    for (i = 0, di = (Elf64_Dyn*)dynamicTable;
         i < dynamicTableSize; i += sizeof(Elf64_Dyn), di++) {
        if (di->d_tag == DT_NEEDED) {
            if (__resolve_add_dependency(dependencies, __get_string_from_strtab64(strTable, di->d_un.d_val))) {
                return -1;
            }
        } else if (di->d_tag == DT_NULL) {
            break;
        }
    }

    return 0;
}

static int __parse_dependencies_64(const char* buffer, size_t bufferSize, struct list* dependencies)
{
    Elf64_Ehdr*             header = (Elf64_Ehdr*)buffer;
    Elf64_Phdr*             pi, *dynamic = NULL;
    struct __elf_address_mapping* mappings;
    int                     mi = 0;
    int                     status = 0;

    // must be executable or dynamic library
    if (header->e_type != ET_EXEC && header->e_type != ET_DYN) {
        fprintf(stderr, "invalid file type\n");
        return 0;
    }

    // no program header present? then we are done
    if (header->e_phoff == 0 || header->e_phentsize == 0 || header->e_phnum == 0) {
        fprintf(stderr, "No program header present\n");
        return 0;
    }

    // allocate space to keep mappings
    mappings = calloc(sizeof(struct __elf_address_mapping), header->e_phnum + 1);
    if (mappings == NULL) {
        errno = ENOMEM;
        return -1;
    }

    pi = (Elf64_Phdr*)(buffer + header->e_phoff);
    for (uint16_t i = 0; i < header->e_phnum; i++, pi++) {
        if (pi->p_type == PT_DYNAMIC) {
            dynamic = pi;
        } else if (pi->p_type == PT_LOAD) {
            mappings[mi].data    = buffer + pi->p_offset;
            mappings[mi].voffset = pi->p_vaddr;
            mappings[mi].size    = pi->p_filesz;
            mappings[mi].valid   = 1;
            mi++;
        }
    }

    if (dynamic) {
        status = __parse_dynamic_section64(mappings,
                                           buffer + dynamic->p_offset,
                                           dynamic->p_filesz,
                                           dependencies
       );
    }

    free(mappings);
    return status;
}

static int __parse_dependencies(const char* buffer, size_t bufferSize, struct list* dependencies)
{
    Elf32_Ehdr* header32 = (Elf32_Ehdr*)buffer;
    if (header32->e_ident[EI_DATA] != ELFDATA2LSB) {
        fprintf(stderr, "oven: only LSB formatted elf files are supported right now\n");
        errno = ENOSYS;
        return -1;
    }

    if (header32->e_ident[EI_CLASS] == ELFCLASS64) {
        return __parse_dependencies_64(buffer, bufferSize, dependencies);
    }
    return __parse_dependencies_32(buffer, bufferSize, dependencies);
}

static enum kitchen_resolve_arch __elf_arch_to_arch(Elf64_Half elf_arch)
{
    switch (elf_arch) {
        case EM_386:
            return KITCHEN_RESOLVE_ARCH_X86;
        case EM_X86_64:
            return KITCHEN_RESOLVE_ARCH_X86_64;
        case EM_ARM:
            return KITCHEN_RESOLVE_ARCH_ARM;
        case EM_AARCH64:
            return KITCHEN_RESOLVE_ARCH_ARM64;
        case EM_MIPS:
            return KITCHEN_RESOLVE_ARCH_MIPS;
        case EM_MIPS_X:
            return KITCHEN_RESOLVE_ARCH_MIPS64;
        case EM_PPC:
            return KITCHEN_RESOLVE_ARCH_PPC;
        case EM_PPC64:
            return KITCHEN_RESOLVE_ARCH_PPC64;
        case EM_SPARC:
            return KITCHEN_RESOLVE_ARCH_SPARC;
        case EM_SPARCV9:
            return KITCHEN_RESOLVE_ARCH_SPARV9;
        case EM_S390:
            return KITCHEN_RESOLVE_ARCH_S390;
        default:
            return KITCHEN_RESOLVE_ARCH_UNKNOWN;
    }
}

int elf_is_valid(const char* path, enum kitchen_resolve_arch* arch)
{
    union {
        Elf32_Ehdr header32;
        Elf64_Ehdr header64;
    } headers;
    FILE*      file;
    size_t     read;

    file = fopen(path, "rb");
    if (file == NULL) {
        return -1;
    }

    read = fread(&headers.header64, 1, sizeof(Elf64_Ehdr), file);
    fclose(file);
    
    if (read != sizeof(Elf64_Ehdr)) {
        return -1;
    }

    if (headers.header32.e_ident[EI_MAG0] != ELFMAG0 ||
        headers.header32.e_ident[EI_MAG1] != ELFMAG1 ||
        headers.header32.e_ident[EI_MAG2] != ELFMAG2 ||
        headers.header32.e_ident[EI_MAG3] != ELFMAG3) {
        return -1;
    }

    if (headers.header32.e_ident[EI_CLASS] == ELFCLASS32) {
        *arch = __elf_arch_to_arch(headers.header32.e_machine);
    } else {
        *arch = __elf_arch_to_arch(headers.header64.e_machine);
    }   
    return 0;
}

int elf_resolve_dependencies(const char* path, struct list* dependencies)
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

/**
 * @brief We may need to use patchelf on linux binaries built patchelf --set-soname libfoo.so libfoo.so
 * This is a TODO
 */

#ifdef TEST
// gcc -I../../platform/include -DTEST ./elf.c
int main(int argc, char** argv)
{
    struct list dependencies;
    list_init(&dependencies);

    if (argc < 2) {
        fprintf(stderr, "usage: %s <elf file>\n", argv[0]);
        return 1;
    }

    if (elf_resolve_dependencies(argv[1], &dependencies)) {
        fprintf(stderr, "oven: failed to resolve dependencies\n");
        return 1;
    }

    return 0;
}

#endif
