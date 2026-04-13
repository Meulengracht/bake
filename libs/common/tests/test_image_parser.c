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

#include <chef/image.h>
#include <chef/list.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("  Assertion failed: %s\n", msg); \
            return 1; \
        } \
    } while(0)

static int __parse_image(const char* yaml, struct chef_image** out)
{
    return chef_image_parse((void*)yaml, strlen(yaml), out);
}

/**
 * Test: minimal MBR image with one partition
 */
int test_image_mbr_minimal(void)
{
    struct chef_image* image = NULL;
    const char* yaml =
        "schema: mbr\n"
        "\n"
        "partitions:\n"
        "- label: boot\n"
        "  type: fat32\n"
        "  id: 0C\n"
        "  size: 134217728\n";

    int status = __parse_image(yaml, &image);
    TEST_ASSERT(status == 0, "chef_image_parse should succeed");
    TEST_ASSERT(image != NULL, "image should not be NULL");

    TEST_ASSERT(image->schema == CHEF_IMAGE_SCHEMA_MBR,
        "schema should be MBR");
    TEST_ASSERT(image->partitions.count == 1,
        "should have 1 partition");

    struct chef_image_partition* part =
        (struct chef_image_partition*)image->partitions.head;
    TEST_ASSERT(strcmp(part->label, "boot") == 0,
        "partition label should be 'boot'");
    TEST_ASSERT(strcmp(part->fstype, "fat32") == 0,
        "partition type should be 'fat32'");
    TEST_ASSERT(part->type == 0x0C,
        "partition MBR type should be 0x0C");
    TEST_ASSERT(part->size == 134217728LL,
        "partition size should be 134217728");

    chef_image_destroy(image);
    return 0;
}

/**
 * Test: minimal GPT image with one partition
 */
int test_image_gpt_minimal(void)
{
    struct chef_image* image = NULL;
    const char* yaml =
        "schema: gpt\n"
        "\n"
        "partitions:\n"
        "- label: efi-system\n"
        "  type: fat32\n"
        "  id: C12A7328-F81F-11D2-BA4B-00A0C93EC93B\n"
        "  size: 268435456\n";

    int status = __parse_image(yaml, &image);
    TEST_ASSERT(status == 0, "chef_image_parse should succeed");
    TEST_ASSERT(image != NULL, "image should not be NULL");

    TEST_ASSERT(image->schema == CHEF_IMAGE_SCHEMA_GPT,
        "schema should be GPT");
    TEST_ASSERT(image->partitions.count == 1,
        "should have 1 partition");

    struct chef_image_partition* part =
        (struct chef_image_partition*)image->partitions.head;
    TEST_ASSERT(strcmp(part->label, "efi-system") == 0,
        "partition label should be 'efi-system'");
    TEST_ASSERT(strcmp(part->fstype, "fat32") == 0,
        "partition type should be 'fat32'");
    TEST_ASSERT(part->guid != NULL, "partition guid should not be NULL");
    TEST_ASSERT(strcmp(part->guid, "C12A7328-F81F-11D2-BA4B-00A0C93EC93B") == 0,
        "partition guid should match");
    TEST_ASSERT(part->size == 268435456LL,
        "partition size should be 268435456");

    chef_image_destroy(image);
    return 0;
}

/**
 * Test: partition with source entries
 */
int test_image_partition_sources(void)
{
    struct chef_image* image = NULL;
    const char* yaml =
        "schema: mbr\n"
        "\n"
        "partitions:\n"
        "- label: data\n"
        "  type: fat32\n"
        "  id: 0C\n"
        "  size: 134217728\n"
        "  sources:\n"
        "  - type: file\n"
        "    source: /host/path/file.txt\n"
        "    target: /dest/file.txt\n"
        "  - type: dir\n"
        "    source: /host/path/mydir\n"
        "    target: /dest/mydir\n"
        "  - type: package\n"
        "    source: vendor/mypackage/stable\n"
        "    target: /install\n"
        "  - type: raw\n"
        "    source: /host/raw.img\n"
        "    target: sector=0\n";

    int status = __parse_image(yaml, &image);
    TEST_ASSERT(status == 0, "chef_image_parse should succeed");

    struct chef_image_partition* part =
        (struct chef_image_partition*)image->partitions.head;
    TEST_ASSERT(part->sources.count == 4,
        "partition should have 4 sources");

    // source 0: file
    struct chef_image_partition_source* src0 =
        (struct chef_image_partition_source*)part->sources.head;
    TEST_ASSERT(src0->type == CHEF_IMAGE_SOURCE_FILE,
        "first source type should be FILE");
    TEST_ASSERT(strcmp(src0->source, "/host/path/file.txt") == 0,
        "first source path should match");
    TEST_ASSERT(strcmp(src0->target, "/dest/file.txt") == 0,
        "first source target should match");

    // source 1: dir
    struct chef_image_partition_source* src1 =
        (struct chef_image_partition_source*)src0->list_header.next;
    TEST_ASSERT(src1->type == CHEF_IMAGE_SOURCE_DIRECTORY,
        "second source type should be DIRECTORY");
    TEST_ASSERT(strcmp(src1->source, "/host/path/mydir") == 0,
        "second source path should match");
    TEST_ASSERT(strcmp(src1->target, "/dest/mydir") == 0,
        "second source target should match");

    // source 2: package
    struct chef_image_partition_source* src2 =
        (struct chef_image_partition_source*)src1->list_header.next;
    TEST_ASSERT(src2->type == CHEF_IMAGE_SOURCE_PACKAGE,
        "third source type should be PACKAGE");
    TEST_ASSERT(strcmp(src2->source, "vendor/mypackage/stable") == 0,
        "third source path should match");
    TEST_ASSERT(strcmp(src2->target, "/install") == 0,
        "third source target should match");

    // source 3: raw
    struct chef_image_partition_source* src3 =
        (struct chef_image_partition_source*)src2->list_header.next;
    TEST_ASSERT(src3->type == CHEF_IMAGE_SOURCE_RAW,
        "fourth source type should be RAW");
    TEST_ASSERT(strcmp(src3->source, "/host/raw.img") == 0,
        "fourth source path should match");
    TEST_ASSERT(strcmp(src3->target, "sector=0") == 0,
        "fourth source target should match");

    chef_image_destroy(image);
    return 0;
}

/**
 * Test: partition with attributes list
 */
int test_image_partition_attributes(void)
{
    struct chef_image* image = NULL;
    const char* yaml =
        "schema: mbr\n"
        "\n"
        "partitions:\n"
        "- label: bootpart\n"
        "  type: fat32\n"
        "  id: 0C\n"
        "  size: 134217728\n"
        "  attributes:\n"
        "  - boot\n"
        "  - readonly\n";

    int status = __parse_image(yaml, &image);
    TEST_ASSERT(status == 0, "chef_image_parse should succeed");

    struct chef_image_partition* part =
        (struct chef_image_partition*)image->partitions.head;
    TEST_ASSERT(part->attributes.count == 2,
        "partition should have 2 attributes");

    struct list_item_string* attr0 =
        (struct list_item_string*)part->attributes.head;
    TEST_ASSERT(strcmp(attr0->value, "boot") == 0,
        "first attribute should be 'boot'");

    struct list_item_string* attr1 =
        (struct list_item_string*)attr0->list_header.next;
    TEST_ASSERT(strcmp(attr1->value, "readonly") == 0,
        "second attribute should be 'readonly'");

    chef_image_destroy(image);
    return 0;
}

/**
 * Test: FAT-specific options
 */
int test_image_fat_options(void)
{
    struct chef_image* image = NULL;
    const char* yaml =
        "schema: mbr\n"
        "\n"
        "partitions:\n"
        "- label: boot\n"
        "  type: fat32\n"
        "  id: 0C\n"
        "  size: 134217728\n"
        "  fat-options:\n"
        "    reserved-image: /path/to/bootloader.img\n";

    int status = __parse_image(yaml, &image);
    TEST_ASSERT(status == 0, "chef_image_parse should succeed");

    struct chef_image_partition* part =
        (struct chef_image_partition*)image->partitions.head;
    TEST_ASSERT(part->options.fat.reserved_image != NULL,
        "FAT reserved_image should not be NULL");
    TEST_ASSERT(strcmp(part->options.fat.reserved_image, "/path/to/bootloader.img") == 0,
        "FAT reserved_image should match");

    chef_image_destroy(image);
    return 0;
}

/**
 * Test: multiple partitions on a GPT disk
 */
int test_image_multiple_partitions(void)
{
    struct chef_image* image = NULL;
    const char* yaml =
        "schema: gpt\n"
        "\n"
        "partitions:\n"
        "- label: efi\n"
        "  type: fat32\n"
        "  id: C12A7328-F81F-11D2-BA4B-00A0C93EC93B\n"
        "  size: 268435456\n"
        "- label: root\n"
        "  type: ext4\n"
        "  id: 0FC63DAF-8483-4772-8E79-3D69D8477DE4\n"
        "  size: 1073741824\n"
        "- label: data\n"
        "  type: ext4\n"
        "  id: 0FC63DAF-8483-4772-8E79-3D69D8477DE4\n";

    int status = __parse_image(yaml, &image);
    TEST_ASSERT(status == 0, "chef_image_parse should succeed");
    TEST_ASSERT(image->schema == CHEF_IMAGE_SCHEMA_GPT,
        "schema should be GPT");
    TEST_ASSERT(image->partitions.count == 3,
        "should have 3 partitions");

    struct chef_image_partition* p0 =
        (struct chef_image_partition*)image->partitions.head;
    TEST_ASSERT(strcmp(p0->label, "efi") == 0,
        "first partition label should be 'efi'");
    TEST_ASSERT(p0->size == 268435456LL,
        "first partition size should match");

    struct chef_image_partition* p1 =
        (struct chef_image_partition*)p0->list_header.next;
    TEST_ASSERT(strcmp(p1->label, "root") == 0,
        "second partition label should be 'root'");
    TEST_ASSERT(p1->size == 1073741824LL,
        "second partition size should match");

    struct chef_image_partition* p2 =
        (struct chef_image_partition*)p1->list_header.next;
    TEST_ASSERT(strcmp(p2->label, "data") == 0,
        "third partition label should be 'data'");
    TEST_ASSERT(p2->size == 0,
        "third partition size should be 0 (unset)");

    chef_image_destroy(image);
    return 0;
}

/**
 * Test: full image with multiple features combined
 */
int test_image_full(void)
{
    struct chef_image* image = NULL;
    const char* yaml =
        "schema: gpt\n"
        "\n"
        "partitions:\n"
        "- label: efi-boot\n"
        "  type: fat32\n"
        "  id: 0C, C12A7328-F81F-11D2-BA4B-00A0C93EC93B\n"
        "  size: 268435456\n"
        "  attributes:\n"
        "  - boot\n"
        "  fat-options:\n"
        "    reserved-image: /boot/loader.img\n"
        "  sources:\n"
        "  - type: file\n"
        "    source: /build/efi/boot.efi\n"
        "    target: /EFI/BOOT/BOOTX64.EFI\n"
        "  - type: dir\n"
        "    source: /build/efi/drivers\n"
        "    target: /EFI/drivers\n"
        "- label: system\n"
        "  type: ext4\n"
        "  id: 0FC63DAF-8483-4772-8E79-3D69D8477DE4\n"
        "  size: 4294967296\n"
        "  content: /system.img\n"
        "  sources:\n"
        "  - type: package\n"
        "    source: myorg/kernel/stable\n"
        "    target: /\n";

    int status = __parse_image(yaml, &image);
    TEST_ASSERT(status == 0, "chef_image_parse should succeed");
    TEST_ASSERT(image->schema == CHEF_IMAGE_SCHEMA_GPT,
        "schema should be GPT");
    TEST_ASSERT(image->partitions.count == 2,
        "should have 2 partitions");

    // partition 0: efi-boot
    struct chef_image_partition* p0 =
        (struct chef_image_partition*)image->partitions.head;
    TEST_ASSERT(strcmp(p0->label, "efi-boot") == 0,
        "first partition label should be 'efi-boot'");
    TEST_ASSERT(strcmp(p0->fstype, "fat32") == 0,
        "first partition type should be 'fat32'");
    TEST_ASSERT(p0->type == 0x0C,
        "first partition MBR type should be 0x0C");
    TEST_ASSERT(p0->guid != NULL,
        "first partition guid should not be NULL");
    TEST_ASSERT(strcmp(p0->guid, "C12A7328-F81F-11D2-BA4B-00A0C93EC93B") == 0,
        "first partition guid should match");
    TEST_ASSERT(p0->size == 268435456LL,
        "first partition size should match");
    TEST_ASSERT(p0->attributes.count == 1,
        "first partition should have 1 attribute");
    TEST_ASSERT(p0->options.fat.reserved_image != NULL,
        "first partition FAT reserved_image should be set");
    TEST_ASSERT(strcmp(p0->options.fat.reserved_image, "/boot/loader.img") == 0,
        "first partition FAT reserved_image should match");
    TEST_ASSERT(p0->sources.count == 2,
        "first partition should have 2 sources");

    // partition 1: system
    struct chef_image_partition* p1 =
        (struct chef_image_partition*)p0->list_header.next;
    TEST_ASSERT(strcmp(p1->label, "system") == 0,
        "second partition label should be 'system'");
    TEST_ASSERT(strcmp(p1->fstype, "ext4") == 0,
        "second partition type should be 'ext4'");
    TEST_ASSERT(p1->size == 4294967296LL,
        "second partition size should match");
    TEST_ASSERT(p1->content != NULL,
        "second partition content should not be NULL");
    TEST_ASSERT(strcmp(p1->content, "/system.img") == 0,
        "second partition content should match");
    TEST_ASSERT(p1->sources.count == 1,
        "second partition should have 1 source");

    chef_image_destroy(image);
    return 0;
}
