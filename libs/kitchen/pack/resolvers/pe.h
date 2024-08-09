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

#ifndef __RESOLVERS_PE_H__
#define __RESOLVERS_PE_H__

#include <stdint.h>

#define MZ_MAGIC 0x5A4D
#define PE_MAGIC 0x00004550

#define IMAGE_FILE_MACHINE_UNKNOWN      0x0
#define IMAGE_FILE_MACHINE_AM33         0x1D3
#define IMAGE_FILE_MACHINE_AMD64        0x8664
#define IMAGE_FILE_MACHINE_ARM          0x1c0
#define IMAGE_FILE_MACHINE_ARM64        0xaa64
#define IMAGE_FILE_MACHINE_ARMNT        0x1c4
#define IMAGE_FILE_MACHINE_EBC          0xEBC
#define IMAGE_FILE_MACHINE_I386         0x14C
#define IMAGE_FILE_MACHINE_IA64         0x200
#define IMAGE_FILE_MACHINE_LOONGARCH32  0x6232
#define IMAGE_FILE_MACHINE_LOONGARCH64  0x6264
#define IMAGE_FILE_MACHINE_M32R         0x9041
#define IMAGE_FILE_MACHINE_MIPS16       0x266
#define IMAGE_FILE_MACHINE_MIPSFPU      0x366
#define IMAGE_FILE_MACHINE_MIPSFPU16    0x466
#define IMAGE_FILE_MACHINE_POWERPC      0x1f0
#define IMAGE_FILE_MACHINE_POWERPCFP    0x1f1
#define IMAGE_FILE_MACHINE_R4000        0x166
#define IMAGE_FILE_MACHINE_RISCV32      0x5032
#define IMAGE_FILE_MACHINE_RISCV64      0x5064
#define IMAGE_FILE_MACHINE_RISCV128     0x5128

#define PE_ARCHITECTURE_32 0x10B
#define PE_ARCHITECTURE_64 0x20B

#define PE_SECTION_EXPORT          0x0
#define PE_SECTION_IMPORT          0x1
#define PE_SECTION_RESOURCE        0x2
#define PE_SECTION_EXCEPTION       0x3
#define PE_SECTION_CERTIFICATE     0x4
#define PE_SECTION_BASE_RELOCATION 0x5
#define PE_SECTION_DEBUG           0x6
#define PE_SECTION_ARCHITECTURE    0x7
#define PE_SECTION_GLOBAL_PTR      0x8
#define PE_SECTION_TLS             0x9
#define PE_SECTION_LOAD_CONFIG     0xA
#define PE_SECTION_BOUND_IMPORT    0xB
#define PE_SECTION_IAT             0xC
#define PE_SECTION_DID             0xD
#define PE_SECTION_CLR             0xE

#define PE_NUM_DIRECTORIES     16
#define PE_SECTION_NAME_LENGTH 8

typedef struct {
    uint16_t Signature;
    uint16_t PageExtraBytes; // Extra Bytes in last page
    uint16_t NumPages;
    uint16_t NumRelocations;
    uint16_t HeaderSize;
    uint16_t MinAllocation;
    uint16_t MaxAllocation;
    uint16_t InitialSS;
    uint16_t InitialSP;
    uint16_t Checksum;
    uint16_t InitialIP;
    uint16_t InitialCS;
    uint16_t RelocationTableAddress;
    uint16_t Overlay;
    uint16_t Reserved0[4];
    uint16_t OemId;
    uint16_t OemInfo;
    uint16_t Reserved1[10];
    uint32_t PeHeaderAddress;
} MzHeader;

typedef struct {
    uint32_t Magic;
    uint16_t Machine;
    uint16_t NumSections;
    uint32_t DateTimeStamp;
    uint32_t SymbolTableOffset;
    uint32_t NumSymbolsInTable;
    uint16_t SizeOfOptionalHeader;
    uint16_t Attributes;
} PeHeader;

typedef struct {
    uint32_t AddressRVA;
    uint32_t Size;
} PeDataDirectory;

typedef struct {
    uint16_t Architecture;
    uint8_t  LinkerVersionMajor;
    uint8_t  LinkerVersionMinor;
    uint32_t SizeOfCode;
    uint32_t SizeOfData;
    uint32_t SizeOfBss;
    uint32_t EntryPoint; // Relative offset
    uint32_t BaseOfCode;
} PeOptionalHeader;

typedef struct {
    PeOptionalHeader Base;
    uint32_t         BaseOfData;
    uint32_t         BaseAddress;
    uint32_t         SectionAlignment;
    uint32_t         FileAlignment;
    uint8_t          Unused[16];
    uint32_t         SizeOfImage; // Must be multiple of SectionAlignment
    uint32_t         SizeOfHeaders; // Must be a multiple of FileAlignment
    uint32_t         ImageChecksum;
    uint16_t         SubSystem;
    uint16_t         DllAttributes;
    uint8_t          Reserved[16];
    uint32_t         LoaderFlags;
    uint32_t         NumDataDirectories;
    PeDataDirectory  Directories[PE_NUM_DIRECTORIES];
} PeOptionalHeader32;

typedef struct {
    PeOptionalHeader Base;
    uint64_t         BaseAddress;
    uint32_t         SectionAlignment;
    uint32_t         FileAlignment;
    uint8_t          Unused[16];
    uint32_t         SizeOfImage; // Must be multiple of SectionAlignment
    uint32_t         SizeOfHeaders; // Must be a multiple of FileAlignment
    uint32_t         ImageChecksum;
    uint16_t         SubSystem;
    uint16_t         DllAttributes;
    uint8_t          Reserved[32];
    uint32_t         LoaderFlags;
    uint32_t         NumDataDirectories;
    PeDataDirectory  Directories[PE_NUM_DIRECTORIES];
} PeOptionalHeader64;

typedef struct {
uint8_t             Name[PE_SECTION_NAME_LENGTH];
uint32_t            VirtualSize;
uint32_t            VirtualAddress;
uint32_t            RawSize;
uint32_t            RawAddress;
uint32_t            PointerToFileRelocations;
uint32_t            PointerToFileLineNumbers;
uint16_t            NumRelocations;
uint16_t            NumLineNumbers;
uint32_t            Flags;
} PeSectionHeader;

typedef struct {
    union {
        uint32_t Attributes;
        uint32_t ImportLookupTable; // RVA Value
    } Variable;
    uint32_t TimeStamp; // Set to 0 if image is not bound
    uint32_t ForwarderChainId;
    uint32_t ModuleName; // RVA
    uint32_t ImportAddressTable; // RVA
} PeImportDescriptor;

#endif //!__RESOLVERS_PE_H__
