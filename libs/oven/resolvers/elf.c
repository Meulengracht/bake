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

#include <errno.h>
#include "elf.h"
#include <liboven.h>
#include <stdio.h>
#include <stdlib.h>

struct address_mapping {
    const char* data;
    size_t      voffset;
    size_t      size;
    int         valid;
};

static int __add_dependency(struct list* dependencies)
{

}

static int __load_file(const char* path, void** bufferOut, size_t* sizeOut)
{
    FILE*  file;
    void*  buffer;
    size_t size;
    
    file = fopen(path, "rb");
    if (file == NULL) {
        return -1;
    }
    
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    buffer = malloc(size);
    if (buffer == NULL) {
        fclose(file);
        return -1;
    }
    
    if (fread(buffer, size, 1, file) != size) {
        fclose(file);
        free(buffer);
        return -1;
    }
    fclose(file);
    
    *bufferOut = buffer;
    *sizeOut = size;
    return 0;
}

static int __parse_dependencies_32(const char* buffer, size_t bufferSize, struct list* dependencies)
{
    Elf32_Ehdr* header = (Elf32_Ehdr*)buffer;

    // must be executable or dynamic library
    if (header->e_type != ET_EXEC && header->e_type != ET_DYN) {
        return 0;
    }

    // no program header present? then we are done
    if (header->e_phoff == 0 || header->e_phentsize == 0) {
        return 0;
    }
}

static const char* __get_string_from_strtab64(const char* strTable, Elf64_Xword index)
{
    return strTable + index;
}

static int __parse_dynamic_section64(struct address_mapping* mappings, const char* dynamicTable, size_t dynamicTableSize, struct list* dependencies)
{
    Elf64_Dyn*  di             = (Elf64_Dyn*)dynamicTable;
    size_t      strTableOffset = 0;
    const char* strTable       = NULL;

    for (size_t i = 0; i < dynamicTableSize; i += sizeof(Elf64_Dyn), di++) {
        if (di->d_tag == DT_STRTAB) {
            strTableOffset = di->d_un.d_ptr;
        }
    }

    if (strTableOffset == 0) {
        return -1;
    }

    // find the correct file offset
    for (int i = 0; mappings[i].valid; i++) {
        if (strTableOffset >= mappings[i].voffset && strTableOffset < (mappings[i].voffset + mappings[i].size)) {
            strTable = mappings[i].data + (strTableOffset - mappings[i].voffset);
            break;
        }
    }

    if (strTable) {
        return -1;
    }

    for (size_t i = 0; i < dynamicTableSize; i += sizeof(Elf64_Dyn), di++) {
        if (di->d_tag == DT_NEEDED) {
            printf("library dependency found: %s\n", __get_string_from_strtab64(strTable, di->d_un.d_val));
        }
    }

    return 0;
}

static int __parse_dependencies_64(const char* buffer, size_t bufferSize, struct list* dependencies)
{
    Elf64_Ehdr*             header = (Elf64_Ehdr*)buffer;
    Elf64_Phdr*             pi, *dynamic = NULL;
    struct address_mapping* mappings;
    int                     mi = 0;
    int                     status = 0;

    // must be executable or dynamic library
    if (header->e_type != ET_EXEC && header->e_type != ET_DYN) {
        return 0;
    }

    // no program header present? then we are done
    if (header->e_phoff == 0 || header->e_phentsize == 0 || header->e_phnum == 0) {
        return 0;
    }

    // allocate space to keep mappings
    mappings = calloc(sizeof(struct address_mapping), header->e_phnum + 1);
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

int elf_resolve_dependencies(const char* path, struct list* dependencies)
{
    char*  buffer;
    size_t bufferSize;
    int    status;

    status = __load_file(path, (void**)&buffer, &bufferSize);
    if (status) {
        return status;
    }

    status = __parse_dependencies(buffer, bufferSize, dependencies);
    free(buffer);
    return 0;
}
